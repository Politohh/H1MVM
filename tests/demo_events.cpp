#include "component/demo/cod4_demo_events.hpp"
#include "component/demo/theater_library_metadata.hpp"
#include <iostream>
#include <cassert>

int wmain(int argc,wchar_t** argv)
{
    try
    {
        if(argc<2 || argc>3) throw std::runtime_error("Pass a COD4 recording fixture and optional converted demo");
        std::ifstream file(std::filesystem::path(argv[1]),std::ios::binary);
        if(!file) throw std::runtime_error("Cannot open fixture");
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
        const auto camera=cod4::decode(bytes);const auto world=cod4::decode_world(bytes);
        const auto events=demo_events::collect(camera,world);
        unsigned kills{},starts{},ends{};int previous=-1;
        for(const auto& e:events)
        {
            assert(e.time>=previous);previous=e.time;
            const auto saved=demo_events::parse(demo_events::encode(e));
            assert(saved && saved->time==e.time && saved->label==e.label && saved->attacker==e.attacker
                && saved->victim==e.victim && saved->weapon==e.weapon && saved->cause==e.cause);
            if(e.kind=="kill") ++kills;
            if(e.kind=="round_start") ++starts;
            if(e.kind=="round_end") ++ends;
            std::cout<<e.time-camera.frames.front().time<<" "<<e.kind<<" "<<e.label<<" weapon="<<e.weapon<<" cause="<<e.cause<<"\n";
        }
        assert(kills==cod4::collect_obituary_events(world).size());
        std::cout<<"PASS source event index: kills="<<kills<<" round_starts="<<starts<<" round_ends="<<ends<<"\n";
        if(argc==3)
        {
            const auto source=demo_events::read_source(argv[1]);
            std::ifstream native(std::filesystem::path(argv[2]),std::ios::binary);
            const auto footer=demo_file::read_footer(native);assert(footer);
            const auto info=theater_library::read_native(native);
            const auto index=demo_timeline::decode(footer->bytes,footer->first,footer->last);assert(index);
            assert(demo_events::matches(source,*index,info.map,footer->first,footer->last));
            assert(!demo_events::matches(source,*index,"wrong_map",footer->first,footer->last));
            auto wrong=*index;wrong.pov=(wrong.pov+1)%42;
            assert(!demo_events::matches(source,wrong,info.map,footer->first,footer->last));
            wrong=*index;assert(!wrong.events.empty());wrong.events[0].victim=(wrong.events[0].victim+1)%42;
            assert(!demo_events::matches(source,wrong,info.map,footer->first,footer->last));
            std::cout<<"PASS verified original/native identity: "<<index->events.size()<<" kills; rejected wrong map, POV and victim\n";
        }
    }
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
