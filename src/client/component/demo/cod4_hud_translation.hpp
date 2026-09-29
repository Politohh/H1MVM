#pragma once

#include "cod4_world_translation.hpp"
#include <charconv>
#include <set>

namespace cod4
{
    using h1_hud_element = std::array<std::uint8_t, 192>;

    // A CoD4 HE_TYPE_VALUE carrying MP_PLUS is the recorded score popup.
    // The converted client demo triggers H1's LUI popup from its omnvar;
    // this preserves the archival HUD value for other playback paths.
    // The other CoD4 HUD elements need separate asset/layout mapping and are
    // deliberately not copied as raw structs: H1's element is 192, not 160 bytes.
    inline h1_hud_element translate_score_popup(const wire_state<160>& source,
        unsigned native_plus_label)
    {
        if (source.get(0) != 2 || !native_plus_label || native_plus_label >= 2048)
            throw std::runtime_error("Invalid native score popup mapping");
        h1_hud_element target{};
        const auto put = [&](std::size_t offset, auto value)
        {
            if (offset + sizeof(value) > target.size()) throw std::runtime_error("Invalid H1 HUD offset");
            std::memcpy(target.data() + offset, &value, sizeof(value));
        };
        // Offsets/widths are from the H1 PC HudElem netfield table
        // (0x12D7CB0) and MSG_WriteDeltaHudElems (0x4F4B90). In particular,
        // the writer checks type at +28 before serializing an element.
        put(0, static_cast<std::uint16_t>(translate_entity_number(source.get(16))));
        put(4, source.get(28));
        // CoD4's six-bit screen anchor (18 for this popup) is not H1's
        // four-bit anchor. Place the native score value above screen center.
        put(8, 5u);
        put(16, source.get(4));
        put(20, source.get(8));
        put(24, source.get(12));
        put(28, source.get(0));
        put(32, source.get(20));
        put(48, source.get(36));
        put(52, source.get(40));
        put(56, source.get(44));
        put(60, source.get(48));
        put(76, native_plus_label);
        put(128, source.get(120));
        return target;
    }

    inline bool is_score_popup(const wire_state<160>& source,
        const std::map<int, std::string>& configstrings)
    {
        if (source.get(0) != 2 || !source.get(52)) return false;
        const auto found = configstrings.find(309 + source.get(52));
        return found != configstrings.end() && found->second == "MP_PLUS";
    }

    struct score_popup_event
    {
        int time{};
        wire_state<160> element;
    };

    // In some Promod recordings the archival HUD popup is authored well after
    // the score changes. Match a consistent run of +N popups to the recorded
    // team-score commands; otherwise retain the HUD's original timing.
    inline std::vector<score_popup_event> collect_score_popups(const world_track& world,
        const std::map<int, std::string>& configstrings)
    {
        struct score_change { int time; char team; int delta; };
        std::vector<score_change> changes;
        std::array<int, 2> prior{};
        std::array<bool, 2> seen{};
        for (const auto& command : world.commands)
        {
            const auto& text = command.text;
            if (text.size() < 3 || (text[0] != 'G' && text[0] != 'H') || text[1] != ' ') continue;
            int score{};
            const auto [end, error] = std::from_chars(text.data() + 2, text.data() + text.size(), score);
            if (error != std::errc{} || end != text.data() + text.size()) continue;
            const unsigned team = text[0] == 'H';
            if (seen[team] && score > prior[team]) changes.push_back({command.time, text[0], score - prior[team]});
            prior[team] = score;
            seen[team] = true;
        }

        std::vector<score_popup_event> events;
        unsigned last_start{};
        for (const auto& frame : world.hud_frames)
            for (const auto& element : frame.groups[1])
            {
                if (!is_score_popup(element, configstrings)) continue;
                const auto start = element.get(44);
                if (start == last_start || start > static_cast<unsigned>(frame.time + 100)
                    || start + 100 < static_cast<unsigned>(frame.time)) continue;
                last_start = start;
                events.push_back({static_cast<int>(start), element});
            }

        struct candidate { std::size_t event; std::size_t change; int delay; };
        std::vector<candidate> candidates;
        std::map<std::pair<char, int>, unsigned> support;
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            const auto value = std::bit_cast<float>(events[i].element.get(120));
            if (!std::isfinite(value) || value < 1 || value > 10000 || std::trunc(value) != value) continue;
            for (std::size_t j = 0; j < changes.size(); ++j)
            {
                const int delay = events[i].time - changes[j].time;
                if (changes[j].delta != static_cast<int>(value) || delay < 250 || delay > 2000) continue;
                candidates.push_back({i, j, delay});
                ++support[{changes[j].team, (delay + 25) / 50}];
            }
        }
        if (!support.empty())
        {
            const auto best = std::max_element(support.begin(), support.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
            if (best->second >= 2)
            {
                std::set<std::size_t> used;
                for (const auto& candidate : candidates)
                {
                    const auto& change = changes[candidate.change];
                    if (change.team != best->first.first
                        || std::abs(candidate.delay - best->first.second * 50) > 50
                        || !used.insert(candidate.change).second) continue;
                    auto& event = events[candidate.event];
                    event.time = change.time;
                    event.element.set(44, static_cast<unsigned>(event.time));
                }
            }
        }
        std::ranges::sort(events, {}, &score_popup_event::time);
        return events;
    }
}
