#pragma once

// Portable COD4 archive decoder. No H1 addresses, assets or runtime dependencies.
#include "cod4_huffman_table.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace cod4
{
    struct camera_frame
    {
        std::int32_t time{};
        std::array<float, 3> origin{}, velocity{}, angles{};
        std::int32_t movement_dir{}, bob_cycle{};
    };

    struct camera_track
    {
        std::string map;
        // Source indices are COD4 indices. Translate names/meaning before using
        // these in H1; the two games do not share a configstring layout.
        std::map<int, std::string> configstrings;
        std::vector<camera_frame> frames;
        std::size_t packets{}, archives{}, replaced_frames{};

        camera_frame sample(double time) const
        {
            if (frames.empty()) throw std::runtime_error("Empty camera track");
            if (time <= frames.front().time) return frames.front();
            if (time >= frames.back().time) return frames.back();
            const auto next = std::upper_bound(frames.begin(), frames.end(), time,
                [](double t, const camera_frame& f) { return t < f.time; });
            const auto& a = *(next - 1);
            const auto& b = *next;
            auto out = a;
            // Do not fly through a teleport or bridge a missing-data interval.
            float distance2 = 0;
            for (int i = 0; i < 3; ++i) distance2 += (b.origin[i] - a.origin[i]) * (b.origin[i] - a.origin[i]);
            if (b.time - a.time > 250 || distance2 > 256 * 256) return out;
            const auto fraction = static_cast<float>((time - a.time) / (b.time - a.time));
            out.time = static_cast<std::int32_t>(time);
            for (int i = 0; i < 3; ++i)
            {
                out.origin[i] = a.origin[i] + fraction * (b.origin[i] - a.origin[i]);
                out.velocity[i] = a.velocity[i] + fraction * (b.velocity[i] - a.velocity[i]);
                out.angles[i] = a.angles[i] + fraction * std::remainder(b.angles[i] - a.angles[i], 360.0f);
            }
            return out;
        }
    };

    namespace detail
    {
        struct reader
        {
            std::span<const std::uint8_t> bytes;
            std::size_t pos{}, bit{};
            void require(std::size_t n) const
            {
                if (pos > bytes.size() || n > bytes.size() - pos)
                    throw std::runtime_error("Truncated COD4 demo at byte " + std::to_string(pos));
            }
            template <typename T> T get()
            {
                require(sizeof(T));
                T value;
                std::memcpy(&value, bytes.data() + pos, sizeof(T));
                pos += sizeof(T);
                return value;
            }
            std::uint32_t bits(int count)
            {
                std::uint32_t value = 0;
                for (int i = 0; i < count; ++i)
                {
                    // COD4's hybrid stream claims a byte only when a fresh bit byte is needed.
                    if ((bit & 7) == 0) { require(1); bit = pos++ * 8; }
                    value |= ((bytes[bit >> 3] >> (bit & 7)) & 1u) << i;
                    ++bit;
                }
                return value;
            }
            std::string string()
            {
                std::string value;
                for (auto c = get<std::uint8_t>(); c; c = get<std::uint8_t>())
                {
                    if (value.size() >= 65535) throw std::runtime_error("Oversized COD4 string");
                    value += static_cast<char>(c);
                }
                return value;
            }
        };

        inline std::vector<std::uint8_t> decompress(std::span<const std::uint8_t> input)
        {
            struct node { int child[2]{-1, -1}; int symbol{-1}; };
            static const auto tree = []
            {
                std::vector<node> nodes(1);
                for (int symbol = 0; symbol < 256; ++symbol)
                {
                    int index = 0;
                    for (auto c : huffman_codes[symbol])
                    {
                        const int b = c - '0';
                        if (nodes[index].child[b] < 0)
                        {
                            nodes[index].child[b] = static_cast<int>(nodes.size());
                            nodes.emplace_back();
                        }
                        index = nodes[index].child[b];
                    }
                    nodes[index].symbol = symbol;
                }
                return nodes;
            }();
            std::vector<std::uint8_t> out;
            std::size_t bit = 0;
            while (bit < input.size() * 8)
            {
                int index = 0;
                while (tree[index].symbol < 0)
                {
                    // The final symbol may use zero padding, as in MSG_ReadBitsCompress.
                    const int b = bit < input.size() * 8 ? (input[bit >> 3] >> (bit & 7)) & 1 : 0;
                    ++bit;
                    index = tree[index].child[b];
                    if (index < 0) throw std::runtime_error("Invalid COD4 Huffman code");
                }
                out.push_back(static_cast<std::uint8_t>(tree[index].symbol));
                if (out.size() > 0x20000) throw std::runtime_error("Oversized COD4 message");
            }
            return out;
        }

        inline void read_gamestate(std::span<const std::uint8_t> packet, camera_track& track)
        {
            if (packet.size() < 5) throw std::runtime_error("Missing COD4 gamestate");
            const auto decoded = decompress(packet.subspan(4)); // reliable acknowledgement is raw
            reader msg{decoded};
            if (msg.get<std::uint8_t>() != 1) throw std::runtime_error("Expected COD4 gamestate");
            msg.get<std::int32_t>(); // server command sequence
            if (msg.get<std::uint8_t>() != 2) throw std::runtime_error("Expected COD4 configstrings");
            const auto count = msg.get<std::uint16_t>();
            if (count > 4096) throw std::runtime_error("Unsupported COD4 configstring layout");
            int index = -1;
            for (unsigned i = 0; i < count; ++i)
            {
                const int next = msg.bits(1) ? index + 1 : static_cast<int>(msg.bits(12));
                if (next <= index || next >= 4096) throw std::runtime_error("Invalid COD4 configstring index");
                index = next;
                const auto value = msg.string();
                track.configstrings.emplace(index, value);
                if (index != 0) continue;
                constexpr std::string_view key = "\\mapname\\";
                const auto begin = value.find(key);
                if (begin == std::string::npos) throw std::runtime_error("COD4 mapname missing");
                const auto map = value.substr(begin + key.size(), value.find('\\', begin + key.size()) - (begin + key.size()));
                if (map.empty() || map.size() > 64 || map.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
                    throw std::runtime_error("Invalid COD4 mapname");
                track.map = map;
            }
            if (track.map.empty()) throw std::runtime_error("COD4 serverinfo missing");
        }
    }

    inline camera_track decode(std::span<const std::uint8_t> bytes)
    {
        detail::reader file{bytes};
        camera_track track;
        bool eof = false;
        while (file.pos < bytes.size())
        {
            const auto type = file.get<std::uint8_t>();
            if (type == 0)
            {
                file.get<std::int32_t>(); // message sequence
                const auto length = file.get<std::int32_t>();
                if (length == -1) { eof = true; break; }
                if (length < 4 || length > 0x20000) throw std::runtime_error("Invalid COD4 packet length");
                file.require(static_cast<std::size_t>(length));
                if (track.packets++ == 0) detail::read_gamestate(bytes.subspan(file.pos, length), track);
                file.pos += length;
            }
            else if (type == 1)
            {
                const auto index = file.get<std::int32_t>();
                if (index < 0 || index > 255) throw std::runtime_error("Invalid COD4 archive index");
                camera_frame frame;
                for (auto& v : frame.origin) v = file.get<float>();
                for (auto& v : frame.velocity) v = file.get<float>();
                frame.movement_dir = file.get<std::int32_t>();
                frame.bob_cycle = file.get<std::int32_t>();
                frame.time = file.get<std::int32_t>();
                for (auto& v : frame.angles) v = file.get<float>();
                if (frame.time < 0) throw std::runtime_error("Invalid COD4 archive time");
                for (int i = 0; i < 3; ++i)
                    if (!std::isfinite(frame.origin[i]) || !std::isfinite(frame.velocity[i]) || !std::isfinite(frame.angles[i])
                        || std::abs(frame.origin[i]) > 1000000 || std::abs(frame.angles[i]) > 36000)
                        throw std::runtime_error("Invalid COD4 camera values");
                ++track.archives;
                // Later records replace overlapping pre-roll/prediction, preserving file chronology.
                while (!track.frames.empty() && track.frames.back().time >= frame.time)
                {
                    track.frames.pop_back();
                    ++track.replaced_frames;
                }
                track.frames.push_back(frame);
            }
            else throw std::runtime_error("Unsupported COD4 block type " + std::to_string(type));
        }
        if (!eof || file.pos != bytes.size()) throw std::runtime_error("Missing or invalid COD4 end marker");
        if (track.map.empty() || track.frames.size() < 2 || track.frames.back().time <= track.frames.front().time)
            throw std::runtime_error("COD4 demo has no playable camera track");
        return track;
    }

    inline camera_track load(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error("Cannot open COD4 demo");
        const auto size = file.tellg();
        if (size <= 0 || size > 256 * 1024 * 1024) throw std::runtime_error("COD4 demo must be between 1 byte and 256 MiB");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("Cannot read COD4 demo");
        return decode(bytes);
    }
}
