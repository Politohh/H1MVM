#pragma once
#include "cod4_demo_reader.hpp"
#include "demo_file_footer.hpp"
#include <limits>
#include <climits>

namespace theater_library
{
    struct metadata
    {
        std::string map, gametype, map_title, mode_title, recorded;
        int duration{};
        bool converted{};
    };

    inline std::vector<std::string> strings(std::span<const char> bytes)
    {
        std::vector<std::string> result;
        std::size_t start = 0;
        for (std::size_t i = 0; i < bytes.size(); ++i)
            if (!bytes[i]) { result.emplace_back(bytes.data() + start, i - start); start = i + 1; }
        return result;
    }

    // Read the metadata prefix and bounded footer for current native recordings.
    // Older layouts skip network payloads without decompressing them.
    // Validate chunk lengths against the actual remaining file before seeking.
    inline metadata read_native(std::istream& file)
    {
        metadata result;
        file.seekg(0, std::ios::end);
        const auto end = file.tellg();
        file.seekg(0);
        const auto tail = demo_file::read_footer(file);
        bool footer = false;
        for (unsigned guard = 0; guard < 1'000'000; ++guard)
        {
            std::uint8_t tag{};
            if (!file.read(reinterpret_cast<char*>(&tag), 1)) break;
            const auto id = tag & 31;
            if (id == 31) break;
            std::uint32_t size{};
            file.read(reinterpret_cast<char*>(&size), tag & 128 ? 1 : 4);
            if (!file || size > 131072 || end - file.tellg() < size)
                throw std::runtime_error("Truncated or invalid demo chunk");
            if (id == 1 || id == 5 || id == 6)
            {
                std::vector<char> bytes(size);
                if (!file.read(bytes.data(), size)) throw std::runtime_error("Truncated demo metadata");
                if (id == 1)
                {
                    const auto fields = strings(bytes);
                    if (fields.size() != 2 || fields[0].empty()) throw std::runtime_error("Invalid demo map header");
                    result.map = fields[0]; result.gametype = fields[1];
                }
                else if (id == 5)
                {
                    const auto fields = strings(bytes);
                    if (fields.size() >= 9)
                    {
                        result.recorded = fields[0]; result.map_title = fields[7]; result.mode_title = fields[8];
                        result.converted = fields[1] == "H1 COD4 conversion v1" || fields[1] == "H1 COD4 conversion v2"
                            || fields[1] == "H1 COD4 conversion v3" || fields[1] == "H1 COD4 conversion v4"
                            || fields[1] == "H1 COD4 conversion v5";
                    }
                }
                else if (size >= 8)
                {
                    std::int32_t first{}, last{};
                    std::memcpy(&first, bytes.data(), 4); std::memcpy(&last, bytes.data() + 4, 4);
                    const auto difference = static_cast<std::int64_t>(last) - first;
                    if (first < 0 || difference < 0 || difference > INT_MAX) throw std::runtime_error("Invalid demo duration");
                    result.duration = static_cast<int>(difference); footer = true;
                }
            }
            else file.seekg(size, std::ios::cur);
            if (tail && !result.map.empty())
            {
                const auto difference = static_cast<std::int64_t>(tail->last) - tail->first;
                if (difference > INT_MAX) throw std::runtime_error("Invalid demo duration");
                result.duration = static_cast<int>(difference); footer = true; break;
            }
        }
        if (result.map.empty() || !footer) throw std::runtime_error("Recording is incomplete or unsupported");
        return result;
    }

    inline metadata read_source(const std::filesystem::path& path)
    {
        const auto track = cod4::load(path);
        metadata result;
        result.map = track.map;
        result.duration = track.frames.back().time - track.frames.front().time;
        const auto info = track.configstrings.find(0);
        if (info != track.configstrings.end())
        {
            constexpr std::string_view key = "\\g_gametype\\";
            const auto start = info->second.find(key);
            if (start != std::string::npos)
            {
                const auto begin = start + key.size();
                result.gametype = info->second.substr(begin, info->second.find('\\', begin) - begin);
            }
        }
        return result;
    }
}
