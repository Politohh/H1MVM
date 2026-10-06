#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace theater_sky
{
    struct header { char magic[8]; std::uint32_t format, width, height, mips, flags, size; };
    static_assert(sizeof(header) == 32);
    struct subresource { std::uint32_t offset, pitch, size; };
    inline std::vector<subresource> layout(const header& h)
    {
        if (std::memcmp(h.magic,"H1SKY001",8) || h.width!=h.height || h.width<16 || h.width>8192 ||
            (h.width&(h.width-1)) || !h.mips || h.mips>14 || h.flags>255)
            throw std::runtime_error("Invalid H1 sky header");
        const bool block8=h.format==71 || h.format==72;
        if (!block8 && h.format!=77 && h.format!=78 && h.format!=95 && h.format!=96 && h.format!=98 && h.format!=99)
            throw std::runtime_error("Unsupported H1 sky format");
        unsigned maximum=1; for (auto n=h.width;n>1;n>>=1) ++maximum;
        if (h.mips>maximum) throw std::runtime_error("Invalid sky mip count");
        std::vector<subresource> result;
        std::uint64_t offset=0;
        for (unsigned face=0;face<6;++face) for(unsigned mip=0;mip<h.mips;++mip)
        {
            const auto width=std::max(1u,h.width>>mip);
            const auto blocks=(width+3)/4, pitch=blocks*(block8?8:16), size=pitch*blocks;
            result.push_back({static_cast<unsigned>(offset),pitch,size});offset+=size;
        }
        if (offset!=h.size || offset>256*1024*1024) throw std::runtime_error("Invalid sky pixel size");
        return result;
    }
}
