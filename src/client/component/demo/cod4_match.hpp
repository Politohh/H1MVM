#pragma once
#include "cod4_world_reader.hpp"
#include <set>
#include <cstdlib>

namespace cod4
{
    inline std::string recorded_gametype(const std::map<int, std::string>& configs)
    {
        const auto info = configs.find(0);
        if (info == configs.end()) return "war";
        constexpr std::string_view key = "\\g_gametype\\";
        const auto offset = info->second.find(key);
        if (offset == std::string::npos) return "war";
        const auto start = offset + key.size();
        const auto result = info->second.substr(start, info->second.find('\\', start) - start);
        if (result.empty() || result.size() > 24 || result.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != result.npos)
            throw std::runtime_error("Invalid recorded game mode");
        return result;
    }

    struct team_score_frame
    {
        int time{}, axis{}, allies{}, limit{};
    };
    inline std::vector<team_score_frame> collect_team_scores(const world_track& world)
    {
        std::vector<team_score_frame> result;
        std::set<int> seen;
        for (const auto& command : world.commands)
        {
            if (!command.text.starts_with("b ") || !seen.insert(command.index).second) continue;
            // COD4 SendScoreboard: b count axis allies scorelimit, followed by
            // seven integers per player. Team enums: axis=1, allies=2.
            std::istringstream input(command.text);
            std::string opcode;
            int count{};
            team_score_frame frame{command.time};
            if (!(input >> opcode >> count >> frame.axis >> frame.allies >> frame.limit)
                || count < 0 || count > 64 || std::abs(static_cast<long long>(frame.axis)) > 1000000
                || std::abs(static_cast<long long>(frame.allies)) > 1000000 || frame.limit < 0 || frame.limit > 1000000) continue;
            int value{};
            bool complete = true;
            for (int i = 0; i < count * 7; ++i) if (!(input >> value)) { complete = false; break; }
            if (!complete || (input >> opcode)) continue;
            if (!result.empty() && result.back().axis == frame.axis && result.back().allies == frame.allies
                && result.back().limit == frame.limit) continue;
            if (!result.empty() && result.back().time == frame.time)
            {
                result.back() = frame;
                continue;
            }
            result.push_back(frame);
        }
        return result;
    }
    inline const team_score_frame* team_scores_at(const std::vector<team_score_frame>& frames, int time)
    {
        const auto next = std::upper_bound(frames.begin(), frames.end(), time,
            [](int t, const team_score_frame& frame) { return t < frame.time; });
        return next == frames.begin() ? nullptr : &*(next - 1);
    }
}
