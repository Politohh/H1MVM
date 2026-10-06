#pragma once
#include "cod4_world_translation.hpp"
#include <span>
#include <set>

namespace cod4
{
    struct corpse_identity
    {
        unsigned entity{}, team{};
        std::uint16_t body{};
        std::array<std::uint16_t, 6> attachments{};
        bool operator==(const corpse_identity&) const = default;
    };
    struct corpse_frame
    {
        int time{};
        std::vector<corpse_identity> corpses;
    };
    struct corpse_session
    {
        std::vector<corpse_frame> frames;
        const corpse_identity* sample(unsigned entity, int time) const
        {
            const auto next = std::upper_bound(frames.begin(), frames.end(), time,
                [](int t, const corpse_frame& frame) { return t < frame.time; });
            if (next == frames.begin()) return nullptr;
            for (const auto& corpse : std::prev(next)->corpses) if (corpse.entity == entity) return &corpse;
            return nullptr;
        }
    };
    inline void validate_corpse(const corpse_identity& value)
    {
        if (value.entity < 42 || value.entity >= 50 || value.team > 3 || !value.body || value.body >= 1024)
            throw std::runtime_error("Invalid converted corpse identity");
        for (const auto model : value.attachments)
            if (model >= 1024) throw std::runtime_error("Invalid converted corpse attachment");
    }
    inline std::vector<std::uint8_t> encode_corpses(const corpse_session& session)
    {
        std::vector<std::uint8_t> bytes;
        const auto put = [&bytes](const auto& value)
        {
            const auto* data = reinterpret_cast<const std::uint8_t*>(&value);
            bytes.insert(bytes.end(), data, data + sizeof(value));
        };
        put(std::uint32_t{0x31494334}); // 4CI1, versioned independently of native packets.
        put(static_cast<std::uint32_t>(session.frames.size()));
        int previous = -1;
        for (const auto& frame : session.frames)
        {
            if (frame.time <= previous || frame.corpses.size() > 8) throw std::runtime_error("Invalid converted corpse frame");
            previous = frame.time;
            put(frame.time); put(static_cast<std::uint8_t>(frame.corpses.size()));
            std::set<unsigned> entities;
            for (const auto& corpse : frame.corpses)
            {
                validate_corpse(corpse);
                if (!entities.insert(corpse.entity).second) throw std::runtime_error("Duplicate converted corpse");
                put(static_cast<std::uint8_t>(corpse.entity)); put(static_cast<std::uint8_t>(corpse.team));
                put(corpse.body); put(corpse.attachments);
            }
        }
        if (bytes.size() > 16 * 1024 * 1024) throw std::runtime_error("Oversized converted corpse data");
        return bytes;
    }
    inline corpse_session decode_corpses(std::span<const std::uint8_t> bytes)
    {
        if (bytes.empty()) return {};
        if (bytes.size() > 16 * 1024 * 1024) throw std::runtime_error("Oversized converted corpse data");
        std::size_t offset = 0;
        const auto read = [&]<typename T>()
        {
            if (sizeof(T) > bytes.size() - offset) throw std::runtime_error("Truncated converted corpse data");
            T value; std::memcpy(&value, bytes.data() + offset, sizeof(T)); offset += sizeof(T); return value;
        };
        if (read.operator()<std::uint32_t>() != 0x31494334) throw std::runtime_error("Unknown converted corpse format");
        const auto count = read.operator()<std::uint32_t>();
        if (count > (bytes.size() - offset) / 5) throw std::runtime_error("Invalid converted corpse frame count");
        corpse_session result;
        int previous = -1;
        for (unsigned i = 0; i < count; ++i)
        {
            corpse_frame frame; frame.time = read.operator()<int>();
            const auto corpse_count = read.operator()<std::uint8_t>();
            if (frame.time <= previous || corpse_count > 8) throw std::runtime_error("Invalid converted corpse frame");
            previous = frame.time;
            std::set<unsigned> entities;
            for (unsigned c = 0; c < corpse_count; ++c)
            {
                corpse_identity value;
                value.entity = read.operator()<std::uint8_t>(); value.team = read.operator()<std::uint8_t>();
                value.body = read.operator()<std::uint16_t>(); value.attachments = read.operator()<std::array<std::uint16_t, 6>>();
                validate_corpse(value);
                if (!entities.insert(value.entity).second) throw std::runtime_error("Duplicate converted corpse");
                frame.corpses.push_back(value);
            }
            result.frames.push_back(std::move(frame));
        }
        if (offset != bytes.size()) throw std::runtime_error("Trailing converted corpse data");
        return result;
    }
}
