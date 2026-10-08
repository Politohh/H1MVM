#include <std_include.hpp>
#include "theater_editor.hpp"
#include "theater_camera.hpp"
#include "theater_scene.hpp"
#include "theater_models.hpp"
#include "game/dvars.hpp"
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
        struct entry { std::string page, name, label; double step; int channel{-1}; };
        // Deliberate allowlist: names are H1 dvars, not T6's custom z_* hooks.
        // Unregistered, read-only, latched and nonscalar entries are omitted.
        const entry entries[] = {
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
        std::vector<entry> catalog;
        unsigned catalog_session = ~0u;
        struct color_entry { std::string name, page, label; std::array<std::string, 3> channels; double maximum; };
        std::vector<color_entry> colors;
        struct edit { std::array<double, 3> values; bool color; int serial; unsigned session; };
        std::map<std::string, edit> pending;
        int serial{};
        nlohmann::json acknowledgements = nlohmann::json::object();

        const entry* find(const std::string& name)
        {
            const auto it = std::find_if(catalog.begin(), catalog.end(), [&](const entry& e) { return e.name == name; });
            return it == catalog.end() ? nullptr : &*it;
        }

        std::optional<nlohmann::json> describe(const entry& e)
        {
            auto* d = game::Dvar_FindVar(e.name.substr(0, e.name.find("#")).c_str());
            if (!d || (d->flags & (game::DVAR_FLAG_READ | game::DVAR_FLAG_WRITE | game::DVAR_FLAG_LATCHED))) return {};
            nlohmann::json j = {{"page", e.page}, {"name", e.name}, {"label", e.label}, {"step", e.step}};
            if (e.name == "demo_sun_pitch") j["inverted"] = true;
            switch (d->type)
            {
            case game::boolean: case game::boolean_hashed:
                j["kind"] = "bool"; j["min"] = 0; j["max"] = 1; break;
            case game::value: case game::value_hashed:
                j["kind"] = "float"; j["min"] = d->domain.value.min; j["max"] = d->domain.value.max; break;
            case game::vec2: case game::vec3: case game::vec4: case game::rgb: case game::color:
                if (e.channel < 0) return {};
                j["kind"] = "float"; j["min"] = d->type == game::color ? 0 : d->domain.vector.min;
                j["max"] = d->type == game::color ? 1 : d->domain.vector.max; break;
            case game::integer: case game::integer_hashed:
                j["kind"] = "int"; j["min"] = d->domain.integer.min; j["max"] = d->domain.integer.max; break;
            case game::enumeration:
                if (d->domain.enumeration.stringCount < 1 || d->domain.enumeration.stringCount > 256 || !d->domain.enumeration.strings) return {};
                j["kind"] = "enum"; j["min"] = 0; j["max"] = d->domain.enumeration.stringCount - 1;
                j["choices"] = nlohmann::json::array();
                for (int i = 0; i < d->domain.enumeration.stringCount; ++i) {
                    std::string name=d->domain.enumeration.strings[i];
                    if(e.name=="demo_skybox")
                    {
                        if(name=="Original") name="ORIGINAL MAP SKY";
                        else
                        {
                            for(const auto prefix:{"hdr_sky_h1_","hdr_sky_","sky_"})
                                if(name.starts_with(prefix)) {name.erase(0,std::strlen(prefix));break;}
                            if(name.ends_with("_ft")) name.resize(name.size()-3);
                            std::replace(name.begin(),name.end(),'_',' ');
                            std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
                        }
                    }
                    j["choices"].push_back(name);
                }
                break;
            default: return {};
            }
            // UI ranges do not modify the engine domain. Very broad native
            // domains need a usable editing window instead of silently vanishing.
            double lo = j["min"], hi = j["max"];
            const bool distance = e.name.find("Distance") != std::string::npos || e.name.find("Dist") != std::string::npos ||
                e.name.find("Start") != std::string::npos || e.name.find("End") != std::string::npos || e.name.find("Depth") != std::string::npos;
            const double window = distance ? 100000. : (j["kind"] == "int" ? 256. : 100.);
            if (!std::isfinite(lo) || lo < -1.e8) lo = -window;
            if (!std::isfinite(hi) || hi > 1.e8) hi = window;
            if (lo > hi) return {};
            j["min"] = lo; j["max"] = hi;
            if (std::string_view(e.name) == "avi_codec") j["choices"] = {"AV1", "H.265", "H.264", "PRORES 4444"};
            if (std::string_view(e.name) == "avi_container") j["choices"] = {"MP4", "MKV"};
            return j;
        }

        double read_value(game::dvar_t* d, int channel = -1)
        {
            // Native string conversion decodes hashed bool/int/float values.
            const std::string value = game::Dvar_ValueToString(d, true, d->current);
            if (channel >= 0)
            {
                if (d->type == game::color) return static_cast<unsigned char>(d->current.color[channel]) / 255.;
                return d->current.vector[channel];
            }
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
            auto* d = game::Dvar_FindVar(name.substr(0, name.find("#")).c_str());
            std::string text;
            if (e->channel >= 0)
            {
                const int count = d->type == game::vec2 ? 2 : (d->type == game::vec4 || d->type == game::color ? 4 : 3);
                std::ostringstream stream; stream << std::setprecision(9);
                for (int channel = 0; channel < count; ++channel)
                    stream << (channel ? " " : "") << (channel == e->channel ? value : read_value(d, channel));
                text = stream.str();
            }
            else if (j->at("kind") == "enum") text = d->domain.enumeration.strings[static_cast<int>(std::round(value))];
            else if (j->at("kind") == "float") { std::ostringstream stream; stream << std::setprecision(9) << value; text = stream.str(); }
            else text = std::to_string(static_cast<int>(std::round(value)));
            game::Dvar_SetFromStringFromSource(d, text.c_str(), game::DVAR_SOURCE_INTERNAL);
            return true;
        }

        // Only direct editor changes activate dependencies. Preset load and
        // reset keep the explicitly saved/default override state.
        void activate_override(const std::string& name)
        {
            std::string target;
            if (name.starts_with("demo_sun_") && name != "demo_sun_override") target = "demo_sun_override";
            else if (name.starts_with("demo_light_") && name != "demo_light_index" && name != "demo_light_override") target = "demo_light_override";
            else if (name.starts_with("demo_fog_") && name != "demo_fog_override") target = "demo_fog_override";
            else if (name.starts_with("demo_shake_")) target = "demo_camera_shake";
            else if (name.starts_with("r_sunflare_") || name.starts_with("r_sunglare_") || name.starts_with("r_sunblind_")) target = "r_sun_from_dvars";
            else if (name.starts_with("r_ssao") && name != "r_ssao" && name != "r_ssaoUseTweaks") target = "r_ssaoUseTweaks";
            else if (name.starts_with("r_sky_fog_")) target = "r_skyFogUseTweaks";
            else if (name.starts_with("r_filmTweak")) target = "r_filmUseTweaks";
            else if (name == "r_veilStrength") target = "r_veilUseTweaks";
            else if (name == "r_lightGridIntensity" || name == "r_lightGridContrast") target = "r_lightGridEnableTweaks";
            else if (name.starts_with("r_primaryLightTweak")) target = "r_primaryLightUseTweaks";
            else if (name.starts_with("r_viewModelPrimaryLightTweak")) target = "r_viewModelPrimaryLightUseTweaks";
            else if (name.starts_with("r_dof_") && name != "r_dof_enable" && name != "r_dof_tweak" && name != "r_dof_physical_enable")
            {
                if (auto* d = game::Dvar_FindVar("r_dof_tweak")) game::Dvar_SetFromStringFromSource(d, "On", game::DVAR_SOURCE_INTERNAL);
                return;
            }
            if (!target.empty())
                if (auto* d = game::Dvar_FindVar(target.c_str())) game::Dvar_SetFromStringFromSource(d, "1", game::DVAR_SOURCE_INTERNAL);
        }

        void build_colors()
        {
            colors.clear();
            const color_entry separate[] = {
                {"demo_sun_color","sun","SUN COLOR",{"demo_sun_r","demo_sun_g","demo_sun_b"},1},
                {"demo_light_color","lights","MAP LIGHT COLOR",{"demo_light_r","demo_light_g","demo_light_b"},1},
                {"demo_point_color","lights","POINT COLOR",{"demo_point_r","demo_point_g","demo_point_b"},1},
                {"demo_fog_sun_color","fog","FOG SUN COLOR",{"demo_fog_sun_r","demo_fog_sun_g","demo_fog_sun_b"},1},
                {"demo_fog_haze_color","fog","HAZE COLOR",{"demo_fog_haze_r","demo_fog_haze_g","demo_fog_haze_b"},1}
            };
            auto available = [](const color_entry& c) { return std::all_of(c.channels.begin(), c.channels.end(), [](const auto& name)
                { const auto* e = find(name); return e && describe(*e).has_value(); }); };
            for (const auto& c : separate) if (available(c)) colors.push_back(c);
            for (const auto& e : catalog)
            {
                if (e.channel != 0) continue;
                const auto base = e.name.substr(0, e.name.find('#'));
                auto* d = game::Dvar_FindVar(base.c_str());
                auto lower = base; std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (!d || !(d->type == game::rgb || d->type == game::color ||
                    ((d->type == game::vec3 || d->type == game::vec4) && (lower.find("color") != std::string::npos || lower.find("tint") != std::string::npos)))) continue;
                color_entry c{base,e.page,e.label.substr(0,e.label.size()-2),{base+"#0",base+"#1",base+"#2"},
                    d->type == game::color ? 1. : std::min(100.,static_cast<double>(d->domain.vector.max))};
                if (c.maximum > 0 && available(c)) colors.push_back(std::move(c));
            }
        }

        bool apply_color(const std::string& name, const std::array<double,3>& values)
        {
            const auto it = std::find_if(colors.begin(), colors.end(), [&](const auto& c) { return c.name == name; });
            if (it == colors.end()) return false;
            for (const auto& channel : it->channels) { const auto* e = find(channel); if (!e || !describe(*e)) return false; }
            if (it->channels[0].find('#') != std::string::npos)
            {
                auto* d = game::Dvar_FindVar(name.c_str());
                std::ostringstream text; text << std::setprecision(9);
                for (int i=0;i<3;++i) text << (i ? " " : "") << std::clamp(values[i],0.,it->maximum);
                // Preserve native RGBA alpha or the fourth vector component.
                if (d->type == game::color || d->type == game::vec4) text << " " << read_value(d,3);
                game::Dvar_SetFromStringFromSource(d,text.str().c_str(),game::DVAR_SOURCE_INTERNAL);
            }
            else for (int i=0;i<3;++i) (void)apply(it->channels[i],values[i]);
            activate_override(it->channels[0].substr(0,it->channels[0].find('#')));
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
            auto selected = [](const std::string& name)
            {
                if (std::find(custom.begin(),custom.end(),name) != custom.end()) return true;
                return std::any_of(colors.begin(),colors.end(),[&](const auto& c) { return
                    std::find(custom.begin(),custom.end(),c.name) != custom.end() && std::find(c.channels.begin(),c.channels.end(),name) != c.channels.end(); });
            };
            for (const auto& e : catalog) if (!e.name.starts_with("demo_point_") && (e.page != "available" || selected(e.name)) && describe(e) && saved.insert(e.name).second)
                cfg << e.name << " " << std::setprecision(9) << read_value(game::Dvar_FindVar(e.name.substr(0, e.name.find("#")).c_str()), e.channel) << "\n";
            cfg << "freecam_fov " << theater_camera::get_freecam_fov() << "\n";
            for (const auto& name : custom) cfg << "// slider " << name << "\n";
            cfg << "// lights " << theater_scene::saved_lights().dump() << "\n";
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
            nlohmann::json points = nlohmann::json::array();
            while (std::getline(cfg, line))
            {
                if (line.starts_with("// lights "))
                {
                    points = nlohmann::json::parse(line.substr(10));
                    theater_scene::restore_lights(points, true);
                    continue;
                }
                if (line.starts_with("// slider ")) { auto name = line.substr(10); if ((find(name) || std::any_of(colors.begin(),colors.end(),[&](const auto& c) { return c.name==name; })) && sliders.size() < 20) sliders.push_back(name); continue; }
                if (line.empty() || line.starts_with("//")) continue;
                std::istringstream tokens(line); std::string name, extra; double value;
                if (!(tokens >> name >> value) || (tokens >> extra) || !std::isfinite(value) || (!find(name) && name != "freecam_fov"))
                    throw std::runtime_error("Invalid preset; no settings were changed");
                changes.emplace_back(name, value);
            }
            theater_scene::restore_lights(points);
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
            else if (name == "reset" || name == "reset_all")
            {
                if (name == "reset_all" && theater_camera::is_hud_hidden()) theater_camera::toggle_hud();
                std::set<std::string> restored;
                std::set<std::string> selected(custom.begin(),custom.end());
                for (const auto& color : colors) if (selected.contains(color.name))
                    selected.insert(color.channels.begin(),color.channels.end());
                for (const auto& e : catalog) if ((name == "reset_all" || argument == e.page) && describe(e) &&
                    (e.page != "available" || selected.contains(e.name)))
                {
                    const auto base = e.name.substr(0, e.name.find("#"));
                    if (restored.insert(base).second)
                        game::Dvar_Reset(game::Dvar_FindVar(base.c_str()), game::DVAR_SOURCE_INTERNAL);
                }
                if (name == "reset_all" || argument == "camera") theater_camera::set_freecam_fov(90);
                if (name == "reset_all") { theater_scene::reset_editor(); theater_models::reset(); }
                message = name == "reset_all" ? "Reset all editor settings; dolly markers retained" : "Reset " + argument + " to engine defaults";
            }
            else if (name == "add_slider")
            {
                const auto* e = find(argument);
                const bool available = (e && describe(*e)) || std::any_of(colors.begin(),colors.end(),[&](const auto& c) { return c.name==argument; });
                if (available && custom.size() < 20 && std::find(custom.begin(), custom.end(), argument) == custom.end())
                    custom.push_back(argument);
            }
            else if (name == "remove_slider") std::erase(custom, argument);
            else if (theater_models::action(name, argument)) message = "";
            else if (theater_scene::action(name)) message = "";
            else if (name == "toggle_hud") theater_camera::toggle_hud();
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
        theater_models::update();
        { std::lock_guard lock(mutex); if (!theater_ui::editor_open() && pending.empty()) return; }
        if (catalog.empty() || catalog_session != demo_playback::session_id())
        {
            catalog.assign(std::begin(entries), std::end(entries));
            for (const auto& e : theater_scene::settings()) catalog.push_back({e.page,e.name,e.label,e.step});
            const entry extras[] = {
                {"sun","r_hdrSkyIntensity","SKY INTENSITY",.1}, {"sun","r_hdrSkyColorTint","SKY TINT",.01},
                {"lights","r_primaryLightUseTweaks","PRIMARY LIGHT OVERRIDE",1},
                {"lights","r_viewModelPrimaryLightUseTweaks","VIEWMODEL LIGHT OVERRIDE",1},
                {"lights","r_viewModelPrimaryLightTweakDiffuseStrength","VIEWMODEL DIFFUSE",.05},
                {"lights","r_viewModelPrimaryLightTweakSpecularStrength","VIEWMODEL SPECULAR",.05},
                {"lights","r_charLightAmbient","CHARACTER AMBIENT",.05},
                {"misc","r_veil","BLOOM / VEIL",1}, {"misc","r_veilUseTweaks","BLOOM OVERRIDE",1},
                {"misc","r_veilStrength","BLOOM STRENGTH",.05}, {"misc","r_znear","NEAR CLIP",.1},
                {"weapon","cg_gun_move_f","FORWARD SWAY",.01},{"weapon","cg_gun_move_r","SIDE SWAY",.01},
                {"weapon","cg_gun_move_u","VERTICAL SWAY",.01},
                {"weapon","cg_gun_ofs_f","BASE FORWARD OFFSET",.1},{"weapon","cg_gun_ofs_r","BASE SIDE OFFSET",.1},
                {"weapon","cg_gun_ofs_u","BASE VERTICAL OFFSET",.1},
                {"weapon","cg_gun_rot_minspeed","ROTATION MIN SPEED",.1},
                {"weapon","cg_gun_rot_pitch","SWAY PITCH",.1},{"weapon","cg_gun_rot_yaw","SWAY YAW",.1},
                {"weapon","cg_gun_rot_roll","SWAY ROLL",.1},
                {"dof","r_dof_viewModelStart","VIEWMODEL NEAR START",1},{"dof","r_dof_viewModelEnd","VIEWMODEL NEAR END",1}
            };
            catalog.insert(catalog.end(), std::begin(extras), std::end(extras));
            for (const auto& [hash, info] : dvars::dvar_map)
            {
                if (!(info.name.starts_with("r_") || info.name.starts_with("cg_") || info.name.starts_with("fx_")) || find(info.name)) continue;
                auto* d = game::Dvar_FindVar(info.name.c_str());
                if (d && (d->type == game::value || d->type == game::value_hashed || d->type == game::vec2 || d->type == game::vec3 || d->type == game::vec4 || d->type == game::rgb || d->type == game::color))
                    catalog.push_back({"available",info.name,info.name,.01});
            }
            std::vector<entry> expanded;
            for (const auto& e : catalog)
            {
                auto* d = game::Dvar_FindVar(e.name.c_str());
                const int count = !d ? 0 : d->type == game::vec2 ? 2 : d->type == game::vec3 || d->type == game::rgb ? 3 : d->type == game::vec4 || d->type == game::color ? 4 : 0;
                if (!count) expanded.push_back(e);
                else for (int i=0;i<count;++i) expanded.push_back({e.page,e.name+"#"+std::to_string(i),e.label+" "+("XYZW"[i]),e.step,i});
            }
            catalog = std::move(expanded); catalog_session = demo_playback::session_id();
            build_colors();
        }
        // Dragging may emit hundreds of native steps. Apply only the latest
        // value per control in this tick, then acknowledge it in the snapshot.
        std::map<std::string, edit> edits;
        { std::lock_guard lock(mutex); edits.swap(pending); }
        std::vector<std::pair<std::string, edit>> ordered(edits.begin(),edits.end());
        std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b) { return a.second.serial < b.second.serial; });
        for (const auto& [name, change] : ordered)
        {
            bool applied = false;
            if (change.session == demo_playback::session_id() && demo_playback::is_replay_armed() && !demo_avidemo::is_capturing())
            {
                applied = change.color ? apply_color(name,change.values) : apply(name,change.values[0]);
                if (applied && !change.color)
                {
                    activate_override(name.substr(0,name.find('#')));
                    theater_scene::setting_changed(name);
                }
            }
            acknowledgements[name] = change.serial;
            if (!applied) message = "Cannot apply " + name + " in the current demo state";
            else message.clear();
        }
        nlohmann::json controls = nlohmann::json::array(), values = nlohmann::json::object();
        std::set<std::string> color_channels;
        for (const auto& c : colors) for (const auto& channel : c.channels) color_channels.insert(channel);
        for (const auto& e : catalog) if (auto j = describe(e))
        {
            try { values[e.name] = read_value(game::Dvar_FindVar(e.name.substr(0, e.name.find("#")).c_str()), e.channel);
                if (!color_channels.contains(e.name)) { if (e.channel == 3) (*j)["label"] = e.label.substr(0,e.label.size()-1)+"ALPHA"; controls.push_back(*j); } }
            catch (const std::exception&) { /* A malformed/unavailable native value is not an editable control. */ }
        }
        for (const auto& c : colors)
        {
            values[c.name] = {values[c.channels[0]],values[c.channels[1]],values[c.channels[2]]};
            controls.push_back({{"page",c.page},{"name",c.name},{"label",c.label},{"kind","color"},{"min",0},{"max",c.maximum},{"channels",c.channels}});
        }
        controls.push_back({{"page", "camera"}, {"name", "freecam_fov"}, {"label", "FREECAM FOV"},
            {"kind", "float"}, {"min", 1}, {"max", 179}, {"step", 1}});
        values["freecam_fov"] = theater_camera::get_freecam_fov();
        const auto models = theater_models::state();
        const auto signature = controls.dump() + nlohmann::json(custom).dump() + std::to_string(models.value("modelCatalogRevision", 0));
        if (signature != catalog_signature) { catalog_signature = signature; ++revision; }
        const auto bounds = demo_playback::time_bounds();
        const auto first = bounds ? bounds->first : 0;
        nlohmann::json data = {{"revision", revision}, {"controls", std::move(controls)}, {"values", std::move(values)},
            {"custom", custom}, {"acks", acknowledgements}, {"session", demo_playback::session_id()}, {"message", message}, {"markers", theater_camera::get_keyframes().size()},
            {"captureIn", demo_avidemo::has_in() ? demo_avidemo::in_point() - first : -1},
            {"captureOut", demo_avidemo::has_out() ? demo_avidemo::out_point() - first : -1},
            {"armed", demo_avidemo::is_armed()}, {"frames", demo_avidemo::captured_frames()}, {"ffmpeg", demo_avidemo::ffmpeg_path()}};
        data.update(theater_scene::state());
        data.update(models);
        std::lock_guard lock(mutex); snapshot = std::move(data);
    }
    nlohmann::json state(int cached_revision)
    {
        std::lock_guard lock(mutex); auto copy = snapshot;
        if (cached_revision == copy.value("revision", -1)) { copy.erase("controls"); copy.erase("custom"); }
        return copy;
    }
    int queue_edit(const std::string& name, const std::array<double,3>& values, bool color)
    {
        if (name.empty() || name.size() > 128 || !std::all_of(values.begin(),values.end(),[](double v) { return std::isfinite(v); }) ||
            !demo_playback::is_replay_armed() || !theater_ui::editor_open() || demo_avidemo::is_capturing()) return 0;
        std::lock_guard lock(mutex);
        if (pending.size() >= 64 && !pending.contains(name)) return 0;
        // A signed serial remains exactly representable by H1's Lua number.
        if (serial == INT_MAX) return 0;
        const auto id = ++serial;
        pending[name] = {values,color,id,demo_playback::session_id()};
        return id;
    }
    int set(const std::string& name, double value) { return queue_edit(name,{value,0,0},false); }
    int set_color(const std::string& name, double red, double green, double blue) { return queue_edit(name,{red,green,blue},true); }
    bool action(const std::string& name, const std::string& argument)
    {
        static const std::set<std::string> allowed{"save_preset", "load_preset", "save_path", "load_path", "reset", "reset_all", "add_slider", "remove_slider",
            "capture_in", "capture_out", "capture_clear", "capture_start", "capture_stop", "toggle_hud",
            "bone_attach", "bone_detach", "bone_next", "point_add", "point_next", "point_remove", "point_clear", "fog_sample",
            "model_select", "model_head", "model_body", "model_restore", "model_reset_all", "model_focus", "model_save", "model_load"};
        if (!allowed.contains(name) || !demo_playback::is_replay_armed() || !theater_ui::editor_open() || argument.size() > 128) return false;
        const auto session = demo_playback::session_id();
        scheduler::once([name, argument, session]
        {
            if (session != demo_playback::session_id() || !demo_playback::is_replay_armed() || (demo_avidemo::is_capturing() && name != "capture_stop")) return;
            // Edits queued before Reset/Load cannot reapply after the action.
            // Acknowledge discarded requests so optimistic Lua values release.
            if (name == "reset" || name == "reset_all" || name == "load_preset")
            {
                std::lock_guard lock(mutex);
                for (const auto& [setting, change] : pending) acknowledgements[setting] = change.serial;
                pending.clear();
            }
            try { run(name, argument); }
            catch (const std::exception& error) { message = error.what(); console::warn("[theater editor] %s\n", error.what()); }
        }, scheduler::main);
        return true;
    }
}
