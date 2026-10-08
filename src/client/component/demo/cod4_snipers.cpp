#include "std_include.hpp"
#include "loader/component_loader.hpp"
#include "cod4_snipers.hpp"
#include "cod4_sniper_style.hpp"
#include "cod4_sniper_animation.hpp"
#include "cod4_fov.hpp"
#include "cod4_facial_native.hpp"
#include "demo_playback.hpp"
#include "demo_game.hpp"
#include "cod4_camera.hpp"
#include "theater_camera.hpp"
#include "theater_weapons.hpp"
#include "component/console.hpp"
#include "component/command.hpp"
#include "component/materials.hpp"
#include "game/dvars.hpp"
#include "utils/hook.hpp"
#include "utils/io.hpp"

namespace cod4_snipers
{
    namespace
    {
        game::dvar_t* enabled{}, *scopes{}, *fov_enabled{};
        bool layout_supported{}, projection_supported{};
        game::Material* overlay{};
        utils::hook::detour zoom_fraction_hook;
        utils::hook::detour gun_fov_hook;
        thread_local bool hip_projection{};
        using clips = cod4::sniper::animation_set;
        std::array<std::shared_ptr<const clips>, 5> original;
        std::array<bool, 5> attempted{};
        struct frame_pose
        {
            std::shared_ptr<const clips> source;
            std::vector<game::XModel*> gun_models;
            float fraction{};
            float down_weight{};
            const cod4::sniper::action* action{};
            double seconds{};
            unsigned session{};
            cod4::sniper::action_blend_sample blend;
        };
        std::atomic<std::shared_ptr<const frame_pose>> pose;
        unsigned session{};
        int previous_time{-1}, previous_weapon{-1};
        float previous_fraction{};
        bool lowering{};
        cod4::sniper::ads_direction scope_direction;
        cod4::sniper::ads_curve_blend curve_blend;
        cod4::sniper::action_clock action_clock;
        cod4::sniper::switch_return_blend return_blend;
        std::atomic_uint64_t applied{}, action_samples{}, missing{}, scope_draws{}, hidden_viewmodels{};
        std::atomic_uint64_t zoom_samples{};
        std::atomic<float> last_zoom_ads{}, last_zoom{};
        std::atomic<float> last_gun_ads{}, last_gun_fov{}, last_hip_fov{};
        std::atomic_uint64_t fov_samples{};
        std::atomic<float> last_scene_fov{}, last_weapon_fov{}, last_fov_ads{};
        bool classic_projection(int local_client)
        {
            return projection_supported && fov_enabled && fov_enabled->current.enabled
                && local_client == 0 && game::CL_IsCgameInitialized() && !game::VirtualLobby_Loaded()
                && (!demo_playback::playing()
                    || theater_camera::get_mode() == theater_camera::THEATER_CAMERA_FIRST_PERSON);
        }
        std::atomic<int> sampled_token{}, sampled_remaining{}, sampled_time{};
        std::atomic<double> sampled_seconds{};
        struct saved_definition
        {
            game::WeaponDef* def;
            float blur;
            bool sniper_scope;
        };
        std::vector<saved_definition> saved;
        constexpr std::array<float game::WeaponDef::*, 28> recoil_fields{
            &game::WeaponDef::adsGunKickReducedKickPercent, &game::WeaponDef::adsGunKickPitchMin,
            &game::WeaponDef::adsGunKickPitchMax, &game::WeaponDef::adsGunKickYawMin, &game::WeaponDef::adsGunKickYawMax,
            &game::WeaponDef::adsGunKickAccel, &game::WeaponDef::adsGunKickSpeedMax,
            &game::WeaponDef::adsGunKickSpeedDecay, &game::WeaponDef::adsGunKickStaticDecay,
            &game::WeaponDef::adsViewKickPitchMin, &game::WeaponDef::adsViewKickPitchMax,
            &game::WeaponDef::adsViewKickYawMin, &game::WeaponDef::adsViewKickYawMax, &game::WeaponDef::adsViewKickCenterSpeed,
            &game::WeaponDef::hipGunKickReducedKickPercent, &game::WeaponDef::hipGunKickPitchMin,
            &game::WeaponDef::hipGunKickPitchMax, &game::WeaponDef::hipGunKickYawMin, &game::WeaponDef::hipGunKickYawMax,
            &game::WeaponDef::hipGunKickAccel, &game::WeaponDef::hipGunKickSpeedMax,
            &game::WeaponDef::hipGunKickSpeedDecay, &game::WeaponDef::hipGunKickStaticDecay,
            &game::WeaponDef::hipViewKickPitchMin, &game::WeaponDef::hipViewKickPitchMax,
            &game::WeaponDef::hipViewKickYawMin, &game::WeaponDef::hipViewKickYawMax, &game::WeaponDef::hipViewKickCenterSpeed};
        std::array<float, 28> saved_recoil{};
        std::array<float, 5> saved_kick_limits{};
        std::array<int, 2> saved_kick_counts{};
        constexpr std::array<float game::WeaponDef::*, 5> kick_limits{
            &game::WeaponDef::adsGunKickMagMin, &game::WeaponDef::hipGunKickMagMin,
            &game::WeaponDef::adsViewKickMagMin, &game::WeaponDef::hipViewKickMagMin, &game::WeaponDef::viewKickScale};
        game::WeaponDef* recoil_definition{};
        static_assert(offsetof(game::WeaponDef, adsViewKickPitchMin) == 2944);
        static_assert(offsetof(game::WeaponDef, hipViewKickPitchMin) == 3024);
        bool replay()
        {
            return layout_supported && (demo_playback::converted() || cod4_camera::active()) && game::CL_IsCgameInitialized()
                && !game::VirtualLobby_Loaded();
        }
        bool classic() { return enabled && enabled->current.enabled && replay() && !theater_weapons::different_weapon(); }
        bool flat_scope() { return replay() && !theater_weapons::different_weapon() && ((scopes && scopes->current.enabled) || classic()); }
        template<typename T> T field(const void* p, std::size_t offset)
        { T value; std::memcpy(&value, static_cast<const std::uint8_t*>(p) + offset, sizeof(value)); return value; }
        std::shared_ptr<const clips> load(int weapon)
        {
            if (attempted[weapon]) return original[weapon];
            attempted[weapon] = true;
            constexpr const char* names[]{"m40a3", "remington", "barrett", "m21", "dragunov"};
            try
            {
                auto result = std::make_shared<clips>();
                for (const auto direction : {false, true})
                {
                    const auto path = std::format("cod4_snipers/viewmodel_{}_ads_{}", names[weapon], direction ? "down" : "up");
                    std::string bytes;
                    if (!utils::io::read_file(path, &bytes)) throw std::runtime_error("Missing " + path);
                    auto value = cod4::face::decode({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()}, true);
                    if (value.tracks.size() != 1 || value.tracks[0].bone != "tag_ads")
                        throw std::runtime_error("Unexpected COD4 sniper ADS controls");
                    (direction ? result->down : result->up) = std::move(value);
                }
                std::string descriptor;
                if (!utils::io::read_file("cod4_snipers/animations.json", &descriptor))
                    throw std::runtime_error("Missing original COD4 sniper animation set");
                const auto manifest = nlohmann::json::parse(descriptor);
                if (manifest.at("version") != 4 || manifest.at("rifles").size() != 5)
                    throw std::runtime_error("Unsupported COD4 sniper animation set");
                const auto& rifle = manifest.at("rifles").at(weapon);
                result->recoil = rifle.at("recoil").get<std::array<float, 28>>();
                for (const auto value : result->recoil)
                    if (!std::isfinite(value) || std::abs(value) > 10000)
                        throw std::runtime_error("Invalid original sniper recoil");
                const auto asset = [](const std::string& name)
                {
                    if (name.empty() || name.size() > 128 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.") != name.npos)
                        throw std::runtime_error("Invalid original sniper asset name");
                    std::string bytes;
                    if (!utils::io::read_file("cod4_snipers/" + name, &bytes)) throw std::runtime_error("Missing " + name);
                    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
                };
                result->hands = cod4::face::decode_rest(asset(rifle.at("hands").get<std::string>()));
                result->gun = cod4::face::decode_rest(asset(rifle.at("gun").get<std::string>()));
                result->rig = cod4::sniper::combine_rigs(result->hands, result->gun);
                for (const auto& entry : rifle.at("actions"))
                {
                    const auto logical = entry.at("logical").get<unsigned>();
                    const auto timer = entry.at("timer_ms").get<int>();
                    if (logical > 42 || timer < 0 || timer > 30000 || result->actions.contains(logical))
                        throw std::runtime_error("Invalid original sniper action");
                    auto clip = cod4::face::decode(asset(entry.at("clip").get<std::string>()), true);
                    result->actions.emplace(logical, cod4::sniper::action{std::move(clip), timer,
                        entry.at("loop").get<bool>(), entry.at("scale_to_timer").get<bool>()});
                }
                cod4::sniper::stabilize_draw_cartridge(*result);
                original[weapon] = result;
                console::info("COD4 sniper: loaded original %s ADS and %zu action clips\n", names[weapon], result->actions.size());
            }
            catch (const std::exception& error) { console::warn("COD4 sniper: %s\n", error.what()); }
            return original[weapon];
        }
        game::WeaponDef* definition(unsigned weapon)
        {
            // Same authenticated BG weapon table used by the converter. The
            // base index is a byte; furniture/attachment bits remain untouched.
            const auto* code = reinterpret_cast<const std::uint8_t*>(0x2E14EF_b);
            const auto displacement = field<std::int32_t>(code, 3);
            auto** table = reinterpret_cast<game::WeaponDef**>(0x2E14F6_b + displacement);
            return table[weapon & 255];
        }
        game::WeaponDef* scoped_definition(unsigned weapon)
        {
            auto* def = definition(weapon);
            if (!def || !def->name || cod4::sniper::rifle(def->name) < 0) return nullptr;
            std::array<game::WeaponAttachment*, 4> parts{};
            const auto count = utils::hook::invoke<int>(0x2E0C20_b, weapon, parts.data());
            for (int i = 0; i < std::clamp(count, 0, 4); ++i)
                if (parts[i] && parts[i]->name && std::string_view(parts[i]->name).find("acog") != std::string_view::npos) return nullptr;
            return def;
        }
        float zoom_fraction(const void* ps, unsigned weapon)
        {
            if (hip_projection) return 0.f;
            // Native CG_GetViewFovZoomFraction receives the rendered PS and
            // attachment-packed weapon. Override its result, so attachment
            // field getters cannot reintroduce H1's delayed zoom window.
            if (classic() && ps && scoped_definition(weapon))
            {
                const auto ads = field<float>(ps, 924);
                const auto zoom = cod4::sniper::zoom_fraction(ads);
                last_zoom_ads.store(ads); last_zoom.store(zoom); ++zoom_samples;
                return zoom;
            }
            return zoom_fraction_hook.invoke<float>(ps, weapon);
        }
        float gun_fov(int local_client, float world_fov)
        {
            const auto cod4_fov = classic_projection(local_client);
            const auto* cg = game::CG_GetLocalClientGlobals();
            auto* fov = game::Dvar_FindVar("cg_fov");
            auto* scale = game::Dvar_FindVar("cg_fovScale");
            const auto flat_sniper = classic() && local_client == 0 && cg
                && theater_camera::get_mode() == theater_camera::THEATER_CAMERA_FIRST_PERSON
                && scoped_definition(field<unsigned>(cg, 916));
            if (!flat_sniper)
            {
                if (const auto projection = cod4::fov::weapon_projection(cod4_fov, true, world_fov, false, 0.f))
                {
                    last_scene_fov.store(world_fov); last_weapon_fov.store(*projection);
                    last_fov_ads.store(cg ? field<float>(cg, 924) : 0.f); ++fov_samples;
                    return *projection;
                }
                return gun_fov_hook.invoke<float>(local_client, world_fov);
            }
            if (!fov || !scale) return gun_fov_hook.invoke<float>(local_client, world_fov);
            // Native hashed float dvars encode all four value words. Decode
            // through the engine instead of treating current.value as a float.
            const auto hip_value = std::strtof(game::Dvar_ValueToString(fov, true, fov->current), nullptr);
            const auto scale_value = std::strtof(game::Dvar_ValueToString(scale, true, scale->current), nullptr);
            const auto hip_world = hip_value * scale_value;
            if (!std::isfinite(hip_world) || hip_world <= 0 || !std::isfinite(scale_value) || scale_value <= 0)
                return gun_fov_hook.invoke<float>(local_client, world_fov);
            // Ask the engine for its configured hip weapon projection. Applying
            // early scene zoom to this projection magnifies the remastered lens.
            struct hip_guard
            {
                bool previous{hip_projection};
                hip_guard() { hip_projection = true; }
                ~hip_guard() { hip_projection = previous; }
            };
            float hip;
            if (const auto projection = cod4::fov::weapon_projection(cod4_fov, true, world_fov, true, hip_world))
            {
                hip = *projection;
                last_scene_fov.store(world_fov); last_weapon_fov.store(hip);
                last_fov_ads.store(field<float>(cg, 924)); ++fov_samples;
            }
            else { hip_guard guard; hip = gun_fov_hook.invoke<float>(local_client, hip_world); }
            if (!std::isfinite(hip) || hip <= 0 || hip >= 180)
                return gun_fov_hook.invoke<float>(local_client, world_fov);
            const auto ads = field<float>(cg, 924);
            // The scene supplies magnification. Keep the 3D optic's projection
            // stable until the flat scope and model visibility switch together.
            last_gun_ads.store(ads); last_gun_fov.store(hip); last_hip_fov.store(hip);
            return hip;
        }
        bool draw_gun(const void* dvar)
        {
            // Native CG viewmodel submission reads cg_drawGun here. A flat
            // overlay needs the scene behind it, rather than the opaque 3D lens.
            // Scope and model switch together, with no opacity cross-fade.
            const auto* cg = game::CG_GetLocalClientGlobals();
            if (flat_scope() && cg && theater_camera::get_mode() == theater_camera::THEATER_CAMERA_FIRST_PERSON
                && cod4::sniper::scope_replaces_gun(field<float>(cg, 924),
                    scope_direction.sample(demo_playback::session_id(), field<unsigned>(cg, 916),
                        game::mp::cl->serverTime, field<float>(cg, 924))))
            {
                const auto weapon = field<unsigned>(cg, 916);
                if (scoped_definition(weapon)) { ++hidden_viewmodels; return false; }
            }
            return utils::hook::invoke<bool>(0x183340_b, dvar);
        }
        void draw_scope_internal(int local_client)
        {
            const auto* cg = game::CG_GetLocalClientGlobals();
            if (!flat_scope() || local_client != 0 || !cg
                || theater_camera::get_mode() != theater_camera::THEATER_CAMERA_FIRST_PERSON) return;
            const auto weapon = field<unsigned>(cg, 916);
            if (!scoped_definition(weapon)) return;
            const auto alpha = cod4::sniper::scope_alpha(field<float>(cg, 924),
                scope_direction.sample(demo_playback::session_id(), weapon,
                    game::mp::cl->serverTime, field<float>(cg, 924)));
            if (alpha <= 0) return;
            if (!overlay)
            {
                std::string bytes;
                if (!utils::io::read_file("cod4_snipers/scope.png", &bytes)) return;
                // $white's blend state ignores texture alpha. Use the native
                // 2D scope's complete render state, including its alpha blend.
                const auto* base = game::Material_RegisterHandle("scope_overlay_m40a3");
                if (!base || !base->name || std::string_view(base->name) != "scope_overlay_m40a3") return;
                overlay = materials::create_material("cod4_sniper_scope", base);
                if (!materials::setup_material_image(overlay, bytes)) { materials::free_material(overlay); overlay = nullptr; return; }
            }
            const auto* placement = game::ScrPlace_GetViewPlacement();
            const float width = placement->realViewportSize[0], height = placement->realViewportSize[1];
            if (width <= 0 || height <= 0) return;
            const float size = height, left = (width - size) * .5f;
            const float x = placement->realViewportPosition[0], y = placement->realViewportPosition[1];
            float tint[]{1, 1, 1, alpha}, black[]{0, 0, 0, alpha};
            if (left > 0)
            {
                // Sample the opaque black corner using the scope's render state.
                game::R_AddCmdDrawStretchPic(x, y, left, height, 0, 0, 0, 0, black, overlay);
                game::R_AddCmdDrawStretchPic(x + left + size, y, left, height, 0, 0, 0, 0, black, overlay);
            }
            game::R_AddCmdDrawStretchPic(x + left, y, size, size, 0, 0, 1, 1, tint, overlay);
            ++scope_draws;
        }
    }
    void draw_scope(int local_client) { draw_scope_internal(local_client); }
    bool preserve_camera_motion()
    {
        const auto* cg = game::CG_GetLocalClientGlobals();
        return classic() && cg && scoped_definition(field<unsigned>(cg, 916))
            && theater_camera::get_mode() == theater_camera::THEATER_CAMERA_FIRST_PERSON;
    }
    void begin_frame()
    {
        end_frame();
        if (!classic() && !flat_scope()) return;
        // Keep mutations inside the native frame. Restore exact asset values
        // afterward, including stop, toggle-off and ordinary multiplayer.
        const auto* cl = game::mp::cl.get();
        if (!cl || !cl->snap.valid) return;
        const auto weapon = field<unsigned>(&cl->snap.ps, 916);
        std::array<game::WeaponAttachment*, 4> parts{};
        const auto count = utils::hook::invoke<int>(0x2E0C20_b, weapon, parts.data());
        for (int i = 0; i < std::clamp(count, 0, 4); ++i)
            if (parts[i] && parts[i]->name && std::string_view(parts[i]->name).find("acog") != std::string_view::npos) return;
        {
            auto* def = definition(weapon);
            if (!def || !def->name || cod4::sniper::rifle(def->name) < 0) return;
            saved_definition value{def, def->adsSceneBlurStrength, def->usesSniperScope};
            saved.push_back(value);
            if (flat_scope()) { def->usesSniperScope = false; def->adsSceneBlurStrength = 0; }
            if (classic()) if (const auto source = load(cod4::sniper::rifle(def->name)))
            {
                // COD4's Barrett ADS fire slot is idle; camera/gun kick comes
                // from these weapon parameters rather than an authored clip.
                // Let the native event/physics path use the authentic values.
                recoil_definition = def;
                for (unsigned i = 0; i < recoil_fields.size(); ++i)
                {
                    saved_recoil[i] = def->*recoil_fields[i];
                    def->*recoil_fields[i] = source->recoil[i];
                }
                saved_kick_counts = {def->adsGunKickReducedKickBullets, def->hipGunKickReducedKickBullets};
                def->adsGunKickReducedKickBullets = def->hipGunKickReducedKickBullets = 0;
                for (unsigned i = 0; i < kick_limits.size(); ++i)
                {
                    saved_kick_limits[i] = def->*kick_limits[i];
                    // These minimum-magnitude/scale controls do not exist in
                    // the original definitions. Use a neutral native value.
                    def->*kick_limits[i] = i == 4 ? 1.f : 0.f;
                }
            }
        }
    }
    void end_frame()
    {
        pose.store(nullptr);
        if (recoil_definition)
        {
            for (unsigned i = 0; i < recoil_fields.size(); ++i)
                recoil_definition->*recoil_fields[i] = saved_recoil[i];
            for (unsigned i = 0; i < kick_limits.size(); ++i)
                recoil_definition->*kick_limits[i] = saved_kick_limits[i];
            recoil_definition->adsGunKickReducedKickBullets = saved_kick_counts[0];
            recoil_definition->hipGunKickReducedKickBullets = saved_kick_counts[1];
            recoil_definition = nullptr;
        }
        for (const auto& value : saved)
        {
            value.def->adsSceneBlurStrength = value.blur; value.def->usesSniperScope = value.sniper_scope;
        }
        saved.clear();
    }
    void prepare_pose()
    {
        pose.store(nullptr);
        const auto* cg = game::CG_GetLocalClientGlobals();
        const auto invalidate = [] { return_blend = {}; previous_time = -1; };
        if (!classic() || !cg || theater_camera::get_mode() != theater_camera::THEATER_CAMERA_FIRST_PERSON)
        { invalidate(); return; }
        const auto weapon = field<unsigned>(cg, 916);
        const auto* def = definition(weapon);
        const int index = def && def->name ? cod4::sniper::rifle(def->name) : -1;
        if (index < 0) { invalidate(); return; }
        std::array<game::WeaponAttachment*, 4> parts{};
        const auto count = utils::hook::invoke<int>(0x2E0C20_b, weapon, parts.data());
        for (int i = 0; i < std::clamp(count, 0, 4); ++i)
            if (parts[i] && parts[i]->name && std::string_view(parts[i]->name).find("acog") != std::string_view::npos)
            { invalidate(); return; }
        const auto source = load(index);
        if (!source) { invalidate(); return; }
        const auto ads = field<float>(cg, 924);
        const int time = game::mp::cl->serverTime;
        const bool reset = session != demo_playback::session_id() || index != previous_weapon
            || previous_time < 0 || time < previous_time || time - previous_time > 250 || demo_playback::seeking();
        if (reset)
            lowering = false;
        else if (ads < previous_fraction - .00001f) lowering = true;
        else if (ads > previous_fraction + .00001f) lowering = false;
        session = demo_playback::session_id(); previous_time = time; previous_weapon = index; previous_fraction = ads;
        const auto down_weight = curve_blend.sample(time, lowering, reset);
        const int token = field<int>(cg, 548);
        const auto action = source->actions.find(static_cast<unsigned>(token) & 2047u);
        if (action == source->actions.end()) { invalidate(); return; } // Offhand/unsupported actions keep their native pose.
        const auto elapsed = action_clock.sample(session, weapon, token, time, field<int>(cg, 552),
            action->second.timer_ms, action->second.loop,
            (static_cast<unsigned>(token) & 2047u) == 33 ? field<int>(cg, 448) : 0,
            source->actions.at(31).timer_ms);
        const auto seconds = action->second.clip_seconds(elapsed);
        const auto blend = return_blend.sample(token, time, action->second, seconds, reset);
        sampled_token.store(token); sampled_remaining.store(field<int>(cg, 552));
        sampled_time.store(time); sampled_seconds.store(seconds);
        auto models = theater_weapons::view_models(weapon);
        if (models.empty() && def->gunModel) models = {def->gunModel[0], def->gunModel[1]};
        if (std::isfinite(ads) && !models.empty())
            pose.store(std::make_shared<const frame_pose>(frame_pose{source,
                std::move(models), ads, down_weight, &action->second, seconds, session, blend}));
    }
    void apply_pose(void* object, const std::uint32_t* fresh)
    {
        const auto value = pose.load();
        if (!value || object != *reinterpret_cast<void**>(0x390B130_b)) return;
        const auto* models = field<game::XModel* const*>(object, 216);
        auto* matrices = field<std::uint8_t*>(object, 168);
        const auto count = field<std::uint8_t>(object, 15), total = field<std::uint8_t>(object, 16);
        if (!models || !matrices || count > 32 || total > 254) return;
        // prepare_pose runs before native prediction. A weapon switch can
        // replace this DObj in that frame; never apply the previous rifle's
        // ADS controls to the new gun (or an ACOG/ordinary weapon).
        bool matches{};
        for (unsigned m = 0; m < count; ++m)
            for (const auto* gun : value->gun_models) if (gun && models[m] == gun) matches = true;
        if (!matches) return;
        cod4::face::vec3 aim_offset{};
        try
        {
            // Worker-local calibration is rebuilt on every playback session.
            // It holds rest values only; never retain evaluated matrix pointers.
            struct calibration { cod4::face::skeleton rig, encoded; float scale{}; };
            thread_local unsigned owner{};
            thread_local std::map<const game::XModel*, calibration> cache;
            thread_local std::map<std::array<std::uintptr_t, 3>, cod4::face::vec3> aim_cache;
            if (owner != value->session) { cache.clear(); aim_cache.clear(); owner = value->session; }
            unsigned first{};
            for (unsigned m = 0; m < count; ++m)
            {
                const auto* model = models[m];
                if (!model) return;
                const auto bones = field<std::uint8_t>(model, 8), roots = field<std::uint8_t>(model, 9);
                if (!bones || !roots || roots > bones || first + bones > total) return;
                if (!cache.contains(model))
                {
                    const auto* ids = field<const std::uint32_t*>(model, 56);
                    const auto* parents = field<const std::uint8_t*>(model, 64);
                    const auto* rotations = field<const std::array<std::int16_t, 4>*>(model, 72);
                    const auto* positions = field<const cod4::face::vec3*>(model, 80);
                    if (!ids || (bones > roots && (!parents || !rotations || !positions))) return;
                    std::vector<std::string> names;
                    for (unsigned b = 0; b < bones; ++b) names.emplace_back(game::SL_ConvertToString(ids[b]));
                    auto encoded = cod4::face::native_rest(names, roots, {parents, std::size_t(bones - roots)},
                        {rotations, std::size_t(bones - roots)}, {positions, std::size_t(bones - roots)});
                    const auto* bind = field<const std::uint8_t*>(model, 96);
                    if (!bind) return;
                    std::vector<cod4::face::pose> bind_world;
                    for (unsigned b = 0; b < bones; ++b)
                    {
                        cod4::face::pose p;
                        std::memcpy(p.rotation.data(), bind + b * 32, 16);
                        std::memcpy(p.translation.data(), bind + b * 32 + 16, 12);
                        p.rotation = cod4::face::normalized(p.rotation);
                        bind_world.push_back(p);
                    }
                    cache.emplace(model, calibration{cod4::sniper::geometry_rest(encoded, bind_world),
                        std::move(encoded), field<float>(model, 16)});
                }
                const auto& c = cache.at(model);
                const auto contains = [&](std::string_view name)
                { return std::any_of(c.rig.begin(), c.rig.end(), [&](const auto& b) { return b.name == name; }); };
                const bool hands = contains("tag_view") || contains("j_shoulder_le");
                const bool gun = contains("j_gun");
                const auto& source_rig = hands ? (gun ? value->source->rig : value->source->hands) : value->source->gun;
                if (hands || gun)
                {
                    auto local = gun && !hands
                        ? cod4::sniper::retarget_gun(source_rig, value->action->animation, value->seconds, c.rig)
                        : cod4::sniper::retarget_action(source_rig, value->action->animation, value->seconds, c.rig);
                    if (value->blend.outgoing)
                    {
                        const auto& blend = value->blend;
                        const auto old = gun && !hands
                            ? cod4::sniper::retarget_gun(source_rig, blend.outgoing->animation, blend.seconds, c.rig)
                            : cod4::sniper::retarget_action(source_rig, blend.outgoing->animation, blend.seconds, c.rig);
                        cod4::sniper::blend_action_pose(local, old, blend.weight);
                    }
                    for (unsigned b = 0; b < bones; ++b)
                    {
                        const auto bone = first + b, bit = 0x80000000u >> (bone & 31), word = bone >> 5;
                        if (!(fresh[word] & bit) || ((field<std::uint32_t>(object, 64 + word * 4)
                            | field<std::uint32_t>(object, 96 + word * 4)) & bit)) continue;
                        if (c.rig[b].name == "tag_ads") continue;
                        // Helper bones must follow the transferred pose too.
                        // Leaving their MWR action offsets in place would bend
                        // the mapped children a second time after composition.
                        const auto target = cod4::face::native_animation_pose(c.encoded[b].rest, local[b], c.scale);
                        auto* matrix = matrices + bone * 32;
                        std::memcpy(matrix, target.rotation.data(), 16);
                        std::memcpy(matrix + 16, target.translation.data(), 12);
                        const float weight{}; std::memcpy(matrix + 28, &weight, 4);
                        ++applied;
                        ++action_samples;
                    }
                }
                first += bones;
            }
            const game::XModel *hands_model{}, *gun_model{}, *scope_model{};
            for (unsigned m = 0; m < count; ++m)
            {
                const auto* model = models[m];
                const auto& rig = cache.at(model).rig;
                const auto contains = [&](std::string_view name)
                { return std::any_of(rig.begin(), rig.end(), [&](const auto& b) { return b.name == name; }); };
                if (contains("tag_ads") && contains("tag_weapon")) hands_model = model;
                if (contains("j_gun")) gun_model = model;
                if (rig.size() > 1 && rig.front().name == "tag_sight_on" && contains("tag_reticle_attach")) scope_model = model;
            }
            if (hands_model && gun_model && scope_model)
            {
                const std::array<std::uintptr_t, 3> key{reinterpret_cast<std::uintptr_t>(hands_model),
                    reinterpret_cast<std::uintptr_t>(gun_model), reinterpret_cast<std::uintptr_t>(scope_model)};
                if (!aim_cache.contains(key)) aim_cache.emplace(key, cod4::sniper::optical_ads_correction(*value->source,
                    cache.at(hands_model).rig, cache.at(gun_model).rig, cache.at(scope_model).rig));
                aim_offset = aim_cache.at(key);
            }
        }
        catch (const std::exception& error)
        {
            if (missing.fetch_add(1) == 0) console::warn("COD4 sniper action layer skipped: %s\n", error.what());
            return;
        }
        unsigned offset{};
        const auto tag = static_cast<std::uint32_t>(game::SL_FindString("tag_ads"));
        for (unsigned m = 0; m < count; ++m)
        {
            const auto* model = models[m];
            if (!model) return;
            const auto bones = field<std::uint8_t>(model, 8);
            const auto roots = field<std::uint8_t>(model, 9);
            const auto* names = field<const std::uint32_t*>(model, 56);
            if (!names || offset + bones > total) return;
            for (unsigned b = 0; b < bones; ++b)
            {
                if (names[b] != tag) continue;
                const auto bone = offset + b;
                if (!(fresh[bone >> 5] & (0x80000000u >> (bone & 31)))) return;
                const auto curve = [&](const auto& clip, bool down)
                { return cod4::face::sample(clip, clip.tracks[0],
                    cod4::sniper::clip_seconds(value->fraction, down, clip.frames, clip.fps), {}); };
                auto local = curve(value->source->up, false);
                const auto down = curve(value->source->down, true);
                local.rotation = cod4::face::interpolate(local.rotation, down.rotation, value->down_weight);
                for (unsigned axis = 0; axis < 3; ++axis)
                    local.translation[axis] += (down.translation[axis] - local.translation[axis]) * value->down_weight
                        + aim_offset[axis] * std::clamp(value->fraction, 0.f, 1.f);
                const float scale = field<float>(model, 16);
                if (!std::isfinite(scale) || scale <= 0 || roots > bones || b < roots) return;
                const auto* positions = field<const cod4::face::vec3*>(model, 80);
                if (!positions) return;
                const auto* rotations = field<const std::array<std::int16_t, 4>*>(model, 72);
                if (!rotations) return;
                cod4::face::pose rest;
                rest.translation = positions[b - roots];
                const auto& q = rotations[b - roots];
                rest.rotation = cod4::face::normalized({float(q[0]), float(q[1]), float(q[2]), float(q[3])});
                // The action layer now supplies COD4's complete hip pose.
                // Its authored ADS endpoint must be applied directly: the old
                // MWR-endpoint calibration cancels the sideways centering and
                // leaves the original hip position in place at full ADS.
                const auto target = cod4::face::native_animation_pose(rest, local, scale);
                auto* matrix = matrices + bone * 32;
                std::memcpy(matrix, target.rotation.data(), 16);
                std::memcpy(matrix + 16, target.translation.data(), 12);
                const float weight{}; std::memcpy(matrix + 28, &weight, 4);
                ++applied;
                return;
            }
            offset += bones;
        }
        ++missing;
    }
    class component final : public component_interface
    {
    public:
        void post_unpack() override
        {
            if (!game::environment::is_mp() || game::environment::is_dedi()) return;
            enabled = dvars::register_bool("cod4_snipers", false, 0, "Original COD4 sniper animations, zoom and 2D scopes on remastered models in converted demos");
            scopes = dvars::register_bool("cod4_2dScopes", false, 0, "Use flat COD4 sniper scopes in converted demos");
            fov_enabled = dvars::register_bool("cod4_fov", false, game::DvarFlags::DVAR_FLAG_SAVED,
                "Use COD4 weapon/world FOV coupling: cg_fov changes hip framing, ADS stays independent of cg_fov");
            constexpr std::array<std::uint8_t, 3> table_code{0x4C, 0x8D, 0x05};
            layout_supported = !std::memcmp(reinterpret_cast<const void*>(0x2E14EF_b), table_code.data(), table_code.size());
            const auto* draw_call = reinterpret_cast<const std::uint8_t*>(0xEC061_b);
            layout_supported = layout_supported && draw_call[0] == 0xE8
                && 0xEC066_b + field<std::int32_t>(draw_call, 1) == 0x183340_b;
            constexpr std::array<std::uint8_t, 9> zoom_code{0x89, 0x54, 0x24, 0x10, 0x53, 0x48, 0x83, 0xEC, 0x60};
            layout_supported = layout_supported && !std::memcmp(reinterpret_cast<const void*>(0x104ED0_b), zoom_code.data(), zoom_code.size());
            constexpr std::array<std::uint8_t, 10> gun_code{0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x60};
            projection_supported = !std::memcmp(reinterpret_cast<const void*>(0x1046F0_b), gun_code.data(), gun_code.size());
            layout_supported = layout_supported && projection_supported;
            if (projection_supported) gun_fov_hook.create(0x1046F0_b, gun_fov);
            if (layout_supported)
            {
                zoom_fraction_hook.create(0x104ED0_b, zoom_fraction);
                utils::hook::call(0xEC061_b, draw_gun);
            }
            if (!projection_supported) console::warn("COD4 FOV disabled: unsupported weapon projection function\n");
            command::add("cod4_fov_status", []
            {
                console::info("COD4 FOV: enabled=%d supported=%d active=%d samples=%llu ADS=%.6f scene=%.6f weapon=%.6f\n",
                    fov_enabled && fov_enabled->current.enabled, projection_supported, classic_projection(0),
                    fov_samples.load(), last_fov_ads.load(), last_scene_fov.load(), last_weapon_fov.load());
                for (const auto* name : {"cg_fov", "cg_fovScale"})
                    if (auto* d = game::Dvar_FindVar(name))
                        console::info("%s = %s\n", name, game::Dvar_ValueToString(d, true, d->current));
            });
            if (!layout_supported) console::warn("COD4 sniper presentation disabled: unsupported weapon table\n");
            command::add("cod4_snipers_status", []
            {
                console::info("COD4 snipers: enabled=%d scope=%d original bone poses=%llu action bone poses=%llu missing tags=%llu overlays=%llu hidden viewmodels=%llu\n",
                    classic(), flat_scope(), applied.load(), action_samples.load(), missing.load(), scope_draws.load(), hidden_viewmodels.load());
                console::info("COD4 sniper zoom: native calls=%llu ADS=%.6f zoom=%.6f\n",
                    zoom_samples.load(), last_zoom_ads.load(), last_zoom.load());
                console::info("COD4 sniper projection: ADS=%.6f weapon FOV=%.6f hip FOV=%.6f\n",
                    last_gun_ads.load(), last_gun_fov.load(), last_hip_fov.load());
                console::info("COD4 sniper action: token=%d remaining=%d time=%d clip_seconds=%.6f\n",
                    sampled_token.load(), sampled_remaining.load(), sampled_time.load(), sampled_seconds.load());
            });
        }
    };
}
REGISTER_COMPONENT(cod4_snipers::component)
