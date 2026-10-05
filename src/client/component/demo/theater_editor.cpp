#include <std_include.hpp>
#include "theater_editor.hpp"
#include "theater_camera.hpp"
#include "theater_ui.hpp"
#include "demo_playback.hpp"
#include "demo_avidemo.hpp"
#include "component/scheduler.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include "utils/io.hpp"
#include <cmath>
#include <iomanip>

namespace theater_editor
{
    namespace
    {
        struct entry { const char* page; const char* name; const char* label; double step; };
        // Deliberate allowlist: names are H1 dvars, not T6's custom z_* hooks.
        // Unregistered, read-only, latched and nonscalar entries are omitted.
        constexpr entry entries[] = {
            {"camera", "demo_camera", "CAMERA MODE", 1},
            {"camera", "demo_camera_speed", "FLY SPEED", 50},
            {"camera", "demo_camera_sens", "MOUSE SENSITIVITY", .005},
            {"camera", "demo_cod4_fov", "COD4 POV FOV", 1},
            {"camera", "demo_camera_boost", "SHIFT SPEED MULTIPLIER", .25},
            {"camera", "demo_camera_slow", "ALT SPEED MULTIPLIER", .05},
            {"camera", "demo_dolly_x", "PATH OFFSET X", 10},
            {"camera", "demo_dolly_y", "PATH OFFSET Y", 10},
            {"camera", "demo_dolly_z", "PATH OFFSET Z", 10},
            {"camera", "demo_dolly_pitch", "PATH PITCH OFFSET", 1},
            {"camera", "demo_dolly_yaw", "PATH YAW OFFSET", 1},
            {"camera", "demo_dolly_roll", "PATH ROLL OFFSET", 1},
            {"misc", "cg_draw2D", "GAME HUD", 1},
            {"misc", "cg_drawGun", "VIEWMODEL", 1},
            {"misc", "fx_enable", "EFFECT SIMULATION", 1},
            {"misc", "fx_draw", "EFFECT DRAWING", 1},
            {"misc", "r_postFX", "POST PROCESSING", 1},
            {"misc", "r_filmUseTweaks", "FILM OVERRIDE", 1},
            {"misc", "r_filmTweakBrightness", "FILM BRIGHTNESS", .05},
            {"misc", "r_filmTweakContrast", "FILM CONTRAST", .05},
            {"misc", "r_filmTweakDesaturation", "FILM DESATURATION", .05},
            {"ssao", "r_ssao", "SSAO MODE", 1},
            {"ssao", "r_ssaoUseTweaks", "SSAO OVERRIDE", 1},
            {"ssao", "r_ssaoStrength", "STRENGTH", .1},
            {"ssao", "r_ssaoWidth", "WIDTH", 1},
            {"ssao", "r_ssaoPower", "POWER", .1},
            {"ssao", "r_ssaoDepthScale", "DEPTH SCALE", .01},
            {"ssao", "r_ssaoFadeDepth", "FADE DEPTH", 50},
            {"ssao", "r_ssaoRejectDepth", "REJECT DEPTH", 50},
            {"ssao", "r_ssaoGradientFalloff", "GRADIENT FALLOFF", .01},
            {"ssao", "r_ssaoGapFalloff", "GAP FALLOFF", .05},
            {"fog", "r_fog", "FOG", 1},
            {"fog", "r_fog_ev_adjust", "FOG EXPOSURE", .1},
            {"fog", "r_fog_depthhack_scale", "VIEWMODEL FOG SCALE", .01},
            {"fog", "r_skyFogUseTweaks", "SKY FOG OVERRIDE", 1},
            {"fog", "r_sky_fog_intensity", "SKY FOG INTENSITY", .05},
            {"fog", "r_sky_fog_min_angle", "SKY FOG MIN ANGLE", 1},
            {"fog", "r_sky_fog_max_angle", "SKY FOG MAX ANGLE", 1},
            {"dof", "r_dof_enable", "DEPTH OF FIELD", 1},
            {"dof", "r_dof_tweak", "DOF OVERRIDE MODE", 1},
            {"dof", "r_dof_physical_enable", "PHYSICAL DOF", 1},
            {"dof", "r_dof_physical_focusDistance", "FOCUS DISTANCE", 25},
            {"dof", "r_dof_physical_fstop", "F-STOP", .125},
            {"dof", "r_dof_physical_filmDiagonal", "FILM DIAGONAL", .5},
            {"dof", "r_dof_physical_bokehEnable", "BOKEH", 1},
            {"dof", "r_dof_physical_bokehPreset", "BOKEH PRESET", 1},
            {"dof", "r_dof_physical_bokehRotation", "BOKEH ROTATION", 1},
            {"dof", "r_dof_physical_bokehSharpness", "BOKEH SHARPNESS", .01},
            {"dof", "r_dof_physical_viewModelFocusDistance", "VIEWMODEL FOCUS", 1},
            {"dof", "r_dof_physical_viewModelFstop", "VIEWMODEL F-STOP", .125},
            {"dof", "r_dof_nearBlur", "NEAR BLUR", .1},
            {"dof", "r_dof_nearStart", "NEAR START", 10},
            {"dof", "r_dof_nearEnd", "NEAR END", 10},
            {"dof", "r_dof_farBlur", "FAR BLUR", .1},
            {"dof", "r_dof_farStart", "FAR START", 50},
            {"dof", "r_dof_farEnd", "FAR END", 50},
            {"dof", "r_dof_bias", "GAUSSIAN BIAS", .05},
            {"sun", "r_drawSun", "SUN FLARE", 1},
            {"sun", "r_sun_from_dvars", "SUN FLARE OVERRIDE", 1},
            {"sun", "r_sunflare_max_alpha", "FLARE ALPHA", .05},
            {"sun", "r_sunflare_min_size", "FLARE MIN SIZE", 1},
            {"sun", "r_sunflare_max_size", "FLARE MAX SIZE", 1},
            {"sun", "r_sunglare_max_lighten", "GLARE", .05},
            {"sun", "r_sunblind_max_darken", "SUN BLINDING", .05},
            {"sun", "sm_sunEnable", "SUN SHADOWS", 1},
            {"sun", "sm_sunFilterRadius", "SUN SHADOW FILTER", .5},
            {"lights", "sm_enable", "SHADOWS", 1},
            {"lights", "sm_spotEnable", "SPOTLIGHT SHADOWS", 1},
            {"lights", "sm_spotFilterRadius", "SPOT SHADOW FILTER", .5},
            {"lights", "r_lightGridEnableTweaks", "LIGHT GRID OVERRIDE", 1},
            {"lights", "r_lightGridIntensity", "LIGHT GRID INTENSITY", .05},
            {"lights", "r_lightGridContrast", "LIGHT GRID CONTRAST", .05},
            {"lights", "r_primaryLightTweakDiffuseStrength", "PRIMARY DIFFUSE", .05},
            {"lights", "r_primaryLightTweakSpecularStrength", "PRIMARY SPECULAR", .05},
            {"streams", "avi_fps", "CAPTURE FPS", 10},
            {"streams", "avi_out_fps", "VIDEO FPS", 1},
            {"streams", "avi_codec", "CODEC", 1},
            {"streams", "avi_crf", "DELIVERY CRF", 1},
            {"streams", "avi_container", "CONTAINER", 1},
            {"streams", "avi_flip", "VERTICAL FLIP", 1},
            {"weapon", "cg_drawGun", "VIEWMODEL", 1},
            {"weapon", "cg_gun_x", "VIEWMODEL OFFSET X", .1},
            {"weapon", "cg_gun_y", "VIEWMODEL OFFSET Y", .1},
            {"weapon", "cg_gun_z", "VIEWMODEL OFFSET Z", .1},
            {"weapon", "cg_gun_rot_p", "VIEWMODEL PITCH", 1},
            {"weapon", "cg_gun_rot_y", "VIEWMODEL YAW", 1},
            {"weapon", "cg_gun_rot_r", "VIEWMODEL ROLL", 1}
        };
        std::mutex mutex;
        nlohmann::json snapshot = {{"revision", 0}, {"controls", nlohmann::json::array()}, {"values", nlohmann::json::object()}};
        std::string catalog_signature, message;
        int revision{};
        std::vector<std::string> custom;

        const entry* find(const std::string& name)
        {
            const auto it = std::find_if(std::begin(entries), std::end(entries), [&](const entry& e) { return e.name == name; });
            return it == std::end(entries) ? nullptr : it;
        }

        std::optional<nlohmann::json> describe(const entry& e)
        {
            auto* d = game::Dvar_FindVar(e.name);
            if (!d || (d->flags & (game::DVAR_FLAG_READ | game::DVAR_FLAG_WRITE | game::DVAR_FLAG_LATCHED))) return {};
            nlohmann::json j = {{"page", e.page}, {"name", e.name}, {"label", e.label}, {"step", e.step}};
            switch (d->type)
            {
            case game::boolean: case game::boolean_hashed:
                j["kind"] = "bool"; j["min"] = 0; j["max"] = 1; break;
            case game::value: case game::value_hashed:
                j["kind"] = "float"; j["min"] = d->domain.value.min; j["max"] = d->domain.value.max; break;
            case game::integer: case game::integer_hashed:
                j["kind"] = "int"; j["min"] = d->domain.integer.min; j["max"] = d->domain.integer.max; break;
            case game::enumeration:
                if (d->domain.enumeration.stringCount < 1 || d->domain.enumeration.stringCount > 64 || !d->domain.enumeration.strings) return {};
                j["kind"] = "enum"; j["min"] = 0; j["max"] = d->domain.enumeration.stringCount - 1;
                j["choices"] = nlohmann::json::array();
                for (int i = 0; i < d->domain.enumeration.stringCount; ++i) j["choices"].push_back(d->domain.enumeration.strings[i]);
                break;
            default: return {};
            }
            // Reject infinite/unusable domains instead of inventing a slider range.
            const double lo = j["min"], hi = j["max"];
            if (!std::isfinite(lo) || !std::isfinite(hi) || lo > hi || std::abs(lo) > 1.e8 || std::abs(hi) > 1.e8) return {};
            if (std::string_view(e.name) == "avi_codec") j["choices"] = {"AV1", "H.265", "H.264", "PRORES 4444"};
            if (std::string_view(e.name) == "avi_container") j["choices"] = {"MP4", "MKV"};
            return j;
        }

        double read_value(game::dvar_t* d)
        {
            // Native string conversion decodes hashed bool/int/float values.
            const std::string value = game::Dvar_ValueToString(d, true, d->current);
            if (d->type == game::enumeration)
            {
                for (int i = 0; i < d->domain.enumeration.stringCount; ++i)
                    if (value == d->domain.enumeration.strings[i]) return i;
                return d->current.integer;
            }
            return std::stod(value);
        }

        bool apply(const std::string& name, double value)
        {
            if (!std::isfinite(value)) return false;
            if (name == "freecam_fov") { theater_camera::set_freecam_fov(static_cast<float>(value)); return true; }
            const auto* e = find(name);
            if (!e) return false;
            const auto j = describe(*e);
            if (!j) return false;
            value = std::clamp(value, j->at("min").get<double>(), j->at("max").get<double>());
            auto* d = game::Dvar_FindVar(name.c_str());
            std::string text;
            if (j->at("kind") == "enum") text = d->domain.enumeration.strings[static_cast<int>(std::round(value))];
            else if (j->at("kind") == "float") { std::ostringstream stream; stream << std::setprecision(9) << value; text = stream.str(); }
            else text = std::to_string(static_cast<int>(std::round(value)));
            game::Dvar_SetFromStringFromSource(d, text.c_str(), game::DVAR_SOURCE_INTERNAL);
            return true;
        }

        std::filesystem::path directory() { return std::filesystem::path("demos") / "theater"; }
        std::filesystem::path preset_file(const std::string& slot)
        {
            if (slot.size() != 1 || slot[0] < '1' || slot[0] > '8') throw std::runtime_error("Choose preset slot 1-8");
            return directory() / ("preset_" + slot + ".cfg");
        }
        std::filesystem::path campath_file()
        { return directory() / (demo_playback::current_path().filename().string() + ".campath.json"); }

        void save_preset(const std::string& slot)
        {
            std::ostringstream cfg;
            cfg << "// H1 theater settings; numeric allowlisted values only\n";
            std::set<std::string> saved;
            for (const auto& e : entries) if (describe(e) && saved.insert(e.name).second)
                cfg << e.name << " " << std::setprecision(9) << read_value(game::Dvar_FindVar(e.name)) << "\n";
            cfg << "freecam_fov " << theater_camera::get_freecam_fov() << "\n";
            for (const auto& name : custom) cfg << "// slider " << name << "\n";
            const auto file = preset_file(slot);
            std::filesystem::create_directories(file.parent_path());
            if (!utils::io::write_file(file.string(), cfg.str())) throw std::runtime_error("Cannot save preset");
            message = "Saved " + file.generic_string();
        }
        void load_preset(const std::string& slot)
        {
            const auto file = preset_file(slot);
            if (!std::filesystem::exists(file) || std::filesystem::file_size(file) > 65536) throw std::runtime_error("Preset file missing or too large");
            std::istringstream cfg(utils::io::read_file(file.string()));
            std::string line;
            std::vector<std::pair<std::string, double>> changes;
            std::vector<std::string> sliders;
            while (std::getline(cfg, line))
            {
                if (line.starts_with("// slider ")) { auto name = line.substr(10); if (find(name) && sliders.size() < 20) sliders.push_back(name); continue; }
                if (line.empty() || line.starts_with("//")) continue;
                std::istringstream tokens(line); std::string name, extra; double value;
                if (!(tokens >> name >> value) || (tokens >> extra) || !std::isfinite(value) || (!find(name) && name != "freecam_fov"))
                    throw std::runtime_error("Invalid preset; no settings were changed");
                changes.emplace_back(name, value);
            }
            for (const auto& [name, value] : changes) (void)apply(name, value);
            custom = std::move(sliders);
            message = "Loaded " + file.generic_string();
        }
        void save_path()
        {
            const auto bounds = demo_playback::time_bounds();
            if (!bounds) throw std::runtime_error("Demo duration is unavailable");
            nlohmann::json j = {{"version", 1}, {"demo", demo_playback::current_path().filename().string()}, {"keys", nlohmann::json::array()}};
            for (const auto& k : theater_camera::get_keyframes()) j["keys"].push_back({{"time", k.demo_time - bounds->first},
                {"position", k.pose.pos}, {"angles", k.pose.angles}, {"fov", k.pose.fov}});
            std::filesystem::create_directories(directory());
            if (!utils::io::write_file(campath_file().string(), j.dump(2))) throw std::runtime_error("Cannot save camera path");
            message = "Saved this demo's camera path";
        }
        void load_path()
        {
            const auto bounds = demo_playback::time_bounds();
            const auto file = campath_file();
            if (!bounds || !std::filesystem::exists(file) || std::filesystem::file_size(file) > 2 * 1024 * 1024)
                throw std::runtime_error("Camera path missing or too large");
            const auto j = nlohmann::json::parse(utils::io::read_file(file.string()));
            if (j.at("version") != 1 || j.at("demo") != demo_playback::current_path().filename().string() ||
                !j.at("keys").is_array() || j.at("keys").size() > 4096) throw std::runtime_error("Invalid camera path");
            std::vector<theater_camera::camera_keyframe_t> keys;
            int last = -50;
            for (const auto& item : j.at("keys"))
            {
                const int t = item.at("time").get<int>();
                if (t < 0 || t > bounds->second - bounds->first || t - last < 50 ||
                    item.at("position").size() != 3 || item.at("angles").size() != 3) throw std::runtime_error("Invalid camera marker");
                theater_camera::camera_pose_t p{};
                for (int axis = 0; axis < 3; ++axis)
                {
                    p.pos[axis] = item.at("position").at(axis).get<float>(); p.angles[axis] = item.at("angles").at(axis).get<float>();
                    if (!std::isfinite(p.pos[axis]) || !std::isfinite(p.angles[axis]) || std::abs(p.pos[axis]) > 1.e7f || std::abs(p.angles[axis]) > 360000.f)
                        throw std::runtime_error("Invalid camera pose");
                }
                p.fov = item.at("fov").get<float>();
                if (!std::isfinite(p.fov) || p.fov < 1 || p.fov > 179) throw std::runtime_error("Invalid camera FOV");
                keys.push_back({bounds->first + t, p}); last = t;
            }
            theater_camera::stop_cinematic(); theater_camera::clear_keyframes();
            for (const auto& key : keys) (void)theater_camera::insert_keyframe(key.demo_time, key.pose);
            message = "Loaded this demo's camera path";
        }
        void run(const std::string& name, const std::string& argument)
        {
            if (name == "save_preset") save_preset(argument);
            else if (name == "load_preset") load_preset(argument);
            else if (name == "save_path") save_path();
            else if (name == "load_path") load_path();
            else if (name == "reset")
            {
                for (const auto& e : entries) if (argument == e.page && describe(e))
                    game::Dvar_Reset(game::Dvar_FindVar(e.name), game::DVAR_SOURCE_INTERNAL);
                if (argument == "camera") theater_camera::set_freecam_fov(90);
                message = "Reset " + argument + " to engine defaults";
            }
            else if (name == "add_slider")
            {
                if (const auto* e = find(argument); e && describe(*e) && custom.size() < 20 && std::find(custom.begin(), custom.end(), argument) == custom.end())
                    custom.push_back(argument);
            }
            else if (name == "remove_slider") std::erase(custom, argument);
            else if (name == "capture_in") { if (const auto t = demo_playback::current_time()) demo_avidemo::set_in_point(*t); }
            else if (name == "capture_out") { if (const auto t = demo_playback::current_time()) demo_avidemo::set_out_point(*t); }
            else if (name == "capture_clear") demo_avidemo::clear_range();
            else if (name == "capture_stop") demo_avidemo::disarm();
            else if (name == "capture_start")
            {
                if (!demo_avidemo::has_range()) throw std::runtime_error("Set In and Out first (Out must be after In)");
                if (!demo_playback::seek_to(demo_avidemo::in_point())) throw std::runtime_error("Cannot seek to capture In");
                if (demo_playback::paused()) demo_playback::toggle_pause();
                if (!demo_avidemo::arm()) throw std::runtime_error("Capture could not start; check ffmpeg and console");
                theater_ui::close();
            }
        }
    }

    void update()
    {
        if (!theater_ui::editor_open()) return;
        nlohmann::json controls = nlohmann::json::array(), values = nlohmann::json::object();
        for (const auto& e : entries) if (auto j = describe(e))
        {
            try { values[e.name] = read_value(game::Dvar_FindVar(e.name)); controls.push_back(*j); }
            catch (const std::exception&) { /* A malformed/unavailable native value is not an editable control. */ }
        }
        controls.push_back({{"page", "camera"}, {"name", "freecam_fov"}, {"label", "FREECAM FOV"},
            {"kind", "float"}, {"min", 1}, {"max", 179}, {"step", 1}});
        values["freecam_fov"] = theater_camera::get_freecam_fov();
        const auto signature = controls.dump() + nlohmann::json(custom).dump();
        if (signature != catalog_signature) { catalog_signature = signature; ++revision; }
        const auto bounds = demo_playback::time_bounds();
        const auto first = bounds ? bounds->first : 0;
        nlohmann::json data = {{"revision", revision}, {"controls", std::move(controls)}, {"values", std::move(values)},
            {"custom", custom}, {"message", message}, {"markers", theater_camera::get_keyframes().size()},
            {"captureIn", demo_avidemo::has_in() ? demo_avidemo::in_point() - first : -1},
            {"captureOut", demo_avidemo::has_out() ? demo_avidemo::out_point() - first : -1},
            {"armed", demo_avidemo::is_armed()}, {"frames", demo_avidemo::captured_frames()}, {"ffmpeg", demo_avidemo::ffmpeg_path()}};
        std::lock_guard lock(mutex); snapshot = std::move(data);
    }
    nlohmann::json state(int cached_revision)
    {
        std::lock_guard lock(mutex); auto copy = snapshot;
        if (cached_revision == copy.value("revision", -1)) { copy.erase("controls"); copy.erase("custom"); }
        return copy;
    }
    bool set(const std::string& name, double value)
    {
        if ((!find(name) && name != "freecam_fov") || !std::isfinite(value) || !demo_playback::is_replay_armed() || !theater_ui::editor_open()) return false;
        const auto session = demo_playback::session_id();
        scheduler::once([name, value, session]
        {
            if (session == demo_playback::session_id() && demo_playback::is_replay_armed() && !demo_avidemo::is_capturing()) (void)apply(name, value);
        }, scheduler::main);
        return true;
    }
    bool action(const std::string& name, const std::string& argument)
    {
        static const std::set<std::string> allowed{"save_preset", "load_preset", "save_path", "load_path", "reset", "add_slider", "remove_slider",
            "capture_in", "capture_out", "capture_clear", "capture_start", "capture_stop"};
        if (!allowed.contains(name) || !demo_playback::is_replay_armed() || !theater_ui::editor_open() || argument.size() > 128) return false;
        const auto session = demo_playback::session_id();
        scheduler::once([name, argument, session]
        {
            if (session != demo_playback::session_id() || !demo_playback::is_replay_armed() || demo_avidemo::is_capturing()) return;
            try { run(name, argument); }
            catch (const std::exception& error) { message = error.what(); console::warn("[theater editor] %s\n", error.what()); }
        }, scheduler::main);
        return true;
    }
}
