#include "component/demo/theater_weapons_policy.hpp"
#include <cassert>
#include <iostream>
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
    std::cout<<"PASS same-class firearm policy, recorded attachment identities and asset path rejection\n";
}
