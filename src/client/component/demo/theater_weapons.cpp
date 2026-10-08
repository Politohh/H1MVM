#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "theater_weapons.hpp"
#include "theater_weapons_policy.hpp"
#include "theater_models.hpp"
#include "demo_playback.hpp"
#include "demo_game.hpp"
#include "cod4_camera.hpp"
#include "cod4_weapon_models.hpp"
#include "component/console.hpp"
#include "component/command.hpp"
#include "component/scheduler.hpp"
#include "utils/flags.hpp"
#include "utils/hook.hpp"
#include "utils/io.hpp"
#include <chrono>

namespace theater_weapons
{
    namespace
    {
        struct choice { std::string id, weapon, kit, label, kit_label, icon; int category{}; };
        struct render_override { unsigned original{}, target{}; std::vector<game::XModel*> models; };
        struct load { std::string source, id; unsigned original{}, target{}; std::set<std::string> models, gun_models;
            std::chrono::steady_clock::time_point start; };
        std::vector<choice> catalog;
        std::map<std::string, std::string> swaps;
        std::map<std::string, load> loaded;
        std::set<std::string> held;
        std::optional<load> pending;
        std::atomic<std::shared_ptr<const render_override>> rendered;
        utils::hook::detour view_weapon_hook;
        std::string path, source_name, source_label, message;
        unsigned session{}, source_weapon{};
        int source_category{-1};
        bool supported{};
        std::mutex catalog_mutex;
        nlohmann::json snapshot = nlohmann::json::object(), catalog_snapshot = nlohmann::json::array();
        bool active() { return supported && demo_playback::playing() && demo_playback::is_replay_armed()
            && game::CL_IsCgameInitialized() && !game::VirtualLobby_Loaded(); }
        template<class T> T field(const void* p, std::size_t offset)
        { T v; std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, sizeof(v)); return v; }
        unsigned view_weapon(const void* ps)
        {
            const auto original = view_weapon_hook.invoke<unsigned>(ps);
            const auto edit = rendered.load();
            // BG_GetViewmodelWeaponIndex, verified at native CG_UpdateWeaponViewmodels
            // 0x1285AB. Only substitute the local render playerstate. Snapshots,
            // inventory, world weapons, events and ammo retain their recorded values.
            if (edit && active() && ps == game::CG_GetLocalClientGlobals() && original == edit->original)
                return edit->target;
            return original;
        }
        game::StringTable* table(const std::string& name)
        {
            if (!game::DB_XAssetExists(game::ASSET_TYPE_STRINGTABLE, name.c_str()) ||
                game::DB_IsXAssetDefault(game::ASSET_TYPE_STRINGTABLE, name.c_str())) return nullptr;
            auto* t = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE, name.c_str(), false).stringTable;
            return t && t->rowCount > 0 && t->rowCount < 16384 && t->columnCount < 160 ? t : nullptr;
        }
        std::string cell(game::StringTable* t, int row, int col)
        {
            if (!t || col >= t->columnCount) return {};
            const auto* p = game::StringTable_GetColumnValueForRow(t, row, col); return p ? p : "";
        }
        game::WeaponDef* definition(const std::string& name)
        {
            if (!identifier(name) || !game::DB_XAssetExists(game::ASSET_TYPE_WEAPON, name.c_str()) ||
                game::DB_IsXAssetDefault(game::ASSET_TYPE_WEAPON, name.c_str())) return nullptr;
            return game::DB_FindXAssetHeader(game::ASSET_TYPE_WEAPON, name.c_str(), false).weapon;
        }
        bool available(const std::string& name)
        {
            if (!identifier(name)) return false;
            std::array<std::uint16_t, 8> zones{};
            return (game::DB_XAssetExists(game::ASSET_TYPE_XMODEL, name.c_str()) &&
                !game::DB_IsXAssetDefault(game::ASSET_TYPE_XMODEL, name.c_str())) ||
                utils::hook::invoke<int>(0x75210_b, name.c_str(), 7, zones.data()) > 0;
        }
        bool ready(const std::string& name)
        { return utils::hook::invoke<bool>(0x75470_b, name.c_str()) &&
            game::DB_XAssetExists(game::ASSET_TYPE_XMODEL, name.c_str()) &&
            !game::DB_IsXAssetDefault(game::ASSET_TYPE_XMODEL, name.c_str()); }
        void discover()
        {
            if (!catalog.empty()) return;
            auto* kits = table("mp/furniturekits.csv");
            if (!kits || kits->columnCount < 7) return;
            std::set<std::string> unique;
            for (int k = 1; k < kits->rowCount; ++k)
            {
                const auto kit = cell(kits, k, 1);
                if (!identifier(kit) || kit == "none") continue;
                auto* models = table("mp/furniturekits/" + kit + ".csv");
                if (!models || models->columnCount < 8) continue;
                for (int r = 0; r < models->rowCount; ++r)
                {
                    const auto weapon = cell(models, r, 0), id = weapon + "|" + kit;
                    if (cell(models, r, 2) != "1" || !available(cell(models, r, 3)) || unique.contains(id)) continue;
                    auto* def = definition(weapon);
                    if (!def || !firearm(def->weapClass)) continue;
                    unique.insert(id);
                    catalog.push_back({id, weapon, kit, def->szDisplayName ? def->szDisplayName : weapon,
                        kit == "base" ? "No weapon kit" : cell(kits, k, 3), kit == "base" ? "" : cell(kits, k, 5), def->weapClass});
                }
            }
            std::sort(catalog.begin(), catalog.end(), [](const auto& a, const auto& b) {
                return std::tuple(a.weapon, a.kit != "base", a.kit) < std::tuple(b.weapon, b.kit != "base", b.kit); });
            std::lock_guard lock(catalog_mutex);
            catalog_snapshot = nlohmann::json::array();
            for (const auto& c : catalog) catalog_snapshot.push_back({{"id",c.id},{"weapon",c.weapon},{"kit",c.kit},
                {"label",c.label},{"kitLabel",c.kit_label},{"icon",c.icon},{"category",c.category}});
            console::info("[weapon swapper] %zu native weapon/kit choices\n", catalog.size());
        }
        const choice& find_choice(const std::string& id)
        {
            const auto c = std::find_if(catalog.begin(), catalog.end(), [&](const auto& item) { return item.id == id; });
            if (c == catalog.end()) throw std::runtime_error("Weapon kit is unavailable in this installation");
            return *c;
        }
        load prepare(const std::string& source, unsigned original, const choice& c)
        {
            const auto identity = split_name(source);
            auto* from = definition(identity.weapon); auto* def = definition(c.weapon);
            if (!from || !def || !compatible(from->weapClass, def->weapClass))
                throw std::runtime_error("Choose a replacement in the recorded weapon's category");
            // Kits preserve the recorded attachment; another gun uses its standard setup.
            const auto attachment = identity.weapon == c.weapon ? identity.attachment : "none";
            if (!identifier(attachment)) throw std::runtime_error("Unsupported recorded attachment kit");
            auto* furniture = table("mp/furniturekits/" + c.kit + ".csv");
            const auto models = cod4::collect_weapon_kit_models(c.weapon, attachment, true,
                furniture ? furniture->rowCount : 0, [&](int row, int col) { return cell(furniture, row, col); });
            if (!models.base_view) throw std::runtime_error("This kit has no native viewmodel for the selected gun");
            load result{source,c.id,original,0,models.names,{},std::chrono::steady_clock::now()};
            for (int r = 0; furniture && r < furniture->rowCount; ++r)
                if (cell(furniture,r,0) == c.weapon && cell(furniture,r,2) == "1")
                    for (int col : {3,6}) if (auto n = cell(furniture,r,col); !n.empty()) result.gun_models.insert(n);
            auto add = [&](game::XModel* model) { if (model && model->name) result.models.insert(model->name); };
            if (def->reticleViewModels) for (unsigned i=0;i<64;++i) add(def->reticleViewModels[i]);
            const auto base = utils::hook::invoke<unsigned>(0x2E2290_b, c.weapon.c_str(), nullptr);
            char name[1024]{}; game::BG_GetWeaponNameComplete(base, false, name, sizeof(name));
            if (!(base & 255) || c.weapon != name) throw std::runtime_error("Native weapon could not be registered");
            if (utils::hook::invoke<int>(0x654E0_b, attachment.c_str()) < 0 ||
                utils::hook::invoke<int>(0x658E0_b, c.kit.c_str()) < 0) throw std::runtime_error("Native weapon kit ID is unavailable");
            const auto variant = utils::hook::invoke<unsigned>(0x2E2D50_b, base, attachment.c_str(), c.kit.c_str(), 0, 0, nullptr);
            if (!variant || variant >= 1024) throw std::runtime_error("Native weapon kit registry is full");
            result.target = base | (variant << 17);
            std::array<game::WeaponAttachment*,4> parts{};
            const auto count = utils::hook::invoke<int>(0x2E0C20_b, result.target, parts.data());
            if (count < 0 || count > 4) throw std::runtime_error("Invalid native weapon attachments");
            for (int i=0;i<count;++i) if (auto* part=parts[i]) {
                if (!models.attachment_view && part->viewModels) for (int j=0;j<2;++j) add(part->viewModels[j]);
                if (part->reticleViewModels) for (int j=0;j<64;++j) add(part->reticleViewModels[j]);
            }
            auto budget = held; budget.insert(result.models.begin(),result.models.end());
            const auto characters = theater_models::held_models(); budget.insert(characters.begin(),characters.end());
            cod4_camera::validate_extra_model_budget(budget);
            for (const auto& model : result.models) if (!available(model)) throw std::runtime_error("Weapon model is not installed: " + model);
            for (const auto& model : result.models) if (!held.contains(model)) {
                utils::hook::invoke<void>(0x75720_b, model.c_str()); held.insert(model);
            }
            return result;
        }
        void publish(const load& edit)
        {
            auto result = std::make_shared<render_override>(); result->original = edit.original; result->target = edit.target;
            for (const auto& model : edit.gun_models)
                result->models.push_back(game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,model.c_str(),false).model);
            rendered.store(std::move(result));
        }
        std::filesystem::path save_path() { return std::filesystem::path(path + ".weapons.json"); }
        void dump(const std::string& label)
        {
            auto data=snapshot; data["supported"]=supported; data["heldModels"]=held;
            if (const auto edit=rendered.load()) { data["original"]=edit->original; data["target"]=edit->target; }
            data["viewModels"]=nlohmann::json::array();
            if (active()) {
                auto* object=*reinterpret_cast<void**>(0x390B130_b);
                if (object) for (unsigned i=0,count=std::min(32u,utils::hook::invoke<unsigned>(0x5C7780_b,object));i<count;++i)
                    if (const auto* model=utils::hook::invoke<game::XModel*>(0x5C7730_b,object,i)) data["viewModels"].push_back(model->name);
                data["snapshotWeapon"]=field<unsigned>(&game::mp::cl->snap.ps,916);
                data["renderPlayerWeapon"]=field<unsigned>(game::CG_GetLocalClientGlobals(),916);
            }
            utils::io::write_file("demos/theater/weapon-swapper-"+label+".json",data.dump(2));
        }
    }
    std::set<std::string> held_models() { return held; }
    bool different_weapon()
    { const auto e=rendered.load(); return e && active() && (e->original & 255) != (e->target & 255); }
    std::vector<game::XModel*> view_models(unsigned original)
    { const auto e=rendered.load(); return e && e->original == original ? e->models : std::vector<game::XModel*>{}; }
    void reset() { swaps.clear(); loaded.clear(); pending.reset(); rendered.store(nullptr); message="Recorded weapons restored"; }
    bool action(const std::string& name, const std::string& argument)
    {
        if (!name.starts_with("weapon_swap_")) return false;
        if (!active()) throw std::runtime_error("Weapon Swapper is available during demo playback only");
        discover();
        if (name=="weapon_swap_reset") { reset(); return true; }
        if (name=="weapon_swap_restore") {
            swaps.erase(source_name); loaded.erase(source_name); pending.reset(); rendered.store(nullptr);
            message="Recorded weapon restored"; return true;
        }
        if (pending) throw std::runtime_error("Wait for the current weapon models to finish loading");
        if (name=="weapon_swap_apply") {
            const auto separator=argument.find('|');
            if (separator==argument.npos || argument.substr(0,separator)!=source_name || !firearm(source_category))
                throw std::runtime_error("The recorded weapon changed; select the kit again");
            pending=prepare(source_name,source_weapon,find_choice(argument.substr(separator+1)));
            message="Loading weapon models..."; return true;
        }
        if (name=="weapon_swap_save") {
            if (!utils::io::write_file(save_path().string(),nlohmann::json{{"version",1},
                {"demo",demo_playback::current_path().filename().string()},{"swaps",swaps}}.dump(2)))
                throw std::runtime_error("Cannot save weapon swaps");
            message="Saved this demo's weapon swaps"; return true;
        }
        if (name=="weapon_swap_load") {
            const auto file=save_path();
            if (!std::filesystem::exists(file) || std::filesystem::file_size(file)>65536) throw std::runtime_error("Saved weapon swaps missing or too large");
            const auto data=nlohmann::json::parse(utils::io::read_file(file.string()));
            if (data.at("version")!=1 || data.at("demo")!=demo_playback::current_path().filename().string() ||
                !data.at("swaps").is_object() || data.at("swaps").size()>64) throw std::runtime_error("Invalid saved weapon swaps");
            auto replacement=data.at("swaps").get<std::map<std::string,std::string>>();
            for (const auto& [source,id]:replacement) {
                if (source.size()>127) throw std::runtime_error("Invalid saved weapon identity");
                const auto identity=split_name(source); auto* def=definition(identity.weapon);
                if (!def || !identifier(identity.attachment) || !identifier(identity.furniture) ||
                    !compatible(def->weapClass,find_choice(id).category)) throw std::runtime_error("Saved weapon is incompatible");
            }
            swaps=std::move(replacement); loaded.clear(); rendered.store(nullptr);
            message="Loaded saved swaps; models load when their recorded weapon appears"; return true;
        }
        return false;
    }
    void update()
    {
        if (!active()) {
            rendered.store(nullptr); pending.reset(); loaded.clear(); source_name.clear(); source_category=-1;
            // Requests survive Restore while native DObjs still reference them, then release on exit.
            for (const auto& model:held) utils::hook::invoke<void>(0x76850_b,model.c_str()); held.clear();
            snapshot=nlohmann::json::object(); return;
        }
        const auto current=demo_playback::current_path().string();
        if (path!=current || session!=demo_playback::session_id()) {
            rendered.store(nullptr); loaded.clear(); pending.reset();
            if (path!=current) { swaps.clear(); message.clear(); }
            path=current; session=demo_playback::session_id();
        }
        discover();
        const auto* ps=game::CG_GetLocalClientGlobals();
        source_weapon=ps ? field<unsigned>(ps,916) : 0;
        char name[1024]{}; if (source_weapon&255) game::BG_GetWeaponNameComplete(source_weapon,false,name,sizeof(name));
        source_name=name; const auto identity=split_name(source_name); auto* def=definition(identity.weapon);
        source_category=def ? def->weapClass : -1; source_label=def && def->szDisplayName ? def->szDisplayName : "Waiting for a recorded gun";
        if (!held.empty()) utils::hook::invoke<void>(0x757A0_b);
        if (pending) {
            if (std::all_of(pending->models.begin(),pending->models.end(),ready)) {
                swaps[pending->source]=pending->id; loaded[pending->source]=*pending;
                message="Weapon applied to first-person playback"; pending.reset();
            } else if (std::chrono::steady_clock::now()-pending->start>std::chrono::seconds(30)) {
                message="Weapon models did not load; previous selection retained"; pending.reset();
            }
        }
        const auto edit=swaps.find(source_name);
        if (edit==swaps.end()) rendered.store(nullptr);
        else if (const auto cached=loaded.find(source_name); cached!=loaded.end() && cached->second.id==edit->second) {
            if (const auto value=rendered.load(); !value || value->original!=source_weapon || value->target!=cached->second.target)
                publish(cached->second);
        } else if (!pending) {
            rendered.store(nullptr);
            try { pending=prepare(source_name,source_weapon,find_choice(edit->second)); }
            catch (const std::exception& error) { message=error.what(); swaps.erase(edit); }
        }
        snapshot={{"weaponSwapSource",source_name},{"weaponSwapLabel",source_label},{"weaponSwapCategory",source_category},
            {"weaponSwapApplied",swaps.contains(source_name)?swaps.at(source_name):""},{"weaponSwapLoading",pending.has_value()},
            {"weaponSwapMessage",message},{"weaponSwapCount",swaps.size()},{"weaponSwapCatalogCount",catalog.size()}};
    }
    nlohmann::json state() { return snapshot; }
    nlohmann::json choices_page(const std::string& source, const std::string& weapon, int page)
    {
        std::lock_guard lock(catalog_mutex);
        const auto identity=split_name(source); int category=-1;
        for (const auto& c:catalog_snapshot) if (c.at("weapon")==identity.weapon) { category=c.at("category"); break; }
        auto items=nlohmann::json::array(), weapons=nlohmann::json::array(); std::set<std::string> seen;
        for (const auto& c:catalog_snapshot) if (compatible(category,c.at("category"))) {
            const auto w=c.at("weapon").get<std::string>();
            if (seen.insert(w).second) weapons.push_back({{"weapon",w},{"label",c.at("label")}});
            if (w==(weapon.empty()?identity.weapon:weapon)) items.push_back(c);
        }
        const int pages=std::max(1,(static_cast<int>(items.size())+3)/4); page=std::clamp(page,1,pages);
        auto rows=nlohmann::json::array(); for (int i=(page-1)*4;i<std::min(page*4,static_cast<int>(items.size()));++i) rows.push_back(items[i]);
        return {{"items",rows},{"weapons",weapons},{"page",page},{"pages",pages},{"total",items.size()},{"category",category_name(category)}};
    }
    class component final:public component_interface
    {
        void post_unpack() override
        {
            if (!game::environment::is_mp()) return;
            const std::uint8_t expected[]{0xF6,0x81,0x98,0x03,0,0,0x02,0x74,0x07,0x8B,0x81,0x88,0x03,0,0,0xC3,0x8B,0x81,0x94,0x03,0,0,0xC3};
            supported=!std::memcmp(reinterpret_cast<void*>(0x2E1270_b),expected,sizeof(expected));
            if (supported) view_weapon_hook.create(0x2E1270_b,view_weapon);
            command::add("demo_weapon_status",[] { dump("status"); });
            command::add("demo_weapon",[](const command::params& args) {
                try {
                    if (args.size()<2) { console::info("demo_weapon apply <native weapon> <kit> | restore | reset | save | load\n"); return; }
                    const std::string operation=args[1];
                    if (operation=="apply" && args.size()==4)
                        action("weapon_swap_apply",source_name+"|"+args[2]+"|"+args[3]);
                    else action("weapon_swap_"+operation,"");
                } catch (const std::exception& error) { message=error.what(); console::warn("[weapon swapper] %s\n",error.what()); }
            });
            // Explicit development run on a disposable copied demo. No automatic
            // changes or sidecar writes during ordinary playback.
            if (utils::flags::has_flag("weaponswapcheck")) scheduler::schedule([] {
                static int stage{}; static auto due=std::chrono::steady_clock::now()+std::chrono::seconds(12);
                static int origin{};
                if (std::chrono::steady_clock::now()<due) return scheduler::cond_continue;
                if (stage==0) {
                    if (!game::VirtualLobby_Loaded()) return scheduler::cond_continue;
                    command::execute("demoplay theater_weapon_check.dm_h1",false); ++stage;
                    due=std::chrono::steady_clock::now()+std::chrono::seconds(8); return scheduler::cond_continue;
                }
                if (!active() || demo_playback::seeking()) return scheduler::cond_continue;
                const auto next=[&] { ++stage; due=std::chrono::steady_clock::now()+std::chrono::seconds(5); };
                try {
                    update();
                    if (pending) return scheduler::cond_continue;
                    switch(stage) {
                    case 1:
                        if (split_name(source_name).weapon!="h1_remington700_mp") return scheduler::cond_continue;
                        if (!demo_playback::paused()) demo_playback::toggle_pause();
                        origin=demo_playback::current_time().value_or(0); dump("check-before");
                        action("weapon_swap_apply",source_name+"|h1_remington700_mp|ndl");next();break;
                    case 2:
                        dump("check-kit");if (!rendered.load()) throw std::runtime_error(message);
                        action("weapon_swap_apply",source_name+"|h1_junsnp_mp|base");next();break;
                    case 3:
                        dump("check-tac");if (!rendered.load()) throw std::runtime_error(message);
                        if (std::filesystem::exists(save_path())) throw std::runtime_error("Test fixture already has a weapon sidecar");
                        action("weapon_swap_save","");action("weapon_swap_restore","");next();break;
                    case 4: dump("check-restored");action("weapon_swap_load","");next();break;
                    case 5: dump("check-loaded");(void)demo_playback::seek_to(origin+5000);next();break;
                    case 6: dump("check-forward");(void)demo_playback::seek_to(origin);next();break;
                    case 7: dump("check-backward");if (demo_playback::paused()) demo_playback::toggle_pause();next();break;
                    case 8: dump("check-playing");action("weapon_swap_reset","");next();break;
                    default: dump("check-end");command::execute("quit",false);return scheduler::cond_end;
                    }
                } catch (const std::exception& error) {
                    message=error.what();snapshot["weaponSwapMessage"]=message;dump("check-failed");
                    command::execute("quit",false);return scheduler::cond_end;
                }
                return scheduler::cond_continue;
            },scheduler::main,100ms);
        }
    };
}
REGISTER_COMPONENT(theater_weapons::component)
