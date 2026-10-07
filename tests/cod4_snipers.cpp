#include "component/demo/cod4_sniper_style.hpp"
#include "component/demo/cod4_sniper_animation.hpp"
#include <nlohmann/json.hpp>
#include "component/demo/cod4_facial_animation.hpp"
#include "component/demo/cod4_facial_native.hpp"
#include <fstream>
#include <iostream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv)
{
    try
    {
        check(argc == 2, "Expected authenticated sniper asset directory");
        check(cod4::sniper::rifle("h1_m40a3_mp") == 0 && cod4::sniper::rifle("h1_m21_mp") == 3,
            "Native rifle identity mismatch");
        check(cod4::sniper::rifle("h1_m14_mp") == -1 && cod4::sniper::rifle("m40a3_mp") == -1,
            "Unrelated weapons selected");
        unsigned count{};
        for (const char* rifle : {"m40a3", "remington", "barrett", "m21", "dragunov"})
        {
            std::array<cod4::face::clip, 2> clips;
            for (int direction = 0; direction < 2; ++direction)
            {
                const auto path = std::string(argv[1]) + "/viewmodel_" + rifle + "_ads_" + (direction ? "down" : "up");
                std::ifstream input(path, std::ios::binary);
                check(bool(input), "Missing original ADS clip");
                std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
                check(cod4::face::decode(bytes).tracks.empty(), "Face decoder started importing viewmodel controls");
                clips[direction] = cod4::face::decode(bytes, true);
                const auto& clip = clips[direction];
                check(clip.tracks.size() == 1 && clip.tracks[0].bone == "tag_ads", "Source ADS controls changed");
                for (float fraction : {0.f, .1f, .5f, .9f, 1.f})
                {
                    const auto seconds = cod4::sniper::clip_seconds(fraction, direction != 0, clip.frames, clip.fps);
                    const auto p = cod4::face::sample(clip, clip.tracks[0], seconds, {});
                    for (float v : p.translation) check(std::isfinite(v), "Nonfinite source pose");
                    // Native hierarchy adds the model's rest translation to
                    // scaled animation offsets. Test nonzero target rests and
                    // scales: raw COD4 absolute positions used to double them.
                    for (float scale : {.5f, 1.f, 2.f})
                    {
                        cod4::face::pose rest;
                        rest.translation = {7.f, -3.f, 11.f};
                        const auto target = cod4::face::native_animation_pose(rest, p, scale);
                        for (unsigned axis = 0; axis < 3; ++axis)
                            check(std::abs(rest.translation[axis] + target.translation[axis] * scale - p.translation[axis]) < .0001f,
                                "ADS target rest position was applied twice");
                    }
                }
                ++count;
            }
            const auto up_end = cod4::face::sample(clips[0], clips[0].tracks[0], double(clips[0].frames) / clips[0].fps, {});
            const auto down_start = cod4::face::sample(clips[1], clips[1].tracks[0], 0, {});
            for (unsigned a = 0; a < 3; ++a) check(std::abs(up_end.translation[a] - down_start.translation[a]) < .02f,
                "Original ADS up/down endpoints do not meet");

        }
        for (float ads : {0.f, .05f, .5f, .7f, .9f, .95f})
            check(cod4::sniper::zoom_fraction(ads) == 0,
                "Scene zoom started before 95 percent ADS");
        check(cod4::sniper::zoom_fraction(.96f) > 0
            && std::abs(cod4::sniper::zoom_fraction(.975f) - .5f) < .00001f,
            "Zoom must occupy only the final 5 percent of ADS on entry and exit");
        check(cod4::sniper::zoom_fraction(1.f) == 1 && cod4::sniper::zoom_fraction(-1.f) == 0,
            "Zoom endpoints are invalid");
        check(cod4::sniper::scope_alpha(.9504f, false) == 0 && cod4::sniper::scope_alpha(.9506f, false) == 1,
            "COD4 reticle handoff threshold changed");
        check(cod4::sniper::scope_alpha(.99999f, true) == 0 && cod4::sniper::scope_alpha(1, true) == 1,
            "Scope exit opacity fade returned");
        for (bool lowering : {false, true})
            for (float ads : {0.f, .9f, .9504f, .9506f, .9999f, 1.f})
                check(cod4::sniper::scope_replaces_gun(ads, lowering) == (cod4::sniper::scope_alpha(ads, lowering) == 1),
                    "3D lens and flat scope overlap during the handoff");
        cod4::sniper::ads_direction direction;
        check(!direction.sample(1, 74, 1000, .9f) && !direction.sample(1, 74, 1004, 1), "ADS entry direction lost");
        check(direction.sample(1, 74, 1008, .999f) && direction.sample(1, 74, 1008, .999f),
            "Repeated paused render brought the scope back on release");
        check(!direction.sample(2, 74, 1008, .96f) && !direction.sample(2, 75, 1012, .96f),
            "New demo/weapon retained stale scope direction");
        check(direction.sample(2, 75, 1016, .9f) && !direction.sample(2, 75, 900, .97f),
            "Backward seek retained stale scope direction");
        cod4::sniper::ads_curve_blend blend;
        check(blend.sample(1000, false, true) == 0 && blend.sample(1010, true, false) == 0,
            "ADS curve reversal jumped at its first frame");
        check(std::abs(blend.sample(1060, true, false) - .5f) < .00001f
            && std::abs(blend.sample(1060, false, false) - .5f) < .00001f,
            "Rapid ADS reversal lost the current blend");
        check(std::abs(blend.sample(1110, false, false) - .25f) < .00001f
            && std::abs(blend.sample(1110, false, false) - .25f) < .00001f,
            "Paused ADS curve blend advanced without playback time");
        check(blend.sample(900, true, true) == 1, "Seek retained an old ADS curve blend");
        const auto asset = [&](const std::string& name)
        {
            std::ifstream input(std::string(argv[1]) + "/" + name, std::ios::binary);
            check(bool(input), "Missing authored sniper action/rig");
            return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(input), {}};
        };
        std::ifstream description(std::string(argv[1]) + "/animations.json");
        const auto manifest = nlohmann::json::parse(description);
        check(manifest.at("version") == 4, "Stale sniper recoil/action descriptor");
        check(manifest.at("rifles").size() == 5, "Missing original rifle kit");
        unsigned actions{}, poses{};
        for (const auto& rifle : manifest.at("rifles"))
        {
            const auto recoil = rifle.at("recoil").get<std::array<float, 28>>();
            if (rifle.at("weapon") == "barrett_mp")
                check(recoil[9] == 80 && recoil[10] == 95 && recoil[13] == 1000
                    && recoil[23] == 80 && recoil[24] == 95 && recoil[27] == 1000,
                    "Original Barrett camera kick disappeared");
            const auto hands = cod4::face::decode_rest(asset(rifle.at("hands")));
            const auto gun = cod4::face::decode_rest(asset(rifle.at("gun")));
            const auto rig = cod4::sniper::combine_rigs(hands, gun);
            std::map<unsigned, std::string> available;
            for (const auto& entry : rifle.at("actions"))
            {
                const auto bytes = asset(entry.at("clip"));
                const auto clip = cod4::face::decode(bytes, true);
                const cod4::sniper::action timed{clip, entry.at("timer_ms"), entry.at("loop"), entry.at("scale_to_timer")};
                if (timed.scale_to_timer && timed.timer_ms > 0)
                    check(std::abs(timed.clip_seconds(timed.timer_ms / 1000.0) - double(clip.frames) / clip.fps) < .00001,
                        "Timed action does not complete at its COD4 weapon duration");
                else check(timed.clip_seconds(.05) == .05, "Natural fire/bolt playback rate changed");
                if (entry.at("logical") == 2 || entry.at("logical") == 7)
                    check(!timed.scale_to_timer, "Fire/rechamber must retain authored rate");
                if (entry.at("logical") == 33) check(timed.scale_to_timer, "Sprint loop must use COD4 weapon rate");
                check(cod4::face::decode(bytes).tracks.empty(), "Sniper tracks entered the face layer");
                check(clip.tracks.size() > 50, "Original action is missing hand/weapon motion");
                available.emplace(entry.at("logical"), entry.at("clip"));
                for (float fraction : {0.f, .25f, .5f, .75f, 1.f})
                {
                    const auto seconds = fraction * clip.frames / clip.fps;
                    const auto retargeted = cod4::sniper::retarget_action(rig, clip, seconds, rig);
                    auto expected = std::vector<cod4::face::pose>{};
                    for (const auto& b : rig) expected.push_back(b.rest);
                    for (const auto& track : clip.tracks)
                    {
                        const auto found = std::find_if(rig.begin(), rig.end(), [&](const auto& b) { return b.name == track.bone; });
                        if (found == rig.end()) continue; // Knife controls are not part of this gun.
                        const auto i = static_cast<unsigned>(found - rig.begin());
                        expected[i] = cod4::sniper::gun_bone(rig, i)
                            ? cod4::face::sample_local(clip, track, seconds, rig[i].rest)
                            : cod4::face::sample(clip, track, seconds, rig[i].rest);
                    }
                    for (unsigned i = 0; i < rig.size(); ++i)
                    {
                        for (unsigned a = 0; a < 3; ++a) check(std::abs(expected[i].translation[a] - retargeted[i].translation[a]) < .005f,
                            "Original action translation changed on identical skeleton");
                        float dot{};
                        for (unsigned a = 0; a < 4; ++a) dot += expected[i].rotation[a] * retargeted[i].rotation[a];
                        check(std::abs(dot) > .9999f, "Original action rotation changed on identical skeleton");
                        ++poses;
                    }
                    // A gun-only DObj part must not receive the arm motion a second
                    // time: its root is already attached to the native tag_weapon.
                    const auto gun_pose = cod4::sniper::retarget_action(gun, clip, seconds, gun);
                    const auto gun_track = std::find_if(clip.tracks.begin(), clip.tracks.end(), [](const auto& t) { return t.bone == "j_gun"; });
                    if (gun_track != clip.tracks.end())
                    {
                        const auto expected_root = cod4::face::sample_local(clip, *gun_track, seconds, gun[0].rest);
                        for (unsigned a = 0; a < 3; ++a) check(std::abs(expected_root.translation[a] - gun_pose[0].translation[a]) < .005f,
                            "Gun attachment movement doubled");
                    }
                }
                ++actions;
            }
            for (unsigned logical : {0u, 3u, 17u, 18u, 21u, 23u, 31u, 33u, 34u, 41u, 42u})
                check(available.contains(logical), "Sniper action coverage incomplete");
            if (rifle.at("weapon") == "m40a3_mp" || rifle.at("weapon") == "remington700_mp")
            {
                for (unsigned logical : {4u, 7u, 22u, 23u}) check(available.contains(logical), "Segmented reload/bolt stage missing");
                check(available.at(7).find("rechamber") != std::string::npos, "Scoped bolt selected a melee animation");
                check(available.at(22).find("reload") != std::string::npos, "Reload start selected a raise animation");
            }
            check(available.at(10).find("melee") != std::string::npos, "Melee action mapping changed");
            if (rifle.at("weapon") == "barrett_mp") check(available.at(5) == "viewmodel_barrett_idle", "COD4 Barrett scoped pose changed");
            if (rifle.at("weapon") == "m21_mp") check(available.at(5) == "viewmodel_m14_fire_ads", "COD4 M21 scoped firing motion changed");
            // Authored gun translations are offsets from their own bind pivot.
            // A near-zero first reload key must not teleport the magazine to
            // j_gun. Check real assets independently of the retarget sampler.
            const bool bolt_action = rifle.at("weapon") == "m40a3_mp" || rifle.at("weapon") == "remington700_mp";
            const auto control = bolt_action ? "j_bolt" : "tag_clip";
            const auto mechanical = cod4::face::decode(asset(available.at(bolt_action ? 7u : 20u)), true);
            const auto index = cod4::sniper::bone_index(gun, control);
            const auto channel = std::find_if(mechanical.tracks.begin(), mechanical.tracks.end(),
                [&](const auto& t) { return t.bone == control; });
            check(channel != mechanical.tracks.end() && !channel->translations.empty(), "Missing mechanical offset channel");
            auto native = gun;
            for (unsigned a = 0; a < 3; ++a) native[index].rest.translation[a] += float(a + 1);
            const auto first = cod4::sniper::retarget_action(gun, mechanical, 0, native);
            const auto first_source = cod4::sniper::retarget_action(gun, mechanical, 0, gun);
            for (unsigned a = 0; a < 3; ++a)
            {
                check(std::abs(channel->translations.front().value[a]) < .1f, "Unexpected first mechanical key");
                check(std::abs(first_source[index].translation[a] - gun[index].rest.translation[a]
                    - channel->translations.front().value[a]) < .005f,
                    "Magazine/bolt first key lost the native bind pivot");
            }
            for (unsigned a = 0; a < 3; ++a) check(std::abs(first[index].translation[a] - native[index].rest.translation[a]
                - channel->translations.front().value[a]) < .005f,
                "Native mechanical pivot was swept around the source pivot");
            if (bolt_action)
            {
                // Real M40/R700 pull-out curves expose the round on a native
                // receiver before snapping back at idle. Exercise the same
                // preparation used by the runtime, including every draw slot.
                cod4::sniper::animation_set kit;
                kit.gun = gun;
                for (const auto& entry : rifle.at("actions"))
                    kit.actions[entry.at("logical").get<unsigned>()].animation =
                        cod4::face::decode(asset(entry.at("clip")), true);
                const auto authored = kit;
                cod4::sniper::stabilize_draw_cartridge(kit);
                const auto bullet = cod4::sniper::bone_index(gun, "bullet_joint");
                const auto idle_pose = cod4::sniper::retarget_gun(gun, kit.actions.at(0).animation, 0, native);
                float exposed{};
                for (const unsigned logical : {18u, 19u, 26u, 28u, 30u, 4u, 7u, 22u})
                {
                    const bool draw = logical == 18 || logical == 19 || logical == 26 || logical == 28 || logical == 30;
                    const auto& c = kit.actions.at(logical).animation;
                    for (unsigned half = 0; half <= c.frames * 2; ++half)
                    {
                        const auto seconds = half / (2.0 * c.fps);
                        const auto before = cod4::sniper::retarget_gun(gun, authored.actions.at(logical).animation, seconds, native);
                        const auto after = cod4::sniper::retarget_gun(gun, c, seconds, native);
                        for (unsigned b = 0; b < native.size(); ++b)
                        {
                            const auto& expected = draw && b == bullet ? idle_pose[b] : before[b];
                            for (unsigned a = 0; a < 3; ++a)
                            {
                                check(std::abs(after[b].translation[a] - expected.translation[a]) < .005f,
                                    "Draw cartridge flashed or unrelated action movement changed");
                                if (draw && b == bullet) exposed = std::max(exposed,
                                    std::abs(before[b].translation[a] - idle_pose[b].translation[a]));
                            }
                            float dot{};
                            for (unsigned a = 0; a < 4; ++a) dot += after[b].rotation[a] * expected.rotation[a];
                            check(std::abs(dot) > .9999f, "Draw cartridge rotation or other action changed");
                        }
                    }
                }
                check(exposed > .3f, "Pull-out fixture no longer reproduces the cartridge offset");
                // The remastered cap is a sibling, not a child, of the bolt.
                native.push_back({"j_bolt_cap", native[index].parent, native[index].rest});
                for (float fraction : {.25f, .5f, .75f})
                {
                    const auto p = cod4::sniper::retarget_action(gun, mechanical, fraction * mechanical.frames / mechanical.fps, native);
                    for (unsigned a = 0; a < 3; ++a) check(std::abs(p[index].translation[a] - p.back().translation[a]) < .005f,
                        "Remastered bolt cap detached from the animated bolt");
                    float dot{};
                    for (unsigned a = 0; a < 4; ++a) dot += p[index].rotation[a] * p.back().rotation[a];
                    check(std::abs(dot) > .9999f, "Remastered bolt cap stopped rotating with the bolt");
                }
            }
        }
        cod4::sniper::action_clock clock;
        // Native viewhands store zero tag positions despite a nonzero geometric
        // bind. The skin bind places tag_view at z=60; that placement must not
        // leak into the camera-relative animation. Preserve actual arm lengths.
        cod4::face::skeleton stripped{{"tag_origin", -1, {}}, {"tag_view", 0, {}},
            {"j_shoulder_le", 1, {}}, {"j_wrist_le", 2, {}}};
        const std::array<cod4::face::pose, 4> bind{{{{0,0,0,1},{0,0,0}},
            {{0,0,0,1},{0,0,60}}, {{0,0,0,1},{2,0,60}}, {{0,0,0,1},{5,0,60}}}};
        const auto geometric = cod4::sniper::geometry_rest(stripped, bind);
        check(geometric[1].rest.translation[2] == 0 && geometric[2].rest.translation[0] == 2
            && geometric[3].rest.translation[0] == 3, "Stripped native viewmodel collapsed its arm lengths");
        const auto offset = cod4::face::native_animation_pose(stripped[3].rest, geometric[3].rest, 1);
        check(offset.translation[0] == 3, "Geometric rest was subtracted instead of encoded tag position");
        // Different rifle origins must place the scope mount at the authored
        // position, including after rotating the weapon during sprint/reload.
        cod4::face::skeleton original_gun{{"j_gun", -1, {}},
            {"tag_m40_scope", 0, {{0,0,0,1},{-6,0,2}}}};
        cod4::face::skeleton remastered_gun{{"j_gun", -1, {}},
            {"tag_m40_scope", 0, {{0,0,0,1},{4,0,4}}}};
        const auto alignment = cod4::sniper::gun_alignment(original_gun, remastered_gun);
        const cod4::face::pose rotated{{0,0,.70710678f,.70710678f},{3,2,1}};
        const auto aligned_mount = cod4::face::compose(cod4::face::compose(rotated, alignment), remastered_gun[1].rest);
        const auto original_mount = cod4::face::compose(rotated, original_gun[1].rest);
        for (unsigned a = 0; a < 3; ++a)
            check(std::abs(aligned_mount.translation[a] - original_mount.translation[a]) < .0001f,
                "Remastered scope offset was not rotated with the authored gun");
        // Differing native attachment offsets must rotate with the rifle.
        // Identical-skeleton tests cannot detect a reversed model-space delta.
        cod4::face::skeleton source_parts{{"j_gun", -1, {}},
            {"tag_clip", 0, {{0,0,0,1},{2,0,1}}}};
        cod4::face::skeleton target_parts{{"j_gun", -1, {}},
            {"tag_clip", 0, {{0,0,0,1},{5,1,3}}}};
        cod4::face::clip turn{10, 10, false, {{"j_gun", {{0,{0,0,0,1}}, {10,{0,0,.70710678f,.70710678f}}},
            {{0,{0,0,0}}, {10,{3,2,1}}}}}};
        const auto turned = cod4::sniper::retarget_action(source_parts, turn, 1, target_parts);
        const auto turned_world = cod4::face::model_space(target_parts, turned);
        const auto attached = cod4::face::compose(turned_world[0], target_parts[1].rest);
        for (unsigned a = 0; a < 3; ++a)
        {
            check(std::abs(turned[1].translation[a] - target_parts[1].rest.translation[a]) < .0001f,
                "Native magazine rest offset moved during a rigid rifle rotation");
            check(std::abs(turned_world[1].translation[a] - attached.translation[a]) < .0001f,
                "Native magazine detached from the rotated rifle");
        }
        // A nonzero mechanical key must add motion to the native bind, not
        // replace it. Keep this nonzero endpoint: reload loops begin with an
        // already-open bolt, so subtracting their first key is also incorrect.
        cod4::face::clip retract{10, 10, false, {{"tag_clip", {}, {{0,{-3,0,0}}, {10,{-1,0,0}}}}}};
        const auto retracted = cod4::sniper::retarget_action(source_parts, retract, 0, target_parts);
        check(std::abs(retracted[1].translation[0] - 2.f) < .0001f,
            "Mechanical offset was replaced or normalized away");
        // Rotate the magazine with zero translation. Its native pivot must
        // stay seated, including when its source/native pivots are different.
        cod4::face::clip spin{10, 10, false, {{"tag_clip", {{0,{0,0,0,1}}, {10,{0,0,.70710678f,.70710678f}}},
            {{0,{0,0,0}}, {10,{0,0,0}}}}}};
        const auto spun = cod4::sniper::retarget_action(source_parts, spin, 1, target_parts);
        for (unsigned a = 0; a < 3; ++a) check(std::abs(spun[1].translation[a] - target_parts[1].rest.translation[a]) < .0001f,
            "Magazine rotation orbited the COD4 pivot instead of the native pivot");
        check(std::abs(spun[1].rotation[2]) > .7f, "Mechanical rotation was suppressed to keep the pivot seated");
        // Simultaneous rifle rotation, part rotation and insertion of a native
        // helper must not change that pivot rule or add the rifle movement twice.
        auto assembled = turn;
        assembled.tracks.push_back(spin.tracks[0]);
        cod4::face::skeleton helper_parts{{"j_gun", -1, {}}, {"mwr_helper", 0, {{0,0,0,1},{3,1,2}}},
            {"tag_clip", 1, {{0,0,0,1},{2,0,1}}}};
        const auto assembled_pose = cod4::sniper::retarget_action(source_parts, assembled, 1, helper_parts);
        const auto assembled_world = cod4::face::model_space(helper_parts, assembled_pose);
        const auto native_bind = cod4::face::rest_model_space(helper_parts);
        const auto seated = cod4::face::compose(assembled_world[0], native_bind[2]);
        for (unsigned a = 0; a < 3; ++a) check(std::abs(assembled_world[2].translation[a] - seated.translation[a]) < .0001f,
            "Inserted helper or rifle rotation displaced the native mechanical pivot");
        // Optical correction must be in tag_ads's parent frame, including a
        // rotated parent. Full ADS centers the native reticle; hip is unchanged.
        cod4::sniper::animation_set optic;
        optic.hands = {{"tag_view", -1, {{.70710678f,0,0,.70710678f},{0,0,0}}},
            {"tag_ads", 0, {}}, {"tag_weapon", 1, {{0,0,0,1},{0,2,3}}}};
        optic.gun = {{"j_gun", -1, {}}, {"tag_m40_scope", 0, {{0,0,0,1},{4,1,2}}},
            {"tag_sight_on", 1, {}}};
        optic.up = {10,10,false, {{"tag_ads", {}, {{0,{0,0,0}}, {10,{2,4,5}}}}}};
        optic.actions[0].animation = {10,10,false,{}};
        const cod4::face::skeleton lens{{"tag_sight_on", -1, {}},
            {"tag_reticle_attach", 0, {{0,0,0,1},{-2,.3f,.7f}}}};
        const auto correction = cod4::sniper::optical_ads_correction(optic, optic.hands, optic.gun, lens);
        const auto optical_point = [&](float ads, bool corrected)
        {
            auto h = cod4::sniper::retarget_action(optic.hands, optic.actions.at(0).animation, 0, optic.hands);
            h[1] = cod4::face::sample(optic.up, optic.up.tracks[0], ads, {});
            if (corrected) for (unsigned a = 0; a < 3; ++a) h[1].translation[a] += correction[a] * ads;
            const auto hw = cod4::face::model_space(optic.hands, h);
            const auto gw = cod4::face::rest_model_space(optic.gun);
            return cod4::face::compose(cod4::face::compose(hw[2], gw[2]), lens[1].rest).translation;
        };
        const auto eye = optical_point(1, true), uncorrected = optical_point(1, false);
        check(std::abs(eye[1]) < .0001f && std::abs(eye[2]) < .0001f,
            "Native optic did not center at the full ADS endpoint");
        check(std::abs(eye[0] - uncorrected[0]) < .0001f && optical_point(0, true) == optical_point(0, false),
            "ADS centering changed optic depth or hip placement");
        // MWR inserts helper bones. Their local bind pose must be preserved
        // while mapped children recover the authored world-space motion.
        cod4::face::skeleton source{{"tag_view", -1, {}}, {"j_shoulder_le", 0, {{0,0,0,1}, {2,0,0}}},
            {"j_wrist_le", 1, {{0,0,0,1}, {3,0,0}}}};
        cod4::face::skeleton target{{"tag_view", -1, {}}, {"j_shoulder_le", 0, {{0,0,0,1}, {2,0,0}}},
            {"mwr_helper", 1, {{0,0,0,1}, {1,0,0}}}, {"j_wrist_le", 2, {{0,0,0,1}, {2,0,0}}}};
        cod4::face::clip motion{10, 10, false, {{"j_shoulder_le", {{0,{0,0,0,1}}, {10,{0,0,.70710678f,.70710678f}}},
            {{0,{2,0,0}}, {10,{2,1,0}}}}}};
        const auto transferred = cod4::sniper::retarget_action(source, motion, .5, target);
        const auto transferred_world = cod4::face::model_space(target, transferred);
        const auto reference_local = cod4::sniper::retarget_action(source, motion, .5, source);
        const auto reference_world = cod4::face::model_space(source, reference_local);
        for (unsigned a = 0; a < 3; ++a)
        {
            check(std::abs(transferred[2].translation[a] - target[2].rest.translation[a]) < .0001f,
                "MWR helper inherited a second animation offset");
            check(std::abs(transferred_world[3].translation[a] - reference_world[2].translation[a]) < .0001f,
                "Inserted MWR helper displaced the authored wrist motion");
        }
        check(std::abs(clock.sample(1, 74, 31, 1000, 300, 300, false, 0)) < .0001, "Sprint entry starts midway");
        check(std::abs(clock.sample(1, 74, 31, 1100, 200, 300, false, 0) - .1) < .0001, "Slow playback clock incorrect");
        check(std::abs(clock.sample(1, 74, 31, 1100, 200, 300, false, 0) - .1) < .0001, "Paused animation advanced");
        check(std::abs(clock.sample(1, 74, 31, 800, 250, 300, false, 0) - .05) < .0001, "Rewind failed to reconstruct action time");
        check(std::abs(clock.sample(1, 74, 31 | 2048, 850, 300, 300, false, 0)) < .0001, "Action toggle did not restart original clip");
        check(std::abs(clock.sample(1, 74, 33, 1300, 0, 700, true, 1000, 300)) < .0001,
            "Sprint loop skipped the beginning by including sprint-in time");
        check(std::abs(clock.sample(1, 74, 33, 1300, 0, 700, true, 1000, 300)) < .0001,
            "Paused sprint loop advanced");
        check(std::abs(clock.sample(1, 74, 33, 2000, 0, 700, true, 1500, 300) - .2) < .0001, "Recorded sprint loop clock lost");
        check(std::abs(clock.sample(1, 74, 33, 1850, 0, 700, true, 1500, 300) - .05) < .0001,
            "Sprint rewind did not reconstruct loop phase");
        check(std::abs(clock.sample(2, 76, 19, 3000, 800, 1130, false, 0) - .33) < .0001, "Session/weapon switch retained old action clock");
        // A detached magazine must keep the authored hand contact through
        // rotation, even when the remastered scope and receiver anchors differ.
        cod4::face::skeleton old_mag{{"j_gun", -1, {}},
            {"tag_clip", 0, {{0,0,0,1},{4,0,2}}},
            {"tag_m82_scope", 0, {{0,0,0,1},{0,0,3}}}};
        auto new_mag = old_mag;
        new_mag[1].rest.translation = {8,0,4};
        new_mag[2].rest.translation = {10,0,8};
        cod4::face::clip reload{10,10,false,{
            {"j_gun", {{0,{0,0,0,1}}, {10,{0,.70710678f,0,.70710678f}}}, {{0,{0,0,0}}, {10,{1,0,2}}}},
            {"tag_clip", {{0,{0,0,0,1}}, {10,{0,0,.70710678f,.70710678f}}}, {{0,{0,0,0}}, {10,{0,-3,0}}}}}};
        for (double time : {0., .25, .5, .75, 1.})
        {
            const auto old_pose = cod4::face::model_space(old_mag,
                cod4::sniper::retarget_action(old_mag, reload, time, old_mag));
            const auto new_local = cod4::sniper::retarget_gun(old_mag, reload, time, new_mag);
            const auto new_pose = cod4::face::model_space(new_mag, new_local);
            for (unsigned a = 0; a < 3; ++a)
                check(std::abs(old_pose[1].translation[a] - new_pose[1].translation[a]) < .0001f,
                    "Detached magazine lost authored hand contact under rotation");
            if (time == 0) for (unsigned a = 0; a < 3; ++a)
                check(std::abs(new_local[1].translation[a] - new_mag[1].rest.translation[a]) < .0001f,
                    "Magazine no longer sits in its native receiver at handoff");
        }
        std::cout << "Validated " << count << " ADS clips, " << actions << " original actions, " << poses
            << " skeletal samples, five rifle kits, native mechanical pivots/helper bones/bolt caps, optical centering,"
            << " draw cartridge continuity, magazine hand contact, recoil profiles, sprint entry/loop clocks, paused/seek clocks,"
            << " 95 percent ADS zoom start, instant scope cut and facial isolation\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
