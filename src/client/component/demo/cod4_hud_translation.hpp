#pragma once

#include "cod4_world_translation.hpp"
#include "cod4_obituary_events.hpp"
#include "cod4_model_translation.hpp"
#include <set>

namespace cod4
{
    inline std::uint32_t native_team_selection(unsigned team, std::int32_t minimum)
    {
        // Integer omnvars store value - minimum. ui_team_selected is a team
        // enum (-1..3), not a boolean indicating that a team was selected.
        if (team > 3 || minimum > 0)
            throw std::runtime_error("Invalid native HUD team selection");
        return static_cast<std::uint32_t>(static_cast<std::int64_t>(team) - minimum);
    }

    inline std::string_view native_faction(character_family family)
    {
        switch (family)
        {
        case character_family::usmc_desert: return "marines";
        case character_family::arab_desert: return "opfor";
        case character_family::sas_ct:
        case character_family::sas_woodland: return "sas";
        case character_family::spetsnaz_urban:
        case character_family::spetsnaz_woodland: return "russian";
        }
        return {};
    }

    // COD4 iw_11.iwd offhandClass: frag=1, smoke/concussion=2, flash=3.
    // H1 stores the actual lethal/tactical weapon handles independently of
    // the currently selected offHand weapon. An exhausted owned grenade must
    // retain its icon and display zero through the normal keyed ammo records.
    inline unsigned source_offhand_class(std::string_view name)
    {
        if (name == "frag_grenade_mp" || name == "frag_grenade_short_mp" || name == "brick_bomb_mp") return 1;
        if (name == "smoke_grenade_mp" || name == "concussion_grenade_mp") return 2;
        if (name == "flash_grenade_mp") return 3;
        return 0;
    }

    template <typename Name>
    std::pair<unsigned, unsigned> select_hud_offhands(const std::vector<unsigned>& inventory,
        unsigned secondary_selection, Name name)
    {
        std::pair<unsigned, unsigned> result{};
        // PM_Weapon_CheckForOffHand maps offhandSecondary 0 -> smoke (2),
        // nonzero -> flash (3); it is not itself an offhandClass value.
        const unsigned secondary_class = secondary_selection ? 3 : 2;
        for (const auto weapon : inventory)
        {
            const auto kind = source_offhand_class(name(weapon));
            if (kind == 1 && !result.first) result.first = weapon;
            if (kind && kind != 1 && kind == secondary_class && !result.second) result.second = weapon;
        }
        return result;
    }

    // Map fastfiles may retain the COD4 name or use MWR's DLC material name.
    // Select only an actual native asset, retaining the recorded compass bounds.
    template <typename Available>
    std::optional<std::string> resolve_minimap_material(const std::string& config, Available available)
    {
        const auto start = config.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return std::nullopt;
        const bool quoted = config[start] == '"';
        const auto begin = start + (quoted ? 1 : 0);
        const auto end = config.find_first_of(quoted ? "\"" : " \t\r\n", begin);
        if (end == std::string::npos || end == begin) return std::nullopt;
        const auto name = config.substr(begin, end - begin);
        if (!name.starts_with("compass_map_") || name == "compass_map_default") return std::nullopt;
        if (available(name)) return config;
        const auto alternate = name.ends_with("_dlc") ? name.substr(0, name.size() - 4) : name + "_dlc";
        if (!available(alternate)) return std::nullopt;
        auto result = config;
        result.replace(begin, end - begin, alternate);
        return result;
    }

    struct hitmarker_event
    {
        int time{};
        bool juggernaut{};
    };

    inline std::vector<hitmarker_event> collect_hitmarkers(const world_track& world,
        const std::map<int, std::string>& configstrings)
    {
        std::vector<hitmarker_event> events;
        std::set<unsigned> seen;
        for (const auto& frame : world.hud_frames)
        {
            for (const auto& group : frame.groups)
            for (const auto& element : group)
            {
                if (element.get(0) != 6) continue;
                const auto material = configstrings.find(2002 + element.get(64));
                if (material == configstrings.end() || (material->second != "damage_feedback"
                    && material->second != "damage_feedback_j")) continue;
                const auto time = element.get(44);
                // This shader fades FROM visible immediately on a hit. Unlike
                // MP_PLUS, its fadeStartTime is the hit, not the dismissal.
                // Ignore initial history and stale HUD restored by edited demos.
                if (!time || !seen.insert(time).second || &frame == &world.hud_frames.front()
                    || !(element.get(40) >> 24) || (element.get(36) >> 24) || !element.get(48)
                    || static_cast<std::int64_t>(frame.time) - time < 0
                    || static_cast<std::int64_t>(frame.time) - time > 250) continue;
                events.push_back({static_cast<int>(time), material->second == "damage_feedback_j"});
            }
        }
        std::ranges::sort(events, {}, &hitmarker_event::time);
        return events;
    }

    inline void translate_minimap_configstrings(const std::map<int, std::string>& source,
        std::map<int, std::string>& target)
    {
        // H1 SetMiniMap (0x440B10) writes 1228; CG (0x305E10) parses the
        // material and four bounds there. North yaw is 1227 (0x305F60).
        // COD4 uses 823/822. Map center (12 -> 8) is a separate field.
        if (const auto map = source.find(823); map != source.end())
        {
            target[1228] = map->second;
            const auto north = source.find(822);
            target[1227] = north == source.end() ? "0" : north->second;
        }
    }

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

    // Promod makes MP_PLUS opaque on the award, then starts fading it a second
    // later. fadeStartTime is the dismissal, not the award time. Detect the
    // visible edge, including repeated equal scores, instead of the fade-out.
    inline std::vector<score_popup_event> collect_score_popups(const world_track& world,
        const std::map<int, std::string>& configstrings)
    {
        std::vector<score_popup_event> events;
        const auto kills = collect_obituary_events(world);
        std::set<std::size_t> used_kills;
        const hud_frame* previous = nullptr;
        for (const auto& frame : world.hud_frames)
        {
            for (std::size_t slot = 0; slot < frame.groups[1].size(); ++slot)
            {
                const auto& element = frame.groups[1][slot];
                if (!previous || !is_score_popup(element, configstrings) || !(element.get(36) >> 24)) continue;
                const auto value = std::bit_cast<float>(element.get(120));
                if (!std::isfinite(value) || value < 1 || value > 10000 || std::trunc(value) != value) continue;
                if (slot < previous->groups[1].size())
                {
                    const auto& prior = previous->groups[1][slot];
                    if (is_score_popup(prior, configstrings) && (prior.get(36) >> 24)
                        && prior.get(120) == element.get(120)) continue;
                }
                auto time = frame.time;
                // The script HUD normally arrives one snapshot after the
                // obituary. Match only this recorder's recent, unused kill;
                // assists/objective awards keep their own recorded time.
                for (std::size_t i = kills.size(); i-- > 0;)
                {
                    const auto& kill = kills[i];
                    if (kill.time > frame.time) continue;
                    if (frame.time - kill.time > 150) break;
                    if (kill.attacker != static_cast<unsigned>(world.client_num)
                        || kill.victim == kill.attacker || !used_kills.insert(i).second) continue;
                    time = kill.time;
                    break;
                }
                auto& event = events.emplace_back(score_popup_event{time, element});
                event.element.set(44, static_cast<unsigned>(time));
            }
            previous = &frame;
        }
        std::ranges::sort(events, {}, &score_popup_event::time);
        return events;
    }
}
