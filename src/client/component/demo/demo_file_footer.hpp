#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <istream>
#include <optional>
#include <vector>

namespace demo_file
{
    struct footer
    {
        std::int32_t first{}, last{};
        std::vector<std::uint8_t> bytes;
    };

    // Native writers finish with gen_footer followed by an empty EOF chunk.
    // Read that bounded tail instead of seeking through every network packet.
    // Older layouts fall back to the existing sequential reader.
    inline std::optional<footer> read_footer(std::istream& file)
    {
        const auto original = file.tellg();
        file.clear(); file.seekg(0, std::ios::end);
        const auto end = file.tellg();
        std::optional<footer> result;
        if (end >= std::streamoff{10})
        {
            const auto size = static_cast<std::size_t>(std::min<std::streamoff>(end, 131072 + 10));
            std::vector<std::uint8_t> tail(size);
            file.seekg(end - static_cast<std::streamoff>(size));
            if (file.read(reinterpret_cast<char*>(tail.data()), size))
            {
                std::size_t eof = size;
                if (tail[size - 2] == 159 && tail[size - 1] == 0) eof -= 2;
                else if (size >= 5 && tail[size - 5] == 31 && !tail[size - 4]
                    && !tail[size - 3] && !tail[size - 2] && !tail[size - 1]) eof -= 5;
                if (eof != size)
                    for (std::size_t at = 0; at + 2 + 8 <= eof; ++at)
                    {
                        const auto tag = tail[at];
                        if (tag != 6 && tag != 134) continue;
                        const auto header = tag == 134 ? 2u : 5u;
                        if (at + header + 8 > eof) continue;
                        std::uint32_t length = tail[at + 1];
                        if (header == 5) std::memcpy(&length, tail.data() + at + 1, 4);
                        if (length < 8 || length > 131072 || length != eof - at - header) continue;
                        footer value;
                        std::memcpy(&value.first, tail.data() + at + header, 4);
                        std::memcpy(&value.last, tail.data() + at + header + 4, 4);
                        if (value.first < 0 || value.last < value.first) continue;
                        value.bytes.assign(tail.begin() + at + header, tail.begin() + eof);
                        result = std::move(value); break;
                    }
            }
        }
        file.clear(); file.seekg(original);
        return result;
    }
}
