#pragma once
#include "cod4_match.hpp"
#include <charconv>
#include <array>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cod4
{
    struct obituary_identity
    {
        unsigned attacker{}, victim{}, attacker_name{}, victim_name{}, attacker_team{}, victim_team{};
    };
    inline bool valid_model_name(std::string_view name)
    {
        if (name.empty() || name.size() > 128) return false;
        for (const auto c : name)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                || c == '_' || c == '/' || c == '.' || c == '-')) return false;
        return true;
    }
    struct model_metadata
    {
        std::vector<team_score_frame> team_scores;
        std::set<std::string> names;
        std::map<unsigned, std::string> bindings;
        std::map<unsigned, int> grenade_times;
        std::map<unsigned, int> car_fx_times;
        std::map<unsigned, int> bullet_times;
        std::map<unsigned, std::string> fx_bindings;
        std::map<unsigned, std::string> animation_bindings;
        std::map<unsigned, std::string> agent_names;
        std::map<unsigned, obituary_identity> obituaries;
    };
    inline model_metadata parse_model_metadata(const std::vector<std::string>& entries)
    {
        if (entries.size() > 2048) throw std::runtime_error("Too many converted metadata records");
        model_metadata result;
        for (const auto& entry : entries)
        {
            if (entry.starts_with("@teamscore:"))
            {
                auto text = entry.substr(11);
                std::replace(text.begin(), text.end(), ':', ' ');
                std::istringstream input(text);
                team_score_frame frame;
                std::string extra;
                if (!(input >> frame.time >> frame.axis >> frame.allies >> frame.limit) || (input >> extra)
                    || frame.time < 0 || std::abs(static_cast<long long>(frame.axis)) > 1000000
                    || std::abs(static_cast<long long>(frame.allies)) > 1000000 || frame.limit < 0 || frame.limit > 1000000
                    || (!result.team_scores.empty() && frame.time <= result.team_scores.back().time))
                    throw std::runtime_error("Invalid recorded team scores");
                result.team_scores.push_back(frame);
                continue;
            }
            if (entry.starts_with("@obit:"))
            {
                const std::string_view text(entry);
                std::array<unsigned, 7> values{};
                std::size_t start = 6;
                for (unsigned i = 0; i < values.size(); ++i)
                {
                    const auto end = i + 1 == values.size() ? text.size() : text.find(':', start);
                    if (end == text.npos) throw std::runtime_error("Incomplete recorded obituary identity");
                    const auto parsed = std::from_chars(text.data() + start, text.data() + end, values[i]);
                    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + end)
                        throw std::runtime_error("Invalid recorded obituary identity");
                    start = end + 1;
                }
                const auto [entity, attacker, victim, aname, vname, ateam, vteam] = values;
                if (entity < 1024 || entity >= 2046 || (attacker >= 42 && attacker != 2046) || victim >= 42
                    || (attacker != 2046 && (!aname || aname >= 1024)) || !vname || vname >= 1024 || ateam > 3 || vteam > 3
                    || !result.obituaries.emplace(entity, obituary_identity{attacker,victim,aname,vname,ateam,vteam}).second)
                    throw std::runtime_error("Invalid recorded obituary identity");
                continue;
            }
            if (entry.starts_with("@agentname:"))
            {
                const std::string_view text(entry);
                const auto separator = text.find(':', 11);
                if (separator == text.npos) throw std::runtime_error("Incomplete recorded agent name");
                unsigned index{};
                const auto parsed = std::from_chars(text.data() + 11, text.data() + separator, index);
                const auto name = text.substr(separator + 1);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + separator || !index || index >= 1024
                    || name.empty() || name.size() > 31 || name.find_first_of("\r\n\0", 0, 3) != name.npos
                    || !result.agent_names.emplace(index, name).second)
                    throw std::runtime_error("Invalid recorded agent name");
                continue;
            }
            if (entry.starts_with("@fx:") || entry.starts_with("@anim:"))
            {
                const std::string_view text(entry);
                const bool animation = entry.starts_with("@anim:");
                const auto prefix = animation ? 6u : 4u;
                auto& bindings = animation ? result.animation_bindings : result.fx_bindings;
                const auto separator = text.find(':', prefix);
                if (separator == text.npos) throw std::runtime_error("Incomplete converted FX binding");
                unsigned index{};
                const auto parsed = std::from_chars(text.data() + prefix, text.data() + separator, index);
                const auto name = text.substr(separator + 1);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + separator || !index || index >= (animation ? 255u : 512u)
                    || !valid_model_name(name) || !bindings.emplace(index, name).second)
                    throw std::runtime_error("Invalid converted FX binding");
                continue;
            }
            if (entry.starts_with("@grenade:") || entry.starts_with("@carfx:") || entry.starts_with("@impact:")
                || entry.starts_with("@scriptfx:"))
            {
                const std::string_view text(entry);
                const bool scripted = entry.starts_with("@scriptfx:");
                const bool car = entry.starts_with("@carfx:") || scripted;
                const bool bullet = entry.starts_with("@impact:");
                const auto prefix = scripted ? 10u : car ? 7u : bullet ? 8u : 9u;
                auto& times = car ? result.car_fx_times : bullet ? result.bullet_times : result.grenade_times;
                const auto separator = text.find(':', prefix);
                if (separator == text.npos) throw std::runtime_error("Incomplete converted FX timing record");
                unsigned number{};
                int time{};
                const auto entity = std::from_chars(text.data() + prefix, text.data() + separator, number);
                const auto clock = std::from_chars(text.data() + separator + 1, text.data() + text.size(), time);
                if (entity.ec != std::errc{} || entity.ptr != text.data() + separator || number < 1024 || number >= 2046
                    || clock.ec != std::errc{} || clock.ptr != text.data() + text.size() || time < 0
                    || result.grenade_times.contains(number) || result.car_fx_times.contains(number) || result.bullet_times.contains(number)
                    || !times.emplace(number, time).second)
                    throw std::runtime_error("Invalid converted FX timing record");
                continue;
            }
            if (!entry.starts_with("@model:"))
            {
                if (!valid_model_name(entry)) throw std::runtime_error("Invalid converted model name");
                result.names.insert(entry);
                continue;
            }
            const std::string_view text(entry);
            const auto separator = text.find(':', 7);
            if (separator == text.npos) throw std::runtime_error("Incomplete model binding");
            unsigned index{};
            const auto parsed = std::from_chars(text.data() + 7, text.data() + separator, index);
            const auto name = text.substr(separator + 1);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + separator || index < 768
                || index >= 1024 || !valid_model_name(name)) throw std::runtime_error("Invalid model binding");
            if (!result.bindings.emplace(index, name).second) throw std::runtime_error("Duplicate model binding");
            result.names.emplace(name);
        }
        if (result.names.size() > 512) throw std::runtime_error("Too many converted model names");
        for (const auto& [entity, value] : result.obituaries)
            if (!result.agent_names.contains(value.victim_name)
                || (value.attacker != 2046 && !result.agent_names.contains(value.attacker_name)))
                throw std::runtime_error("Missing recorded obituary name");
        if (!result.car_fx_times.empty() && result.fx_bindings.empty())
            throw std::runtime_error("Converted car FX has no native binding");
        return result;
    }
}
