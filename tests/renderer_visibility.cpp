#include "component/renderer_visibility.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

int main()
{
    try
    {
        for (const auto count : {0u, 1u, 31u, 32u, 33u, 63u, 64u, 65u, 65535u, 65536u})
        {
            const auto words = (count + 31) / 32;
            std::vector<std::uint32_t> data(words + 2, 0x13579BDFu);
            if (!renderer::visibility::include_models(std::span(data).subspan(1, words), count))
                throw std::runtime_error("Valid native list rejected");
            if (data.front() != 0x13579BDFu || data.back() != 0x13579BDFu)
                throw std::runtime_error("Visibility write crossed the allocation");
            for (unsigned bit = 0; bit < words * 32; ++bit)
                if (bool(data[1 + bit / 32] & (0x80000000u >> (bit % 32))) != (bit < count))
                    throw std::runtime_error("Model index/padding mismatch");
        }
        std::array<std::uint32_t, 4> padded{1, 2, 3, 4};
        if (!renderer::visibility::include_models(padded, 33)
            || padded != std::array<std::uint32_t, 4>{UINT32_MAX, 0x80000000u, 0, 0})
            throw std::runtime_error("Native allocation padding was not cleared");
        const auto saved = padded;
        if (renderer::visibility::include_models(std::span(padded).first(1), 33)
            || renderer::visibility::include_models(padded, 65537) || padded != saved)
            throw std::runtime_error("Invalid layout changed visibility memory");
        std::cout << "PASS: visibility bounds, high-bit-first indices, tail bits and padded allocations\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
