#include "component/demo/theater_sky_format.hpp"
#include <cassert>
#include <iostream>
using namespace theater_sky;
int main()
{
    header h{};std::memcpy(h.magic,"H1SKY001",8);h.format=95;h.width=h.height=64;h.mips=1;h.size=6*16*16*16;
    auto parts=layout(h);assert(parts.size()==6 && parts[0].pitch==256 && parts[5].offset==5*4096);
    h.mips=7;h.size=6*(4096+1024+256+64+16+16+16);parts=layout(h);
    assert(parts.size()==42 && parts[7].offset==5488 && parts.back().offset+parts.back().size==h.size);
    auto rejects=[](header bad) {try{(void)layout(bad);}catch(const std::runtime_error&){return;}assert(false);};
    auto bad=h;bad.size--;rejects(bad);bad=h;bad.mips=8;rejects(bad);bad=h;bad.width=63;rejects(bad);
    bad=h;bad.format=0;rejects(bad);bad=h;bad.magic[0]='X';rejects(bad);bad=h;bad.height=32;rejects(bad);
    std::cout<<"PASS native cubemap face/mip offsets, pitches, header and size validation\n";
}
