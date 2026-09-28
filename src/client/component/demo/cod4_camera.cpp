#include "std_include.hpp"
#include "loader/component_loader.hpp"
#include "cod4_camera.hpp"
#include "cod4_demo_reader.hpp"
#include "cod4_playerstate_reader.hpp"
#include "cod4_action_translation.hpp"
#include "cod4_body_translation.hpp"
#include "cod4_view_translation.hpp"
#include "cod4_world_translation.hpp"
#include "cod4_model_metadata.hpp"
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
        bool parts_resolved{};
        utils::hook::detour get_configstring_hook;
        std::map<unsigned, unsigned> weapons;
        std::map<unsigned, game::WeaponDef*> weapon_defs;
        std::set<unsigned> alternate_weapons;
        std::vector<cod4::body_animation> body_actions;
        std::vector<std::pair<unsigned, unsigned>> body_indices;
        std::vector<cod4::ammo_slots> source_ammo;
        std::set<std::string> requested_models;
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

        const char* get_configstring(int index)
        {
            const auto found = model_bindings.find(static_cast<unsigned>(index - 1240));
            if (found != model_bindings.end()) return found->second.c_str();
            return get_configstring_hook.invoke<const char*>(index);
        }

        void request_model(const std::string& name)
        {
            if (requested_models.insert(name).second)
            {
                utils::hook::invoke<void>(0x75720_b, name.c_str());
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
                    if (entity.get(4) == 6 && entity.get(136) && !source_model(entity.get(136), true))
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

        void resolve_world_parts()
        {
            if (parts_resolved) return;
            for (const auto& frame : world.frames)
            {
                for (const auto& [number, entity] : frame.entities)
                {
                    const auto index = entity.get(136);
                    if (entity.get(4) != 6 || !index || source_parts.contains(index)) continue;
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
            weapons.clear(); weapons[0] = 0; weapon_defs.clear(); alternate_weapons.clear(); hands_models.clear();
            body_actions.clear(); body_indices.clear();
            std::set<unsigned> recorded_weapons;
            std::set<unsigned> held_weapons;
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
            for (const auto& movement : track.frames)
            {
                const auto& player = player_at(movement.time);
                const auto source = player.integer("weapon");
                if (source >= names.size()) throw std::runtime_error("Invalid COD4 body weapon");
                body_actions.push_back(cod4::select_body_animation(player, movement, names[source]));
            }
            for (const auto& frame : world.frames)
                for (const auto& [number, entity] : frame.entities)
                    if (entity.get(4) == 4 && entity.get(196))
                    {
                        recorded_weapons.insert(entity.get(196));
                        projectile_weapons.insert(entity.get(196));
                    }
            for (const auto source : recorded_weapons)
            {
                if (source >= names.size()) throw std::runtime_error("COD4 weapon index outside its name table");
                const auto& mapping = cod4::map_weapon(names[source]);
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
                bool kit_view_model = false, kit_world_model = false;
                if (kit) for (int row = 0; row < kit->rowCount; ++row)
                {
                    const auto column = [&](int col) { return game::StringTable_GetColumnValueForRow(
                        const_cast<game::StringTable*>(kit), row, col); };
                    if (target != column(0)) continue;
                    const bool base_part = std::string_view(column(2)) == "1";
                    if (!base_part && mapping.attachment != column(1)) continue;
                    has_furniture |= base_part;
                    // The kit selects dry/wet view and world-model parts.
                    for (int col : {3, 4, 6, 7})
                    {
                        const std::string model = column(col);
                        if (!model.empty())
                        {
                            if (col == 3 || col == 6) kit_view_model = true;
                            else kit_world_model = true;
                        }
                        if (!model.empty() && requested_models.insert(model).second)
                        {
                            utils::hook::invoke<void>(0x75720_b, model.c_str());
                            lifecycle << "request kit model " << model << std::endl;
                        }
                    }
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
                        utils::hook::invoke<void>(0x75720_b, model_name.c_str());
                        lifecycle << "request model " << model_name << std::endl;
                    }
                };
                // Native scopes/reticles are separate view models in the weapon
                // definition. Keep their transient zones resident with the rifle.
                // Furniture supplies the effective native models. Some base
                // definitions retain obsolete model names (AK47's NPC model)
                // that have no transient zone and are never used by this kit.
                if (!kit_view_model && def->gunModel)
                    for (unsigned i = 0; i < 2; ++i) request_model(def->gunModel[i]);
                if (!kit_world_model && def->worldModel)
                    for (unsigned i = 0; i < 2; ++i) request_model(def->worldModel[i]);
                if (def->reticleViewModels)
                    for (unsigned i = 0; i < 64; ++i) request_model(def->reticleViewModels[i]);
                std::array<game::WeaponAttachment*, 4> attachments{};
                const auto count = utils::hook::invoke<int>(0x2E0C20_b, weapon, attachments.data());
                if (count < 0 || count > 4) throw std::runtime_error("Invalid native attachment count");
                for (int a = 0; a < count; ++a)
                {
                    const auto* part = attachments[a];
                    if (!part) throw std::runtime_error("Missing native weapon attachment");
                    lifecycle << "native attachment " << part->name << std::endl;
                    if (part->viewModels) for (unsigned i = 0; i < 2; ++i) request_model(part->viewModels[i]);
                    if (part->worldModels) for (unsigned i = 0; i < 2; ++i) request_model(part->worldModels[i]);
                    if (part->reticleViewModels) for (unsigned i = 0; i < 64; ++i) request_model(part->reticleViewModels[i]);
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
            alignas(16) std::array<std::uint8_t, 18576> ps{};
            const auto put = [&](std::size_t offset, const auto& value) { std::memcpy(ps.data() + offset, &value, sizeof(value)); };
            // H1's linked-view entity uses ENTITYNUM_NONE, not zero. Zero links
            // the view to entity 0 and applies the player's rotation a second time.
            put(0x2C, std::uint16_t{2047});
            put(0, static_cast<std::uint8_t>(world.client_num));
            put(0x4C, time); // commandTime, verified against the live PC netfield table
            put(0x5C, std::uint32_t{0x4000}); // recorded view: no local input prediction
            put(0x74, cod4::translate_bob_cycle(frame.bob_cycle));
            put(0x78, frame.origin);
            put(0x84, frame.velocity);
            put(0x12C, frame.angles);
            const auto& player = player_at(time);
            const auto shock = player.integer("shellshockIndex");
            cod4::write_view_motion(player, time, shock ? shock_profiles.at(shock).index : std::uint8_t{}, put);
            const auto next_body = std::upper_bound(track.frames.begin(), track.frames.end(), time,
                [](int t, const cod4::camera_frame& f) { return t < f.time; });
            const auto body_index = next_body == track.frames.begin() ? 0 : next_body - track.frames.begin() - 1;
            const auto& [legs, torso] = body_indices.at(static_cast<std::size_t>(body_index));
            const auto source_weapon = player.integer("weapon");
            put(0x22, cod4::translate_entity_number(player.integer("groundEntityNum")));
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
            cod4::h1_entity from{};
            utils::hook::invoke<void>(0x4EC490_b, from.data(), -1);
            const std::array<float, 3> zero{};
            utils::hook::invoke<void>(0x5B0CF0_b, from.data() + 96, zero.data());
            const auto write_entity = [&](cod4::h1_entity native)
            {
                // Every MWR position trajectory stores trBase with process-local
                // protection, including TR_STATIONARY. The native writer decodes
                // it before serialization; portable translations contain floats.
                std::array<float, 3> origin{};
                std::memcpy(origin.data(), native.data() + 108, sizeof(origin));
                utils::hook::invoke<void>(0x5B0CF0_b, native.data() + 96, origin.data());
                utils::hook::invoke<void>(0x4F6B30_b, context.data(), &packet.msg, time,
                    static_cast<int>(native[10]), from.data(), native.data(), true);
            };
            for (const auto& [number, entity] : recorded.entities)
            {
                if (entity.get(4) == 4)
                {
                    write_entity(cod4::translate_missile(entity, weapons.at(entity.get(196)), time,
                        alternate_weapons.contains(entity.get(196))));
                    continue;
                }
                if (entity.get(4) != 6 || !entity.get(136)) continue;
                const auto index = entity.get(136);
                if (!source_models.at(index)) continue; // Never serialize a default or invented replacement.
                const auto parts = source_parts.find(index);
                const auto native = cod4::translate_script_model(entity, source_models.at(index),
                    parts == source_parts.end() ? nullptr : &parts->second);
                write_entity(native);
            }
            for (const auto& [number, native] : map_models)
                write_entity(native);
            utils::hook::invoke<void>(0x4F6C10_b, &packet.msg, 2047, 11);
            packet.reset_index(); packet.byte(0);
            for (const auto& [number, client] : recorded.clients)
            {
                const auto& models = character_models.at(client_model_key(client));
                if (!models.body) continue; // Spectators have no recorded character model.
                const auto native = cod4::translate_client(client, models.body, models.attachments);
                utils::hook::invoke<void>(0x4F3910_b, context.data(), &packet.msg, time, nullptr, native.data(), true);
            }
            packet.bit0(); packet.reset_index(); packet.bit0();
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
            for (const auto& name : requested_models)
                utils::hook::invoke<void>(0x76850_b, name.c_str());
            requested_models.clear();
            model_bindings.clear();
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
            std::filesystem::create_directories(conversion_output.parent_path());
            auto temporary = conversion_output;
            temporary += ".tmp";
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.exceptions(std::ios::badbit | std::ios::failbit);
            std::string header;
            for (const auto& item : std::vector<std::string>{"", "H1 COD4 conversion v2", "v0.0.2",
                std::to_string(PROTOCOL), "", "COD4 conversion", "", track.map, "war"})
            { header += item; header.push_back('\0'); }
            for (const auto& model : requested_models) { header += model; header.push_back('\0'); }
            for (const auto& [index, model] : model_bindings)
            { header += std::format("@model:{}:{}", index, model); header.push_back('\0'); }
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
                auto decoded_world = cod4::decode_world(source_bytes);
                if (!game::SV_MapExists(decoded.map.c_str())) throw std::runtime_error("This map is not installed in H1: " + decoded.map);
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
                    world = std::move(decoded_world);
                    world_resolved = false;
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

    void clear_model_bindings() { model_bindings.clear(); }
    void set_model_bindings(const std::map<unsigned, std::string>& bindings)
    {
        model_bindings.clear();
        for (const auto& [index, name] : bindings)
        {
            if (index < 768 || index >= 1024 || !cod4::valid_model_name(name)
                || *get_configstring_hook.invoke<const char*>(1240 + index))
                throw std::runtime_error("Converted model slot is invalid or occupied");
        }
        model_bindings = bindings;
    }

    void bootstrap_demo(const std::string& map, const std::vector<std::uint8_t>& gamestate)
    {
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
            resolve_world_assets();
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
            for (const auto& name : requested_models)
                if (!utils::hook::invoke<bool>(0x75470_b, name.c_str())) return true;
            resolve_world_parts();
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
