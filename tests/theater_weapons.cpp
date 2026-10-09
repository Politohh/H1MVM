#include "component/demo/theater_weapons_policy.hpp"
#include <cassert>
#include <iostream>
#include <map>
int main()
{
    using namespace theater_weapons;
    for (int from=-1;from<18;++from) for (int to=-1;to<18;++to)
        assert(compatible(from,to)==(from>=0 && from<=5 && from==to));
    const auto r700=split_name("h1_remington700_mp_a#acog_f#base");
    assert(r700.weapon=="h1_remington700_mp" && r700.attachment=="acog" && r700.furniture=="base");
    const auto standard=split_name("h1_junsnp_mp");
    assert(standard.weapon=="h1_junsnp_mp" && standard.attachment=="none" && standard.furniture=="base");
    assert(identifier(r700.weapon) && identifier(r700.attachment));
    for (const auto* bad:{"", "../base", "C:\\x", "kit/name", "foo|bar", "foo;quit"}) assert(!identifier(bad));
    assert(!identifier(std::string(64,'a')));
    const std::map<unsigned, unsigned> replacements{{131155,1179661},{262227,393299}};
    auto lookup = [&](unsigned original) {
        const auto found = replacements.find(original);
        return found == replacements.end() ? original : found->second;
    };
    for (const auto type : {1,18})
    {
        std::array<std::uint8_t,256> entity;
        entity.fill(0x5a); entity[10] = static_cast<std::uint8_t>(type);
        const world_weapon_state original{131155,0,262227,131155};
        entity = world_entity(entity.data(),original);
        const auto before = entity;
        const auto desired = world_weapons_for(entity.data(),lookup);
        assert((desired == world_weapon_state{1179661,0,393299,1179661}));
        const auto render = world_entity(entity.data(),desired);
        assert(entity == before); // Never rewrite snapshots or the caller's entity.
        assert(world_entity(render.data(),original) == before); // Every other byte preserved.
        assert(world_weapons_for(render.data(),lookup) == desired); // No chained substitution.
        for (const auto other : {0,2,3,4,19})
        {
            auto unrelated = before; unrelated[10] = static_cast<std::uint8_t>(other);
            assert(world_weapons_for(unrelated.data(),lookup) == original);
        }
        // A kit for another packed variant of the same gun remains unchanged.
        assert(lookup(83) == 83 && lookup(524371) == 524371);
        const auto restored = world_weapons_for(entity.data(),[](unsigned weapon) { return weapon; });
        assert(restored == original && restored != desired);
    }
    assert(world_weapons_for(nullptr,lookup) == world_weapon_state{});
    std::cout<<"PASS same-class firearm policy, recorded attachment identities and asset path rejection\n";
    std::cout<<"PASS player/agent world weapons, simultaneous swaps, exact variants, restore and immutable snapshots\n";
}
