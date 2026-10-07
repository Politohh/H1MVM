#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>
#include <zlib.h>

namespace cod4
{
    inline std::vector<std::uint8_t> compress_conversion_packet(std::span<const std::uint8_t> input)
    {
        if (input.size() > 131072) throw std::runtime_error("Oversized conversion packet");
        const auto size = static_cast<uLong>(input.size());
        uLongf length = compressBound(size);
        std::vector<std::uint8_t> output(length);
        // Independent native keyframes are numerous. Level 1 keeps identical
        // decompressed packets without the level-9 search cost on every frame.
        auto result = compress2(output.data(), &length, input.data(), size, Z_BEST_SPEED);
        if (result == Z_OK && length + 8 > 131072)
        {
            // Retain the native chunk limit even for poorly compressible data.
            length = static_cast<uLongf>(output.size());
            result = compress2(output.data(), &length, input.data(), size, Z_BEST_COMPRESSION);
        }
        if (result != Z_OK || length + 8 > 131072) throw std::runtime_error("Cannot compress conversion packet within native chunk limit");
        output.resize(length);
        return output;
    }
}
