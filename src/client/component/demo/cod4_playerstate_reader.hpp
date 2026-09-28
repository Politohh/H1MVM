#pragma once
#include "cod4_demo_reader.hpp"
#include "cod4_player_fields.hpp"
#include <bit>

namespace cod4
{
    struct player_frame
    {
        int time{}, sequence{};
        std::array<std::uint32_t, player_fields.size()> values{};
        std::array<std::int16_t, 64> ammo{};
        std::array<std::int16_t, 128> clips{};
        std::uint32_t integer(std::string_view name) const
        {
            for (std::size_t i = 0; i < player_fields.size(); ++i)
                if (player_fields[i].name == name) return values[i];
            throw std::runtime_error("Unknown COD4 player field");
        }
        float real(std::string_view name) const { return std::bit_cast<float>(integer(name)); }
    };

    inline float interpolate_ads(const std::vector<player_frame>& players, int time)
        {
            if (players.empty()) throw std::runtime_error("Cannot sample an empty player track");
            const auto next = std::upper_bound(players.begin(), players.end(), time,
                [](int t, const player_frame& f) { return t < static_cast<int>(f.integer("commandTime")); });
            if (next == players.begin()) return players.front().real("fWeaponPosFrac");
            const auto& prior = *(next - 1);
            if (next == players.end() || prior.integer("weapon") != next->integer("weapon"))
                return prior.real("fWeaponPosFrac");
            const auto start = static_cast<int>(prior.integer("commandTime"));
            const auto end = static_cast<int>(next->integer("commandTime"));
            if (end <= start || end - start > 250) return prior.real("fWeaponPosFrac");
            return std::lerp(prior.real("fWeaponPosFrac"), next->real("fWeaponPosFrac"),
                std::clamp(static_cast<float>(time - start) / (end - start), 0.0f, 1.0f));
        }

    namespace detail
    {
        // Player fields use hybrid bit/raw-byte coding. Do not read multi-byte
        // integer payloads as a contiguous bitstream.
        inline std::uint32_t player_delta(reader& msg, int time, player_field field, std::uint32_t old,
            const std::array<float, 3>& center = {})
        {
            if (field.hint != 2 && !msg.bits(1)) return old;
            const auto float_bits = [](float f) { return std::bit_cast<std::uint32_t>(f); };
            const auto short_float = [&](int n, int bias)
            {
                const auto low = msg.bits(n);
                const auto encoded = (static_cast<unsigned>(msg.get<std::uint8_t>()) << n) | low;
                const float prior = std::bit_cast<float>(old);
                if (!std::isfinite(prior) || std::abs(prior) > 10000000)
                    throw std::runtime_error("Invalid compressed COD4 float baseline");
                return float_bits(static_cast<float>(static_cast<int>(encoded ^ (static_cast<int>(prior) + bias)) - bias));
            };
            switch (field.bits)
            {
            case -85:
                if (msg.bits(1)) return (old & 0xFFFFFFu) | ((old >> 24 ? 0u : 255u) << 24);
                {
                    auto color = old & 0xFFFFFFu;
                    if (!msg.bits(1))
                    {
                        const auto r = msg.get<std::uint8_t>(), g = msg.get<std::uint8_t>(), b = msg.get<std::uint8_t>();
                        color = r | (g << 8) | (b << 16);
                    }
                    return color | ((8 * msg.bits(5)) << 24);
                }
            case -86: return float_bits(msg.bits(5) / 10.0f + 1.4f);
            case -90: case -91: case -92:
                {
                    const float prior = std::bit_cast<float>(old);
                    if (!msg.bits(1)) return float_bits(prior + static_cast<int>(msg.bits(7)) - 64);
                    const auto c = static_cast<int>(center[field.bits + 92] + 0.5f);
                    return float_bits(static_cast<float>(c + static_cast<int>((static_cast<int>(prior) + 32768 - c) ^ msg.bits(16)) - 32768));
                }
            case -93: case -94: return msg.get<std::uint8_t>();
            case -96:
                if (msg.bits(1)) return 1022;
                if (msg.bits(1)) return 0;
                { const auto low = msg.bits(2); return low | (static_cast<unsigned>(msg.get<std::uint8_t>()) << 2); }
            case -99:
                if (!msg.bits(1)) return 0;
                if (!msg.bits(1)) return short_float(4, 2048);
                return old ^ msg.get<std::uint32_t>();
            case -88: return old ^ msg.get<std::uint32_t>();
            case -87: return float_bits(msg.get<std::uint16_t>() * (360.0f / 65536.0f));
            case -100: return msg.bits(1) ? float_bits(msg.get<std::uint16_t>() * (360.0f / 65536.0f)) : 0;
            case -97: return msg.bits(1) ? msg.get<std::uint32_t>() : time - msg.bits(8);
            case -98:
                if (!msg.bits(1)) return old ^ (1u << msg.bits(5));
                {
                    const auto a = msg.get<std::uint8_t>();
                    const auto b = msg.get<std::uint8_t>();
                    const auto c = msg.get<std::uint8_t>();
                    return a | (b << 8) | (c << 16);
                }
            case 0:
                if (!msg.bits(1)) return msg.bits(1) << 31;
                if (!msg.bits(1)) return short_float(5, 4096);
                return old ^ msg.get<std::uint32_t>();
            default:
                if (field.bits < -32 || field.bits > 32) throw std::runtime_error("Unsupported COD4 player field encoding");
                if (!msg.bits(1)) return 0;
                const int count = std::abs(field.bits), remainder = count & 7;
                auto value = msg.bits(remainder);
                for (int i = remainder; i < count; i += 8) value |= static_cast<std::uint32_t>(msg.get<std::uint8_t>()) << i;
                const auto mask = count == 32 ? UINT32_MAX : (1u << count) - 1;
                value ^= old & mask;
                if (field.bits < 0 && (value & (1u << (count - 1)))) value |= ~mask;
                return value;
            }
        }
    }

    // Decode fixed player fields and ammo. Entity and HUD tails remain untouched;
    // no source server commands
    // are executed. Each file packet supplies its own framing boundary.
    inline std::vector<player_frame> decode_players(std::span<const std::uint8_t> bytes)
    {
        detail::reader file{bytes};
        std::map<int, player_frame> baselines;
        std::vector<player_frame> frames;
        bool first = true, eof = false;
        while (file.pos < bytes.size())
        {
            const auto type = file.get<std::uint8_t>();
            if (type == 1) { file.require(52); file.pos += 52; continue; }
            if (type != 0) throw std::runtime_error("Unsupported COD4 block in player decoder");
            const int sequence = file.get<int>(), size = file.get<int>();
            if (size == -1) { eof = true; break; }
            if (size < 4 || size > 0x20000) throw std::runtime_error("Invalid COD4 player packet length");
            file.require(size);
            const auto packet = bytes.subspan(file.pos, size);
            file.pos += size;
            if (first) { first = false; continue; }
            const auto decoded = detail::decompress(packet.subspan(4));
            detail::reader msg{decoded};
            while (msg.pos < decoded.size())
            {
                const auto op = msg.get<std::uint8_t>();
                if (op == 7) break;
                if (op == 0) continue;
                if (op == 4) { msg.get<int>(); msg.string(); continue; }
                if (op == 11) { msg.get<int>(); msg.get<std::uint8_t>(); msg.string(); msg.string(); continue; }
                if (op != 6) throw std::runtime_error("Unsupported COD4 service " + std::to_string(op));
                player_frame frame;
                frame.time = msg.get<int>(); frame.sequence = sequence;
                const auto delta = msg.get<std::uint8_t>();
                msg.get<std::uint8_t>(); // snapshot flags
                if (delta)
                {
                    const auto previous = baselines.find(sequence - delta);
                    if (previous == baselines.end()) throw std::runtime_error("Missing COD4 player delta baseline " + std::to_string(sequence - delta));
                    frame.values = previous->second.values;
                    frame.ammo = previous->second.ammo;
                    frame.clips = previous->second.clips;
                }
                const bool absolute_prediction = msg.bits(1) != 0;
                const auto count = msg.bits(8);
                if (count > player_fields.size()) throw std::runtime_error("Invalid COD4 player field count");
                for (std::size_t i = 0; i < count; ++i)
                {
                    const auto old = absolute_prediction && player_fields[i].hint == 3 ? 0 : frame.values[i];
                    frame.values[i] = detail::player_delta(msg, frame.time, player_fields[i], old);
                }
                // Fixed fields are followed by stats, reserve ammo and clip ammo.
                if (msg.bits(1))
                {
                    const auto mask = msg.bits(5);
                    for (unsigned i = 0; i < 3; ++i)
                        if (mask & (1u << i)) msg.get<std::int16_t>();
                    if (mask & 8) msg.bits(6);
                    if (mask & 16) msg.get<std::uint8_t>();
                }
                const auto ammo_groups = [&](auto& counts, unsigned groups)
                {
                    for (unsigned group = 0; group < groups; ++group)
                        if (msg.bits(1))
                        {
                            const auto mask = msg.get<std::uint16_t>();
                            for (unsigned i = 0; i < 16; ++i)
                                if (mask & (1u << i)) counts[group * 16 + i] = msg.get<std::int16_t>();
                        }
                };
                if (msg.bits(1)) ammo_groups(frame.ammo, 4);
                ammo_groups(frame.clips, 8);
                if (frame.time < 0 || !std::isfinite(frame.real("fWeaponPosFrac"))
                    || frame.real("fWeaponPosFrac") < 0 || frame.real("fWeaponPosFrac") > 1)
                    throw std::runtime_error("Invalid COD4 player snapshot");
                baselines[sequence] = frame;
                while (!baselines.empty() && baselines.begin()->first < sequence - 255) baselines.erase(baselines.begin());
                while (!frames.empty() && frames.back().time >= frame.time) frames.pop_back();
                frames.push_back(frame);
                break; // The remainder is outside this prefix decoder's scope.
            }
        }
        if (!eof || file.pos != bytes.size() || frames.empty()) throw std::runtime_error("Incomplete COD4 player track");
        return frames;
    }
}
