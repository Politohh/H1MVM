#pragma once
#include "cod4_model_metadata.hpp"
#include <array>

namespace cod4
{
    // Class counterparts verified in MWR's model catalog. Original _teams.gsc
    // assigns Arab CQB to SMG, with character_mp_arab_regular_cqb's headwrap.
    inline std::string_view model_target(std::string_view source)
    {
        constexpr std::array<std::pair<std::string_view, std::string_view>, 4> mappings{{
            {"body_mp_usmc_sniper", "body_usmc_desert_sniper_mp_camo"},
            {"head_mp_usmc_tactical_baseball_cap", "head_usmc_desert_sniper_mp_patch"},
            {"body_mp_arab_regular_cqb", "body_arab_desert_smg_mp_camo"},
            {"head_mp_arab_regular_headwrap", "head_arab_desert_smg_mp"}
        }};
        if (!valid_model_name(source)) throw std::runtime_error("Invalid COD4 model name");
        for (const auto& [name, target] : mappings) if (source == name) return target;
        return source;
    }

    enum class model_usage { prop, character };

    template<typename Available>
    std::string select_model(std::string_view source, model_usage usage, Available&& available)
    {
        const std::string target(model_target(source));
        if (available(target)) return target;
        if (usage == model_usage::prop) return {};
        throw std::runtime_error("Required remastered character model unavailable: " + target);
    }
}
