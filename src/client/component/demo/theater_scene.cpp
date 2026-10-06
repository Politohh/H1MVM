#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "theater_scene.hpp"
#include "theater_scene_math.hpp"
#include "demo_game.hpp"
#include "demo_playback.hpp"
#include "component/console.hpp"
#include "utils/hook.hpp"
#include "theater_sky_runtime.hpp"

namespace theater_scene
{
    namespace
    {
        std::vector<setting> catalog;
        std::unordered_map<std::string, game::dvar_t*> variables;
        utils::hook::detour fog_hook, scene_hook, dobj_hook, sun_direction_hook, sky_texture_hook;
        theater_sky::runtime skies;
        math::fog source_fog;
        bool source_fog_valid{};
        unsigned session{};
        struct light { math::vector position{}; math::vector color{1, 1, 1}; float radius{300}, intensity{1}; bool enabled{true}; };
        std::vector<light> lights;
        std::size_t selected{};
        bool attached{}, seed_attachment{};
        int attached_entity{-1};
        unsigned attached_tag{};
        math::vector bone_offset{};
        math::axis relative_axis{};
        std::string status;
        std::mutex state_mutex;
        nlohmann::json snapshot = {{"boneAttached", false}, {"sceneMessage", ""}, {"lightCount", 0}};

        float value(const char* name) { return variables.at(name)->current.value; }
        int integer(const char* name) { return variables.at(name)->current.integer; }
        bool enabled(const char* name) { return variables.at(name)->current.enabled; }
        void assign(const char* name, float v) { game::Dvar_SetFromStringFromSource(variables.at(name), std::to_string(v).c_str(), game::DVAR_SOURCE_INTERNAL); }
        bool active() { return demo_playback::is_replay_armed() && demo_playback::playing(); }
        void sky_texture(void* state, unsigned slot, const game::GfxImage* image)
        {
            const auto replacement=skies.replace(image,active(),integer("demo_skybox"));
            sky_texture_hook.invoke<void>(state,slot,replacement);
        }
        char add_dobj(void* obj, std::uint8_t* pose, unsigned entity, unsigned flags,
            const float* lighting, int material_time, int material_data)
        {
            if (active())
            {
                // World actors are identified by the actual centity pose, not
                // model names. Props and first-person weapon poses are retained.
                const auto pass = integer("demo_render_pass");
                if ((enabled("demo_hide_actors") || pass == 1) && entity < 2047 && pose &&
                    pose == utils::hook::invoke<std::uint8_t*>(0x6BFF0_b, 0, static_cast<short>(entity)) &&
                    (*reinterpret_cast<const unsigned*>(pose + 0x23C) & 1) &&
                    (pose[0x146] == 1 || pose[0x146] == 2 || pose[0x146] == 18 || pose[0x146] == 19)) return 0;
            }
            return dobj_hook.invoke<char>(obj, pose, entity, flags, lighting, material_time, material_data);
        }
        void reset_session()
        {
            const auto current = demo_playback::session_id();
            if (current == session) return;
            session = current; attached = seed_attachment = false; attached_entity = -1;
            lights.clear(); selected = 0; source_fog_valid = false;
            game::Dvar_SetFromStringFromSource(variables.at("demo_skybox"), "Original", game::DVAR_SOURCE_INTERNAL);
            // Saved values remain available, but scene overrides must be explicitly
            // enabled again in a new replay; no map assets or network state are edited.
            for (const auto* name : {"demo_fog_override", "demo_sun_override", "demo_light_override"})
                game::Dvar_SetFromStringFromSource(variables.at(name), "0", game::DVAR_SOURCE_INTERNAL);
        }
        math::vector rgb(const char* r, const char* g, const char* b) { return {value(r), value(g), value(b)}; }
        // Native R_UpdateSunLight (0x1C71B0) resolves its direction before
        // copying primary lights, and writes the shadow direction separately.
        // Overriding only the later memcpy left shadows using the old direction.
        std::uint64_t sun_direction(int time, float* direction)
        {
            if (active() && enabled("demo_sun_override"))
            {
                const auto dir = math::direction(value("demo_sun_pitch"), value("demo_sun_yaw"));
                std::memcpy(direction, dir.data(), sizeof(dir));
                return 1;
            }
            return sun_direction_hook.invoke<std::uint64_t>(time, direction);
        }
        void sync_sun_shadow_cache()
        {
            static game::dvar_t* cache{};
            static bool held{}, saved{};
            if (!cache) cache = game::Dvar_FindVar("sm_cacheSunShadowEnabled");
            if (!cache || cache->type != game::boolean) return;
            const bool override = active() && enabled("demo_sun_override");
            if (override)
            {
                if (!held) { saved = cache->current.enabled; held = true; }
                if (cache->current.enabled) game::Dvar_SetFromStringFromSource(cache,"0",game::DVAR_SOURCE_INTERNAL);
            }
            else if (held)
            {
                game::Dvar_SetFromStringFromSource(cache,saved ? "1" : "0",game::DVAR_SOURCE_INTERNAL);
                held = false;
            }
        }
        std::uint64_t prepare_sun(unsigned client, void* primary_lights, void* shadow_parameters)
        {
            if (active()) reset_session();
            sync_sun_shadow_cache();
            return utils::hook::invoke<std::uint64_t>(0x1C71B0_b,client,primary_lights,shadow_parameters);
        }
        math::fog make_fog(const void* source)
        {
            math::fog fog; std::memcpy(fog.bytes.data(), source, sizeof(fog));
            fog.set<std::uint8_t>(60, 1);
            const std::pair<std::size_t, const char*> fields[] = {{64,"demo_fog_haze_strength"}, {68,"demo_fog_haze_spread"},
                {72,"demo_fog_inscatter"}, {76,"demo_fog_extinction"}, {80,"demo_fog_start"}, {84,"demo_fog_scale"},
                {88,"demo_fog_sky_distance"}, {92,"demo_fog_sky_start"}, {96,"demo_fog_sky_range"}, {112,"demo_fog_height"}};
            for (const auto& [offset, name] : fields) fog.set(offset, value(name));
            constexpr float ln2 = .69314718056f;
            fog.set(140, ln2 / std::max(value("demo_fog_half_distance"), 1.f));
            fog.set(144, ln2 / std::max(value("demo_fog_height_distance"), 1.f));
            const auto dir = math::direction(value("demo_fog_pitch"), value("demo_fog_yaw"));
            for (int i = 0; i < 3; ++i) fog.set(100 + i * 4, dir[i]);
            fog.set(148, math::color(value("demo_fog_sun_r"), value("demo_fog_sun_g"), value("demo_fog_sun_b")));
            fog.set(152, math::color(value("demo_fog_haze_r"), value("demo_fog_haze_g"), value("demo_fog_haze_b")));
            return fog;
        }
        std::uint64_t set_fog(int local_client, const void* input, int slot)
        {
            if (active() && input && local_client == 0 && slot >= 0 && slot < 7)
            {
                reset_session();
                if (slot == 5 || !source_fog_valid) { std::memcpy(source_fog.bytes.data(), input, 156); source_fog_valid = true; }
                if (enabled("demo_fog_override"))
                {
                    const auto replacement = make_fog(input);
                    return fog_hook.invoke<std::uint64_t>(local_client, replacement.bytes.data(), slot);
                }
            }
            return fog_hook.invoke<std::uint64_t>(local_client, input, slot);
        }
        // R_SetupScene copies refdef's GfxLight[152] into the backend. Change only
        // that frame's copy, before visibility/shadow work, never the map asset.
        void* copy_lights(void* destination, const void* source, std::size_t size)
        {
            std::memcpy(destination, source, size);
            if (!active() || size % 152 || size > 152 * 6528) return destination;
            reset_session();
            auto* bytes = static_cast<std::uint8_t*>(destination);
            const auto count = size / 152;
            for (std::size_t i = 0; i < count; ++i)
            {
                auto* light = bytes + i * 152;
                const bool sun = light[0] == 1 && enabled("demo_sun_override");
                const bool map_light = i == static_cast<std::size_t>(integer("demo_light_index")) && light[0] > 1 && enabled("demo_light_override");
                if (!sun && !map_light) continue;
                const auto color = sun ? rgb("demo_sun_r", "demo_sun_g", "demo_sun_b") : rgb("demo_light_r", "demo_light_g", "demo_light_b");
                const auto intensity = value(sun ? "demo_sun_intensity" : "demo_light_intensity");
                for (int channel = 0; channel < 3; ++channel) { const float v = color[channel] * intensity; std::memcpy(light + 8 + channel * 4, &v, 4); }
                if (sun)
                {
                    const auto dir = math::direction(value("demo_sun_pitch"), value("demo_sun_yaw"));
                    std::memcpy(light + 20, dir.data(), 12);
                    // A perpendicular up vector keeps the light basis orthogonal.
                    const auto up = math::direction(value("demo_sun_pitch") - 90.f, value("demo_sun_yaw"));
                    std::memcpy(light + 32, up.data(), 12);
                }
            }
            return destination;
        }
        void read_light_controls()
        {
            if (selected >= lights.size()) return;
            const auto& l = lights[selected];
            const char* positions[] = {"demo_point_x", "demo_point_y", "demo_point_z"};
            const char* colors[] = {"demo_point_r", "demo_point_g", "demo_point_b"};
            for (int i = 0; i < 3; ++i) { assign(positions[i], l.position[i]); assign(colors[i], l.color[i]); }
            assign("demo_point_radius", l.radius); assign("demo_point_intensity", l.intensity);
            game::Dvar_SetFromStringFromSource(variables.at("demo_point_enabled"), l.enabled ? "1" : "0", game::DVAR_SOURCE_INTERNAL);
        }
        void write_light_controls()
        {
            if (selected >= lights.size()) return;
            lights[selected] = {rgb("demo_point_x", "demo_point_y", "demo_point_z"), rgb("demo_point_r", "demo_point_g", "demo_point_b"),
                value("demo_point_radius"), value("demo_point_intensity"), enabled("demo_point_enabled")};
        }
        // Native R_AddOmniLightToScene (0x1BD840) has 7 arguments. Its small
        // settings block supplies bulb length/radius/falloff; the native routine
        // owns allocation, default lightDef and the 32-light capacity check.
        void submit_points()
        {
            if (!active()) return;
            reset_session(); write_light_controls();
            const math::vector up{0, 0, 1};
            const float parameters[4] = {0.f, 0.f, 0.f, 0.f};
            for (const auto& l : lights) if (l.enabled)
            {
                if (*reinterpret_cast<const int*>(0x339F2B0_b) >= 32) break;
                utils::hook::invoke<void>(0x1BD840_b, l.position.data(), up.data(), l.radius,
                    l.color[0] * l.intensity, l.color[1] * l.intensity, l.color[2] * l.intensity, parameters);
            }
        }
        std::uint64_t setup_scene(void* refdef, void* view, void* a3, void* a4, int a5)
        {
            submit_points();
            return scene_hook.invoke<std::uint64_t>(refdef, view, a3, a4, a5);
        }
        bool bone_matrix(int entity, unsigned tag, math::axis& axis, math::vector& origin)
        {
            if (entity < 0 || entity >= 2047) return false;
            auto* centity = utils::hook::invoke<std::uint8_t*>(0x6BFF0_b, 0, static_cast<short>(entity));
            if (!centity || !(*reinterpret_cast<const unsigned*>(centity + 0x23C) & 1)) return false;
            auto* obj = utils::hook::invoke<void*>(0x4E98E0_b, *reinterpret_cast<const unsigned short*>(centity + 0x13C), 0);
            return obj && utils::hook::invoke<bool>(0xF3D20_b, centity, obj, tag, axis.data(), origin.data());
        }
        const char* bones[] = {"j_head", "j_neck", "j_spineupper", "j_spinelower", "j_hip", "j_wrist_ri", "j_wrist_le", "j_ankle_ri", "j_ankle_le", "tag_weapon", nullptr};
        void add(const char* page, const char* name, const char* label, float initial, float min, float max, float step)
        {
            variables[name] = game::demo_compat::register_float(name, initial, min, max, game::DVAR_FLAG_NONE, label);
            catalog.push_back({page, name, label, step});
        }
        void boolean(const char* page, const char* name, const char* label, bool initial = false)
        {
            variables[name] = dvars::register_bool(name, initial, game::DVAR_FLAG_NONE, label);
            catalog.push_back({page, name, label, 1});
        }
        void register_settings()
        {
            boolean("misc", "demo_hide_actors", "HIDE PLAYERS / CORPSES");
            static const char* passes[] = {"Current settings", "Without players", nullptr};
            variables["demo_render_pass"] = game::demo_compat::register_enum("demo_render_pass", passes, 0, 0, "Demo actor render pass");
            catalog.push_back({"streams", "demo_render_pass", "RENDER PASS", 1});
            boolean("camera", "demo_camera_frozen", "FREEZE CAMERA");
            boolean("camera", "demo_camera_shake", "CAMERA SHAKE");
            add("camera", "demo_shake_speed", "SHAKE SPEED", 1, 0, 10, .1f);
            add("camera", "demo_shake_position", "SHAKE POSITION", 0, 0, 20, .1f);
            add("camera", "demo_shake_rotation", "SHAKE ROTATION", .2f, 0, 5, .05f);
            variables["demo_bone_entity"] = dvars::register_int("demo_bone_entity", 0, 0, 2046, 0, "Bone camera entity number");
            catalog.push_back({"bone_camera", "demo_bone_entity", "TARGET ENTITY", 1});
            variables["demo_bone_tag"] = game::demo_compat::register_enum("demo_bone_tag", bones, 0, 0, "Bone camera attachment tag");
            catalog.push_back({"bone_camera", "demo_bone_tag", "ATTACHMENT BONE", 1});
            boolean("bone_camera", "demo_bone_rotation", "FOLLOW BONE ROTATION", true);
            for (const auto& e : std::initializer_list<setting>{{"bone_camera","demo_bone_x","LOCAL OFFSET X",1},
                {"bone_camera","demo_bone_y","LOCAL OFFSET Y",1},{"bone_camera","demo_bone_z","LOCAL OFFSET Z",1}})
                add(e.page, e.name, e.label, 0, -2000, 2000, 1);
            boolean("fog", "demo_fog_override", "ATMOSPHERE OVERRIDE");
            add("fog","demo_fog_start","START DISTANCE",0,0,100000,25);
            add("fog","demo_fog_half_distance","HALF DISTANCE",5000,1,100000,50);
            add("fog","demo_fog_height","HEIGHT BASE",0,-10000,10000,25);
            add("fog","demo_fog_height_distance","HEIGHT HALF DISTANCE",100000,1,100000,50);
            add("fog","demo_fog_haze_strength","HAZE STRENGTH",.5f,0,10,.05f);
            add("fog","demo_fog_haze_spread","HAZE SPREAD",.25f,0,1,.01f);
            add("fog","demo_fog_extinction","EXTINCTION",1,0,10,.05f);
            add("fog","demo_fog_inscatter","IN-SCATTER",0,0,10,.05f);
            add("fog","demo_fog_scale","DISTANCE SCALE",1,0,10,.05f);
            add("fog","demo_fog_sky_distance","SKY DISTANCE",100000,1,100000,100);
            add("fog","demo_fog_sky_start","SKY FALLOFF START",-90,-90,90,1);
            add("fog","demo_fog_sky_range","SKY FALLOFF RANGE",9000,1,9000,10);
            add("fog","demo_fog_pitch","SCATTER PITCH",-45,-90,90,1);
            add("fog","demo_fog_yaw","SCATTER YAW",0,-180,180,1);
            for (const auto& e : std::initializer_list<setting>{{"fog","demo_fog_sun_r","FOG SUN RED",.01},
                {"fog","demo_fog_sun_g","FOG SUN GREEN",.01},{"fog","demo_fog_sun_b","FOG SUN BLUE",.01},
                {"fog","demo_fog_haze_r","HAZE RED",.01},{"fog","demo_fog_haze_g","HAZE GREEN",.01},{"fog","demo_fog_haze_b","HAZE BLUE",.01},
                {"sun","demo_sun_r","SUN RED",.01},{"sun","demo_sun_g","SUN GREEN",.01},{"sun","demo_sun_b","SUN BLUE",.01},
                {"lights","demo_light_r","MAP LIGHT RED",.01},{"lights","demo_light_g","MAP LIGHT GREEN",.01},{"lights","demo_light_b","MAP LIGHT BLUE",.01},
                {"lights","demo_point_r","POINT RED",.01},{"lights","demo_point_g","POINT GREEN",.01},{"lights","demo_point_b","POINT BLUE",.01}})
                add(e.page,e.name,e.label,1,0,1,.01f);
            variables["demo_skybox"] = game::demo_compat::register_enum("demo_skybox", skies.discover(), 0, 0, "Native demo sky cubemap");
            catalog.push_back({"sun","demo_skybox","SKYBOX",1});
            boolean("sun","demo_sun_override","SUN LIGHT OVERRIDE");
            add("sun","demo_sun_intensity","SUN INTENSITY",1,0,20,.1f);
            add("sun","demo_sun_pitch","SUN ELEVATION",-45,-90,90,1);
            add("sun","demo_sun_yaw","SUN AZIMUTH",0,-180,180,1);
            add("sun","demo_sun_timeofday","TIME OF DAY",12,0,24,1.f/6.f);
            boolean("lights","demo_light_override","MAP LIGHT OVERRIDE");
            variables["demo_light_index"] = dvars::register_int("demo_light_index",1,0,6527,0,"Native map light index");
            catalog.push_back({"lights","demo_light_index","MAP LIGHT INDEX",1});
            add("lights","demo_light_intensity","MAP LIGHT INTENSITY",1,0,20,.1f);
            boolean("lights","demo_point_enabled","SELECTED POINT ENABLED",true);
            add("lights","demo_point_x","POINT X",0,-100000,100000,10);
            add("lights","demo_point_y","POINT Y",0,-100000,100000,10);
            add("lights","demo_point_z","POINT Z",0,-100000,100000,10);
            add("lights","demo_point_radius","POINT RADIUS",300,1,10000,25);
            add("lights","demo_point_intensity","POINT INTENSITY",1,0,20,.1f);
        }
    }
    const std::vector<setting>& settings() { return catalog; }
    nlohmann::json state() { std::lock_guard lock(state_mutex); return snapshot; }
    nlohmann::json saved_lights()
    {
        write_light_controls();
        auto data = nlohmann::json::array();
        for (const auto& l : lights) data.push_back({{"position",l.position},{"color",l.color},
            {"radius",l.radius},{"intensity",l.intensity},{"enabled",l.enabled}});
        return data;
    }
    void restore_lights(const nlohmann::json& data, bool validate_only)
    {
        if (!data.is_array() || data.size() > 16) throw std::runtime_error("Invalid preset lights");
        std::vector<light> replacement;
        for (const auto& item : data)
        {
            light l{item.at("position").get<math::vector>(), item.at("color").get<math::vector>(),
                item.at("radius").get<float>(), item.at("intensity").get<float>(), item.at("enabled").get<bool>()};
            for (int i=0;i<3;++i) if (!std::isfinite(l.position[i]) || std::abs(l.position[i])>100000 ||
                !std::isfinite(l.color[i]) || l.color[i]<0 || l.color[i]>1) throw std::runtime_error("Invalid preset light values");
            if (!std::isfinite(l.radius) || l.radius<1 || l.radius>10000 || !std::isfinite(l.intensity) ||
                l.intensity<0 || l.intensity>20) throw std::runtime_error("Invalid preset light radius/intensity");
            replacement.push_back(l);
        }
        if (!validate_only) { reset_session(); lights = std::move(replacement); selected = 0; read_light_controls(); }
    }
    void setting_changed(const std::string& name)
    {
        if (name == "demo_sun_timeofday")
        {
            const auto angles = math::day_angles(value("demo_sun_timeofday"));
            assign("demo_sun_pitch", angles[0]); assign("demo_sun_yaw", angles[1]);
        }
    }
    void reset_editor()
    {
        attached = seed_attachment = false; attached_entity = -1;
        lights.clear(); selected = 0; status = "Editor settings reset";
        sync_sun_shadow_cache();
    }
    bool action(const std::string& name)
    {
        if (!active()) return false;
        reset_session();
        if (name == "bone_attach")
        {
            theater_camera::stop_cinematic(); theater_camera::set_mode(theater_camera::THEATER_CAMERA_FREECAM);
            attached_entity = integer("demo_bone_entity"); attached_tag = game::SL_GetString(bones[integer("demo_bone_tag")], 0);
            attached = seed_attachment = true; status = "Attachment requested";
        }
        else if (name == "bone_detach") { attached = seed_attachment = false; status = "Detached; free camera retained"; }
        else if (name == "bone_next")
        {
            const auto first = integer("demo_bone_entity");
            for (int i = 1; i < 2047; ++i)
            {
                const auto n = (first + i) % 2047;
                auto* c = utils::hook::invoke<std::uint8_t*>(0x6BFF0_b, 0, static_cast<short>(n));
                if (c && (*reinterpret_cast<const unsigned*>(c + 0x23C) & 1) &&
                    (c[0x146] == 1 || c[0x146] == 2 || c[0x146] == 18 || c[0x146] == 19))
                { assign("demo_bone_entity", static_cast<float>(n)); status = "Selected actor entity " + std::to_string(n); break; }
            }
        }
        else if (name == "point_add")
        {
            if (lights.size() >= 16) throw std::runtime_error("Maximum 16 editor point lights");
            write_light_controls(); light l;
            if (!theater_camera::get_view_origin(l.position.data())) throw std::runtime_error("Camera position unavailable");
            lights.push_back(l); selected = lights.size() - 1; read_light_controls(); status = "Point light placed at camera";
        }
        else if (name == "point_next") { write_light_controls(); if (!lights.empty()) selected = (selected + 1) % lights.size(); read_light_controls(); }
        else if (name == "point_remove") { if (selected < lights.size()) lights.erase(lights.begin() + selected); selected = 0; read_light_controls(); }
        else if (name == "point_clear") { lights.clear(); selected = 0; }
        else if (name == "fog_sample")
        {
            if (!source_fog_valid) throw std::runtime_error("Map atmosphere is not available yet");
            const std::pair<std::size_t, const char*> fields[] = {{64,"demo_fog_haze_strength"},{68,"demo_fog_haze_spread"},
                {72,"demo_fog_inscatter"},{76,"demo_fog_extinction"},{80,"demo_fog_start"},{84,"demo_fog_scale"},
                {88,"demo_fog_sky_distance"},{92,"demo_fog_sky_start"},{96,"demo_fog_sky_range"},{112,"demo_fog_height"}};
            for (const auto& [offset, variable] : fields) assign(variable, source_fog.get<float>(offset));
            assign("demo_fog_half_distance", .69314718f / std::max(source_fog.get<float>(140), .0000069314718f));
            assign("demo_fog_height_distance", .69314718f / std::max(source_fog.get<float>(144), .0000069314718f));
            const char* colors[] = {"demo_fog_sun_b","demo_fog_sun_g","demo_fog_sun_r","demo_fog_haze_b","demo_fog_haze_g","demo_fog_haze_r"};
            for (int i=0;i<6;++i) assign(colors[i], source_fog.bytes[(i < 3 ? 148 : 152) + i % 3] / 255.f);
            float angles[3]{}; const auto dir = math::vector{source_fog.get<float>(100),source_fog.get<float>(104),source_fog.get<float>(108)};
            angles[0] = -std::asin(std::clamp(dir[2],-1.f,1.f))*57.2957795f; angles[1] = std::atan2(dir[1],dir[0])*57.2957795f;
            assign("demo_fog_pitch",angles[0]); assign("demo_fog_yaw",angles[1]); status = "Copied map atmosphere into controls";
        }
        else return false;
        return true;
    }
    void camera(theater_camera::camera_pose_t& pose)
    {
        if (!active()) { attached = false; return; }
        reset_session();
        if (attached && theater_camera::get_mode() == theater_camera::THEATER_CAMERA_FREECAM)
        {
            math::axis axis{}; math::vector origin{};
            if (bone_matrix(attached_entity, attached_tag, axis, origin))
            {
                if (seed_attachment)
                {
                    math::vector delta{}; for (int i=0;i<3;++i) delta[i] = pose.pos[i]-origin[i];
                    bone_offset = math::local(delta, axis);
                    const auto camera_axis = math::angles(pose.angles);
                    for (int i=0;i<3;++i) relative_axis[i] = math::local({camera_axis[i][0],camera_axis[i][1],camera_axis[i][2]}, axis);
                    seed_attachment = false; status = "Following entity " + std::to_string(attached_entity);
                }
                auto offset = bone_offset; offset[0] += value("demo_bone_x"); offset[1] += value("demo_bone_y"); offset[2] += value("demo_bone_z");
                const auto delta = math::world(offset, axis); for(int i=0;i<3;++i) pose.pos[i] = origin[i]+delta[i];
                if (enabled("demo_bone_rotation"))
                { math::axis rotation; for(int i=0;i<3;++i) rotation[i] = math::world(relative_axis[i],axis); game::AxisToAngles(reinterpret_cast<float(*)[3]>(rotation.data()), pose.angles); }
                theater_camera::set_freecam_pose(pose);
            }
            else { attached = false; status = "Target/bone no longer available; camera detached"; }
        }
        else attached = false;
        if (enabled("demo_camera_shake"))
        {
            const float time = demo_playback::current_time().value_or(0) / 1000.f * value("demo_shake_speed");
            for(int i=0;i<3;++i)
            {
                const float noise = .6f*std::sin(time*(2.1f+i*.71f)+i*1.3f)+.4f*std::sin(time*(4.7f+i*.37f));
                pose.pos[i] += noise*value("demo_shake_position"); pose.angles[i] += noise*value("demo_shake_rotation");
            }
        }
        std::lock_guard lock(state_mutex);
        snapshot = {{"boneAttached",attached},{"boneEntity",attached_entity},{"sceneMessage",status},
            {"lightCount",lights.size()},{"selectedLight",lights.empty()?0:selected+1}};
    }
    class component final : public component_interface
    {
    public:
        void post_unpack() override
        {
            if (!game::environment::is_mp() || game::environment::is_dedi()) return;
            register_settings();
            // ABI/code provenance: work/h1-theater-analysis, native CG_ParseFog,
            // R_SetFog, R_SetupScene and R_AddOmniLightToScene. Fail closed if a
            // different game build moves the verified primary-light copy call.
            if (*reinterpret_cast<const std::uint8_t*>(0x1C5822_b) != 0xE8 ||
                *reinterpret_cast<const std::int32_t*>(0x1C5823_b) != static_cast<std::int32_t>(0x59F870 - 0x1C5827))
                throw std::runtime_error("Unsupported H1 renderer build for Theater scene controls");
            fog_hook.create(0x67E610_b, set_fog);
            // Same three-argument native pixel-texture binding ABI used by
            // materials.cpp. Substitute only the active world's sky images.
            constexpr unsigned char texture_prefix[] = {0x8B,0xC2,0x4C,0x39,0x84,0xC1,0xB8,0x16,0,0};
            if (std::memcmp(reinterpret_cast<const void*>(0x6B33E0_b),texture_prefix,sizeof(texture_prefix)))
                throw std::runtime_error("Unsupported H1 sky texture binding build");
            sky_texture_hook.create(0x6B33E0_b, sky_texture);
            // R_UpdateSunLight(client, GfxLight*, shadowParams) calls the
            // direction resolver with (frameTime, float[3]); both ABIs verified.
            if (*reinterpret_cast<const std::uint8_t*>(0x1C2852_b) != 0xE8 ||
                *reinterpret_cast<const std::int32_t*>(0x1C2853_b) != static_cast<std::int32_t>(0x1C71B0 - 0x1C2857))
                throw std::runtime_error("Unsupported H1 sun setup build");
            sun_direction_hook.create(0x6503D0_b, sun_direction);
            utils::hook::call(0x1C2852_b, prepare_sun);
            scene_hook.create(0x1C57E0_b, setup_scene);
            dobj_hook.create(0x1BCE00_b, add_dobj);
            utils::hook::call(0x1C5822_b, copy_lights);
        }
    };
}
REGISTER_COMPONENT(theater_scene::component)
