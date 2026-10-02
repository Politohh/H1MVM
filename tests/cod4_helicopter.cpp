#include "component/demo/cod4_helicopter.hpp"
#include "component/demo/cod4_model_translation.hpp"
#include <iostream>

void check(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
template<class T> T value(const cod4::h1_entity& e, unsigned offset) { T v{}; std::memcpy(&v,e.data()+offset,sizeof(v));return v; }
template<class F> void rejects(F f) { bool threw=false;try{f();}catch(const std::runtime_error&){threw=true;}check(threw,"Invalid input accepted"); }
cod4::entity_state helicopter(unsigned number)
{
    cod4::entity_state e;
    e.set(0,number);e.set(4,12);e.set(216,92);e.set(12,1);e.set(48,1);
    return e;
}
int main(int argc, char** argv)
{
    try
    {
        // Crash fixture: the file selected Cobra slot 34 while the client
        // registry was empty. Residency in the DB must not count as ready.
        std::map<unsigned, std::string> registry;
        unsigned registrations = 0;
        const auto lookup = [&](unsigned index) -> std::string_view { return registry[index]; };
        const auto register_definition = [&](std::string_view name, unsigned index)
        { ++registrations; registry[index] = name; };
        check(cod4::ensure_helicopter_definition(34,"cobra_mp",lookup,register_definition),"Missing client definition not initialized");
        check(registry.at(34)=="cobra_mp" && registrations==1,"Wrong client slot initialized");
        check(!cod4::ensure_helicopter_definition(34,"cobra_mp",lookup,register_definition),"Stable definition reinitialized");
        registry.clear(); // A demo restart/map bootstrap rebuilds client state.
        check(cod4::ensure_helicopter_definition(34,"cobra_mp",lookup,register_definition) && registrations==2,"Restart lost registration");
        registry[34]="defaultvehicle";
        check(cod4::ensure_helicopter_definition(34,"cobra_mp",lookup,register_definition),"Default definition accepted");
        registry.clear();
        rejects([&]{cod4::ensure_helicopter_definition(34,"cobra_mp",lookup,[](auto,auto){});});
        rejects([&]{cod4::ensure_helicopter_definition(64,"cobra_mp",lookup,register_definition);});
        check(cod4::native_helicopter_definition("h1_vehicle_mi24_hind")=="hind_mp","Native Hind binding");
        std::map<int,std::string> configs{{922,"vehicle_cobra_helicopter_fly"}};
        check(cod4::helicopter_definition(configs.at(922))=="cobra_mp","Cobra definition");
        check(cod4::helicopter_definition("vehicle_mi24p_hind_desert")=="hind_mp","Hind definition");
        check(cod4::model_target("vehicle_mi24p_hind_desert")=="h1_vehicle_mi24_hind","Native Hind model");
        rejects([] { cod4::helicopter_definition("unknown_helicopter"); });
        cod4::world_track w;
        w.frames.push_back({});w.frames.back().time=1000;
        for(unsigned i=0;i<8;++i) w.frames.back().entities[300+i]=helicopter(300+i);
        auto slots=cod4::collect_helicopters(w,configs);
        check(slots.at(900).slots.size()==8,"Early camera sample");
        w.frames.back().entities[308]=helicopter(308);
        rejects([&]{cod4::collect_helicopters(w,configs);});
        w.frames.back().entities.erase(308);
        w.frames.push_back(w.frames.back());w.frames.back().time=1050;
        w.frames.back().entities.erase(300);w.frames.back().entities[308]=helicopter(308);
        slots=cod4::collect_helicopters(w,configs);
        check(slots.at(1050).slots.at(308)==slots.at(1000).slots.at(300),"Vehicle slot reuse");
        check(slots.at(1050).slots.at(301)==slots.at(1000).slots.at(301),"Living vehicle slot changed");
        auto& a=w.frames.front().entities.at(301);auto& b=w.frames.back().entities.at(301);
        a.set(24,std::bit_cast<unsigned>(10.0f));b.set(24,std::bit_cast<unsigned>(20.0f));
        a.set(64,std::bit_cast<unsigned>(350.0f));b.set(64,std::bit_cast<unsigned>(10.0f));
        auto sampled=cod4::sample_helicopter(w,301,1025);
        check(std::abs(std::bit_cast<float>(sampled.get(24))-15.0f)<0.001f,"Interpolated helicopter position");
        check(std::abs(std::remainder(std::bit_cast<float>(sampled.get(64)),360.0f))<0.001f,"Angle wrap interpolation");
        b.set(8,2);sampled=cod4::sample_helicopter(w,301,1025);
        check(std::bit_cast<float>(sampled.get(24))==10.0f,"Interpolated across teleport");
        b.set(8,0);b.set(216,93);sampled=cod4::sample_helicopter(w,301,1025);
        check(std::bit_cast<float>(sampled.get(24))==10.0f,"Interpolated across model replacement");
        b.set(216,92);w.frames.back().entities.erase(301);sampled=cod4::sample_helicopter(w,301,1025);
        check(std::bit_cast<float>(sampled.get(24))==10.0f,"Interpolated across disappearance");
        a.set(128,10);
        rejects([&]{cod4::collect_helicopters(w,configs);});
        w.commands.push_back({999,0,0,"d 1352 mp_cobra_helicopter"});
        slots=cod4::collect_helicopters(w,configs);
        check(slots.at(1000).loops.at(301)=="mp_cobra_helicopter","Dynamic loop config lost");
        w.commands[0].time=1001;rejects([&]{cod4::collect_helicopters(w,configs);});
        rejects([&]{cod4::translate_helicopter(a,768,0,0);});
        rejects([&]{cod4::translate_helicopter(a,768,64,0);});
        rejects([&]{cod4::translate_helicopter(a,768,2,8);});
        rejects([&]{cod4::translate_helicopter(a,768,2,0,512);});
        a.set(228,1);rejects([&]{cod4::translate_helicopter(a,768,2,0);});
        a.set(228,0);
        unsigned states=0;
        for(int arg=1;arg<argc;++arg)
        {
            std::ifstream file(argv[arg],std::ios::binary);check(bool(file),"Fixture unavailable");
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
            auto camera=cod4::decode(bytes);auto world=cod4::decode_world(bytes);
            auto helis=cod4::collect_helicopters(world,camera.configstrings);
            unsigned count=0;
            for(const auto& frame:world.frames)for(const auto&[id,e]:frame.entities)
            {
                if(e.get(4)!=12)continue;
                const auto pose=cod4::sample_helicopter(world,id,frame.time);
                auto native=cod4::translate_helicopter(pose,768,2,helis.at(frame.time).slots.at(id),29);
                check(value<std::uint8_t>(native,10)==12 && value<unsigned>(native,208)==768,"Native helicopter renderer identity");
                check(value<unsigned>(native,168)==(16 | helis.at(frame.time).slots.at(id)),"Native vehicle slot/definition packing");
                check(value<std::uint16_t>(native,8)==29 && value<unsigned>(native,24)==e.get(112),"Native looping sound or rotor input lost");
                check(value<unsigned>(native,172)==0 && value<unsigned>(native,40)==0,"Foreign flags/events leaked into H1");
                for(unsigned i=0;i<3;++i)
                {
                    check(value<unsigned>(native,108+i*4)==e.get(24+i*4),"Recorded helicopter position changed");
                    check(value<unsigned>(native,144+i*4)==e.get(60+i*4),"Recorded helicopter angle changed");
                }
                ++count;
            }
            for(const auto& frame:camera.frames)
                for(const auto&[id,e]:world.sample(frame.time).entities)
                    if(e.get(4)==12) cod4::translate_helicopter(cod4::sample_helicopter(world,id,frame.time),768,2,helis.at(frame.time).slots.at(id));
            std::cout<<argv[arg]<<": helicopters="<<helis.entities.size()<<" states="<<count
                <<" event_states="<<helis.event_states<<" loop_aliases="<<helis.sounds.size()<<'\n';
            states+=count;
        }
        std::cout<<"PASS native helicopter layout, slot capacity/reuse, source identity, dynamic sound aliases, flight interpolation; states="<<states<<'\n';
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
