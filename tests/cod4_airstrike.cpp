#include "component/demo/cod4_airstrike.hpp"
#include "component/demo/cod4_model_translation.hpp"
#include <iostream>

void check(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
template<class T> T value(const cod4::h1_entity& e, unsigned offset) { T v{}; std::memcpy(&v,e.data()+offset,sizeof(v));return v; }
template<class F> void rejects(F f) { bool threw=false;try{f();}catch(const std::runtime_error&){threw=true;}check(threw,"Invalid input accepted"); }
int main(int argc, char** argv)
{
    try
    {
        std::map<int,std::string> configs{{1604,"fire/jet_afterburner"},{1605,"smoke/jet_contrail"},
            {1624,"explosions/clusterbomb"},{1705,"06tag_engine_right"},{1710,"26tag_origin"},{910,"tag_origin"}};
        check(cod4::jet_effect(cod4::source_tagged_fx(7,configs))==509,"Jet FX resolution");
        rejects([&]{cod4::source_tagged_fx(0,configs);});
        auto bad=configs;bad[1705]="x6tag_engine_right";
        rejects([&]{cod4::source_tagged_fx(7,bad);});
        cod4::world_track synthetic;
        cod4::entity_state e;e.set(0,300);e.set(4,6);e.set(136,80);e.set(160,255);
        e.set(164+2*4,56);e.set(180+2*4,12);
        synthetic.frames.push_back({});synthetic.frames.back().time=1000;synthetic.frames.back().entities[300]=e;
        synthetic.frames.push_back(synthetic.frames.back());synthetic.frames.back().time=1050;
        synthetic.frames.back().entities.clear(); // PVS absence must not repeat the burst.
        synthetic.frames.push_back(synthetic.frames.front());synthetic.frames.back().time=1075;
        e.set(160,0);e.set(164+3*4,56);e.set(180+3*4,12);
        synthetic.frames.push_back({});synthetic.frames.back().time=1100;synthetic.frames.back().entities[300]=e;
        auto bursts=cod4::collect_airstrikes(synthetic,configs);
        check(bursts.bursts.size()==2 && bursts.bursts[0].time==1000 && bursts.bursts[1].time==1100,"Ring wrap or repeated snapshot");
        synthetic.frames.back().entities[300].set(12,2);
        synthetic.frames.back().entities[300].set(160,1);
        synthetic.frames.back().entities[300].set(164,56);
        synthetic.frames.back().entities[300].set(180,12);
        rejects([&]{cod4::collect_airstrikes(synthetic,configs);});
        auto registered = synthetic;
        registered.frames.resize(1);
        registered.commands.push_back({1000, 1, 0, "d 1710 \"26tag_origin\""});
        auto initial = configs;
        initial.erase(1710);
        check(cod4::collect_airstrikes(registered, initial).bursts.size() == 1,
            "FX tag registered after gamestate was lost");
        registered.commands[0].time = 1050;
        rejects([&]{cod4::collect_airstrikes(registered, initial);}); // No future identity.
        registered.commands[0].time = 1000;
        registered.commands.insert(registered.commands.begin(), {1000, 0, 0, "d 1624 explosions/clusterbomb"});
        initial.erase(1624);
        check(cod4::collect_airstrikes(registered, initial).bursts.size() == 1,
            "Dynamically registered effect name was lost");
        const auto metadata=cod4::parse_model_metadata({"@scriptfx:1500:1000","@fx:510:fx/explosions/clusterbomb_mp"});
        check(metadata.car_fx_times.at(1500)==1000,"Script FX metadata timing");
        rejects([&]{cod4::parse_model_metadata({"@scriptfx:1500:1000","@carfx:1500:1000","@fx:510:fx/test"});});
        check(cod4::model_target("vehicle_mig29_desert")=="h1_vehicle_mig29","Native MiG model");
        unsigned planes=0,bombs=0,total_bursts=0;
        for(int arg=1;arg<argc;++arg)
        {
            std::ifstream file(argv[arg],std::ios::binary);check(bool(file),"Fixture unavailable");
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
            auto camera=cod4::decode(bytes);auto world=cod4::decode_world(bytes);
            auto strike=cod4::collect_airstrikes(world,camera.configstrings);
            cod4::jet_fx_parameters tags;
            for(auto& frame:world.frames)for(auto&[id,entity]:frame.entities)
            {
                if(entity.get(4)==13)
                {
                    for(unsigned i=0;i<4;++i)if(entity.get(164+i*4)==56)
                    {
                        auto p=entity.get(180+i*4);auto fx=strike.plane_tags.at({entity.get(136),p});
                        if(auto idx=cod4::jet_effect(fx))tags[{entity.get(136),p}]=idx|((i+1)<<9);
                    }
                    auto native=cod4::translate_plane(entity,768,tags);
                    check(value<std::uint8_t>(native,10)==6 && value<unsigned>(native,172)==768,"Native plane render layout");
                    for(unsigned i=0;i<9;++i)check(value<unsigned>(native,96+i*4)==entity.get(12+i*4),"Plane trajectory changed");
                    for(unsigned i=0;i<4;++i)if(entity.get(164+i*4)==56)
                    {
                        check(value<unsigned>(native,44+i*8)==112,"Native bolted event lost");
                        check(value<unsigned>(native,48+i*8)==tags.at({entity.get(136),entity.get(180+i*4)}),"Native bone payload");
                    }
                    ++planes;
                }
                const auto model=camera.configstrings.find(830+entity.get(136));
                if(entity.get(4)==6 && entity.get(136) && model!=camera.configstrings.end()
                    && model->second=="projectile_cbu97_clusterbomb" && entity.get(12)==5)
                {
                    auto native=cod4::translate_script_model(entity,769);
                    cod4::preserve_clusterbomb_trajectory(native,entity,frame.time);
                    const auto expected=cod4::sample_trajectory(entity,12,frame.time);
                    check(value<std::array<float,3>>(native,108)==expected.origin && value<std::array<float,3>>(native,120)==expected.velocity,"Bomb gravity changed");
                    ++bombs;
                }
            }
            for(const auto& burst:strike.bursts)
            {
                const auto native=cod4::translate_car_fx(burst,1500,510);
                check(value<unsigned>(native,36)==510 && value<std::array<float,3>>(native,108)==burst.origin,"Burst payload");
                std::cout<<"burst "<<burst.time<<" source="<<burst.entity<<'\n';
            }
            std::cout<<argv[arg]<<": planes="<<strike.planes.size()<<" bursts="<<strike.bursts.size()<<" helicopters="<<strike.helicopters.size()<<'\n';
            if(std::string_view(argv[arg]).ends_with("demo0011.dm_1"))
                check(strike.planes.size()==3 && strike.bursts.size()==3 && strike.effects.size()==3,"demo0011 strike coverage");
            if(std::string_view(argv[arg]).ends_with("airstrike_to_h1.dm_1"))
                check(strike.planes.size()==3 && strike.bursts.size()==3 && strike.plane_tags.size()==4,
                    "New airstrike demo lost dynamically registered tags or bursts");
            total_bursts+=static_cast<unsigned>(strike.bursts.size());
        }
        std::cout<<"PASS plane states="<<planes<<" gravity bomb states="<<bombs<<" bursts="<<total_bursts<<'\n';
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
