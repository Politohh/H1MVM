#include "component/demo/cod4_facial_native.hpp"
#include "component/demo/cod4_facial_calibration.hpp"
#include "component/demo/cod4_facial_catalog.hpp"
#include <rapidjson/document.h>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace cod4::face;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<std::uint8_t> bytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Native facial fixture could not be opened");
    return {(std::istreambuf_iterator<char>(file)), {}};
}
unsigned index(const skeleton& rig, std::string_view name)
{
    const auto found = std::find_if(rig.begin(), rig.end(), [&](const bone& b) { return b.name == name; });
    if (found == rig.end()) throw std::runtime_error("Expected sampled facial joint missing");
    return static_cast<unsigned>(found - rig.begin());
}
int main(int argc, char** argv)
{
    try
    {
        const std::array<std::string, 2> names{"j_head", "jnt_mandible"};
        std::array<std::uint8_t, 1> parents{1};
        const std::array<std::array<std::int16_t, 4>, 1> angles{{{0, 0, -23170, 23170}}};
        const std::array<vec3, 1> positions{{{0, 2, 0}}};
        const auto native = native_rest(names, 1, parents, angles, positions);
        check(native[1].rest.rotation[2] < 0 && native[1].rest.rotation[3] > 0,
            "Native signed four-component quaternion changed");
        const auto neutral = native_animation_pose(native[1].rest, native[1].rest, 2);
        check(neutral.translation == vec3{}, "Native neutral pose doubled model tag translation");
        auto moved = native[1].rest; moved.translation[0] += 4;
        check(native_animation_pose(native[1].rest, moved, 2).translation == vec3{2, 0, 0},
            "Native translation scale ignored");
        bool rejected{}; parents[0] = 0;
        try { native_rest(names, 1, parents, angles, positions); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Native zero parent distance accepted");
        rejected = false;
        try { native_animation_pose({}, {}, 0); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Native zero scale accepted");
        if (argc != 1 && argc != 4) throw std::runtime_error("Usage: test [capture.json source-head-directory source-xanim-directory]");
        if (argc == 4)
        {
            const auto input = bytes(argv[1]); rapidjson::Document capture;
            capture.Parse(reinterpret_cast<const char*>(input.data()), input.size());
            check(!capture.HasParseError() && capture.IsObject() && capture.HasMember("models"), "Invalid native capture JSON");
            const std::array<std::pair<std::string_view, std::string_view>, 5> fixtures{{
                {"head_arab_desert_assault_mp_lobby", "head_mp_arab_regular_suren_surenbody_lod01"},
                {"head_arab_desert_lmg_mp_lobby", "head_mp_arab_regular_asad_asadbody_lod01"},
                {"head_arab_desert_sniper_mp_lobby", "head_mp_arab_regular_sadiq_sadiqbody_lod01"},
                {"head_usmc_desert_lmg_mp_lobby", "head_mp_usmc_shaved_head_lod01"},
                {"head_usmc_desert_smg_mp_lobby", "head_mp_usmc_tactical_mich_stripes_nomex_lod01"}}};
            unsigned checked{};
            for (const auto& model : capture["models"].GetArray())
            {
                const std::string name = model["name"].GetString();
                const auto fixture = std::find_if(fixtures.begin(), fixtures.end(), [&](const auto& f) { return f.first == name; });
                if (fixture == fixtures.end()) continue;
                std::vector<std::string> bone_names; std::vector<std::uint8_t> distances;
                std::vector<std::array<std::int16_t, 4>> packed; std::vector<vec3> translations;
                const unsigned roots = model["roots"].GetUint(); unsigned b{};
                for (const auto& joint : model["bones"].GetArray())
                {
                    bone_names.emplace_back(joint["name"].GetString());
                    if (b >= roots)
                    {
                        const auto parent = joint["parent"].GetUint();
                        check(parent < b && b - parent <= 255, "Invalid captured parent");
                        distances.push_back(static_cast<std::uint8_t>(b - parent));
                        std::array<std::int16_t, 4> q{}; vec3 t{};
                        for (unsigned c = 0; c < 4; ++c) q[c] = static_cast<std::int16_t>(joint["packed_rotation"][c].GetInt());
                        for (unsigned c = 0; c < 3; ++c) t[c] = joint["local_translation"][c].GetFloat();
                        packed.push_back(q); translations.push_back(t);
                    }
                    ++b;
                }
                const auto target = native_rest(bone_names, roots, distances, packed, translations);
                check(target.size() == 89, "Sampled detailed head rig changed");
                const auto bind = rest_model_space(target);
                float max_position_error{};
                for (unsigned i = 0; i < target.size(); ++i)
                {
                    const auto& sampled = model["bones"][i]; float rotation_dot{};
                    for (unsigned c = 0; c < 3; ++c)
                        max_position_error = std::max(max_position_error, std::abs(bind[i].translation[c] - sampled["bind_translation"][c].GetFloat()));
                    for (unsigned c = 0; c < 4; ++c) rotation_dot += bind[i].rotation[c] * sampled["bind_rotation"][c].GetFloat();
                    check(std::abs(rotation_dot) > 0.9999f, "Native rest rotation disagrees with engine bind matrix");
                    check(native_animation_pose(target[i].rest, target[i].rest, 1).translation == vec3{},
                        "Captured head neutral pose creates animation translation");
                }
                check(max_position_error < 0.02f, "Native hierarchy disagrees with engine bind positions");
                const auto source = decode_rest(bytes(std::filesystem::path(argv[2]) / fixture->second));
                // Exercise exactly the mapping used by playback, including
                // centre lips, per-feature proportions and jaw-relative motion.
                const auto calibrated = calibrate_controls(source, target);
                const auto& mapping = calibrated.mapping;
                const bool masked = fixture->second.find("nomex") != std::string_view::npos;
                check(mapping.size() == (masked ? 24u : 48u), "Native region control coverage changed");
                const auto source_bind = rest_model_space(source);
                // Real compiled firing clips encode a small jaw OFFSET, not
                // its approximately one-unit head-relative bind position.
                const auto firing = decode(bytes(std::filesystem::path(argv[3]) / "pt_stand_shoot"));
                const auto jaw_channel = std::find_if(firing.tracks.begin(), firing.tracks.end(),
                    [](const track& t) { return t.bone == "j_jaw"; });
                check(jaw_channel != firing.tracks.end(), "Authored firing jaw track missing");
                const auto& jaw_rest = source[index(source, "j_jaw")].rest;
                const auto jaw_offset = sample(firing, *jaw_channel, 0, {});
                const auto jaw_local = sample_local(firing, *jaw_channel, 0, jaw_rest);
                for (unsigned c = 0; c < 3; ++c)
                    check(std::abs(jaw_local.translation[c] - jaw_rest.translation[c] - jaw_offset.translation[c]) < 1e-5f,
                        "Authored firing translation discarded source bind position");
                const auto width = [](const std::vector<pose>& p, unsigned left, unsigned right)
                {
                    float sum{};
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        const float difference = p[left].translation[c] - p[right].translation[c];
                        sum += difference * difference;
                    }
                    return std::sqrt(sum);
                };
                const float scale = width(bind, index(target, "jnt_eyeball_l"), index(target, "jnt_eyeball_r"))
                    / width(source_bind, index(source, "j_eyeball_le"), index(source, "j_eyeball_ri"));
                check(std::isfinite(scale) && scale > 0 && scale <= 4, "Invalid sampled eye-width calibration");
                check(std::abs(calibrated.translation_scale-scale)<1e-5f,"Runtime and fixture proportion calibration disagree");
                expression_strength disabled; disabled.values.fill(0);
                unsigned clips{};
                for (const auto& file : std::filesystem::directory_iterator(argv[3]))
                {
                    if (!file.is_regular_file() || file.path().filename() == "pb_stand_alert") continue;
                    const auto animation = decode(bytes(file.path()));
                    for (const double fraction : {0.0, 0.25, 0.5, 0.75, 1.0})
                    {
                        const auto output = retarget(source, animation, fraction * animation.frames / animation.fps,
                            index(source, "j_head"), target, index(target, "j_head"), mapping, scale);
                        for (unsigned i = 0; i < output.size(); ++i)
                            (void)native_animation_pose(target[i].rest, output[i], 1);
                        check(output[0].translation == target[0].rest.translation, "Facial transfer moved body attachment root");
                        const auto neutral_output = retarget(source, animation, fraction * animation.frames / animation.fps,
                            index(source, "j_head"), target, index(target, "j_head"), mapping, scale, disabled);
                        for (unsigned i = 0; i < target.size(); ++i)
                        {
                            for (unsigned c = 0; c < 3; ++c)
                                check(std::abs(neutral_output[i].translation[c]-target[i].rest.translation[c])<1e-4f,
                                    "Disabled regional expression changed a captured head bind translation");
                            float dot{};
                            for(unsigned c=0;c<4;++c) dot+=neutral_output[i].rotation[c]*target[i].rest.rotation[c];
                            check(std::abs(dot)>0.99999f,"Disabled regional expression changed a captured head bind rotation");
                        }
                    }
                    ++clips;
                }
                for(const auto required:source_face_clips)
                    check(std::filesystem::is_regular_file(std::filesystem::path(argv[3])/required),
                        "Complete stock facial clip fixture set is missing an authored BG input");
                check(clips >= std::size(source_face_clips)-1, "Complete stock facial clip fixture set changed");
                unsigned source_models{};
                for (const auto& [source_name, parts] : source_face_parts)
                {
                    const auto original = decode_rest(bytes(std::filesystem::path(argv[2])/parts));
                    if (std::none_of(original.begin(),original.end(),[](const bone& b){return facial_bone(b.name);})) continue;
                    const auto controls=calibrate_controls(original,target);
                    check(!controls.mapping.empty(),"Stock masked or complete-body face has no mapped native controls");
                    for (const auto clip_name : {"pt_stand_shoot_auto","pt_crouch_shoot_auto","pt_prone_shoot_auto",
                        "pb_stand_alert","pb_stand_death_frontspin"})
                    {
                        const auto animation=decode(bytes(std::filesystem::path(argv[3])/clip_name));
                        bool deformed{};
                        for(const double fraction : {0.0,0.5,1.0})
                        {
                            const auto output=retarget(original,animation,fraction*animation.frames/animation.fps,
                                index(original,"j_head"),target,index(target,"j_head"),controls.mapping,controls.translation_scale);
                            for(const auto& pair:controls.mapping)
                            {
                                const auto native_pose=native_animation_pose(target[pair.destination].rest,output[pair.destination],1);
                                float dot{};
                                for(unsigned c=0;c<4;++c)dot+=output[pair.destination].rotation[c]*target[pair.destination].rest.rotation[c];
                                deformed |= std::abs(dot)<0.99999f;
                                for(float x:native_pose.translation)deformed |= std::abs(x)>1e-5f;
                            }
                        }
                        check(deformed,"Stock source firing/idle/death face remained entirely neutral after retargeting");
                    }
                    ++source_models;
                }
                check(source_models==28,"Stock source face retarget coverage changed");
                std::cout << "PASS " << name << " bones=" << target.size() << " bind_position_error=" << max_position_error
                    << " eye_width_scale=" << scale << " region_controls=" << mapping.size() << " clips=" << clips
                    << " source_models="<<source_models<<'\n';
                ++checked;
            }
            check(checked == 5, "Expected all five sampled head rigs");
        }
        std::cout << "PASS native parent distances, signed quaternion components, translation offsets and scale rejection\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
