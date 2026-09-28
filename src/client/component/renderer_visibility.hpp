#pragma once
#include <algorithm>
#include <cstdint>
#include <span>

namespace renderer::visibility
{
    // H1's DPVS model lists use high-bit-first words. Tail/padding bits must
    // remain clear: the draw loop treats every set bit as a model index.
    inline bool include_models(std::span<std::uint32_t> words, std::uint32_t models)
    {
        if (models > 65536 || words.size() > 2048 || words.size() < (models + 31u) / 32u) return false;
        std::fill(words.begin(), words.end(), 0u);
        const auto full = models / 32;
        std::fill_n(words.begin(), full, UINT32_MAX);
        if (const auto tail = models % 32) words[full] = UINT32_MAX << (32 - tail);
        return true;
    }
}
