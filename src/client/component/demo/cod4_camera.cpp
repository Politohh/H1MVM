#include "std_include.hpp"
#include "loader/component_loader.hpp"
#include "cod4_camera.hpp"
#include "cod4_demo_reader.hpp"
#include "cod4_playerstate_reader.hpp"
#include "cod4_action_translation.hpp"
#include "cod4_body_translation.hpp"
#include "cod4_view_translation.hpp"
#include "cod4_world_translation.hpp"
#include "cod4_hud_translation.hpp"
#include "cod4_grenade_events.hpp"
#include "cod4_bullet_events.hpp"
#include "cod4_obituary_events.hpp"
#include "cod4_gun_events.hpp"
#include "cod4_actor_translation.hpp"
#include "cod4_replay_agents.hpp"
#include "cod4_car_fx.hpp"
#include "cod4_airstrike.hpp"
#include "cod4_helicopter.hpp"
#include "cod4_car_motion.hpp"
#include "cod4_model_metadata.hpp"
#include "cod4_transient_budget.hpp"
#include "cod4_weapon_models.hpp"
#include "cod4_model_translation.hpp"
#include "demo_playback.hpp"
#include "demo_game.hpp"
#include "demo_utils.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/party.hpp"
#include "component/scheduler.hpp"
#include "utils/hook.hpp"
#include "utils/mapents.hpp"
#include "component/gsc/script_loading.hpp"
#include "game/dvars.hpp"
#include <mutex>

namespace cod4_camera
{
    namespace
    {
        std::mutex mutex;
        cod4::camera_track track;
        std::vector<cod4::player_frame> players;
        cod4::world_track world;
        cod4::client_slot_map client_slots;
        std::map<std::string, unsigned> agent_name_ids;
        std::map<unsigned, std::string> recorded_agent_names;
        cod4::grenade_events grenade_track;
        std::vector<cod4::bullet_event> bullet_track;
        std::vector<cod4::h1_entity> bullet_entities;
        std::map<unsigned, int> bullet_times;
        std::vector<cod4::obituary_event> obituary_track;
        std::vector<cod4::h1_entity> obituary_entities;
        cod4::gun_event_track gun_track;
        cod4::actor_event_track actor_events;
        std::vector<std::string> source_weapon_names;
        std::map<std::tuple<unsigned, unsigned, unsigned>, unsigned> actor_animations;
        std::vector<cod4::h1_entity> grenade_entities;
        std::map<unsigned, int> grenade_times;
        bool grenade_effects_resolved{};
        cod4::car_fx_track car_effects;
        std::vector<cod4::h1_entity> car_effect_entities;
        std::map<unsigned, int> car_effect_times;
        unsigned car_effect_index{};
        std::string car_effect_name;
        cod4::airstrike_track airstrikes;
        cod4::helicopter_track helicopters;
        std::map<unsigned, unsigned> helicopter_definitions;
        std::map<std::string, unsigned> helicopter_sounds;
        bool helicopters_resolved{};
        cod4::jet_fx_parameters jet_parameters;
        std::vector<cod4::h1_entity> airstrike_entities;
        std::map<unsigned, int> airstrike_times;
        bool airstrikes_resolved{};
        std::vector<cod4::car_animation> car_animations;
        int car_animation_duration{};
        std::map<unsigned, std::string> animation_bindings;
        std::map<unsigned, std::uint16_t> source_models;
        std::map<unsigned, std::string> omitted_props;
        std::map<unsigned, std::set<unsigned>> omitted_prop_entities;
        std::map<unsigned, cod4::part_mapping> source_parts;
        std::map<unsigned, std::string> model_bindings;
        std::map<std::string, std::uint16_t> bound_models;
        using character_key = std::array<unsigned, 7>;
        struct native_character_models
        {
            std::uint16_t body{};
            std::array<std::uint16_t, 6> attachments{};
        };
        std::map<character_key, native_character_models> character_models;
        std::map<std::pair<unsigned, unsigned>, std::uint16_t> hands_models;
        std::map<unsigned, cod4::h1_entity> map_models;
        bool world_resolved{};
        std::optional<unsigned> score_popup_label;
        std::optional<unsigned> score_popup_omnvar;
        std::int32_t score_popup_omnvar_min{};
        bool score_popup_resolved{};
        std::vector<cod4::score_popup_event> score_popups;
        std::vector<cod4::hitmarker_event> hitmarkers;
        std::optional<unsigned> hitmarker_omnvar;
        std::array<unsigned, 2> hitmarker_values{};
        bool hitmarkers_resolved{};
        bool minimap_resolved{};
        std::optional<unsigned> team_selection_omnvar;
        std::int32_t team_selection_min{};
        bool converted_bindings{};
        std::atomic_bool replay_asset_pool{};
        bool parts_resolved{};
        utils::hook::detour get_configstring_hook;
        std::map<unsigned, unsigned> weapons;
        std::map<unsigned, game::WeaponDef*> weapon_defs;
        std::set<unsigned> alternate_weapons;
        std::vector<cod4::body_animation> body_actions;
        std::vector<std::pair<unsigned, unsigned>> body_indices;
        std::vector<cod4::ammo_slots> source_ammo;
        std::set<std::string> requested_models;
        std::set<std::string> acquired_models;
        status state;
        using clock = std::chrono::steady_clock;
        clock::time_point last_tick, load_started;
        double playhead{};
        std::ofstream trace;
        int trace_bucket{-1};
        int snapshot_sequence{}, last_snapshot_time{-1};
        unsigned last_player_command{}, last_action{UINT_MAX};
        bool bootstrapping{};
        bool converting{};
        bool disconnect_pending{};
        std::filesystem::path conversion_output;
        std::vector<std::uint8_t> initial_gamestate;
        std::map<int, std::string> initial_configs;
        std::map<unsigned, cod4::shock_profile> shock_profiles;
        bool shocks_resolved{};
        void save_initial_gamestate(const std::string& map);
        std::ofstream lifecycle;

        std::uint8_t reserve_view_weapon_pool(const char* name, unsigned native_count)
        {
            // Replace only the view-weapon reservation call at 0x7445D. The
            // native allocator still owns slot links, virtual reservations,
            // on-demand page commits, and teardown. No live pool is resized.
            const auto used = *reinterpret_cast<const unsigned*>(0x14D51A4_b);
            const auto count = cod4::view_weapon_slot_budget(replay_asset_pool.load(), native_count, used);
            const auto category = utils::hook::invoke<std::uint8_t>(0x74100_b, name, count);
            if (replay_asset_pool.load())
                console::info("COD4 replay: native transient pool %s slots=%u (normal=%u, preceding=%u)\n",
                    name, count, native_count, used);
            return category;
        }

        bool minimap_material_available(const std::string& name)
        {
            return game::DB_XAssetExists(game::ASSET_TYPE_MATERIAL, name.c_str())
                && !game::DB_IsXAssetDefault(game::ASSET_TYPE_MATERIAL, name.c_str());
        }

        const char* get_configstring(int index)
        {
            if (index == 1228 && (state.active || converted_bindings))
            {
                const auto* original = get_configstring_hook.invoke<const char*>(index);
                if (original && *original)
                {
                    if (const auto resolved = cod4::resolve_minimap_material(original, minimap_material_available))
                    {
                        thread_local std::string material;
                        material = *resolved;
                        return material.c_str();
                    }
                }
                return original;
            }
            if (const auto* animation = cod4::script_animation_binding(animation_bindings, index)) return animation;
            const auto found = model_bindings.find(static_cast<unsigned>(index - 1240));
            if (found != model_bindings.end()) return found->second.c_str();
            return get_configstring_hook.invoke<const char*>(index);
        }

        void request_model(const std::string& name)
        {
            if (requested_models.insert(name).second)
            {
                lifecycle << "request model " << name << std::endl;
            }
        }

        bool model_available(const std::string& name)
        {
            if (!cod4::valid_model_name(name)) throw std::runtime_error("Invalid remastered model name: " + name);
            std::array<std::uint16_t, 8> zones{};
            const bool resident = game::DB_XAssetExists(game::ASSET_TYPE_XMODEL, name.c_str())
                && !game::DB_IsXAssetDefault(game::ASSET_TYPE_XMODEL, name.c_str());
            const auto transient_count = utils::hook::invoke<int>(0x75210_b, name.c_str(), 7, zones.data());
            return resident || transient_count > 0;
        }

        std::uint16_t bind_model(const std::string& name)
        {
            if (const auto found = bound_models.find(name); found != bound_models.end()) return found->second;
            if (!model_available(name))
                throw std::runtime_error("Remastered world model unavailable: " + name);
            request_model(name);
            for (unsigned i = 1; i < 1024; ++i)
                if (name == get_configstring(1240 + i))
                    return bound_models[name] = static_cast<std::uint16_t>(i);
            for (unsigned i = 768; i < 1024; ++i)
            {
                if (model_bindings.contains(i) || *get_configstring_hook.invoke<const char*>(1240 + i)) continue;
                model_bindings[i] = name;
                lifecycle << "bind model " << i << ' ' << name << std::endl;
                return bound_models[name] = static_cast<std::uint16_t>(i);
            }
            throw std::runtime_error("No unused H1 model slot for " + name);
        }

        std::uint16_t source_model(unsigned index, bool prop = false)
        {
            if (!index) return 0;
            if (const auto found = source_models.find(index); found != source_models.end())
            {
                if (!prop && !found->second) throw std::runtime_error("Required character model unavailable: " + omitted_props.at(index));
                return found->second;
            }
            const auto name = track.configstrings.at(830 + index);
            const auto target = cod4::select_model(name,
                prop ? cod4::model_usage::prop : cod4::model_usage::character, model_available, track.map);
            if (target.empty())
            {
                source_models[index] = 0;
                omitted_props[index] = name;
                lifecycle << "omitted prop " << index << ' ' << name << ": no remastered counterpart available" << std::endl;
                console::info("COD4 conversion warning: omitted prop %s (no remastered counterpart available)\n", name.c_str());
                return 0;
            }
            const auto model = bind_model(target);
            source_models[index] = model;
            lifecycle << "world model " << index << ' ' << name << " -> " << model << ' ' << target << std::endl;
            return model;
        }

        character_key client_model_key(const cod4::client_state& client)
        {
            character_key key{};
            for (unsigned i = 0; i < key.size(); ++i) key[i] = client.get(8 + i * 4);
            return key;
        }

        const native_character_models& resolve_client_models(const cod4::client_state& client)
        {
            const auto key = client_model_key(client);
            if (const auto found = character_models.find(key); found != character_models.end()) return found->second;
            native_character_models native;
            if (key[0])
            {
                const auto& body = track.configstrings.at(830 + key[0]);
                std::array<std::string_view, 6> attachments{};
                for (unsigned i = 0; i < attachments.size(); ++i)
                    if (key[i + 1]) attachments[i] = track.configstrings.at(830 + key[i + 1]);
                const auto mapped = cod4::map_character_models(body, attachments, track.map);
                native.body = bind_model(mapped.body);
                for (unsigned i = 0; i < native.attachments.size(); ++i)
                    if (!mapped.attachments[i].empty()) native.attachments[i] = bind_model(mapped.attachments[i]);
                lifecycle << "character " << body << " -> " << mapped.body << std::endl;
            }
            return character_models.emplace(key, native).first->second;
        }

        unsigned local_body_at(int time)
        {
            const auto& clients = world.sample(time).clients;
            const auto found = clients.find(world.client_num);
            return found == clients.end() ? 0 : found->second.get(8);
        }

        std::pair<unsigned, unsigned> hands_key_at(int time);
        void resolve_hands_assets();

        void resolve_world_assets()
        {
            if (world_resolved) return;
            source_models.clear(); character_models.clear(); hands_models.clear();
            omitted_props.clear(); omitted_prop_entities.clear(); map_models.clear();
            for (const auto& frame : world.frames)
            {
                for (const auto& [number, entity] : frame.entities)
                    if (entity.get(4) == 12) source_model(entity.get(216));
                for (const auto& [number, entity] : frame.entities)
                    if ((entity.get(4) == 6 || entity.get(4) == 13) && entity.get(136) && !cod4::script_model_is_brush(entity)
                        && !source_model(entity.get(136), true))
                        omitted_prop_entities[entity.get(136)].insert(number);
                for (const auto& [number, client] : frame.clients)
                {
                    resolve_client_models(client);
                }
            }
            resolve_hands_assets();
            // MWR moved several pieces of baked COD4 geometry into authored
            // script models (including Crash's wreck). Read this map's own data.
            const auto path = "maps/mp/" + track.map + ".d3dbsp";
            const auto* map = game::DB_FindXAssetHeader(game::ASSET_TYPE_MAP_ENTS, path.c_str(), false).mapEnts;
            if (!map || !map->entityString || map->numEntityChars <= 0)
                throw std::runtime_error("H1 map entities unavailable: " + track.map);
            const auto authored = mapents::parse(std::string(map->entityString, map->numEntityChars),
                [](std::uint32_t id) { return std::string(gsc::gsc_ctx->token_name(id)); });
            unsigned number = 1024; // COD4's recorded entities use only 0..1022.
            for (const auto& entity : authored.entities)
            {
                const auto name = entity.get("model");
                if (entity.get("classname") != "script_model" || name.empty() || name[0] == '*'
                    || name[0] == '?' || entity.get("targetname") == "oldschool_pickup"
                    || !entity.get("script_gameobjectname").empty()) continue;
                // Gametype objectives are not permanent map dressing.
                if (name.starts_with("com_bomb_") || name.starts_with("h1_flag_")
                    || name == "wpn_h1_briefcase_bomb_npc") continue;
                if (!game::DB_XAssetExists(game::ASSET_TYPE_XMODEL, name.c_str())
                    || game::DB_IsXAssetDefault(game::ASSET_TYPE_XMODEL, name.c_str())) continue;
                std::array<float, 3> origin{}, angles{};
                std::istringstream pos(entity.get("origin")), rot(entity.get("angles"));
                pos >> origin[0] >> origin[1] >> origin[2];
                if (!pos) throw std::runtime_error("Invalid authored model origin: " + name);
                if (!entity.get("angles").empty() && !(rot >> angles[0] >> angles[1] >> angles[2]))
                    throw std::runtime_error("Invalid authored model angles: " + name);
                bool duplicate = false;
                for (const auto& [id, recorded] : world.frames.front().entities)
                {
                    if (recorded.get(4) != 6 || !recorded.get(136)
                        || cod4::script_model_is_brush(recorded)
                        || track.configstrings.at(830 + recorded.get(136)) != name) continue;
                    float distance = 0;
                    for (unsigned i = 0; i < 3; ++i)
                    {
                        const auto delta = std::bit_cast<float>(recorded.get(24 + i * 4)) - origin[i];
                        distance += delta * delta;
                    }
                    if (distance < 1) duplicate = true;
                }
                if (duplicate) continue;
                if (number >= 2047) throw std::runtime_error("Too many authored replay models");
                cod4::entity_state source;
                source.set(0, number); source.set(4, 6);
                for (unsigned i = 0; i < 3; ++i)
                {
                    source.set(24 + i * 4, std::bit_cast<unsigned>(origin[i]));
                    source.set(60 + i * 4, std::bit_cast<unsigned>(angles[i]));
                }
                map_models[number++] = cod4::translate_script_model(source, bind_model(name));
                lifecycle << "authored model " << name << " origin=" << origin[0] << ',' << origin[1] << ',' << origin[2] << std::endl;
            }
            world_resolved = true;
            lifecycle << "world ready frames=" << world.frames.size() << " authored=" << map_models.size() << std::endl;
        }

        void resolve_score_popup_label()
        {
            if (score_popup_resolved) return;
            score_popup_label.reset();
            score_popup_omnvar.reset();
            score_popup_omnvar_min = 0;
            const bool recorded = std::ranges::any_of(world.hud_frames, [](const cod4::hud_frame& frame)
            {
                return std::ranges::any_of(frame.groups[1], [](const cod4::wire_state<160>& element)
                {
                    return cod4::is_score_popup(element, track.configstrings);
                });
            });
            if (!recorded) { score_popup_resolved = true; return; }
            const auto globals = utils::hook::invoke<unsigned>(0x5A47B0_b);
            const auto players = utils::hook::invoke<unsigned>(0x5A4790_b);
            for (unsigned i = globals; i < globals + players; ++i)
            {
                const auto* def = static_cast<const std::uint8_t*>(game::Omnvar_GetDef(i));
                const auto* name = *reinterpret_cast<const char* const*>(def);
                if (name && std::string_view(name) == "ui_points_popup")
                {
                    if (*reinterpret_cast<const std::uint32_t*>(def + 12) != 3)
                        throw std::runtime_error("Native MWR score popup omnvar is not an integer");
                    score_popup_omnvar = i - globals;
                    score_popup_omnvar_min = *reinterpret_cast<const std::int32_t*>(def + 0x20);
                    lifecycle << "native score popup omnvar index=" << i << " player_offset="
                        << 0x4540 + *score_popup_omnvar * 8 << " minimum=" << score_popup_omnvar_min << std::endl;
                    break;
                }
            }
            if (!score_popup_omnvar)
                throw std::runtime_error("Native MWR score popup omnvar unavailable: ui_points_popup");
            const auto& strings = game::s_netConstStringMaps[game::NETCONSTSTRINGTYPE_LOCSTRING];
            unsigned index = 0;
            for (auto* list = strings.head; list; list = list->next)
            {
                if (!list->ncs || !list->ncs->stringList) continue;
                for (unsigned i = 0; i < list->ncs->entryCount; ++i, ++index)
                    if (list->ncs->stringList[i] && std::string_view(list->ncs->stringList[i]) == "MP_PLUS")
                    {
                        if (!index || index >= 2048)
                            throw std::runtime_error("Native MWR MP_PLUS label index outside HUD bounds");
                        score_popup_label = index;
                        score_popup_resolved = true;
                        lifecycle << "native score popup label MP_PLUS index=" << index << std::endl;
                        return;
                    }
            }
            throw std::runtime_error("Native MWR score popup label unavailable: MP_PLUS");
        }

        void resolve_hitmarkers()
        {
            if (hitmarkers_resolved) return;
            if (hitmarkers.empty()) { hitmarkers_resolved = true; return; }
            const auto globals = utils::hook::invoke<unsigned>(0x5A47B0_b);
            const auto count = utils::hook::invoke<unsigned>(0x5A4790_b);
            for (unsigned i = globals; i < globals + count; ++i)
            {
                const auto* def = static_cast<const std::uint8_t*>(game::Omnvar_GetDef(i));
                const auto* name = *reinterpret_cast<const char* const*>(def);
                if (!name || std::string_view(name) != "damage_feedback") continue;
                if (!(def[8] & 8) || *reinterpret_cast<const unsigned*>(def + 12) != 5
                    || 0x4540 + (i - globals + 1) * 8 > 18576)
                    throw std::runtime_error("Invalid native MWR damage_feedback omnvar layout");
                hitmarker_omnvar = i - globals;
                break;
            }
            if (!hitmarker_omnvar) throw std::runtime_error("Native MWR hitmarker omnvar unavailable");
            std::array<bool, 2> found{};
            unsigned index = 0;
            for (auto* list = game::s_netConstStringMaps[game::NETCONSTSTRINGTYPE_LUI].head; list; list = list->next)
            {
                if (!list->ncs || !list->ncs->stringList) continue;
                for (unsigned i = 0; i < list->ncs->entryCount; ++i, ++index)
                {
                    const auto* name = list->ncs->stringList[i];
                    if (!name) continue;
                    for (unsigned kind = 0; kind < 2; ++kind)
                        if (std::string_view(name) == (kind ? "hitjuggernaut" : "standard"))
                        {
                            hitmarker_values[kind] = index;
                            found[kind] = true;
                        }
                }
            }
            if (!found[0] || (!found[1] && std::ranges::any_of(hitmarkers, &cod4::hitmarker_event::juggernaut)))
                throw std::runtime_error("Native MWR hitmarker LUI strings unavailable");
            hitmarkers_resolved = true;
            lifecycle << "native hitmarkers=" << hitmarkers.size() << " player_omnvar=" << *hitmarker_omnvar
                << " standard=" << hitmarker_values[0] << " juggernaut=" << hitmarker_values[1] << std::endl;
        }

        void resolve_minimap()
        {
            if (minimap_resolved) return;
            team_selection_omnvar.reset();
            const auto globals = utils::hook::invoke<unsigned>(0x5A47B0_b);
            const auto per_player = utils::hook::invoke<unsigned>(0x5A4790_b);
            for (unsigned i = globals; i < globals + per_player; ++i)
            {
                const auto* def = static_cast<const std::uint8_t*>(game::Omnvar_GetDef(i));
                const auto* name = *reinterpret_cast<const char* const*>(def);
                if (!name || std::string_view(name) != "ui_team_selected") continue;
                if (!(def[8] & 8) || *reinterpret_cast<const unsigned*>(def + 12) != 3)
                    throw std::runtime_error("Native HUD team selection is not a player integer");
                team_selection_omnvar = i - globals;
                team_selection_min = *reinterpret_cast<const std::int32_t*>(def + 0x20);
                break;
            }
            if (!team_selection_omnvar)
                throw std::runtime_error("Native HUD team selection omnvar unavailable");
            // Native _teams::initscoreboard normally supplies these dvars.
            // Client-only replay has no GSC server. Resolve its faction rows
            // from recorded characters, then archive the dvars in systeminfo:
            // CL_SystemInfoChanged (0x342ED0) applies configstring 1 on load.
            std::map<unsigned, std::string_view> factions;
            for (const auto& snapshot : world.frames)
                for (const auto& [number, client] : snapshot.clients)
                {
                    const auto team = client.get(4);
                    if ((team != 1 && team != 2) || factions.contains(team)) continue;
                    const auto body = track.configstrings.find(830 + client.get(8));
                    if (body == track.configstrings.end()) continue;
                    if (const auto* identity = cod4::source_character_for(body->second))
                        factions.emplace(team, cod4::native_faction(identity->family));
                }
            const auto* table = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
                "mp/factiontable.csv", false).stringTable;
            if (table && table->columnCount > 16)
                for (const auto& [team, faction] : factions)
                    for (int row = 0; row < table->rowCount; ++row)
                    {
                        const auto column = [&](int col) { return game::StringTable_GetColumnValueForRow(
                            const_cast<game::StringTable*>(table), row, col); };
                        if (faction != column(0)) continue;
                        const std::string icon = column(5);
                        if (!minimap_material_available(icon)) break;
                        const auto set = [&](const std::string& name, const std::string& value)
                        {
                            initial_configs[1] += "\\" + name + "\\" + value;
                            game::Dvar_SetFromStringByNameFromSource(name.c_str(), value.c_str(), game::DVAR_SOURCE_INTERNAL);
                        };
                        const std::string side = team == 1 ? "Axis" : "Allies";
                        set("g_TeamName_" + side, column(2));
                        set("g_TeamIcon_" + side, icon);
                        set("g_TeamIcon_My" + side, icon);
                        set("g_TeamIcon_Enemy" + side, icon);
                        set("g_ScoresColor_" + side, std::format("{} {} {}", column(14), column(15), column(16)));
                        lifecycle << "native HUD faction team=" << team << " faction=" << faction << " icon=" << icon << std::endl;
                        break;
                    }
            if (const auto config = initial_configs.find(1228); config != initial_configs.end() && !config->second.empty())
            {
                const auto resolved = cod4::resolve_minimap_material(config->second, minimap_material_available);
                if (!resolved) throw std::runtime_error("Native MWR minimap material unavailable: " + config->second);
                lifecycle << "native minimap " << config->second << " -> " << *resolved << std::endl;
                config->second = *resolved;
                utils::hook::invoke<void>(0x342F90_b, 1228, resolved->c_str());
            }
            save_initial_gamestate(track.map);
            minimap_resolved = true;
        }

        void resolve_world_parts()
        {
            if (parts_resolved) return;
            for (const auto& frame : world.frames)
            {
                for (const auto& [number, entity] : frame.entities)
                {
                    const auto index = entity.get(entity.get(4) == 12 ? 216 : 136);
                    if ((entity.get(4) != 6 && entity.get(4) != 13 && entity.get(4) != 12) || !index || cod4::script_model_is_brush(entity) || source_parts.contains(index)) continue;
                    if (!source_models.at(index)) continue; // Omitted props have no native skeleton.
                    const auto& name = track.configstrings.at(830 + index);
                    const auto bones = cod4::source_bones(name);
                    if (bones.empty())
                    {
                        if (entity.get(228) || entity.get(232) || entity.get(236) || entity.get(240))
                            throw std::runtime_error("COD4 model bone catalog unavailable: " + name);
                        continue;
                    }
                    const auto target = get_configstring(1240 + source_models.at(index));
                    const auto* model = game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL, target, false).model;
                    if (!model || !model->numBones || !model->boneNames)
                        throw std::runtime_error("Remastered model bone table unavailable: " + name);
                    std::vector<std::string_view> native_bones;
                    for (unsigned i = 0; i < model->numBones; ++i)
                    {
                        const auto bone = game::SL_ConvertToString(model->boneNames[i]);
                        if (!bone || !*bone) throw std::runtime_error("Invalid remastered model bone name: " + name);
                        native_bones.emplace_back(bone);
                    }
                    source_parts[index] = cod4::map_parts(bones, native_bones);
                    lifecycle << "part mapping " << name << " source_bones=" << bones.size()
                        << " native_bones=" << native_bones.size() << std::endl;
                }
            }
            parts_resolved = true;
        }

        const cod4::player_frame& player_at(double time)
        {
            const auto next = std::upper_bound(players.begin(), players.end(), time,
                [](double t, const cod4::player_frame& f) { return t < f.integer("commandTime"); });
            return next == players.begin() ? players.front() : *(next - 1);
        }

        std::pair<unsigned, unsigned> hands_key_at(int time)
        {
            return {player_at(time).integer("viewmodelIndex"), local_body_at(time)};
        }

        void resolve_hands_assets()
        {
            for (const auto& frame : track.frames)
            {
                const auto key = hands_key_at(frame.time);
                if (hands_models.contains(key)) continue;
                const auto [index, body_index] = key;
                if (!index) { hands_models[key] = 0; continue; }
                const auto& source = track.configstrings.at(830 + index);
                const auto body = body_index ? track.configstrings.at(830 + body_index) : std::string{};
                const auto target = std::string(cod4::hands_target(source, track.map, body));
                hands_models[key] = bind_model(target);
                lifecycle << "hands " << hands_models.at(key) << ' ' << source << " -> " << target
                    << " body=" << body << std::endl;
            }
        }

        void resolve_player_assets()
        {
            // Resolve the recorded ordinal through COD4's own weapon-file list.
            // H1's current loaded registry supplies the destination indices.
            std::vector<std::string> names{ "" };
            std::istringstream source_names(track.configstrings.at(2258));
            for (std::string name; source_names >> name;) names.push_back(name);
            source_ammo = cod4::resolve_ammo_slots(names);
            source_weapon_names = names;
            weapons.clear(); weapons[0] = 0; weapon_defs.clear(); alternate_weapons.clear(); hands_models.clear();
            body_actions.clear(); body_indices.clear();
            actor_animations.clear();
            actor_events = cod4::collect_actor_events(world);
            std::set<unsigned> recorded_weapons;
            std::set<unsigned> held_weapons;
            std::set<unsigned> actor_weapons;
            std::set<unsigned> projectile_weapons;
            for (const auto& frame : players)
            {
                if (const auto source = frame.integer("weapon")) held_weapons.insert(source);
                for (const auto key : {"weapon", "offHandIndex"})
                    if (const auto source = frame.integer(key)) recorded_weapons.insert(source);
                for (unsigned source = 1; source < names.size(); ++source)
                    if (source < 128 && (frame.integer(std::format("weapons[{}]", source / 32)) & (1u << (source % 32))))
                        recorded_weapons.insert(source);
            }
            // Only the recorded camera player's inventory needs view models.
            // Remote actors and pickups use world models. H1 reserves six
            // simultaneous transient view-weapon slots in normal matches. Demo
            // bootstrap reserves a bounded larger pool for inventory changes
            // across the recording; remote actors still need no view assets.
            const auto view_weapons = recorded_weapons;
            for (const auto& movement : track.frames)
            {
                const auto& player = player_at(movement.time);
                const auto source = player.integer("weapon");
                if (source >= names.size()) throw std::runtime_error("Invalid COD4 body weapon");
                body_actions.push_back(cod4::select_body_animation(player, movement, names[source]));
            }
            for (const auto& frame : world.frames)
                for (const auto& [number, entity] : frame.entities)
                {
                    if ((entity.get(4) == 1 || entity.get(4) == 2) && entity.get(196))
                    {
                        recorded_weapons.insert(entity.get(196));
                        actor_weapons.insert(entity.get(196));
                    }
                    if (entity.get(4) == 3 && entity.get(136))
                        recorded_weapons.insert(cod4::item_weapon_index(entity));
                    if (entity.get(4) == 4 && entity.get(196))
                    {
                        recorded_weapons.insert(entity.get(196));
                        projectile_weapons.insert(entity.get(196));
                    }
                }
            for (const auto& event : grenade_track.events)
            {
                if (event.weapon >= names.size() || !cod4::grenade_weapon_matches(event.source_event, names[event.weapon]))
                    throw std::runtime_error("Recorded grenade explosion has no verified native weapon mapping: event "
                        + std::to_string(event.source_event) + ", weapon " + std::to_string(event.weapon)
                        + (event.weapon < names.size() ? " (" + names[event.weapon] + ")" : ""));
                recorded_weapons.insert(event.weapon);
            }
            for (const auto& event : bullet_track) recorded_weapons.insert(event.weapon);
            for (const auto& event : obituary_track)
                if (event.source_weapon) recorded_weapons.insert(event.source_weapon);
            for (const auto source : recorded_weapons)
            {
                if (source >= names.size()) throw std::runtime_error("COD4 weapon index outside its name table");
                const auto& mapping = cod4::map_weapon(names[source]);
                const bool needs_view = view_weapons.contains(source);
                const std::string target(mapping.base), attachment_kit(mapping.attachment);
                if (mapping.alternate) alternate_weapons.insert(source);
                auto weapon = utils::hook::invoke<unsigned>(0x2E2290_b, target.c_str(), nullptr);
                char resolved[1024]{};
                game::BG_GetWeaponNameComplete(weapon, false, resolved, sizeof(resolved));
                if (!(weapon & 255) || target != resolved)
                    throw std::runtime_error("H1 weapon unavailable: " + target);
                // Base weapon names are H1 net-constant strings. Sending them
                // as ordinary configstrings is rejected by CL_ParseGamestate.
                const auto* kit = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
                    "mp/furniturekits/base.csv", false).stringTable;
                bool has_furniture = false;
                const auto kit_models = cod4::collect_weapon_kit_models(target, attachment_kit, needs_view,
                    kit ? kit->rowCount : 0, [&](int row, int col)
                    {
                        return game::StringTable_GetColumnValueForRow(
                            const_cast<game::StringTable*>(kit), row, col);
                    });
                for (const auto& model : kit_models.names)
                    if (requested_models.insert(model).second)
                        lifecycle << "request kit model " << model << std::endl;
                if (kit) for (int row = 0; row < kit->rowCount; ++row)
                {
                    const auto column = [&](int col) { return game::StringTable_GetColumnValueForRow(
                        const_cast<game::StringTable*>(kit), row, col); };
                    if (target != column(0)) continue;
                    const bool base_part = std::string_view(column(2)) == "1";
                    if (!base_part && mapping.attachment != column(1)) continue;
                    has_furniture |= base_part;
                }
                if (has_furniture || mapping.attachment != "none")
                {
                    const auto attachment = utils::hook::invoke<int>(0x654E0_b, attachment_kit.c_str());
                    const auto furniture = utils::hook::invoke<int>(0x658E0_b, "base");
                    if (attachment < 0 || furniture < 0) throw std::runtime_error("H1 kit IDs unavailable: " + attachment_kit);
                    const auto variant = utils::hook::invoke<unsigned>(0x2E2D50_b,
                        weapon, attachment_kit.c_str(), "base", 0, 0, nullptr);
                    if (!variant || variant >= 1024) throw std::runtime_error("Cannot register H1 weapon kit: " + target);
                    initial_configs[4557 + variant] = std::format("#{} {} {} 0 0", weapon & 255, attachment, furniture);
                    weapon |= variant << 17;
                    game::BG_GetWeaponNameComplete(weapon, false, resolved, sizeof(resolved));
                    lifecycle << "native kit " << variant << ' ' << resolved << std::endl;
                }
                weapons[source] = weapon;
                weapon_defs[source] = game::DB_FindXAssetHeader(game::ASSET_TYPE_WEAPON, target.c_str(), false).weapon;
                lifecycle << "weapon " << source << ' ' << names[source] << " -> " << weapon << ' ' << resolved << std::endl;
                lifecycle << "source ammo " << source << " reserve=" << source_ammo.at(source).reserve
                    << " clip=" << source_ammo.at(source).clip << std::endl;
                const auto* def = weapon_defs[source];
                if (!def || (held_weapons.contains(source) && (!def->gunModel || !def->gunModel[0])))
                    throw std::runtime_error("H1 weapon has no view model: " + target);
                // Weapon metadata can be resident while its model's transient zone
                // is not. CG_UpdateWeaponViewmodels then builds hands only. Acquire
                // the native model request rather than bypassing its readiness gate.
                const auto request_model = [](const game::XModel* model)
                {
                    if (!model || !model->name) return;
                    const std::string model_name = model->name;
                    if (requested_models.insert(model_name).second)
                    {
                        lifecycle << "request model " << model_name << std::endl;
                    }
                };
                // Native scopes/reticles are separate view models in the weapon
                // definition. Keep their transient zones resident with the rifle.
                // Furniture supplies the effective native models. Some base
                // definitions retain obsolete model names (AK47's NPC model)
                // that have no transient zone and are never used by this kit.
                if (needs_view && !kit_models.base_view && def->gunModel)
                    for (unsigned i = 0; i < 2; ++i) request_model(def->gunModel[i]);
                if (!kit_models.base_world && def->worldModel)
                    for (unsigned i = 0; i < 2; ++i) request_model(def->worldModel[i]);
                if (needs_view && def->reticleViewModels)
                    for (unsigned i = 0; i < 64; ++i) request_model(def->reticleViewModels[i]);
                std::array<game::WeaponAttachment*, 4> attachments{};
                const auto count = utils::hook::invoke<int>(0x2E0C20_b, weapon, attachments.data());
                if (count < 0 || count > 4) throw std::runtime_error("Invalid native attachment count");
                for (int a = 0; a < count; ++a)
                {
                    const auto* part = attachments[a];
                    if (!part) throw std::runtime_error("Missing native weapon attachment");
                    lifecycle << "native attachment " << part->name << std::endl;
                    // A selected furniture row supplies the attachment's native
                    // geometry too. M16 GL metadata still names an obsolete
                    // launcher; the base kit supplies the combined M203 part.
                    if (needs_view && !kit_models.attachment_view && part->viewModels)
                        for (unsigned i = 0; i < 2; ++i) request_model(part->viewModels[i]);
                    if (!kit_models.attachment_world && part->worldModels)
                        for (unsigned i = 0; i < 2; ++i) request_model(part->worldModels[i]);
                    if (needs_view && part->reticleViewModels) for (unsigned i = 0; i < 64; ++i) request_model(part->reticleViewModels[i]);
                    // WA model overrides store asset names (for example the
                    // GP25/M203 projectile). The base rifle definition has no
                    // projectileModel; retain the attachment's own native asset.
                    for (unsigned i = 0; i < part->numFields; ++i)
                        if (part->fields[i].type == game::WAFIELD_TYPE_MODEL)
                        {
                            const auto* model_name = part->fields[i].parm.string;
                            if (model_name && *model_name) cod4_camera::request_model(std::string(model_name));
                        }
                }
                if (actor_weapons.contains(source) || std::any_of(gun_track.events.begin(), gun_track.events.end(),
                    [source](const auto& event) { return event.weapon == source; }))
                {
                    // Native firing resolves the effective attachment kit. Check
                    // the base definition's authored FX references and log their
                    // identities; null effects remain native authored omissions.
                    for (const auto& [label, effect] : std::array<std::pair<const char*, const game::FxEffectDef*>, 6>{{
                        {"view_muzzle", def->viewFlashEffect}, {"view_ads_muzzle", def->viewFlashADSEffect},
                        {"world_muzzle", def->worldFlashEffect}, {"view_brass", def->viewShellEjectEffect},
                        {"world_brass", def->worldShellEjectEffect}, {"view_last_brass", def->viewLastShotEjectEffect}}})
                    {
                        if (effect && (!effect->name || !*effect->name
                            || !game::DB_XAssetExists(game::ASSET_TYPE_FX, effect->name)
                            || game::DB_IsXAssetDefault(game::ASSET_TYPE_FX, effect->name)))
                            throw std::runtime_error("Native MWR gun FX unavailable: " + target + " / " + label);
                        lifecycle << "native gun FX " << target << " kit=" << attachment_kit
                            << ' ' << label << '=' << (effect ? effect->name : "<none authored>") << std::endl;
                    }
                }
                                if (projectile_weapons.contains(source))
                {
                    if (!mapping.alternate && (!def->projectileModel || !def->projectileModel->name || !*def->projectileModel->name))
                        throw std::runtime_error("H1 weapon has no projectile model: " + target);
                    if (def->projectileModel) lifecycle << "native projectile " << target << ' ' << def->projectileModel->name << std::endl;
                    request_model(def->projectileModel);
                }
            }
            for (const auto& [index, value] : initial_configs)
                if (index >= 3945) utils::hook::invoke<void>(0x342F90_b, index, value.c_str());
            save_initial_gamestate(track.map);
        }

        void resolve_grenade_effects()
        {
            if (grenade_effects_resolved) return;
            const auto resident = [](const game::FxEffectDef* effect)
            {
                return effect && effect->name && *effect->name
                    && game::DB_XAssetExists(game::ASSET_TYPE_FX, effect->name)
                    && !game::DB_IsXAssetDefault(game::ASSET_TYPE_FX, effect->name);
            };
            for (const auto& event : grenade_track.events)
            {
                const auto weapon = weapons.at(event.weapon);
                const auto* def = weapon_defs.at(event.weapon);
                const auto* weapon_effect = utils::hook::invoke<const game::FxEffectDef*>(0x2E9890_b, weapon, 0);
                const game::FxEffectDef* impact_effect{};
                const auto surface = cod4::grenade_surface(event.surface);
                if (event.source_event == 45)
                {
                    if (def->impactType != game::IMPACT_TYPE_GRENADE_EXPLODE)
                        throw std::runtime_error("Native explosive has an unsupported impact type: " + std::string(def->szInternalName));
                    // CG_GetImpactEffect (0x101D00), impact type 9 -> row 13
                    // (0x101F83); table getter 0x316450 is shared with the native
                    // event handler. It is ready after cgame initialization,
                    // rather than during early weapon registration.
                    const auto* table = utils::hook::invoke<const game::FxImpactTable*>(0x316450_b);
                    if (table && table->table) impact_effect = table->table[13].nonflesh[surface];
                }
                if (!cod4::grenade_effect_available(event.source_event, resident(weapon_effect), resident(impact_effect)))
                    throw std::runtime_error("Native MWR grenade explosion FX unavailable: " + std::string(def->szInternalName)
                        + ", event " + std::to_string(event.source_event) + ", surface " + std::to_string(surface));
                const auto* effect = event.source_event == 45 ? impact_effect : weapon_effect;
                lifecycle << "native grenade FX " << def->szInternalName << " event=" << event.source_event
                    << " surface=" << static_cast<unsigned>(surface) << " -> " << effect->name << std::endl;
            }
            grenade_effects_resolved = true;
        }

        void resolve_car_effects()
        {
            if (car_effects.events.empty() || car_effect_index) return;
            // Verified in the installed Backlot FX pool. It is resident, but
            // absent from the map's scripted FX table. Playback binds its
            // name at this valid native index only during our event dispatch.
            const auto* name = cod4::native_car_explosion_fx.data();
            if (!game::DB_XAssetExists(game::ASSET_TYPE_FX, name)
                || game::DB_IsXAssetDefault(game::ASSET_TYPE_FX, name))
                throw std::runtime_error("Native MWR car explosion FX unavailable: " + std::string(name));
            const auto* effect = game::DB_FindXAssetHeader(game::ASSET_TYPE_FX, name, false).fx;
            if (!effect || !effect->elemDefs || effect->elemDefCountOneShot <= 0)
                throw std::runtime_error("Native MWR car explosion FX has no burst: " + std::string(name));
            car_effect_index = cod4::converted_car_fx_index;
            car_effect_name = name;
            lifecycle << "native car FX " << name << " index=" << car_effect_index
                << " oneshot_elements=" << effect->elemDefCountOneShot << std::endl;
        }

        void register_helicopter_definition(unsigned index, std::string_view name)
        {
            const auto lookup = [](unsigned slot) -> std::string_view
            {
                const auto* def = reinterpret_cast<game::VehicleDef**>(0xCABA310_b)[slot];
                return def && def->type == game::VEH_HELICOPTER && def->name ? def->name : "";
            };
            if (cod4::ensure_helicopter_definition(index, name, lookup, [](std::string_view expected, unsigned slot)
                {
                    const std::string asset(expected);
                    if (!game::DB_XAssetExists(game::ASSET_TYPE_VEHICLE, asset.c_str())
                        || game::DB_IsXAssetDefault(game::ASSET_TYPE_VEHICLE, asset.c_str()))
                        throw std::runtime_error("Native MWR helicopter definition unavailable: " + asset);
                    const auto* def = game::DB_FindXAssetHeader(game::ASSET_TYPE_VEHICLE, asset.c_str(), false).vehDef;
                    if (!def || def->type != game::VEH_HELICOPTER)
                        throw std::runtime_error("Invalid native helicopter vehicle type: " + asset);
                    // Native CG vehicle configstring update: load this definition
                    // and populate cg_vehicleDefs[slot]. Asset residency alone
                    // does not populate the client registry during replay bootstrap.
                    utils::hook::invoke<void>(0x5FD1F0_b, asset.c_str(), slot);
                }))
                console::info("COD4 replay: registered native helicopter %s in vehicle slot %u\n",
                    std::string(name).c_str(), index);
        }

        void resolve_helicopters()
        {
            if (helicopters_resolved) return;
            for (const auto& [model, name] : helicopters.definitions)
            {
                unsigned index = 0;
                for (unsigned i = 1; i < 64; ++i)
                    if (name == get_configstring(2232 + i)) { index = i; break; }
                if (!index || !game::DB_XAssetExists(game::ASSET_TYPE_VEHICLE, name.c_str())
                    || game::DB_IsXAssetDefault(game::ASSET_TYPE_VEHICLE, name.c_str()))
                    throw std::runtime_error("Native MWR helicopter definition unavailable: " + name);
                const auto* def = game::DB_FindXAssetHeader(game::ASSET_TYPE_VEHICLE, name.c_str(), false).vehDef;
                if (!def || def->type != game::VEH_HELICOPTER)
                    throw std::runtime_error("Invalid native helicopter vehicle type: " + name);
                register_helicopter_definition(index, name);
                helicopter_definitions[model] = index;
                lifecycle << "native helicopter " << name << " definition=" << index
                    << " source_model=" << model << " native_model=" << source_models.at(model) << std::endl;
            }
            for (const auto& name : helicopters.sounds)
            {
                unsigned index = 0;
                // CG_PlayLoopSound (0x31D2D0) resolves IDs <512 through
                // configstring 2328+ID; these are native looping sound aliases.
                for (unsigned i = 1; i < 512; ++i)
                    if (name == get_configstring(2328 + i)) { index = i; break; }
                if (!index) throw std::runtime_error("Native MWR helicopter loop sound unavailable: " + name);
                helicopter_sounds[name] = index;
                lifecycle << "native helicopter loop " << name << " index=" << index << std::endl;
            }
            helicopters_resolved = true;
        }

        void resolve_airstrikes()
        {
            if (airstrikes_resolved) return;
            for (const auto& [index, name] : airstrikes.effects)
            {
                if (!game::DB_XAssetExists(game::ASSET_TYPE_FX, name.c_str())
                    || game::DB_IsXAssetDefault(game::ASSET_TYPE_FX, name.c_str()))
                    throw std::runtime_error("Native MWR airstrike FX unavailable: " + name);
                lifecycle << "native airstrike FX " << name << " index=" << index << std::endl;
            }
            for (const auto& [key, fx] : airstrikes.plane_tags)
            {
                const auto model_index = key.first;
                if (!model_index || !source_models.at(model_index))
                    throw std::runtime_error("Native MWR strike plane unavailable");
                const auto effect = cod4::jet_effect(fx);
                if (!effect) continue;
                const auto* name = get_configstring(1240 + source_models.at(model_index));
                const auto* model = game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL, name, false).model;
                if (!model || !model->boneNames) throw std::runtime_error("Native strike plane skeleton unavailable");
                unsigned bone = 0;
                for (; bone < model->numBones; ++bone)
                {
                    const auto* tag = game::SL_ConvertToString(model->boneNames[bone]);
                    if (tag && fx.tag == tag) break;
                }
                if (bone >= model->numBones || bone >= 254)
                    throw std::runtime_error("Native strike plane FX tag unavailable: " + fx.tag);
                jet_parameters[key] = effect | (bone << 9);
                lifecycle << "native jet tag " << name << ' ' << fx.tag << " bone=" << bone << std::endl;
            }
            airstrikes_resolved = true;
        }

        bool resolve_body_assets()
        {
            if (!body_indices.empty()) return true;
            // BG's globalScriptData and 40-byte animation rows are read from
            // this H1 process. PS animation ordinals use a 0x800 restart bit.
            const auto* data = *reinterpret_cast<const std::uint8_t* const*>(0x388C610_b);
            if (!data) return false;
            unsigned count{};
            std::memcpy(&count, data + 0x17638, sizeof(count));
            if (!count) return false;
            if (count > 2048) throw std::runtime_error("Invalid native body animation count");
            std::map<std::string_view, unsigned> catalog;
            for (unsigned i = 0; i < count; ++i)
            {
                const char* name{};
                std::memcpy(&name, data + i * 40, sizeof(name));
                if (!name || !*name) throw std::runtime_error("Invalid native body animation name");
                catalog.emplace(name, i);
            }
            std::set<std::string_view> reported;
            cod4::actor_animation_codec codec;
            for (unsigned i = 0; i < count; ++i)
            {
                std::int16_t legs{}, torso{};
                std::memcpy(&legs, data + 0x70556 + i * 2, 2);
                std::memcpy(&torso, data + 0x6EEA0 + i * 2, 2);
                codec.legs.push_back(legs); codec.torso.push_back(torso);
            }
            std::set<std::tuple<unsigned, unsigned, unsigned>> checked_actor_states;
            std::set<std::string> animation_failures;
            for (const auto& snapshot : world.frames)
                for (const auto& [number, entity] : snapshot.entities)
                {
                    if (entity.get(4) != 1 && entity.get(4) != 2) continue;
                    const auto key = std::tuple{entity.get(204), entity.get(208), entity.get(196)};
                    if (!checked_actor_states.insert(key).second) continue;
                    const auto& weapon = source_weapon_names.at(entity.get(196));
                    try
                    {
                        const auto packed = cod4::actor_animation(entity.get(204), entity.get(208), weapon, catalog, codec);
                        actor_animations.emplace(key, packed);
                        lifecycle << "native actor animation source_legs=" << entity.get(204)
                            << " source_torso=" << entity.get(208) << " weapon=" << weapon << " packed=" << packed << std::endl;
                    }
                    catch (const std::runtime_error& error)
                    {
                        animation_failures.insert(error.what());
                    }
                }
            if (!animation_failures.empty())
            {
                for (const auto& failure : animation_failures) lifecycle << "animation audit: " << failure << std::endl;
                throw std::runtime_error(std::format("{} actor animation mapping failure(s); see demos/cod4-replay-last.log. First: {}",
                    animation_failures.size(), *animation_failures.begin()));
            }
            lifecycle << "remote actors ready animation_states=" << actor_animations.size()
                << " gun_actions=" << actor_events.actions << std::endl;
            std::vector<std::pair<unsigned, unsigned>> indices;
            for (const auto& action : body_actions)
            {
                indices.emplace_back(cod4::resolve_body_animation(action.legs, action.legs_restart, catalog),
                    cod4::resolve_body_animation(action.torso, action.torso_restart, catalog));
                for (const auto name : {action.legs, action.torso})
                    if (!name.empty() && reported.insert(name).second)
                        lifecycle << "native body animation " << catalog.at(name) << ' ' << name << std::endl;
            }
            body_indices = std::move(indices);
            lifecycle << "body ready frames=" << body_indices.size() << " native_clips=" << reported.size() << std::endl;
            return true;
        }

        struct replay_message
        {
            std::array<char, 0x20000> bytes{};
            game::msg_t msg{};
            replay_message() { utils::hook::invoke<void>(0x4EB1D0_b, &msg, bytes.data(), static_cast<int>(bytes.size())); }
            void bits(int v, int n) { utils::hook::invoke<void>(0x4EBD40_b, &msg, v, n); }
            void bit0() { utils::hook::invoke<void>(0x4EBCB0_b, &msg); }
            void byte(int v) { utils::hook::invoke<void>(0x4EBF30_b, &msg, v); }
            void word(int v) { byte(v & 255); byte((v >> 8) & 255); }
            void integer(int v) { utils::hook::invoke<void>(0x4EBFF0_b, &msg, v); }
            void string(const std::string& v) { for (unsigned char c : v) byte(c); byte(0); }
            void reset_index() { utils::hook::invoke<void>(0x4EC1E0_b, &msg); }
            void parse()
            {
                if (msg.overflowed) throw std::runtime_error("H1 replay message overflow");
                msg.readcount = msg.bit = 0;
                // The internal parser accepts an already decoded message. There is
                // no network packet, compression wrapper or remote peer involved.
                utils::hook::invoke<void>(0x341420_b, 0, &msg);
                if (msg.overflowed) throw std::runtime_error("H1 rejected replay message");
            }
        };

        void save_initial_gamestate(const std::string& map)
        {
            replay_message packet;
            packet.bits(0, 3); // H1 svc_gamestate
            packet.integer(0); // reliable command sequence
            packet.string(map);
            packet.integer(0);
            packet.string("war");
            packet.integer(0);
            packet.bits(1, 3); // configstring section
            packet.word(static_cast<int>(initial_configs.size()));
            for (const auto& [index, value] : initial_configs)
            {
                packet.bit0(); packet.bits(index, 13); packet.string(value);
            }
            packet.bits(0, 10); // weapon configstrings are included in the ordinary section
            packet.bits(7, 3); // end gamestate
            packet.integer(0); // demo client
            packet.integer(0); // checksum feed
            packet.bit0();
            packet.bits(7, 3); // end message
            initial_gamestate.assign(packet.bytes.begin(), packet.bytes.begin() + packet.msg.cursize);
        }

        void bootstrap_client(const std::string& map)
        {
            replay_asset_pool = true;
            bootstrapping = true;
            const auto clear_bootstrap = gsl::finally([] { bootstrapping = false; });
            if (game::VirtualLobby_Loaded()) utils::hook::invoke<void>(0x13C9C0_b, 1);
            game::netadr_s address{};
            address.type = game::NA_BAD; // replay has no network destination
            std::array<char, 0x100> session{};
            utils::hook::invoke<void>(0x12DFF0_b, 0, session.data(), &address, map.c_str(), "war");
            *game::mp::connstate = game::CA_CONNECTED;
            if (game::SV_Loaded()) throw std::runtime_error("Replay initialization unexpectedly started a server");

            std::string serverinfo;
            const auto field = [&](const char* name, const std::string& value)
            {
                serverinfo += std::format("\\0x{:08X}\\{}", static_cast<unsigned>(dvars::generate_hash(name)), value);
            };
            field("mapname", map);
            field("g_gametype", "war");
            field("sv_maxclients", "18");
            field("sv_hostname", "COD4 demo replay");
            field("sv_running", "0");
            field("sv_cheats", "1");
            std::map<int, std::string> configs{{0, serverinfo}, {1, ""}, {3, "0"}, {6, "0"}, {9, "0000000000000000"}, {10, "0 0 0 1"}};
            if (const auto center = track.configstrings.find(12); center != track.configstrings.end()) configs[8] = center->second;
            cod4::translate_minimap_configstrings(track.configstrings, configs);
            initial_configs = std::move(configs);
            save_initial_gamestate(map);
            replay_message packet;
            std::memcpy(packet.bytes.data(), initial_gamestate.data(), initial_gamestate.size());
            packet.msg.cursize = static_cast<int>(initial_gamestate.size());
            packet.parse();
            console::info("COD4 replay: client initialized, server=%d, state=%d\n", game::SV_Loaded(), static_cast<int>(*game::mp::connstate));
        }

        void feed_snapshot(int time, std::ofstream* converted_file = nullptr)
        {
            const auto frame = track.sample(time);
            // H1's demo connection is client 0. A COD4 recorder can be any
            // client; keep it at 0 and compact sparse remote identities into
            // native player/agent slots. UI, actors and events share this map.
            const auto remap_client = [](unsigned number) { return client_slots.client(number); };
            alignas(16) std::array<std::uint8_t, 18576> ps{};
            const auto put = [&](std::size_t offset, const auto& value) { std::memcpy(ps.data() + offset, &value, sizeof(value)); };
            // H1's linked-view entity uses ENTITYNUM_NONE, not zero. Zero links
            // the view to entity 0 and applies the player's rotation a second time.
            put(0x2C, std::uint16_t{2047});
            put(0, std::uint8_t{0});
            put(0x4C, time); // commandTime, verified against the live PC netfield table
            put(0x5C, std::uint32_t{0x4000}); // recorded view: no local input prediction
            put(0x74, cod4::translate_bob_cycle(frame.bob_cycle));
            put(0x78, frame.origin);
            put(0x84, frame.velocity);
            put(0x12C, frame.angles);
            if (hitmarker_omnvar)
            {
                const auto offset = 0x4540 + *hitmarker_omnvar * 8;
                put(offset + 4, hitmarker_values[0]);
                const auto next = std::upper_bound(hitmarkers.begin(), hitmarkers.end(), time,
                    [](int t, const cod4::hitmarker_event& event) { return t < event.time; });
                if (next != hitmarkers.begin())
                {
                    const auto& event = *(next - 1);
                    put(offset, static_cast<unsigned>(event.time));
                    put(offset + 4, hitmarker_values[event.juggernaut ? 1 : 0]);
                }
            }
            if (score_popup_omnvar && !score_popups.empty())
            {
                const auto next = std::upper_bound(score_popups.begin(), score_popups.end(), time,
                    [](int t, const cod4::score_popup_event& event) { return t < event.time; });
                if (next != score_popups.begin())
                {
                    const auto& event = *(next - 1);
                    const auto score = std::bit_cast<float>(event.element.get(120));
                    if (std::isfinite(score) && score > 0 && score < 10000 && std::trunc(score) == score)
                    {
                        const auto offset = 0x4540 + *score_popup_omnvar * 8;
                        put(offset, static_cast<std::uint32_t>(event.time));
                        put(offset + 4, static_cast<std::uint32_t>(static_cast<std::int32_t>(score)
                            - score_popup_omnvar_min));
                    }
                }
            }
            const auto& player = player_at(time);
            if (team_selection_omnvar)
            {
                const auto& clients = world.sample(time).clients;
                const auto client = clients.find(static_cast<unsigned>(world.client_num));
                const unsigned team = client == clients.end() ? 0u : client->second.get(4);
                const auto offset = 0x4540 + *team_selection_omnvar * 8;
                put(offset, static_cast<unsigned>(time));
                put(offset + 4, cod4::native_team_selection(team, team_selection_min));
            }
            // The native HUD checks live player stats before presenting weapon
            // and match widgets. Preserve the recorded health/death state.
            for (unsigned i = 0; i < 4; ++i) put(0x15C + i * 4, player.stats[i]);
            if (score_popup_label && !score_popups.empty())
            {
                const auto next = std::upper_bound(score_popups.begin(), score_popups.end(), time,
                    [](int t, const cod4::score_popup_event& event) { return t < event.time; });
                if (next != score_popups.begin())
                {
                    const auto& event = *(next - 1);
                    if (time - event.time < static_cast<int>(event.element.get(48)))
                    {
                        const auto native = cod4::translate_score_popup(event.element, *score_popup_label);
                        // H1's archival HUD bank begins at +0x3A00.
                        std::memcpy(ps.data() + 0x3A00, native.data(), native.size());
                    }
                }
            }
            const auto shock = player.integer("shellshockIndex");
            cod4::write_view_motion(player, time, shock ? shock_profiles.at(shock).index : std::uint8_t{}, put);
            const auto next_body = std::upper_bound(track.frames.begin(), track.frames.end(), time,
                [](int t, const cod4::camera_frame& f) { return t < f.time; });
            const auto body_index = next_body == track.frames.begin() ? 0 : next_body - track.frames.begin() - 1;
            const auto& [legs, torso] = body_indices.at(static_cast<std::size_t>(body_index));
            const auto source_weapon = player.integer("weapon");
            put(0x22, static_cast<std::uint16_t>(client_slots.reference(
                cod4::translate_entity_number(player.integer("groundEntityNum")))));
            put(0x54, cod4::translate_movement_flags(player.integer("pm_flags")));
            put(0xB0, player.integer("legsTimer"));
            put(0xB4, legs);
            put(0xB8, player.integer("torsoTimer"));
            put(0xBC, torso);
            put(0xC8, cod4::translate_body_heading(frame));
            put(0x3C, hands_models.at(hands_key_at(time)));
            put(0x138, player.integer("viewHeightTarget"));
            put(0x13C, player.real("viewHeightCurrent"));
            put(0x394, weapons.at(source_weapon));
            put(0x39C, cod4::interpolate_ads(players, time));
            put(0x224, cod4::translate_animation(player.integer("weapAnim")));
            put(0x228, player.integer("weaponTime"));
            put(0x22C, player.integer("weaponDelay"));
            put(0x230, player.integer("weaponRestrictKickTime"));
            put(0x234, cod4::translate_weapon_state(player.integer("weaponstate")));
            put(0x23C, player.integer("weaponShotCount"));
            cod4::write_gun_events(cod4::gun_events_at(gun_track, time), put);
            const auto offhand = player.integer("offHandIndex");
            put(0x388, offhand ? weapons.at(offhand) : 0u);
            put(0x398, cod4::native_weapon_flags(player.integer("weapFlags"), alternate_weapons.contains(source_weapon)));
            for (const auto& [offset, name] : std::array<std::pair<unsigned, const char*>, 5>{{
                {440, "sprintState.sprintButtonUpRequired"}, {444, "sprintState.sprintDelay"},
                {448, "sprintState.lastSprintStart"}, {452, "sprintState.lastSprintEnd"},
                {456, "sprintState.sprintStartMaxLength"}}}) put(offset, player.integer(name));

            // Populate native keyed ammo records. H1 keys are resolved by its own
            // BG ammo/clip functions; COD4's ordinals must never be copied into H1.
            // The limit applies to one snapshot's inventory, not every weapon
            // picked up during the whole recording. Primary/GL share an equipped
            // handle but have separate keyed ammo records.
            std::vector<unsigned> inventory;
            const auto add_inventory = [&](unsigned source)
            {
                if (source && std::find(inventory.begin(), inventory.end(), source) == inventory.end()) inventory.push_back(source);
            };
            add_inventory(source_weapon); add_inventory(offhand);
            for (const auto& [source, target] : weapons)
            {
                if (source < 128 && (player.integer(std::format("weapons[{}]", source / 32)) & (1u << (source % 32))))
                    add_inventory(source);
            }
            std::set<unsigned> equipped;
            const auto [lethal, tactical] = cod4::select_hud_offhands(inventory,
                player.integer("offhandSecondary"), [](unsigned source) -> std::string_view
                { return source_weapon_names.at(source); });
            static_assert(offsetof(game::mp::playerState_s, weapCommon) == 904);
            static_assert(offsetof(game::mp::PlayerWeaponCommonState, lethalWeapon) == 4);
            static_assert(offsetof(game::mp::PlayerWeaponCommonState, tacticalWeapon) == 8);
            put(908, lethal ? weapons.at(lethal) : 0u);
            put(912, tactical ? weapons.at(tactical) : 0u);
            std::map<std::uint64_t, int> reserves, clips;
            for (const auto source : inventory)
            {
                const auto target = weapons.at(source);
                const bool alternate = alternate_weapons.contains(source);
                const auto slots = source_ammo.at(source);
                const auto reserve_key = utils::hook::invoke<std::uint64_t>(0x2DE400_b, target, alternate);
                const auto clip_key = utils::hook::invoke<std::uint64_t>(0x2DE580_b, target, alternate);
                reserves.emplace(reserve_key, player.ammo.at(slots.reserve));
                clips.emplace(clip_key, player.clips.at(slots.clip));
                // Equipped data is needed by native weapon animation lookup.
                if (!equipped.insert(target).second) continue;
                if (equipped.size() > 15) throw std::runtime_error("Recorded inventory exceeds H1's equipped slots");
                const auto slot = equipped.size() - 1;
                put(604 + slot * 4, target);
                put(664 + slot * 16, std::uint8_t{1});
                put(666 + slot * 16, static_cast<std::uint8_t>(alternate));
            }
            if (reserves.size() > 15 || clips.size() > 15) throw std::runtime_error("Recorded inventory exceeds H1's ammo slots");
            unsigned slot = 0;
            for (const auto& [key, amount] : reserves)
            { put(956 + slot * 12, key); put(964 + slot * 12, amount); ++slot; }
            slot = 0;
            for (const auto& [key, amount] : clips)
            { put(1136 + slot * 24, key); put(1144 + slot * 24, amount); ++slot; }
            if (source_weapon && last_action != player.integer("weapAnim"))
            {
                last_action = player.integer("weapAnim");
                lifecycle << "action time=" << time << " source_weapon=" << source_weapon
                    << " source_anim=" << last_action << " h1_anim=" << cod4::translate_animation(last_action)
                    << " state=" << cod4::translate_weapon_state(player.integer("weaponstate"))
                    << " clip=" << player.clips.at(source_ammo.at(source_weapon).clip)
                    << " native_clip=" << utils::hook::invoke<int>(0xE9640_b, ps.data(), weapons.at(source_weapon), alternate_weapons.contains(source_weapon), 0)
                    << std::endl;
            }
            alignas(16) std::array<std::uint8_t, 128> context{};
            context[29] = 1; // engine encoder's client-independent keyframe mode
            std::memcpy(context.data()+4, reinterpret_cast<void*>(0x2ED20A8_b), 12);
            replay_message packet;
            packet.bits(6, 3); packet.integer(time); packet.byte(0); packet.byte(8); packet.bit0();
            utils::hook::invoke<void>(0x4F52E0_b, context.data(), &packet.msg, time, nullptr, ps.data());
            // Full world keyframes make removals, seek and restart independent
            // of any live server. Entity and client writers use MWR's PC tables.
            packet.bit0(); packet.bit0(); packet.reset_index();
            const auto& recorded = world.sample(time);
            const auto recorded_clients = cod4::clients_with_corpse_identities(world, time);
            cod4::h1_entity from{};
            // The gamestate contains no entity baselines: CL decodes from
            // zeroed records. MSG_InitEntityState (0x4EC490) instead supplies
            // position type 3, rotation type 1, flesh and ENTITYNUM_WORLD.
            // Using those defaults here omits equal values from the wire,
            // leaving linear-stop jets with stationary type 0 in playback.
            // Protect only the zero origin as required by the native writer.
            const std::array<float, 3> zero{};
            utils::hook::invoke<void>(0x5B0CF0_b, from.data() + 96, zero.data());
            cod4::entity_snapshot snapshot_entities;
            const auto write_entity = [&](const cod4::h1_entity& native) { snapshot_entities.add(native); };
            for (const auto& [number, entity] : recorded.entities)
            {
                if ((entity.get(4) == 1 || entity.get(4) == 2) && time >= world.frames.front().time)
                {
                    if (entity.get(4) == 1 && number == static_cast<unsigned>(world.client_num)) continue;
                    const auto client = recorded_clients.find(entity.get(140));
                    if (client == recorded_clients.end() || !client->second.get(8))
                        throw std::runtime_error("Recorded actor has no character identity");
                    const auto& events = cod4::actor_events_at(actor_events, time).actors;
                    const auto event = events.find(number);
                    auto native = cod4::translate_actor(entity, cod4::sample_actor_motion(world, number, time),
                        weapons.at(entity.get(196)), actor_animations.at({entity.get(204), entity.get(208), entity.get(196)}),
                        event == events.end() ? nullptr : &event->second, alternate_weapons.contains(entity.get(196)));
                    cod4::assign_actor_slot(native, remap_client(entity.get(140)));
                    cod4::remap_entity_references(native, client_slots);
                    write_entity(native);
                    continue;
                }
                if (entity.get(4) == 3 && entity.get(136))
                {
                    const auto weapon_index = cod4::item_weapon_index(entity);
                    auto native = cod4::translate_item(entity, weapons.at(weapon_index), time,
                        alternate_weapons.contains(weapon_index));
                    cod4::remap_entity_references(native, client_slots);
                    write_entity(native);
                    continue;
                }
                if (entity.get(4) == 4)
                {
                    auto native = cod4::translate_missile(entity, weapons.at(entity.get(196)), time,
                        alternate_weapons.contains(entity.get(196)));
                    cod4::remap_entity_references(native, client_slots);
                    write_entity(native);
                    continue;
                }
                if (entity.get(4) == 12)
                {
                    const auto index = entity.get(216);
                    const auto& vehicle_frame = helicopters.at(time);
                    const auto sound = vehicle_frame.loops.find(number);
                    const auto parts = source_parts.find(index);
                    write_entity(cod4::translate_helicopter(cod4::sample_helicopter(world, number, time),
                        source_models.at(index), helicopter_definitions.at(index), vehicle_frame.slots.at(number),
                        sound == vehicle_frame.loops.end() ? 0 : helicopter_sounds.at(sound->second),
                        parts == source_parts.end() ? nullptr : &parts->second));
                    continue;
                }
                if (entity.get(4) == 13)
                {
                    const auto index = entity.get(136);
                    const auto parts = source_parts.find(index);
                    write_entity(cod4::translate_plane(entity, source_models.at(index), jet_parameters,
                        parts == source_parts.end() ? nullptr : &parts->second));
                    continue;
                }
                if (entity.get(4) != 6 || !entity.get(136) || cod4::script_model_is_brush(entity)) continue;
                const auto index = entity.get(136);
                if (!source_models.at(index)) continue; // Never serialize a default or invented replacement.
                const auto parts = source_parts.find(index);
                auto native = cod4::translate_script_model(entity, source_models.at(index),
                    parts == source_parts.end() ? nullptr : &parts->second);
                if (track.configstrings.at(830 + index) == "projectile_cbu97_clusterbomb")
                    cod4::preserve_clusterbomb_trajectory(native, entity, time);
                const auto animation = std::find_if(car_animations.rbegin(), car_animations.rend(), [&](const auto& car)
                { return car.entity == number && car.model == index && car.time <= time; });
                if (animation != car_animations.rend()) cod4::apply_car_animation(native, *animation, car_animation_duration);
                write_entity(native);
            }
            for (const auto& [number, native] : map_models)
                write_entity(native);
            if (converted_file)
            {
                for (std::size_t i = 0, count = cod4::grenade_events_at(grenade_track.events, time); i < count; ++i)
                    write_entity(grenade_entities.at(i));
                for (std::size_t i = 0, count = cod4::car_fx_at(car_effects.events, time); i < count; ++i)
                    write_entity(car_effect_entities.at(i));
                for (std::size_t i = 0, count = cod4::car_fx_at(airstrikes.bursts, time); i < count; ++i)
                    write_entity(airstrike_entities.at(i));
                for (std::size_t i = 0, count = cod4::bullet_events_at(bullet_track, time); i < count; ++i)
                    write_entity(bullet_entities.at(i));
                for (std::size_t i = 0, count = cod4::obituary_events_at(obituary_track, time); i < count; ++i)
                    write_entity(obituary_entities.at(i));
            }
            // Corpse slots move from COD4 64..71 to H1 42..49. Recorded
            // pickups may precede them in source order, and later grenade
            // identities can precede earlier car FX identities. H1's compact
            // index writer requires increasing destination entity numbers.
            // Archived snapshots use the native corpse-origin override bit
            // (CL_ParsePacketEntity 0x3408C9). MSG_WriteDeltaEntity checks
            // context +28 and writes context +35 after each corpse. Keep the
            // override false so the recorded corpse origin remains intact.
            context[28] = 1;
            context[35] = 0;
            for (auto [number, native] : snapshot_entities.entities)
            {
                // Every native trajectory stores trBase with process-local
                // protection. The writer decodes it before serialization.
                std::array<float, 3> origin{};
                std::memcpy(origin.data(), native.data() + 108, sizeof(origin));
                utils::hook::invoke<void>(0x5B0CF0_b, native.data() + 96, origin.data());
                utils::hook::invoke<void>(0x4F6B30_b, context.data(), &packet.msg, time,
                    static_cast<int>(native[10]), from.data(), native.data(), true);
            }
            utils::hook::invoke<void>(0x4F6C10_b, &packet.msg, 2047, 11);
            context[28] = 0;
            packet.reset_index(); packet.byte(0);
            std::map<unsigned, cod4::h1_client> snapshot_clients;
            std::map<unsigned, cod4::h1_agent> snapshot_agents;
            for (const auto& [number, client] : recorded_clients)
            {
                const auto& models = character_models.at(client_model_key(client));
                if (!models.body) continue; // Spectators have no recorded character model.
                const auto& names = world.sample(time).client_names;
                const auto name = names.find(number);
                const auto destination = remap_client(number);
                const auto full_name = name == names.end() ? std::string_view{} : std::string_view(name->second);
                if (cod4::agent_slot(destination))
                {
                    snapshot_agents.emplace(destination, cod4::translate_agent(client, destination,
                        models.body, models.attachments, agent_name_ids.at(cod4::recorded_actor_name(client, full_name))));
                    continue;
                }
                auto native = cod4::translate_client(client, models.body, models.attachments, full_name);
                cod4::write_native(native, 0, static_cast<std::uint8_t>(destination));
                snapshot_clients.emplace(destination, native);
            }
            for (const auto& [number, native] : snapshot_clients)
            {
                utils::hook::invoke<void>(0x4F3910_b, context.data(), &packet.msg, time, nullptr, native.data(), true);
            }
            packet.bit0(); packet.reset_index();
            for (const auto& [number, native] : snapshot_agents)
                utils::hook::invoke<void>(0x4F3330_b, context.data(), &packet.msg, time, nullptr, native.data(), true);
            packet.bit0();
            utils::hook::invoke<void>(0x417CA0_b, &packet.msg);
            utils::hook::invoke<void>(0x500B40_b, &packet.msg);
            packet.bits(7, 3);
            if (converted_file)
            {
                if (packet.msg.overflowed) throw std::runtime_error("Converted H1 snapshot overflow");
                const auto compressed = utils::compression::zlib::compress(
                    std::span(reinterpret_cast<const std::uint8_t*>(packet.bytes.data()), packet.msg.cursize), 4096);
                demo_utils::write_id_and_size(*converted_file, compressed.size() + 8, demo_utils::demo_data_id::network_data);
                const std::uint32_t sequence = ++snapshot_sequence, flags = 0x80000000u;
                converted_file->write(reinterpret_cast<const char*>(&sequence), 4);
                converted_file->write(reinterpret_cast<const char*>(&flags), 4);
                converted_file->write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
                return;
            }
            game::mp::clc->serverMessageSequence = ++snapshot_sequence;
            game::mp::clc->reliableAcknowledge = game::mp::clc->reliableSequence;
            packet.parse();
            if (!game::mp::cl->snap.valid) throw std::runtime_error("H1 rejected COD4 camera snapshot");
            last_snapshot_time = time;
            last_player_command = player.integer("commandTime");
        }

        void stop_locked(const std::string& message)
        {
            state.active = state.loading = false;
            replay_asset_pool = false;
            for (const auto& name : acquired_models)
                utils::hook::invoke<void>(0x76850_b, name.c_str());
            acquired_models.clear();
            requested_models.clear();
            model_bindings.clear();
            animation_bindings.clear();
            agent_name_ids.clear(); recorded_agent_names.clear();
            bound_models.clear(); character_models.clear(); hands_models.clear();
            state.message = message;
            if (trace.is_open()) trace.close();
            if (lifecycle.is_open()) lifecycle << "stop: " << message << std::endl;
        }

        void disconnect_then_stop_locked(const std::string& message)
        {
            if (!state.active)
            {
                stop_locked(message);
                converting = false;
                return;
            }
            if (disconnect_pending) return;
            disconnect_pending = true;
            state.loading = false;
            state.paused = true;
            state.message = message;
            lifecycle << "disconnect requested: " << message << std::endl;
            // CL_SetCGameTime must stay gated until the client is disconnected.
            // CL_Disconnect can perform an engine longjmp. A scheduler callback
            // runs in a noexcept frame destructor, so queue the engine command
            // and release bindings only after its normal teardown has finished.
            command::execute("disconnect", false);
            scheduler::schedule([message]
            {
                if (*game::mp::connstate != game::CA_DISCONNECTED && !game::VirtualLobby_Loaded())
                    return scheduler::cond_continue;
                std::lock_guard lock(mutex);
                stop_locked(message);
                converting = disconnect_pending = false;
                lifecycle << "cleanup: disconnected before releasing model bindings" << std::endl;
                return scheduler::cond_end;
            }, scheduler::main);
        }

        void export_demo()
        {
            grenade_entities.clear(); grenade_times.clear();
            car_effect_entities.clear(); car_effect_times.clear();
            airstrike_entities.clear(); airstrike_times.clear();
            bullet_entities.clear(); bullet_times.clear();
            obituary_entities.clear();
            unsigned fx_entity_number = map_models.empty() ? 1024u : map_models.rbegin()->first + 1;
            for (const auto& event : grenade_track.events)
            {
                grenade_entities.push_back(cod4::translate_grenade_event(event, fx_entity_number, weapons.at(event.weapon)));
                grenade_times.emplace(fx_entity_number++, event.time);
                lifecycle << "grenade event time=" << event.time << " source_entity=" << event.entity
                    << " source_event=" << event.source_event << " native_event=" << cod4::native_grenade_event(event.source_event)
                    << " native_entity=" << fx_entity_number - 1 << " source_weapon=" << event.weapon
                    << " normal=" << event.parameter << " surface=" << event.surface
                    << " origin=" << event.origin[0] << ',' << event.origin[1] << ',' << event.origin[2] << std::endl;
            }
            for (const auto& event : car_effects.events)
            {
                car_effect_entities.push_back(cod4::translate_car_fx(event, fx_entity_number, car_effect_index));
                car_effect_times.emplace(fx_entity_number++, event.time);
                lifecycle << "car FX time=" << event.time << " source_entity=" << event.entity
                    << " native_entity=" << fx_entity_number - 1 << " native_event=111 effect=" << car_effect_name
                    << " index=" << car_effect_index << " origin=" << event.origin[0] << ',' << event.origin[1]
                    << ',' << event.origin[2] << " angles=" << event.angles[0] << ',' << event.angles[1]
                    << ',' << event.angles[2] << std::endl;
            }
            for (const auto& event : airstrikes.bursts)
            {
                airstrike_entities.push_back(cod4::translate_car_fx(event, fx_entity_number, cod4::airstrike_burst_index));
                airstrike_times.emplace(fx_entity_number++, event.time);
                lifecycle << "airstrike burst time=" << event.time << " source_entity=" << event.entity
                    << " native_entity=" << fx_entity_number - 1 << " effect=" << cod4::native_airstrike_burst << std::endl;
            }
            for (const auto& event : bullet_track)
            {
                bullet_entities.push_back(cod4::translate_bullet_event(event, fx_entity_number,
                    weapons.at(event.weapon), alternate_weapons.contains(event.weapon),
                    0, &client_slots));
                bullet_times.emplace(fx_entity_number++, event.time);
                lifecycle << "bullet impact time=" << event.time << " source_entity=" << event.entity
                    << " source_event=" << event.source_event << " native_event=" << cod4::native_bullet_event(event.source_event)
                    << " native_entity=" << fx_entity_number - 1 << " shooter=" << event.shooter
                    << " target=" << event.target << " client=" << event.client << " source_weapon=" << event.weapon
                    << " normal=" << event.parameter << " surface=" << event.surface << " flags=" << event.impact_flags
                    << " origin=" << event.origin[0] << ',' << event.origin[1] << ',' << event.origin[2] << std::endl;
            }
            for (const auto& event : obituary_track)
            {
                const auto remap = [](unsigned client)
                { return client == 1022 ? 2046u : client_slots.client(client); };
                obituary_entities.push_back(cod4::translate_obituary_event(event, fx_entity_number,
                    remap(event.attacker), remap(event.victim),
                    event.source_weapon ? weapons.at(event.source_weapon) : 0,
                    alternate_weapons.contains(event.source_weapon)));
                lifecycle << "obituary time=" << event.time << " source_entity=" << event.entity
                    << " native_entity=" << fx_entity_number << " attacker=" << event.attacker
                    << " victim=" << event.victim << " source_parameter=" << event.event_parameter << std::endl;
                ++fx_entity_number;
            }
            std::filesystem::create_directories(conversion_output.parent_path());
            auto temporary = conversion_output;
            temporary += ".tmp";
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.exceptions(std::ios::badbit | std::ios::failbit);
            std::string header;
            for (const auto& item : std::vector<std::string>{"", "H1 COD4 conversion v3", "v0.0.3",
                std::to_string(PROTOCOL), "", "COD4 conversion", "", track.map, "war"})
            { header += item; header.push_back('\0'); }
            for (const auto& model : requested_models) { header += model; header.push_back('\0'); }
            for (const auto& [index, model] : model_bindings)
            { header += std::format("@model:{}:{}", index, model); header.push_back('\0'); }
            for (const auto& [index, animation] : animation_bindings)
            { header += std::format("@anim:{}:{}", index, animation); header.push_back('\0'); }
            for (const auto& [index, name] : recorded_agent_names)
            { header += std::format("@agentname:{}:{}", index, name); header.push_back('\0'); }
            for (const auto& [entity, time] : grenade_times)
            { header += std::format("@grenade:{}:{}", entity, time); header.push_back('\0'); }
            for (const auto& [entity, time] : car_effect_times)
            { header += std::format("@carfx:{}:{}", entity, time); header.push_back('\0'); }
            for (const auto& [entity, time] : airstrike_times)
            { header += std::format("@scriptfx:{}:{}", entity, time); header.push_back('\0'); }
            for (const auto& [index, name] : airstrikes.effects)
            { header += std::format("@fx:{}:{}", index, name); header.push_back('\0'); }
            for (const auto& [entity, time] : bullet_times)
            { header += std::format("@impact:{}:{}", entity, time); header.push_back('\0'); }
            if (!car_effect_times.empty())
            { header += std::format("@fx:{}:{}", car_effect_index, car_effect_name); header.push_back('\0'); }
            demo_utils::write_id_and_size(output, header.size(), demo_utils::demo_data_id::gen_header);
            output.write(header.data(), header.size());
            const auto map_header = track.map + std::string("\0war\0", 5);
            demo_utils::write_id_and_size(output, map_header.size(), demo_utils::demo_data_id::map_header);
            output.write(map_header.data(), map_header.size());
            const auto compressed = utils::compression::zlib::compress(std::span<const std::uint8_t>(initial_gamestate), 4096);
            demo_utils::write_id_and_size(output, compressed.size() + 8, demo_utils::demo_data_id::network_data);
            const std::uint32_t sequence = 1, flags = 0x80000000u;
            output.write(reinterpret_cast<const char*>(&sequence), 4);
            output.write(reinterpret_cast<const char*>(&flags), 4);
            output.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
            if (!demo_utils::write_gamestate_data(output)) throw std::runtime_error("Cannot serialize H1 gamestate");
            snapshot_sequence = 1;
            // Emit each archive sample as a native snapshot. This retains camera
            // sampling without an external track, replay clock or source file.
            std::uint8_t archive_index{};
            for (const auto& frame : track.frames)
            {
                game::ClientArchiveData archive{};
                archive.serverTime = frame.time;
                std::memcpy(archive.origin, frame.origin.data(), sizeof(archive.origin));
                std::memcpy(archive.velocity, frame.velocity.data(), sizeof(archive.velocity));
                archive.bobCycle = cod4::translate_bob_cycle(frame.bob_cycle);
                archive.movementDir = cod4::translate_body_heading(frame);
                game::vec3_t angles{};
                std::memcpy(angles, frame.angles.data(), sizeof(angles));
                // Normal H1 demos restore the recorded view through this archive,
                // independently of snapshot prediction. Emit it before its snapshot.
                demo_utils::write_predicted_player_data(output, archive, angles,
                    archive_index++, demo_utils::predicted_data_id::player);
                feed_snapshot(frame.time, &output);
            }
            demo_utils::write_general_footer(output, track.frames.front().time, track.frames.back().time);
            demo_utils::write_end_of_file(output);
            output.close();
            auto report_path = conversion_output;
            report_path += ".conversion.txt";
            std::ofstream report(report_path, std::ios::trunc);
            report.exceptions(std::ios::badbit | std::ios::failbit);
            report << "COD4 to H1 conversion\nSource: " << state.name << "\nMap: " << track.map
                << "\nOutput: " << conversion_output.filename().string()
                << "\nOmitted prop model identities: " << omitted_props.size() << "\n";
            report << "\nCharacter model combinations: " << character_models.size()
                << "\nHands/body combinations: " << hands_models.size() << "\n";
            for (const auto& [source, destination] : client_slots.slots)
                report << "Player slot: COD4 " << source << " -> H1 " << destination << '\n';
            report << "\nNative MWR grenade detonations: " << grenade_track.events.size() << '\n';
            std::set<unsigned> remote_players, corpses, pickups;
            for (const auto& snapshot : world.frames)
                for (const auto& [number, entity] : snapshot.entities)
                {
                    if (entity.get(4) == 1 && number != static_cast<unsigned>(world.client_num)) remote_players.insert(number);
                    if (entity.get(4) == 2) corpses.insert(number);
                    if (entity.get(4) == 3) pickups.insert(number);
                }
            report << "Remote players: " << remote_players.size() << ", corpse entities: " << corpses.size()
                << ", weapon pickup entities: " << pickups.size() << '\n';
            report << "Remote native gun actions: " << actor_events.actions
                << ", native packed actor animation states: " << actor_animations.size() << '\n';
            report << "Source snapshot packets discarded for unavailable delta baseline: " << world.discarded_deltas << '\n';
            std::set<std::pair<unsigned, unsigned>> brushes, pickup_variants;
            for (const auto& snapshot : world.frames) for (const auto& [number, entity] : snapshot.entities)
            {
                if (cod4::script_model_is_brush(entity)) brushes.emplace(number, entity.get(136));
                if (entity.get(4) == 3 && entity.get(136) >= 128)
                    pickup_variants.emplace(cod4::item_weapon_index(entity), entity.get(136) / 128);
            }
            for (const auto& [number, index] : brushes)
                report << "Inline BSP brush omitted: entity " << number << ", source brush " << index
                    << " (native map brush correspondence not verified)\n";
            for (const auto& [weapon, variant] : pickup_variants)
                report << "Pickup world-model variant omitted: " << source_weapon_names.at(weapon) << ", variant " << variant
                    << " (native weapon appearance used)\n";
            report << "Native MWR gun firing/rechamber/brass events: " << gun_track.events.size() << '\n';
            for (const auto& event : gun_track.events)
                report << "Gun action: command time " << event.time << " ms, snapshot time " << event.snapshot_time
                    << " ms, event " << event.source << " -> native " << event.native
                    << ", weapon " << weapon_defs.at(event.weapon)->szInternalName << '\n';
            for (const auto& event : gun_track.omitted)
                report << "Gun action omitted: command time " << event.time << " ms, event " << event.source
                    << ", no recorded held weapon\n";
            report << "Native MWR bullet impact events: " << bullet_track.size() << '\n';
            for (const auto& event : bullet_track)
                report << "Bullet impact: source time " << event.time << " ms, source entity " << event.entity
                    << ", event " << event.source_event << " -> native " << cod4::native_bullet_event(event.source_event)
                    << ", weapon " << weapon_defs.at(event.weapon)->szInternalName
                    << ", surface " << event.surface << ", shooter " << event.shooter << ", target " << event.target
                    << ", origin " << event.origin[0] << ' ' << event.origin[1] << ' ' << event.origin[2]
                    << ", direction " << event.parameter << ", flags " << event.impact_flags << '\n';
            report << "Native MWR car explosion FX: " << car_effects.events.size() << '\n';
            report << "Native MWR strike plane entities: " << airstrikes.planes.size() << '\n';
            report << "Native MWR cluster bursts: " << airstrikes.bursts.size() << '\n';
            report << "Native jet FX tag bindings: " << jet_parameters.size() << '\n';
            report << "Native MWR helicopter entities: " << helicopters.entities.size() << '\n';
            report << "Helicopter event-bearing states (combat events not yet translated): " << helicopters.event_states << '\n';
            for (const auto& event : airstrikes.bursts)
                report << "Cluster burst: source time " << event.time << " ms, source entity " << event.entity
                    << ", native FX " << cod4::native_airstrike_burst << '\n';
            report << "Native MWR car destruction animations: " << car_animations.size() << '\n';
            for (const auto& car : car_animations)
                report << "Car animation: entity " << car.entity << ", time " << car.time
                    << " ms, clip " << cod4::native_car_destroy_animation << '\n';
            report << "Script model trajectories: native acceleration/deceleration and client physics\n";
            for (const auto& event : car_effects.events)
                report << "Car FX: source time " << event.time << " ms, source entity " << event.entity
                    << ", event 55 -> native 111, effect " << car_effect_name << ", native index " << car_effect_index
                    << ", origin " << event.origin[0] << ' ' << event.origin[1] << ' ' << event.origin[2] << '\n';
            for (const auto& [name, count] : car_effects.other_effects)
                report << "Other scripted FX omitted: " << name << " (" << count << ")\n";
            for (const auto& event : grenade_track.events)
                report << "Grenade: source time " << event.time << " ms, source entity " << event.entity
                    << ", event " << event.source_event << " -> native " << cod4::native_grenade_event(event.source_event)
                    << ", weapon " << weapon_defs.at(event.weapon)->szInternalName
                    << ", origin " << event.origin[0] << ' ' << event.origin[1] << ' ' << event.origin[2]
                    << ", direction " << event.parameter << ", surface " << event.surface << '\n';
            for (const auto& [event, count] : grenade_track.other_explosions)
                report << "Unsupported explosion event " << event << ": " << count << " recorded occurrence(s)\n";
            for (const auto& [key, models] : character_models)
            {
                if (!models.body) continue;
                report << "Character: " << track.configstrings.at(830 + key[0])
                    << " -> " << get_configstring(1240 + models.body) << "\nAttachments:";
                for (const auto index : models.attachments)
                    if (index) report << ' ' << get_configstring(1240 + index);
                report << '\n';
            }
            for (const auto& [key, index] : hands_models)
                if (index) report << "Hands: " << track.configstrings.at(830 + key.first)
                    << " -> " << get_configstring(1240 + index) << '\n';
            for (const auto& [index, name] : omitted_props)
            {
                report << "\n" << name << " (source model " << index << ")\n"
                    << "Reason: no remastered counterpart available in the loaded map/native transient catalog.\nEntities:";
                for (const auto number : omitted_prop_entities.at(index)) report << ' ' << number;
                report << '\n';
            }
            report.close();
            std::filesystem::rename(temporary, conversion_output);
            lifecycle << "converted=" << std::filesystem::absolute(conversion_output).string()
                << " snapshots=" << track.frames.size() << std::endl;
            converting = false;
            std::lock_guard lock(mutex);
            auto message = "Saved " + conversion_output.string() + ". Refresh the demo list and press Play.";
            if (!omitted_props.empty()) message += std::format(" Omitted {} prop model(s); see {}.",
                omitted_props.size(), report_path.filename().string());
            disconnect_then_stop_locked(message);
        }

        void play(const std::filesystem::path& path, bool convert = false)
        {
            {
                std::lock_guard lock(mutex);
                if (disconnect_pending) return;
            }
            try
            {
                converting = convert;
                conversion_output = std::filesystem::path("demos/client/auto") / (path.stem().string() + ".dm_h1");
                if (convert)
                {
                    unsigned version = 0;
                    auto temporary = conversion_output;
                    temporary += ".tmp";
                    while (std::filesystem::exists(conversion_output) || std::filesystem::exists(temporary))
                    {
                        if (++version > 9999) throw std::runtime_error("No unused converted demo filename available");
                        conversion_output = conversion_output.parent_path()
                            / std::format("{}.{:04}.dm_h1", path.stem().string(), version);
                        temporary = conversion_output;
                        temporary += ".tmp";
                    }
                }
                auto decoded = cod4::load(path);
                std::ifstream source_file(path, std::ios::binary);
                const std::vector<std::uint8_t> source_bytes((std::istreambuf_iterator<char>(source_file)), {});
                auto decoded_players = cod4::decode_players(source_bytes);
                auto decoded_gun_events = cod4::collect_gun_events(decoded_players);
                auto decoded_world = cod4::decode_world(source_bytes);
                const auto decoded_slots = cod4::make_client_slots(decoded_world);
                auto decoded_grenades = cod4::collect_grenade_events(decoded_world);
                auto decoded_bullets = cod4::collect_bullet_events(decoded_world);
                auto decoded_obituaries = cod4::collect_obituary_events(decoded_world);
                auto decoded_car_fx = cod4::collect_car_fx(decoded_world, decoded.configstrings);
                auto decoded_airstrikes = cod4::collect_airstrikes(decoded_world, decoded.configstrings);
                auto decoded_helicopters = cod4::collect_helicopters(decoded_world, decoded.configstrings);
                auto decoded_car_animations = cod4::collect_car_animations(decoded_world, decoded.configstrings, decoded_car_fx.events);
                if (!game::SV_MapExists(decoded.map.c_str())) throw std::runtime_error("This map is not installed in H1: " + decoded.map);
                game::Dvar_SetFromStringByNameFromSource("developer", "1", game::DVAR_SOURCE_INTERNAL);
                if (!demo_playback::playing() && game::CL_IsCgameInitialized() && !game::VirtualLobby_Loaded() && !active())
                    throw std::runtime_error("Disconnect from the match before starting COD4 camera playback");
                if (demo_playback::playing())
                {
                    demo_playback::stopplay();
                    command::execute("disconnect", true);
                }
                const auto map = decoded.map;
                {
                    std::lock_guard lock(mutex);
                    stop_locked("");
                    track = std::move(decoded);
                    players = std::move(decoded_players);
                    gun_track = std::move(decoded_gun_events);
                    world = std::move(decoded_world);
                    client_slots = decoded_slots;
                    agent_name_ids = cod4::collect_agent_names(world, client_slots);
                    for (const auto& [name, index] : agent_name_ids) recorded_agent_names.emplace(index, name);
                    score_popups = cod4::collect_score_popups(world, track.configstrings);
                    hitmarkers = cod4::collect_hitmarkers(world, track.configstrings);
                    hitmarker_omnvar.reset();
                    hitmarkers_resolved = false;
                    minimap_resolved = false;
                    grenade_track = std::move(decoded_grenades);
                    bullet_track = std::move(decoded_bullets);
                    obituary_track = std::move(decoded_obituaries);
                    bullet_entities.clear(); bullet_times.clear();
                    obituary_entities.clear();
                    grenade_entities.clear(); grenade_times.clear();
                    grenade_effects_resolved = false;
                    car_effects = std::move(decoded_car_fx);
                    airstrikes = std::move(decoded_airstrikes);
                    helicopters = std::move(decoded_helicopters);
                    helicopter_definitions.clear(); helicopter_sounds.clear(); helicopters_resolved = false;
                    jet_parameters.clear(); airstrike_entities.clear(); airstrike_times.clear();
                    airstrikes_resolved = false;
                    car_animations = std::move(decoded_car_animations);
                    car_animation_duration = 0;
                    car_effect_entities.clear(); car_effect_times.clear();
                    car_effect_index = 0; car_effect_name.clear();
                    world_resolved = false;
                    score_popup_label.reset();
                    score_popup_omnvar.reset();
                    score_popup_omnvar_min = 0;
                    score_popup_resolved = false;
                    source_parts.clear();
                    parts_resolved = false;
                    shock_profiles.clear();
                    shocks_resolved = false;
                    state = {};
                    state.active = state.loading = true;
                    state.name = path.filename().string();
                    state.map = map;
                    state.duration = (track.frames.back().time - track.frames.front().time) / 1000.0f;
                    state.message = convert ? "Loading remastered map assets for conversion..."
                        : "Loading remastered map for COD4 replay...";
                    playhead = 0;
                    load_started = last_tick = clock::now();
                    trace_bucket = -1;
                    std::filesystem::create_directories("demos");
                    trace.open("demos/cod4-camera-last.csv", std::ios::trunc);
                    if (lifecycle.is_open()) lifecycle.close();
                    lifecycle.open("demos/cod4-replay-last.log", std::ios::trunc);
                    lifecycle << "source=" << path.string() << " map=" << map << " frames=" << track.frames.size() << std::endl;
                    trace << "seconds,source_time,source_x,source_y,source_z,pitch,yaw,roll,render_x,render_y,render_z\n";
                    console::info("COD4 camera: %s, map=%s, frames=%zu, replaced pre-roll=%zu, duration=%.3fs\n",
                        state.name.c_str(), map.c_str(), track.frames.size(), track.replaced_frames, state.duration);
                }
                snapshot_sequence = 0;
                last_snapshot_time = -1;
                last_player_command = 0;
                last_action = UINT_MAX;
                bootstrap_client(map);
                resolve_player_assets();
                lifecycle << "initialized: server=" << game::SV_Loaded() << " state=" << static_cast<int>(*game::mp::connstate) << std::endl;
            }
            catch (const std::exception& e)
            {
                std::lock_guard lock(mutex);
                disconnect_then_stop_locked(e.what());
                console::error("COD4 camera: %s\n", e.what());
            }
        }

        bool number(const command::params& args, float& value)
        {
            if (args.size() != 2) return false;
            char* end = nullptr;
            value = std::strtof(args[1], &end);
            return end != args[1] && *end == '\0' && std::isfinite(value);
        }
    }

    status get_status() { std::lock_guard lock(mutex); return state; }
    bool active() { std::lock_guard lock(mutex); return state.active; }

    void set_agent_names(const std::map<unsigned, std::string>& names) { recorded_agent_names = names; }
    bool has_replay_agents() { return !recorded_agent_names.empty(); }

    std::optional<std::string> agent_name(unsigned client)
    {
        if (!cod4::agent_slot(client) || recorded_agent_names.empty()) return std::nullopt;
        // Mirror CL_GetAgentName (0x3439D0): current parsed agent ring,
        // rather than indexing H1's 18-entry clientInfo array with an agent.
        const auto* cl = reinterpret_cast<const std::uint8_t*>(game::mp::cl.get());
        if (!cl) return std::nullopt;
        const int count = *reinterpret_cast<const int*>(cl + 0x4A6C);
        const int first = *reinterpret_cast<const int*>(cl + 0x4A70);
        const int capacity = *reinterpret_cast<const int*>(cl + 0x8FC0);
        const auto* ring = *reinterpret_cast<const std::uint8_t* const*>(cl + 0x8FC8);
        if (!ring || count < 0 || count > 24 || first < 0 || capacity <= 0 || capacity > 65536)
            return std::nullopt;
        for (int i = 0; i < count; ++i)
        {
            const auto* agent = ring + ((static_cast<unsigned>(first) + i) % capacity) * 128;
            if (*reinterpret_cast<const std::uint16_t*>(agent) != client) continue;
            const auto found = recorded_agent_names.find(*reinterpret_cast<const unsigned*>(agent + 124));
            if (found != recorded_agent_names.end()) return found->second;
        }
        return std::nullopt;
    }

    void validate_model_budget(const std::set<std::string>& models)
    {
        std::map<unsigned, std::set<unsigned>> required;
        for (const auto& name : models)
        {
            std::array<std::uint16_t, 8> entries{};
            const auto count = utils::hook::invoke<int>(0x75210_b, name.c_str(), 7, entries.data());
            if (count < 0 || count > static_cast<int>(entries.size()))
                throw std::runtime_error("Invalid native transient model lookup: " + name);
            for (int i = 0; i < count; ++i)
            {
                // Model records identify their owning zone. Several parts of
                // one kit share a zone and consume only one native slot.
                if (entries[i] >= 9216) throw std::runtime_error("Invalid native transient model index");
                const auto zone = *reinterpret_cast<const std::uint16_t*>(0x152E0B0_b + entries[i] * 12 + 6);
                if (zone >= 7144) throw std::runtime_error("Invalid native transient zone index");
                const auto category = *reinterpret_cast<const std::uint8_t*>(0x14D6D40_b + zone * 48 + 8);
                if (category >= 13) throw std::runtime_error("Invalid native transient category");
                required[category].insert(zone);
            }
        }
        for (const auto& [category, zones] : required)
        {
            const auto* pool = reinterpret_cast<const std::uint8_t*>(0x14D4BF0_b + category * 112);
            const auto capacity = *reinterpret_cast<const unsigned*>(pool + 0x48);
            if (zones.size() > capacity)
                throw std::runtime_error(std::format("Demo needs {} native {} asset slots; this session has {}",
                    zones.size(), reinterpret_cast<const char*>(pool), capacity));
        }
    }

    void prepare_helicopter_vehicle(const std::uint8_t* entity_state)
    {
        if (!entity_state || entity_state[10] != 12)
            throw std::runtime_error("Invalid replay helicopter state");
        unsigned packed{}, model{};
        std::memcpy(&packed, entity_state + 168, sizeof(packed));
        std::memcpy(&model, entity_state + 208, sizeof(model));
        if (packed >= 512 || !(packed >> 3) || !model || model >= 1024)
            throw std::runtime_error("Invalid replay helicopter vehicle/model index");
        const auto definition = packed >> 3;
        const auto expected = cod4::native_helicopter_definition(get_configstring(1240 + model));
        if (expected != get_configstring(2232 + definition))
            throw std::runtime_error("Recorded helicopter definition no longer matches this map's registry");
        register_helicopter_definition(definition, expected);
    }

    void clear_model_bindings()
    {
        model_bindings.clear(); animation_bindings.clear(); converted_bindings = false;
        recorded_agent_names.clear();
        replay_asset_pool = false;
    }
    void set_model_bindings(const std::map<unsigned, std::string>& bindings,
        const std::map<unsigned, std::string>& animations)
    {
        model_bindings.clear();
        for (const auto& [index, name] : bindings)
        {
            if (index < 768 || index >= 1024 || !cod4::valid_model_name(name)
                || *get_configstring_hook.invoke<const char*>(1240 + index))
                throw std::runtime_error("Converted model slot is invalid or occupied");
        }
        model_bindings = bindings;
        animation_bindings.clear();
        for (const auto& [index, name] : animations)
        {
            if (!index || index >= 255 || !cod4::valid_model_name(name))
                throw std::runtime_error("Invalid converted animation binding");
        }
        animation_bindings = animations;
        converted_bindings = true;
    }

    void bootstrap_demo(const std::string& map, const std::vector<std::uint8_t>& gamestate)
    {
        replay_asset_pool = true;
        bootstrapping = true;
        const auto clear = gsl::finally([] { bootstrapping = false; });
        if (game::VirtualLobby_Loaded()) utils::hook::invoke<void>(0x13C9C0_b, 1);
        game::netadr_s address{};
        address.type = game::NA_BAD;
        std::array<char, 0x100> session{};
        utils::hook::invoke<void>(0x12DFF0_b, 0, session.data(), &address, map.c_str(), "war");
        *game::mp::connstate = game::CA_CONNECTED;
        if (game::SV_Loaded()) throw std::runtime_error("Demo bootstrap unexpectedly started a server");
        replay_message packet;
        if (gamestate.size() > packet.bytes.size()) throw std::runtime_error("Oversized demo gamestate");
        std::memcpy(packet.bytes.data(), gamestate.data(), gamestate.size());
        packet.msg.cursize = static_cast<int>(gamestate.size());
        packet.parse();
    }

    bool prepare_client_frame()
    {
        if (!active()) return false;
        if (bootstrapping || disconnect_pending) return true;
        try
        {
            static int reported_state = -1;
            if (reported_state != static_cast<int>(*game::mp::connstate))
            {
                reported_state = static_cast<int>(*game::mp::connstate);
                lifecycle << "state=" << reported_state << " server=" << game::SV_Loaded() << std::endl;
            }
            if (*game::mp::connstate < game::CA_PRIMED) return true;
            resolve_minimap();
            resolve_world_assets();
            resolve_score_popup_label();
            resolve_hitmarkers();
            resolve_grenade_effects();
            resolve_car_effects();
            if (!car_animations.empty() && animation_bindings.empty())
            {
                const auto name = std::string(cod4::native_car_destroy_animation);
                if (!game::DB_XAssetExists(game::ASSET_TYPE_XANIMPARTS, name.c_str())
                    || game::DB_IsXAssetDefault(game::ASSET_TYPE_XANIMPARTS, name.c_str()))
                    throw std::runtime_error("Native MWR car destruction animation unavailable: " + name);
                if (*get_configstring_hook.invoke<const char*>(cod4::script_animation_configstring(cod4::converted_car_animation_index)))
                    throw std::runtime_error("Converted car animation slot is occupied");
                const auto* clip = game::DB_FindXAssetHeader(game::ASSET_TYPE_XANIMPARTS, name.c_str(), false).parts;
                if (!clip || !clip->numframes || !std::isfinite(clip->framerate) || clip->framerate <= 0)
                    throw std::runtime_error("Invalid native MWR car destruction animation");
                car_animation_duration = static_cast<int>(std::lround(1000.0f * clip->numframes / clip->framerate));
                if (car_animation_duration <= 0 || car_animation_duration > 60000)
                    throw std::runtime_error("Native MWR car animation duration outside bounds");
                animation_bindings[cod4::converted_car_animation_index] = name;
                lifecycle << "native car animation " << name << " cars=" << car_animations.size()
                    << " duration=" << car_animation_duration << " flags=" << static_cast<unsigned>(clip->flags) << std::endl;
            }
            if (!shocks_resolved)
            {
                std::map<unsigned, std::string> native_shock_profiles;
                for (unsigned index = 1; index < 16; ++index)
                {
                    const auto* name = get_configstring_hook.invoke<const char*>(3416 + index);
                    if (name && *name)
                    {
                        native_shock_profiles.emplace(index, name);
                        lifecycle << "native shock slot " << index << ' ' << name << std::endl;
                    }
                }
                const auto existing_profiles = native_shock_profiles;
                const bool const_strings = utils::hook::invoke<bool>(0x2B19B0_b, 3417);
                native_shock_profiles = cod4::complete_shock_registry(players, track.configstrings,
                    std::move(native_shock_profiles), const_strings, [](const std::string& name)
                    {
                        const auto asset = "shock/" + name + ".shock";
                        return game::DB_XAssetExists(game::ASSET_TYPE_RAWFILE, asset.c_str())
                            && !game::DB_IsXAssetDefault(game::ASSET_TYPE_RAWFILE, asset.c_str());
                    });
                shock_profiles = cod4::resolve_shock_profiles(players, track.configstrings, native_shock_profiles);
                for (const auto& [source, profile] : shock_profiles)
                {
                    const auto asset = "shock/" + profile.name + ".shock";
                    if (!game::DB_XAssetExists(game::ASSET_TYPE_RAWFILE, asset.c_str())
                        || game::DB_IsXAssetDefault(game::ASSET_TYPE_RAWFILE, asset.c_str()))
                        throw std::runtime_error("Remastered shellshock profile unavailable: " + profile.name);
                    if (!existing_profiles.contains(profile.index))
                    {
                        initial_configs[3416 + profile.index] = profile.name;
                        utils::hook::invoke<void>(0x342F90_b, 3416 + profile.index, profile.name.c_str());
                    }
                    // The client-only bootstrap has no server to precache a
                    // dynamically registered profile. Use H1's own parser and
                    // native profile store, as CG_RegisterGraphics does.
                    if (!utils::hook::invoke<bool>(0x2C3910_b, profile.name.c_str()))
                        throw std::runtime_error("Cannot parse remastered shellshock profile: " + profile.name);
                    utils::hook::invoke<void>(0x2C5740_b,
                        utils::hook::invoke<void*>(0x2C3550_b, static_cast<int>(profile.index)));
                    lifecycle << "shock mapping " << source << ' ' << profile.name << " -> "
                        << static_cast<unsigned>(profile.index) << std::endl;
                }
                if (!shock_profiles.empty()) save_initial_gamestate(track.map);
                shocks_resolved = true;
            }
            // H1's normal frontend transition pumps these requests only while
            // its map-preload flag is set. Client-only conversion does not set
            // that flag, so service H1's own transient queue before readiness.
            // Requesting many Promod characters/weapons spans multiple batches.
            validate_model_budget(requested_models);
            for (const auto& name : requested_models)
                if (acquired_models.insert(name).second) utils::hook::invoke<void>(0x75720_b, name.c_str());
            utils::hook::invoke<void>(0x757A0_b);
            for (const auto& name : requested_models)
                if (!utils::hook::invoke<bool>(0x75470_b, name.c_str())) return true;
            resolve_world_parts();
            resolve_airstrikes();
            resolve_helicopters();
            if (!resolve_body_assets()) return true;
            if (converting)
            {
                for (const auto& name : requested_models)
                    if (!game::DB_XAssetExists(game::ASSET_TYPE_XMODEL, name.c_str())
                        || game::DB_IsXAssetDefault(game::ASSET_TYPE_XMODEL, name.c_str()))
                        throw std::runtime_error("Requested remastered model failed to load: " + name);
                export_demo();
                return true;
            }
            const int time = track.frames.front().time + static_cast<int>(playhead * 1000.0);
            if (last_snapshot_time < 0 || time >= last_snapshot_time + 50
                || player_at(time).integer("commandTime") != last_player_command)
            {
                feed_snapshot(time);
                if (snapshot_sequence == 1) lifecycle << "first snapshot valid at " << time << std::endl;
            }
            if (*game::mp::connstate == game::CA_ACTIVE)
                game::mp::cl->newSnapshots = 0;
        }
        catch (const std::exception& e)
        {
            std::lock_guard lock(mutex);
            disconnect_then_stop_locked(e.what());
            console::error("COD4 replay: %s\n", e.what());
            return true;
        }
        return false;
    }

    void finish_client_frame()
    {
        if (!active() || bootstrapping || disconnect_pending || *game::mp::connstate != game::CA_ACTIVE) return;
        const int time = track.frames.front().time + static_cast<int>(playhead * 1000.0);
        auto* cl = game::mp::cl.get();
        cl->serverTimeDelta -= cl->serverTime - time;
        cl->serverTime = time;
    }

    bool view(theater_camera::camera_pose_t& pose)
    {
        std::lock_guard lock(mutex);
        if (!state.active || disconnect_pending) return false;
        const auto now = clock::now();
        if (demo_playback::playing()) { stop_locked("Stopped for H1 demo playback"); return false; }
        auto* map = game::Dvar_FindVar("mapname");
        const bool ready = !game::VirtualLobby_Loaded() && *game::connectionState == 10
            && map && map->current.string && state.map == map->current.string;
        if (!ready)
        {
            if (!state.loading) stop_locked("Playback stopped: map disconnected");
            last_tick = now;
            return false;
        }
        if (state.loading)
        {
            for (const auto& name : requested_models)
                if (!utils::hook::invoke<bool>(0x75470_b, name.c_str())) return false;
            for (const auto& name : requested_models)
                lifecycle << "model ready " << name << std::endl;
            state.loading = false;
            state.message = "COD4 replay: camera, ADS, recorded ammo and weapon animations.";
            last_tick = now;
            console::info("COD4 camera: map ready; playing recorded track\n");
        }
        if (!state.paused) playhead += std::chrono::duration<double>(now - last_tick).count() * state.speed;
        last_tick = now;
        if (playhead >= state.duration) { playhead = state.duration; state.paused = true; }
        state.seconds = static_cast<float>(playhead);
        const auto sample = track.sample(track.frames.front().time + playhead * 1000.0);
        for (int i = 0; i < 3; ++i) { pose.pos[i] = sample.origin[i]; pose.angles[i] = sample.angles[i]; }
        const auto& player = player_at(track.frames.front().time + playhead * 1000.0);
        pose.pos[2] += player.real("viewHeightCurrent") + state.eye_height - 60.0f;
        return true;
    }

    void record_applied(const float* origin, float horizontal_fov)
    {
        std::lock_guard lock(mutex);
        const auto bucket = static_cast<int>(playhead * 4);
        if (!state.active || state.loading || bucket == trace_bucket || !trace) return;
        trace_bucket = bucket;
        const auto sample = track.sample(track.frames.front().time + playhead * 1000.0);
        trace << playhead << ',' << sample.time;
        for (auto v : sample.origin) trace << ',' << v;
        for (auto v : sample.angles) trace << ',' << v;
        for (int i = 0; i < 3; ++i) trace << ',' << origin[i];
        trace << '\n';
        trace.flush();
        if (bucket % 4 == 0)
        {
            auto* cg = reinterpret_cast<const std::uint8_t*>(game::CG_GetLocalClientGlobals());
            auto* cl = reinterpret_cast<const std::uint8_t*>(game::mp::cl.get());
            const auto u32 = [](const std::uint8_t* p, int off) { std::uint32_t v; std::memcpy(&v, p + off, 4); return v; };
            lifecycle << "view " << playhead << " cl_weapon=" << u32(cl, 0x394) << " cg_weapon=" << u32(cg, 0x394)
                << " cg_hands=" << (u32(cg, 0x3C) & 65535) << " pm_type=" << static_cast<int>(cg[2])
                << " dobj=" << *reinterpret_cast<void**>(0x390B130_b)
                << " thirdperson=" << u32(cg, 0x92C04)
                << " cubemap=" << u32(cg, 0x48D4)
                << " view_hidden=" << static_cast<int>(cg[0xEF220])
                << " special_view=" << utils::hook::invoke<bool>(0x2C3760_b, cg)
                << " eflags=" << u32(cg, 0x58) << " otherflags=" << u32(cg, 0x5C);
            lifecycle << " horizontal_fov=" << horizontal_fov;
            lifecycle << " anim=" << u32(cg, 548) << " weapon_state=" << u32(cg, 564)
                << " clip=" << utils::hook::invoke<int>(0xE9640_b, cg, u32(cg, 916), false, 0)
                << " offhand=" << u32(cg, 904) << " weapon_flags=" << u32(cg, 920);
            for (const auto* name : {"cg_drawGun", "cg_thirdPerson", "cg_fov", "cg_fovScale"})
                if (auto* d = game::Dvar_FindVar(name)) lifecycle << ' ' << name << '=' << game::Dvar_ValueToString(d, true, d->current);
            lifecycle << " native_drawgun=" << utils::hook::invoke<bool>(0x183340_b, *reinterpret_cast<void**>(0x390B740_b));
            for (const auto off : {0x78, 0xEF214})
            {
                float p[3]; std::memcpy(p, cg + off, sizeof(p));
                lifecycle << " origin_" << off << '=' << p[0] << ',' << p[1] << ',' << p[2];
            }
            if (auto* obj = *reinterpret_cast<void**>(0x390B130_b))
            {
                const auto count = utils::hook::invoke<unsigned>(0x5C7780_b, obj);
                lifecycle << " models=" << count;
                for (unsigned i = 0; i < count && i < 32; ++i)
                {
                    const auto* model = utils::hook::invoke<game::XModel*>(0x5C7730_b, obj, i);
                    lifecycle << ' ' << (model ? model->name : "null");
                }
            }
            lifecycle << std::endl;
        }
    }

    class component final : public component_interface
    {
    public:
        void post_unpack() override
        {
            if (!game::environment::is_mp() || game::environment::is_dedi()) return;
            get_configstring_hook.create(0x33B820_b, get_configstring);
            utils::hook::call(0x7445D_b, reserve_view_weapon_pool);
            command::add("cod4cam", [](const command::params& args)
            {
                if (args.size() != 2) { console::info("Usage: cod4cam \"path/to/demo.dm_1\"\n"); return; }
                play(args[1]);
            });
            command::add("cod4cam_stop", [] { std::lock_guard lock(mutex); disconnect_then_stop_locked("COD4 camera stopped"); });
            command::add("cod4cam_pause", [] { std::lock_guard lock(mutex); state.paused = !state.paused; last_tick = clock::now(); });
            command::add("cod4cam_seek", [](const command::params& args)
            {
                float value;
                if (!number(args, value)) return;
                std::lock_guard lock(mutex);
                playhead = std::clamp(value, 0.0f, state.duration);
                state.seconds = static_cast<float>(playhead);
                last_tick = clock::now();
                trace_bucket = -1;
            });
            command::add("cod4cam_speed", [](const command::params& args)
            {
                float value;
                if (!number(args, value)) return;
                std::lock_guard lock(mutex); state.speed = std::clamp(value, 0.05f, 4.0f); last_tick = clock::now();
            });
            command::add("cod4cam_height", [](const command::params& args)
            {
                float value;
                if (!number(args, value)) return;
                std::lock_guard lock(mutex); state.eye_height = std::clamp(value, 0.0f, 100.0f);
            });
            scheduler::loop([]
            {
                std::lock_guard lock(mutex);
                if (state.loading && clock::now() - load_started > 90s) disconnect_then_stop_locked("Map load timed out");
            }, scheduler::main, 1s);
            command::add("cod4convert", [](const command::params& args)
            {
                if (args.size() != 2) { console::info("Usage: cod4convert \"source.dm_1\"\n"); return; }
                if (game::Live_SyncOnlineDataFlags(0) != 0 || !game::Sys_IsDatabaseReady2())
                {
                    std::lock_guard lock(mutex);
                    state.message = "Open Play Online and finish initialization before converting a demo.";
                    console::info("%s\n", state.message.c_str());
                    return;
                }
                play(args[1], true);
            });
            command::add("cod4_mapping_audit", []
            {
                if (!game::Sys_IsDatabaseReady2() || game::VirtualLobby_Loaded())
                { console::info("Load a demo before auditing the native weapon catalog.\n"); return; }
                std::ofstream report("cod4-mapping-audit.log", std::ios::trunc);
                unsigned passed = 0;
                for (const auto& mapping : cod4::weapon_mappings)
                {
                    try
                    {
                        const std::string base(mapping.base), kit(mapping.attachment);
                        auto weapon = utils::hook::invoke<unsigned>(0x2E2290_b, base.c_str(), nullptr);
                        char name[1024]{};
                        game::BG_GetWeaponNameComplete(weapon, false, name, sizeof(name));
                        if (!(weapon & 255u) || base != name) throw std::runtime_error("native base missing");
                        if (kit != "none")
                        {
                            if (utils::hook::invoke<int>(0x654E0_b, kit.c_str()) < 0) throw std::runtime_error("native kit missing");
                            const auto variant = utils::hook::invoke<unsigned>(0x2E2D50_b, weapon, kit.c_str(), "base", 0, 0, nullptr);
                            if (!variant || variant >= 1024) throw std::runtime_error("native variant missing");
                            weapon |= variant << 17;
                            std::array<game::WeaponAttachment*, 4> parts{};
                            const auto count = utils::hook::invoke<int>(0x2E0C20_b, weapon, parts.data());
                            if (count <= 0 || count > 4) throw std::runtime_error("native attachment resolution failed");
                        }
                        if (mapping.alternate && !utils::hook::invoke<game::WeaponAttachment*>(0x2E0DA0_b, weapon, 1))
                            throw std::runtime_error("native alternate attachment missing");
                        game::BG_GetWeaponNameComplete(weapon, mapping.alternate, name, sizeof(name));
                        report << "PASS " << mapping.source << " -> " << name << '\n';
                        ++passed;
                    }
                    catch (const std::exception& e) { report << "FAIL " << mapping.source << ": " << e.what() << '\n'; }
                }
                report << "Native mappings passed " << passed << '/' << cod4::weapon_mappings.size() << '\n';
                console::info("Native COD4 weapon mappings passed %u/%zu; see cod4-mapping-audit.log\n", passed, cod4::weapon_mappings.size());
            });

        }
    };
}

REGISTER_COMPONENT(cod4_camera::component)
