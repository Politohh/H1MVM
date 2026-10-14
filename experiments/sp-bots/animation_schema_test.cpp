#include "animation_schema.hpp"
#include "peer_capabilities.hpp"
#include <cassert>
#include <iostream>
#include <string_view>

int main()
{
    using namespace spai_probe::animation_schema;
    using namespace std::literals;
    assert(supports_score_wire(current_wire,false) && supports_score_wire(current_wire,true));
    for(const auto* legacy:{"1","2","3","4","5","6","7","8"})
    {
        assert(!supports_score_wire(legacy,false));
        assert(supports_score_wire(legacy,true));
    }
    for(const auto* malformed:{"","0","10","07","08","09","7extra","-7"})
        for(const auto replay:{false,true})assert(!supports_score_wire(malformed,replay));
    assert(state_count(0)==0 && state_count(1)==10 && state_count(2)==11 && state_count(3)==11 && state_count(4)==12 && state_count(5)==19 && state_count(6)==24 && state_count(7)==24 && state_count(8)==34 && state_count(9)==0);
    // Published schema-1 positions are a compatibility contract, independent
    // of the current schema size and new traversal entries.
    const char* legacy_names[]={"idle","move","reaction","cover_left","cover_right","vault","fire","aim","reload","throwback"};
    const size_t legacy_clips[]={1,2,3,4,5,6,9,10,11,12};
    for(size_t i=0;i<10;++i)
    {
        assert(states[i].name==std::string_view(legacy_names[i]));
        assert(states[i].campaign_index==legacy_clips[i]);
    }
    assert(states[5].client_slot=="h1_mp_hold_grenade_throw"sv);
    assert(states[10].name=="mantle"sv && states[10].campaign_index==13 && states[10].single_cycle);
    assert(states[11].name=="death"sv && states[11].campaign_index==16 && states[11].single_cycle);
    assert(read_marker({"mp_backlot","H1SPAI:5"},true)==5);
    assert(read_marker({"mp_backlot","H1SPAI:6"},true)==6);
    assert(read_marker({"mp_backlot","H1SPAI:7"},true)==7);
    assert(read_marker({"mp_backlot","H1SPAI:8"},true)==8);
    for(size_t i=24;i<34;++i)assert(states[i].campaign_index==i+5 && states[i].single_cycle);
    for(size_t i=19;i<24;++i)assert(states[i].campaign_index==i+5 && states[i].single_cycle);
    for(size_t i=12;i<19;++i)assert(states[i].campaign_index==i+5 && !states[i].single_cycle);
    for(size_t i=0;i<states.size();++i)for(size_t j=i+1;j<states.size();++j)
    {
        assert(std::string_view(states[i].name)!=states[j].name);
        assert(std::string_view(states[i].server_slot)!=states[j].server_slot);
        assert(std::string_view(states[i].client_slot)!=states[j].client_slot);
    }
    assert(read_marker({"mp_backlot","war"},false)==0);
    assert(read_marker({"mp_backlot","H1SPAI:1"},true)==1);
    assert(read_marker({"mp_crash","H1SPAI:2"},true)==2);
    assert(read_marker({"mp_carentan","H1SPAI:3"},true)==3);
    assert(read_marker({"mp_backlot","H1SPAI:4"},true)==4);
    for(const auto& fields:std::vector<std::vector<std::string>>{
        {"H1SPAI:1","H1SPAI:1"},{"H1SPAI:1","H1SPAI:2"},
        {"H1SPAI:2","H1SPAI:1"},{"H1SPAI:2","H1SPAI:2"},
        {"H1SPAI:3","H1SPAI:3"},{"H1SPAI:2","H1SPAI:3"},{"H1SPAI:3","H1SPAI:1"},
        {"H1SPAI:4","H1SPAI:4"},{"H1SPAI:3","H1SPAI:4"},{"H1SPAI:5","H1SPAI:5"},{"H1SPAI:4","H1SPAI:5"},
        {"H1SPAI:6","H1SPAI:6"},{"H1SPAI:5","H1SPAI:6"},{"H1SPAI:0"},{"H1SPAI:9"},{"H1SPAI:7","H1SPAI:6"},{"H1SPAI:7","H1SPAI:7"},{"H1SPAI:8","H1SPAI:8"},{"H1SPAI:7","H1SPAI:8"},{"H1SPAI:"},{"H1SPAI:01"},{"H1SPAI:2extra"}})
    {
        bool rejected=false;
        try{read_marker(fields,true);}catch(const std::runtime_error&){rejected=true;}
        assert(rejected);
    }
    for(const auto* marker:{"H1SPAI:1","H1SPAI:2","H1SPAI:3","H1SPAI:4","H1SPAI:5","H1SPAI:6","H1SPAI:7","H1SPAI:8"})
    {
        bool rejected=false;
        try{read_marker({marker},false);}catch(const std::runtime_error&){rejected=true;}
        assert(rejected);
    }
    spai_probe::peer_capabilities peers;
    for(const auto* old:{"","1","2","3","4","5","6","7","8","9"})
    {
        peers.remember(0,"endpoint",42,old);
        assert(!peers.supports(0,"endpoint",42,network_capability));
    }
    peers.remember(0,"endpoint",42,network_capability);
    assert(peers.supports(0,"endpoint",42,network_capability));
    assert(!peers.supports(0,"endpoint",43,network_capability));
    std::cout<<"PASS old/new recording schema parsing, immutable legacy states, unique carriers, and capability-10 compatibility policy; not native playback/network acceptance\n";
}
