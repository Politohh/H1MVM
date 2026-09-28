#pragma once
#include "cod4_model_metadata.hpp"
#include "cod4_character_catalog.hpp"
#include <algorithm>

namespace cod4
{
    inline std::string_view character_environment(std::string_view map)
    {
        // The original map scripts select these environments. This supplies
        // context for reused OpForce names; it never restricts map loading.
        constexpr std::array<std::pair<std::string_view, std::string_view>, 25> environments{{
            {"mp_backlot", "desert"}, {"mp_bloc", "woodland"}, {"mp_bog", "desert"},
            {"mp_broadcast", "desert"}, {"mp_carentan", "urban"}, {"mp_cargoship", "woodland"},
            {"mp_citystreets", "desert"}, {"mp_convoy", "desert"}, {"mp_countdown", "woodland"},
            {"mp_crash", "desert"}, {"mp_crash_snow", "desert"}, {"mp_creek", "woodland"},
            {"mp_crossfire", "desert"}, {"mp_destructibles", "desert"}, {"mp_dusk", "desert"},
            {"mp_farm", "woodland"}, {"mp_hill", "woodland"}, {"mp_killhouse", "woodland"},
            {"mp_overgrown", "woodland"}, {"mp_pipeline", "woodland"}, {"mp_shipment", "woodland"},
            {"mp_showdown", "desert"}, {"mp_strike", "desert"}, {"mp_test", "woodland"},
            {"mp_vacant", "woodland"}
        }};
        for (const auto& [name, environment] : environments) if (map == name) return environment;
        return {};
    }

    inline const source_character* source_character_for(std::string_view body)
    {
        for (const auto& entry : source_characters) if (body == entry.body) return &entry;
        return nullptr;
    }

    inline const character_assets& native_character_for(character_family family, character_role role)
    {
        return native_characters.at(static_cast<unsigned>(family) * 5 + static_cast<unsigned>(role));
    }

    inline character_family character_family_for(const source_character& entry, std::string_view map)
    {
        // Urban and woodland Russian classes reuse the same COD4 bodies and
        // heads except the sniper. The map environment selects MWR's variant.
        if (entry.family == character_family::spetsnaz_woodland
            && entry.role != character_role::sniper && character_environment(map) == "urban")
            return character_family::spetsnaz_urban;
        return entry.family;
    }

    inline const character_assets* character_assets_for(std::string_view body, std::string_view map = {})
    {
        const auto* entry = source_character_for(body);
        return entry ? &native_character_for(character_family_for(*entry, map), entry->role) : nullptr;
    }

    inline std::string_view model_target(std::string_view source, std::string_view map = {},
        std::string_view body = {})
    {
        if (!valid_model_name(source)) throw std::runtime_error("Invalid COD4 model name");
        if (const auto* assets = character_assets_for(source, map)) return assets->body;
        const auto* context = source_character_for(body);
        if (context && source == context->head)
            return native_character_for(character_family_for(*context, map), context->role).head;
        for (const auto& entry : source_characters)
            if (!entry.head.empty() && source == entry.head)
                return native_character_for(context ? character_family_for(*context, map)
                    : character_family_for(entry, map), entry.role).head;
        return source;
    }

    inline std::string_view hands_target(std::string_view source, std::string_view map,
        std::string_view body = {})
    {
        if (!valid_model_name(source)) throw std::runtime_error("Invalid COD4 hands model name");
        if (const auto* entry = source_character_for(body); entry && source == entry->hands)
            return native_character_for(character_family_for(*entry, map), entry->role).hands;
        if (source == "viewhands_marine_sniper")
            throw std::runtime_error("Ghillie hands require the recorded sniper body context");
        constexpr std::array<std::pair<std::string_view, character_family>, 11> aliases{{
            {"viewmodel_base_viewhands", character_family::usmc_desert},
            {"viewhands_force_recon", character_family::usmc_desert},
            {"viewhands_usmc", character_family::usmc_desert},
            {"viewhands_player_marines", character_family::usmc_desert},
            {"viewhands_player_usmc", character_family::usmc_desert},
            {"viewhands_desert_opfor", character_family::arab_desert},
            {"viewhands_black_kit", character_family::sas_ct},
            {"viewhands_player_blackkit", character_family::sas_ct},
            {"viewhands_sas_woodland", character_family::sas_woodland},
            {"viewhands_player_sas_woodland", character_family::sas_woodland},
            {"viewhands_op_force", character_family::spetsnaz_woodland}
        }};
        for (const auto& [name, family] : aliases) if (source == name)
            return native_character_for(family == character_family::spetsnaz_woodland
                && character_environment(map) == "urban" ? character_family::spetsnaz_urban : family,
                character_role::assault).hands;
        return source; // Custom identities require a real installed MWR model.
    }

    struct character_models
    {
        std::string body;
        std::array<std::string, 6> attachments;
    };

    inline character_models map_character_models(std::string_view body,
        const std::array<std::string_view, 6>& attachments, std::string_view map)
    {
        character_models result;
        result.body = model_target(body, map);
        bool has_head = false;
        for (unsigned i = 0; i < attachments.size(); ++i)
        {
            const auto name = attachments[i];
            // Native weapon handles assemble their own world models.
            if (name.empty() || name.starts_with("weapon_")) continue;
            result.attachments[i] = model_target(name, map, body);
            has_head |= name.starts_with("head_");
        }
        if (const auto* assets = character_assets_for(body, map); assets && !has_head)
        {
            // COD4's urban SAS (and legacy USMC) bodies contain their head;
            // MWR splits it out. Include the class's native head explicitly.
            const auto empty = std::find(result.attachments.begin(), result.attachments.end(), "");
            if (empty == result.attachments.end()) throw std::runtime_error("No H1 attachment slot for character head");
            *empty = assets->head;
        }
        return result;
    }

    enum class model_usage { prop, character };

    template<typename Available>
    std::string select_model(std::string_view source, model_usage usage, Available&& available,
        std::string_view map = {}, std::string_view body = {})
    {
        const std::string target(model_target(source, map, body));
        if (available(target)) return target;
        if (usage == model_usage::prop) return {};
        throw std::runtime_error("Required remastered character model unavailable: " + target);
    }
}
