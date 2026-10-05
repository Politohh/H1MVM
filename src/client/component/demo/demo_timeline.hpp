#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Optional index appended after the existing footer's duration string. Older
// demos keep their original footer; the network stream and chunk IDs are unchanged.
namespace demo_timeline
{
    struct event
    {
        std::int32_t time{}, attacker{}, victim{};
        bool operator==(const event&) const = default;
    };

    struct index
    {
        int pov{-1};
        std::vector<event> events;
    };

    inline constexpr std::size_t max_events = 10'000;

    inline bool valid_client(int client) { return client >= 0 && client < 42; }
    inline bool valid_event(const event& e, int first, int last)
    {
        return e.time >= first && e.time <= last && valid_client(e.victim)
            && (valid_client(e.attacker) || e.attacker == 2046);
    }

    inline std::string encode(int pov, std::span<const event> events, int first, int last)
    {
        if (!valid_client(pov) || last < first) return {};
        std::vector<event> sorted;
        for (const auto& e : events) if (valid_event(e, first, last)) sorted.push_back(e);
        std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        if (sorted.size() > max_events) sorted.resize(max_events);
        std::string result("TUI1", 4);
        const auto append = [&](auto value) { result.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
        append(static_cast<std::int32_t>(pov));
        append(static_cast<std::uint32_t>(sorted.size()));
        for (const auto& e : sorted) { append(e.time); append(e.attacker); append(e.victim); }
        return result;
    }

    inline std::optional<index> decode(std::span<const std::uint8_t> footer, int first, int last)
    {
        if (footer.size() < 9 || last < first) return std::nullopt;
        const auto terminator = std::find(footer.begin() + 8, footer.end(), 0);
        if (terminator == footer.end()) return std::nullopt;
        const auto offset = static_cast<std::size_t>(terminator - footer.begin()) + 1;
        const auto data = footer.subspan(offset);
        if (data.size() < 12 || std::memcmp(data.data(), "TUI1", 4) != 0) return std::nullopt;
        const auto read = [&](std::size_t at) { std::int32_t value; std::memcpy(&value, data.data() + at, 4); return value; };
        const auto pov = read(4), count = read(8);
        if (!valid_client(pov) || count < 0 || count > max_events || data.size() != 12 + static_cast<std::size_t>(count) * 12)
            return std::nullopt;
        index result{pov, {}};
        result.events.reserve(count);
        for (int i = 0; i < count; ++i)
        {
            const auto at = 12 + static_cast<std::size_t>(i) * 12;
            event e{read(at), read(at + 4), read(at + 8)};
            if (!valid_event(e, first, last) || (!result.events.empty() && e.time < result.events.back().time))
                return std::nullopt;
            result.events.push_back(e);
        }
        return result;
    }

    inline int clamp_target(int target, int first, int last)
    {
        // Keep a forward seek inside the last snapshot, rather than feeding EOF.
        return std::clamp(target, first, std::max(first, last - 250));
    }
}
