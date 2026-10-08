#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "theater_models.hpp"
#include "theater_models_catalog.hpp"
#include "theater_models_policy.hpp"
#include "demo_game.hpp"
#include "demo_playback.hpp"
#include "cod4_camera.hpp"
#include "theater_camera.hpp"
#include "component/console.hpp"
#include "component/command.hpp"
#include "component/scheduler.hpp"
#include "utils/flags.hpp"
#include "utils/hook.hpp"
#include "utils/io.hpp"
#include <chrono>
#include <bit>

namespace theater_models
{
    namespace
    {
        struct choice { int id{}; std::string model, kind, label, icon, faction, role, collection; };
        struct actor
        {
            std::string key, name;
            int slot{}, team{};
            std::uint8_t* info{};
            composition original, applied;
            bool has_applied{};
            unsigned rebuilds{};
            int attachment_mask{};
            void* bgs{};
        };
        struct swap { std::string head, body; };
        struct load { std::string key, model, kind; std::chrono::steady_clock::time_point start; };
        std::vector<choice> catalog;
        std::map<int, actor> actors;
        std::map<std::string, swap> swaps;
        std::set<std::string> held;
        std::optional<load> pending;
        std::optional<std::chrono::steady_clock::time_point> restoring;
        std::map<std::string, swap> previous_swaps;
        std::string selected, message, path;
        std::mutex catalog_mutex;
        nlohmann::json catalog_json = nlohmann::json::array();
        unsigned session{};
        bool supported{};
        utils::hook::detour player_hook, agent_hook, player_dobj_hook, agent_dobj_hook;
        nlohmann::json snapshot = nlohmann::json::object();
        int catalog_revision{};
        bool active() { return supported && demo_playback::playing() && demo_playback::is_replay_armed(); }
        void** bgs_slot()
        {
            // Native BG builders resolve their callbacks through game TLS.
            // Capture the native client scope; never invent a bgs layout.
            auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
            const auto index = *reinterpret_cast<const unsigned*>(0x1126FCE0_b);
            if (!slots || !slots[index]) return nullptr;
            return reinterpret_cast<void**>(static_cast<std::uint8_t*>(slots[index]) + 16);
        }
        std::string bounded(const std::uint8_t* p, std::size_t n)
        { return {reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), n)}; }
        composition read(const std::uint8_t* ci)
        {
            composition c; c.body = bounded(ci + 36, 64);
            for (int i = 0; i < 8; ++i) { c.models[i] = bounded(ci + 100 + i * 64, 64); c.tags[i] = bounded(ci + 612 + i * 64, 64); }
            return c;
        }
        void write(std::uint8_t* ci, const composition& c, bool dirty = true)
        {
            auto copy = [&](int offset, const std::string& value) { std::memset(ci + offset, 0, 64); std::memcpy(ci + offset, value.data(), std::min(value.size(), std::size_t{63})); };
            copy(36, c.body);
            for (int i = 0; i < 8; ++i) { copy(100 + i * 64, c.models[i]); copy(612 + i * 64, c.tags[i]); }
            if (dirty) *reinterpret_cast<unsigned*>(ci + 1160) = 1;
        }
        void release()
        {
            // Retain acquired zones until the replay closes. Old DObjs and
            // native corpse copies can still refer to earlier selections.
            for (const auto& model : held) utils::hook::invoke<void>(0x76850_b, model.c_str());
            held.clear(); pending.reset(); restoring.reset(); previous_swaps.clear();
        }
        void sync_session()
        {
            const auto now = demo_playback::session_id();
            if (session == now) return;
            session = now;
            actors.clear(); selected.clear(); pending.reset();
            const auto current = demo_playback::current_path().string();
            if (current != path) { swaps.clear(); message.clear(); }
            path = current; // Keep edits through a fallback seek restart of this file.
        }
        bool available(const std::string& model)
        {
            if (model.empty() || model.size() >= 64 || !cod4::valid_model_name(model)) return false;
            std::array<std::uint16_t, 8> zones{};
            return (game::DB_XAssetExists(game::ASSET_TYPE_XMODEL, model.c_str()) &&
                !game::DB_IsXAssetDefault(game::ASSET_TYPE_XMODEL, model.c_str())) ||
                utils::hook::invoke<int>(0x75210_b, model.c_str(), 7, zones.data()) > 0;
        }
        bool ready(const std::string& model)
        {
            return utils::hook::invoke<bool>(0x75470_b, model.c_str()) &&
                game::DB_XAssetExists(game::ASSET_TYPE_XMODEL, model.c_str()) &&
                !game::DB_IsXAssetDefault(game::ASSET_TYPE_XMODEL, model.c_str());
        }
        std::string faction(std::string_view model)
        {
            if (model.find("sas_ct") != model.npos) return "SAS / Urban";
            if (model.find("sas_woodland") != model.npos) return "SAS / Woodland";
            if (model.find("usmc") != model.npos) return "USMC";
            if (model.find("arab") != model.npos) return "OpFor";
            if (model.find("spetsnaz_woodland") != model.npos) return "Spetsnaz / Woodland";
            if (model.find("spetsnaz") != model.npos) return "Spetsnaz / Urban";
            return "Other";
        }
        void discover()
        {
            if (!catalog.empty()) return;
            if (!game::DB_XAssetExists(game::ASSET_TYPE_STRINGTABLE, "mp/costumemodeltable.csv")) return;
            auto* table = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE, "mp/costumemodeltable.csv", false).stringTable;
            if (!table || table->rowCount < 1 || table->rowCount > 16384 || table->columnCount < 21) return;
            std::map<std::string, std::pair<std::string, std::string>> gear;
            for (const auto* file : {"mp/costumeidtable.csv", "mp/costumeidtable_cg.csv"})
            {
                if (!game::DB_XAssetExists(game::ASSET_TYPE_STRINGTABLE, file)) continue;
                auto* ids = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE, file, false).stringTable;
                if (!ids || ids->rowCount > 4096 || ids->columnCount < 7) continue;
                for (int i = 0; i < ids->rowCount; ++i)
                {
                    auto cell = [&](int col) { const auto* p = game::StringTable_GetColumnValueForRow(ids, i, col); return std::string(p ? p : ""); };
                    gear[cell(0) + ":" + cell(1)] = {cell(5), cell(6)};
                }
            }
            std::set<std::string> unique;
            for (int i = 1; i < table->rowCount; ++i)
            {
                auto cell = [&](int col) { const auto* p = game::StringTable_GetColumnValueForRow(table, i, col); return std::string(p ? p : ""); };
                const auto model = cell(0), kind = cell(1), index = cell(2);
                if ((kind != "head" && kind != "shirt") || !model.starts_with(kind == "head" ? "head_" : "body_") ||
                    !available(model) || !unique.insert(model).second) continue;
                const auto [label, icon] = gear[kind + ":" + index];
                const auto group = faction(model), role = cell(12);
                auto display = index == "1" || label.empty() ? group + " " + role : label;
                if (index == "1")
                {
                    if (model.find("female") != model.npos) display += " / Female";
                    for (const auto& [suffix, variant] : {std::pair{"_brn", "Brown"}, {"_tan", "Tan"}, {"_wht", "White"}, {"_blk", "Black"}})
                        if (model.ends_with(suffix)) display += std::string(" / ") + variant;
                }
                catalog.push_back({i, model, kind == "head" ? "head" : "body",
                    display,
                    index == "1" ? "" : icon, group, role, index == "1" ? "Default" : cell(20)});
            }
            {
                std::lock_guard lock(catalog_mutex);
                for (const auto& c : catalog) catalog_json.push_back({{"id", c.id}, {"model", c.model}, {"kind", c.kind}, {"label", c.label},
                    {"icon", c.icon}, {"faction", c.faction}, {"role", c.role}, {"collection", c.collection}});
            }
            ++catalog_revision;
            console::info("[model swapper] %zu native head/body variants available\n", catalog.size());
        }
        actor* selection()
        {
            for (auto& [slot, a] : actors) if (a.key == selected) return &a;
            return nullptr;
        }
        void observe(int slot, std::uint8_t* ci, std::string key, std::string name)
        {
            if (!active() || !ci || slot < 0 || slot >= 42 || key.empty()) return;
            sync_session();
            auto& a = actors[slot];
            if (a.key != key || a.info != ci) a = {};
            a.key = std::move(key); a.name = std::move(name); a.slot = slot; a.info = ci;
            a.team = *reinterpret_cast<int*>(ci + 12); a.original = read(ci);
            if (selected.empty()) selected = a.key;
        }
        void restore_before_snapshot(std::uint8_t* ci)
        {
            if (!active() || !ci) return;
            sync_session();
            for (auto& [slot, a] : actors) if (a.info == ci && a.has_applied)
            {
                // Present the recorded composition to the native snapshot
                // comparison without invalidating an already matching DObj.
                if (read(ci) == a.applied) write(ci, a.original, false);
            }
        }
        void update_player(int client, int index, void* player_state, const std::uint8_t* state,
            std::uint8_t* ci, unsigned* changed, int flags)
        {
            restore_before_snapshot(ci);
            player_hook.invoke<void>(client, index, player_state, state, ci, changed, flags);
            if (client == 0 && state && ci)
            {
                const auto name = bounded(state + 42, 32);
                observe(*reinterpret_cast<int*>(ci), ci, "player:" + std::to_string(index) + ":" + name, name);
            }
        }
        void update_agent(int client, int index, const std::uint8_t* state, std::uint8_t* ci, unsigned* changed, int flags)
        {
            restore_before_snapshot(ci);
            agent_hook.invoke<void>(client, index, state, ci, changed, flags);
            if (client == 0 && state && ci)
            {
                const auto token = *reinterpret_cast<const unsigned*>(state + 124);
                const auto name = cod4_camera::recorded_name(token);
                const auto slot = *reinterpret_cast<int*>(ci);
                observe(slot, ci, token && name ? "actor:" + std::to_string(token) : "agent:" + std::to_string(slot),
                    name.value_or("Actor " + std::to_string(slot)));
            }
        }
        void apply_character(int client, const std::uint8_t* entity, std::uint8_t* ci)
        {
            if (active() && client == 0 && ci && entity && (entity[10] == 1 || entity[10] == 18))
            {
                sync_session();
                const auto slot = *reinterpret_cast<int*>(ci);
                const auto found = actors.find(slot);
                if (found != actors.end() && found->second.info == ci)
                {
                    auto& a = found->second;
                    const auto current = read(ci);
                    if (!a.has_applied || current != a.applied) a.original = current;
                    const auto edit = swaps.find(a.key);
                    const bool loaded = edit != swaps.end() && (edit->second.head.empty() || ready(edit->second.head)) &&
                        (edit->second.body.empty() || ready(edit->second.body));
                    const auto replacement = !loaded ? std::optional(a.original) : replace(a.original, edit->second.head, edit->second.body);
                    if (replacement)
                    {
                        if (current != *replacement) write(ci, *replacement, !a.has_applied || a.applied != *replacement);
                        a.applied = *replacement; a.has_applied = true;
                    }
                }
            }
        }
        int rebuild_player(int client, void* obj, const std::uint8_t* entity, std::uint8_t* ci)
        { if (active() && ci && actors.contains(*reinterpret_cast<int*>(ci)))
              if (const auto context = bgs_slot(); context && *context) actors.at(*reinterpret_cast<int*>(ci)).bgs = *context;
          apply_character(client, entity, ci); const auto result = player_dobj_hook.invoke<int>(client, obj, entity, ci);
          if (result && active() && ci && actors.contains(*reinterpret_cast<int*>(ci))) ++actors.at(*reinterpret_cast<int*>(ci)).rebuilds;
          return result; }
        int rebuild_agent(int client, void* obj, const std::uint8_t* entity, std::uint8_t* ci, int attachment_mask)
        { if (active() && ci && actors.contains(*reinterpret_cast<int*>(ci))) actors.at(*reinterpret_cast<int*>(ci)).attachment_mask = attachment_mask;
          if (active() && ci && actors.contains(*reinterpret_cast<int*>(ci)))
              if (const auto context = bgs_slot(); context && *context) actors.at(*reinterpret_cast<int*>(ci)).bgs = *context;
          apply_character(client, entity, ci); const auto result = agent_dobj_hook.invoke<int>(client, obj, entity, ci, attachment_mask);
          if (result && active() && ci && actors.contains(*reinterpret_cast<int*>(ci))) ++actors.at(*reinterpret_cast<int*>(ci)).rebuilds;
          return result; }
        void acquire(const std::string& model)
        {
            if (held.contains(model)) return;
            auto budget = held; budget.insert(model);
            for (const auto& [slot, a] : actors)
            {
                if (!a.original.body.empty()) budget.insert(a.original.body);
                for (const auto& part : a.original.models) if (!part.empty()) budget.insert(part);
            }
            cod4_camera::validate_extra_model_budget(budget);
            utils::hook::invoke<void>(0x75720_b, model.c_str()); held.insert(model);
        }
        std::filesystem::path save_path()
        { return std::filesystem::path(demo_playback::current_path().string() + ".models.json"); }
        void focus(actor& a)
        {
            auto* entity = utils::hook::invoke<std::uint8_t*>(0x6BFF0_b, 0, static_cast<short>(a.slot));
            if (!entity || !(*reinterpret_cast<unsigned*>(entity + 0x23C) & 1)) throw std::runtime_error("Player is not visible at this playhead");
            auto* obj = utils::hook::invoke<void*>(0x4E98E0_b, *reinterpret_cast<std::uint16_t*>(entity + 0x13C), 0);
            std::array<float, 9> axis{}; std::array<float, 3> origin{};
            if (!obj || !utils::hook::invoke<bool>(0xF3D20_b, entity, obj, game::SL_FindString("j_head"), axis.data(), origin.data()))
                throw std::runtime_error("Player head pose is not ready");
            auto pose = theater_camera::get_freecam_pose();
            float dx = pose.pos[0] - origin[0], dy = pose.pos[1] - origin[1];
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length < 1) { dx = 1; dy = 0; } else { dx /= length; dy /= length; }
            pose.pos[0] = origin[0] + dx * 100; pose.pos[1] = origin[1] + dy * 100; pose.pos[2] = origin[2] - 12;
            pose.angles[0] = -6.84f; pose.angles[1] = std::atan2(-dy, -dx) / .01745329252f; pose.angles[2] = 0; pose.fov = 55;
            theater_camera::stop_cinematic(); theater_camera::set_mode(theater_camera::THEATER_CAMERA_FREECAM);
            theater_camera::set_freecam_pose(pose);
        }
    }
    void reset() { pending.reset(); restoring.reset(); previous_swaps.clear(); swaps.clear(); message = "All player models restored"; }
    bool action(const std::string& name, const std::string& argument)
    {
        if (!name.starts_with("model_")) return false;
        if (!active()) throw std::runtime_error("Model swapping is available during demo playback only");
        sync_session(); discover();
        if (restoring && name != "model_select" && name != "model_focus" && name != "model_reset_all")
            throw std::runtime_error("Wait for the saved character fastfiles to finish loading");
        if (pending && (name == "model_save" || name == "model_load"))
            throw std::runtime_error("Wait for the character appearance to finish loading before saving or loading");
        if (name == "model_select")
        {
            bool found{}; for (const auto& [slot, a] : actors) if (a.key == argument) found = true;
            if (!found) throw std::runtime_error("Player is no longer in this snapshot");
            selected = argument; message.clear(); return true;
        }
        if (name == "model_reset_all") { reset(); return true; }
        if (name == "model_save")
        {
            auto entries = nlohmann::json::array();
            for (const auto& [key, s] : swaps) entries.push_back({{"player", key}, {"head", s.head}, {"body", s.body}});
            if (!utils::io::write_file(save_path().string(), nlohmann::json{{"version", 1}, {"demo", demo_playback::current_path().filename().string()}, {"swaps", entries}}.dump(2)))
                throw std::runtime_error("Cannot save demo model selections");
            message = "Saved this demo's model selections"; return true;
        }
        if (name == "model_load")
        {
            const auto file = save_path();
            if (!std::filesystem::exists(file) || std::filesystem::file_size(file) > 65536) throw std::runtime_error("Demo model selections missing or too large");
            const auto data = nlohmann::json::parse(utils::io::read_file(file.string()));
            if (data.at("version") != 1 || data.at("demo") != demo_playback::current_path().filename().string() ||
                !data.at("swaps").is_array() || data.at("swaps").size() > 64) throw std::runtime_error("Invalid demo model selections");
            std::map<std::string, swap> replacement;
            for (const auto& item : data.at("swaps"))
            {
                const auto key = item.at("player").get<std::string>();
                if (key.empty() || key.size() > 100) throw std::runtime_error("Invalid saved player identity");
                swap s{item.at("head").get<std::string>(), item.at("body").get<std::string>()};
                for (const auto& [kind, model] : {std::pair{"head", s.head}, std::pair{"body", s.body}})
                    if (!model.empty() && std::none_of(catalog.begin(), catalog.end(), [&](const auto& c) { return c.kind == kind && c.model == model; }))
                        throw std::runtime_error("Saved character model is unavailable in this installation");
                if (!replacement.emplace(key, s).second) throw std::runtime_error("Duplicate saved player identity");
            }
            auto budget = held;
            for (const auto& [key, s] : replacement) { if (!s.head.empty()) budget.insert(s.head); if (!s.body.empty()) budget.insert(s.body); }
            cod4_camera::validate_extra_model_budget(budget);
            pending.reset(); previous_swaps = swaps; swaps = std::move(replacement);
            restoring = std::chrono::steady_clock::now(); message = "Loading saved demo model selections..."; return true;
        }
        auto* a = selection(); if (!a) throw std::runtime_error("Select an active player first");
        if (name == "model_focus") { focus(*a); message = "Previewing " + a->name + " in free camera"; return true; }
        if (name == "model_restore") { swaps.erase(a->key); if (pending && pending->key == a->key) pending.reset(); message = "Original head and body restored"; return true; }
        if (name != "model_head" && name != "model_body") return false;
        const auto kind = name == "model_head" ? "head" : "body";
        const auto found = std::find_if(catalog.begin(), catalog.end(), [&](const auto& c) { return std::to_string(c.id) == argument && c.kind == kind; });
        if (found == catalog.end()) throw std::runtime_error("Choose a model from the native character catalog");
        if (!replace(a->original, kind == std::string_view("head") ? found->model : "", kind == std::string_view("body") ? found->model : ""))
            throw std::runtime_error("Player has no free head attachment slot");
        if (pending) throw std::runtime_error("Wait for the current character fastfile to finish loading");
        acquire(found->model); pending = load{a->key, found->model, kind, std::chrono::steady_clock::now()};
        message = "Loading character fastfile..."; return true;
    }
    void update()
    {
        if (!supported) { snapshot = {{"modelMessage", "This executable does not support model swapping"}}; return; }
        sync_session();
        if (!active()) { actors.clear(); selected.clear(); if (!held.empty() || pending || restoring) release(); snapshot = nlohmann::json::object(); return; }
        discover();
        bool failed{};
        for (const auto& [key, s] : swaps)
            for (const auto& model : {s.head, s.body}) if (!model.empty() && !held.contains(model))
            {
                try { acquire(model); }
                catch (const std::exception& error) { message = error.what(); failed = true; }
            }
        if (!held.empty()) utils::hook::invoke<void>(0x757A0_b);
        if (restoring)
        {
            const bool loaded = std::all_of(swaps.begin(), swaps.end(), [](const auto& entry)
            { const auto& s = entry.second; return (s.head.empty() || ready(s.head)) && (s.body.empty() || ready(s.body)); });
            if (failed || (!loaded && std::chrono::steady_clock::now() - *restoring > std::chrono::seconds(30)))
            {
                swaps = std::move(previous_swaps); restoring.reset();
                message = "Saved character models could not load; previous selections restored";
            }
            else if (loaded) { previous_swaps.clear(); restoring.reset(); message = "Loaded this demo's model selections"; }
        }
        if (pending)
        {
            if (ready(pending->model))
            {
                auto& s = swaps[pending->key];
                (pending->kind == "head" ? s.head : s.body) = pending->model;
                message = "Applied " + pending->kind + ". Preview on the selected player."; pending.reset();
            }
            else if (std::chrono::steady_clock::now() - pending->start > std::chrono::seconds(30))
            { message = "Character fastfile did not become ready; previous selection retained"; pending.reset(); }
        }
        // Paused playback does not run the next snapshot's character update.
        // Refresh only changed, existing DObjs through the native builder,
        // preserving their animation tree and the last native attachment mask.
        if (demo_playback::paused() && !demo_playback::seeking()) for (auto& [slot, a] : actors)
        {
            const auto edit = swaps.find(a.key);
            const bool loaded = edit != swaps.end() && (edit->second.head.empty() || ready(edit->second.head)) &&
                (edit->second.body.empty() || ready(edit->second.body));
            const auto desired = !loaded ? std::optional(a.original) : replace(a.original, edit->second.head, edit->second.body);
            if (!desired || !a.has_applied || a.applied == *desired || !a.bgs) continue;
            auto* entity = utils::hook::invoke<std::uint8_t*>(0x6BFF0_b, 0, static_cast<short>(slot));
            auto* obj = utils::hook::invoke<void*>(0x4E98E0_b, slot, 0);
            if (!entity || !obj || !(*reinterpret_cast<unsigned*>(entity + 0x23C) & 1)) continue;
            auto** context = bgs_slot(); if (!context) continue;
            const auto saved = *context;
            *context = a.bgs;
            const auto restore_context = gsl::finally([&] { *context = saved; });
            if (entity[0x146] == 1) rebuild_player(0, obj, entity + 0x13C, a.info);
            else if (entity[0x146] == 18) rebuild_agent(0, obj, entity + 0x13C, a.info, a.attachment_mask);
        }
        auto roster = nlohmann::json::array();
        for (const auto& [slot, a] : actors)
        {
            auto* entity = utils::hook::invoke<std::uint8_t*>(0x6BFF0_b, 0, static_cast<short>(slot));
            const bool visible = entity && (*reinterpret_cast<unsigned*>(entity + 0x23C) & 1) && (entity[0x146] == 1 || entity[0x146] == 18);
            roster.push_back({{"key", a.key}, {"name", a.name}, {"slot", slot}, {"team", a.team}, {"visible", visible}, {"body", a.original.body}});
        }
        auto detail = nlohmann::json::object();
        if (const auto* a = selection())
        {
            const auto current = a->has_applied ? a->applied : a->original;
            std::string original_head, current_head;
            for (const auto& head : a->original.models) if (head.starts_with("head_")) original_head = head;
            for (const auto& head : current.models) if (head.starts_with("head_")) current_head = head;
            detail = {{"name", a->name}, {"originalHead", original_head}, {"originalBody", a->original.body}, {"head", current_head}, {"body", current.body}};
        }
        snapshot = {{"modelPlayers", roster}, {"modelCatalogRevision", catalog_revision}, {"modelSelected", selected},
            {"modelDetail", detail}, {"modelLoading", pending.has_value() || restoring.has_value()}, {"modelMessage", message}};
    }
    nlohmann::json state() { return snapshot; } // Copied only by the main-thread editor publisher.
    nlohmann::json choices() { std::lock_guard lock(catalog_mutex); return catalog_json; }
    nlohmann::json choices_page(const std::string& kind, const std::string& faction, const std::string& role,
        const std::string& collection, int page, const std::string& head, const std::string& body)
    {
        std::lock_guard lock(catalog_mutex);
        return catalog_page(catalog_json, kind, faction, role, collection, page, head, body);
    }
    void dump_state(const std::string& label)
    {
        update(); auto data = state(); data["modelCatalog"] = choices();
        data["nativeDObjs"] = nlohmann::json::array();
        if (active()) for (const auto& [slot, a] : actors)
        {
            auto models = nlohmann::json::array();
            auto* obj = utils::hook::invoke<void*>(0x4E98E0_b, slot, 0);
            if (obj)
            {
                const auto count = utils::hook::invoke<unsigned>(0x5C7780_b, obj);
                for (unsigned i = 0; i < count && i < 32; ++i)
                    if (const auto* model = utils::hook::invoke<game::XModel*>(0x5C7730_b, obj, i)) models.push_back(model->name);
            }
            data["nativeDObjs"].push_back({{"slot", slot}, {"key", a.key}, {"models", models}, {"rebuilds", a.rebuilds}, {"tags", read(a.info).tags}});
        }
        data["supported"] = supported; data["demo"] = path;
        // Read-only diagnostic of native MT's 16-byte block bitmap. Ordinary
        // playback never scans this pool or changes its allocation limits.
        if (supported)
        {
            const auto* bitmap = reinterpret_cast<const unsigned*>(0xB087D80_b);
            std::size_t allocated{};
            for (std::size_t i = 0; i < 8192; ++i) allocated += std::popcount(bitmap[i]);
            data["scriptStringAllocatedBytes"] = allocated * 16;
        }
        if (label.size() > 32 || label.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") != std::string::npos)
            throw std::runtime_error("Diagnostic label must contain lowercase letters, numbers, hyphens or underscores");
        const auto output = "demos/theater/model-swapper-status" + (label.empty() ? "" : "-" + label) + ".json";
        if (!utils::io::write_file(output, data.dump(2))) throw std::runtime_error("Cannot write model swapper diagnostics");
        console::info("[model swapper] Status exported to %s\n", output.c_str());
    }
    class component final : public component_interface
    {
        void post_unpack() override
        {
            if (!game::environment::is_mp()) return;
            const std::uint8_t player[] = {0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57};
            const std::uint8_t agent[] = {0x40,0x53,0x55,0x56,0x57,0x41,0x54};
            const std::uint8_t dobj[] = {0x48,0x83,0xEC,0x58,0x45,0x8B,0x90,0xC8,0,0,0};
            const std::uint8_t agent_dobj[] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18};
            supported = !std::memcmp(reinterpret_cast<void*>(0x32F1B0_b), player, sizeof(player)) &&
                !std::memcmp(reinterpret_cast<void*>(0x32EE10_b), agent, sizeof(agent)) &&
                !std::memcmp(reinterpret_cast<void*>(0x2BB440_b), dobj, sizeof(dobj)) &&
                !std::memcmp(reinterpret_cast<void*>(0x2BC700_b), agent_dobj, sizeof(agent_dobj));
            if (utils::flags::has_flag("modelcheck"))
            {
                nlohmann::json audit = {{"supported", supported}, {"phase", "post_unpack"}, {"pid", GetCurrentProcessId()}};
                for (const auto& address : {0x32F1B0_b, 0x32EE10_b, 0x2BB440_b, 0x2BC700_b})
                {
                    const auto* bytes = reinterpret_cast<const std::uint8_t*>(address);
                    audit["prologues"].push_back(std::vector<std::uint8_t>(bytes, bytes + 11));
                }
                utils::io::write_file("demos/theater/model-swapper-check-startup.json", audit.dump(2));
            }
            if (!supported) { console::warn("[model swapper] Native character hook signature mismatch\n"); return; }
            player_hook.create(0x32F1B0_b, update_player); agent_hook.create(0x32EE10_b, update_agent);
            player_dobj_hook.create(0x2BB440_b, rebuild_player); agent_dobj_hook.create(0x2BC700_b, rebuild_agent);
            // Console parity for diagnostics and keyboard-only editing. All
            // mutations have the same replay guard and catalog allowlist.
            command::add("demo_model", [](const command::params& args)
            {
                try
                {
                    if (args.size() < 2) { console::info("demo_model slot <number> | head/body <catalog id> | focus | restore | reset_all | save | load | dump\n"); return; }
                    const std::string operation = args[1];
                    if (operation == "dump") { dump_state(args.size() > 2 ? args[2] : ""); return; }
                    if (operation == "slot")
                    {
                        sync_session();
                        if (args.size() < 3) return;
                        for (const auto& [slot, a] : actors) if (std::to_string(slot) == args[2]) { action("model_select", a.key); return; }
                        throw std::runtime_error("Player slot is not in this snapshot");
                    }
                    if (!action("model_" + operation, args.size() > 2 ? args[2] : "")) throw std::runtime_error("Unknown model operation");
                }
                catch (const std::exception& error) { message = error.what(); console::warn("[model swapper] %s\n", error.what()); }
            });
            // Explicit developer check, never enabled by ordinary playback.
            // Wait for actual character readiness rather than fixed startup
            // CFG delays; exercise the same actions as the native browser.
            if (utils::flags::has_flag("modelcheck")) scheduler::schedule([]
            {
                static int stage{};
                static auto due = std::chrono::steady_clock::now();
                static std::string target;
                static int origin{};
                static auto last_status = std::chrono::steady_clock::time_point{};
                if (std::chrono::steady_clock::now() - last_status > std::chrono::seconds(2))
                {
                    last_status = std::chrono::steady_clock::now();
                    utils::io::write_file("demos/theater/model-swapper-check-progress.json", nlohmann::json{
                        {"stage", stage}, {"playing", demo_playback::playing()}, {"armed", demo_playback::is_replay_armed()},
                        {"actors", actors.size()}, {"seeking", demo_playback::seeking()}}.dump(2));
                }
                if (!active() || actors.empty() || demo_playback::seeking() || std::chrono::steady_clock::now() < due)
                    return scheduler::cond_continue;
                try
                {
                    update();
                    const auto delay = [&](int seconds) { due = std::chrono::steady_clock::now() + std::chrono::seconds(seconds); ++stage; };
                    switch (stage)
                    {
                    case 0:
                        if (!demo_playback::paused()) demo_playback::toggle_pause();
                        target = actors.begin()->second.key; origin = demo_playback::current_time().value_or(0);
                        action("model_select", target); dump_state("before"); action("model_head", "143"); delay(5); break;
                    case 1: dump_state("head"); action("model_body", "43"); delay(5); break;
                    case 2: dump_state("both"); action("model_focus", "");
                        if (std::filesystem::exists(save_path())) throw std::runtime_error("Model check requires a test recording without existing saved swaps");
                        action("model_save", ""); delay(20); break;
                    case 3: dump_state("static"); (void)demo_playback::seek_to(origin + 5000); delay(3); break;
                    case 4: dump_state("forward"); (void)demo_playback::seek_to(origin); delay(3); break;
                    case 5: dump_state("backward"); action("model_restore", ""); delay(3); break;
                    case 6: dump_state("restored"); action("model_load", ""); delay(5); break;
                    case 7: dump_state("loaded"); if (demo_playback::paused()) demo_playback::toggle_pause(); delay(15); break;
                    case 8: dump_state("playing"); action("model_select", target); action("model_head", "143"); delay(20); break;
                    case 9: dump_state("playing-swapped"); if (!demo_playback::paused()) demo_playback::toggle_pause();
                        action("model_head", "154"); delay(5); break;
                    case 10: dump_state("alternate"); action("model_restore", ""); delay(5); break;
                    case 11: dump_state("alternate-restored"); if (demo_playback::paused()) demo_playback::toggle_pause(); delay(90); break;
                    default: dump_state("end"); command::execute("quit", false); return scheduler::cond_end;
                    }
                }
                catch (const std::exception& error) { message = error.what(); dump_state("failed"); command::execute("quit", false); return scheduler::cond_end; }
                return scheduler::cond_continue;
            }, scheduler::main, 100ms);
        }
    };
}
REGISTER_COMPONENT(theater_models::component)
