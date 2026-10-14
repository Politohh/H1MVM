#pragma once
#include "spai_scores.hpp"
#include "animation_schema.hpp"
#include "spai_replay.hpp"
#include <utils/info_string.hpp>

namespace spai_scores
{
    inline nlohmann::json received()
    {
        auto result=nlohmann::json{{"valid",false},{"rows",nlohmann::json::array()}};
        if(!game::environment::is_mp() || game::environment::is_dedi() ||
           !game::CL_IsCgameInitialized() || game::VirtualLobby_Loaded())return result;
        const auto* raw=game::CL_GetConfigString(0);
        if(!raw || !*raw)return result;
        const utils::info_string info{std::string(raw)};
        const auto wire=info.get("spai_wire_schema");
        // Recorded score envelopes have their own validated version and epoch.
        // A new animation wire version must not hide scores in supported old
        // demos. Only validated SP replay permits these historical contracts;
        // live matches still require the current wire version.
        if(!spai_probe::animation_schema::supports_score_wire(wire,spai_probe::replay_requested()))return result;
        const auto epoch=info.get("spai_match_epoch");
        std::vector<row> rows;
        if(!decode_envelope(info.get("spai_scores"),epoch,rows))return result;
        result["valid"]=true;result["epoch"]=epoch;
        for(const auto& row:rows)
            result["rows"].push_back({{"entity",row.entity},{"team",row.team},{"alive",row.alive!=0},
                {"score",row.score},{"kills",row.kills},{"deaths",row.deaths},{"assists",row.assists}});
        return result;
    }
}
