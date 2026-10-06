#include "component/demo/cod4_facial_playback.hpp"
#include "component/demo/cod4_character_catalog.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace cod4::face;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<std::uint8_t> read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return {(std::istreambuf_iterator<char>(file)), {}};
}
int main(int argc, char** argv)
{
    try
    {
        session synthetic;
        synthetic.rigs.push_back({"fixture", {{"j_head", -1, {}}, {"j_jaw", 0, {}}}});
        synthetic.clips.push_back({"shoot", {10, 30, false, {{"j_jaw", {{0, {0,0,0,1}}, {10, {0,0,1,0}}}, {}}}}});
        synthetic.actors[0] = {{100, 300, 100, 0, 0, 1, 0}, {400, 700, 400, 0, 0, 513, 1}};
        const auto encoded = encode_session(synthetic);
        const auto restored = decode_session(encoded);
        check(restored.at(0, 350) == nullptr, "Expression leaked across actor removal");
        check(restored.at(0, 450)->started == 400, "Restart/corpse reuse retained prior phase");
        for (const int time : {150, 550, 250, 550, 150})
        {
            const auto* value = restored.at(0, time);
            const auto pose = sample(restored.clips[0].second, restored.clips[0].second.tracks[0], (time-value->started)/1000.0, {});
            check(std::isfinite(pose.rotation[0]), "Seek produced invalid expression");
        }
        check(restored.at(0, 150)->started == 100 && restored.at(0, 700) == nullptr, "Facial interval bounds changed");
        for (std::size_t n : {std::size_t{0}, encoded.size()-1})
        {
            bool rejected{};
            try { decode_session(std::span(encoded).first(n)); } catch (const std::runtime_error&) { rejected=true; }
            check(rejected, "Truncated facial data accepted");
        }
        auto corrupt = encoded; corrupt.push_back(1);
        bool rejected{};
        try { decode_session(corrupt); } catch (const std::runtime_error&) { rejected=true; }
        check(rejected, "Trailing facial data accepted");
        auto static_pose = synthetic; static_pose.clips[0].second.frames = 0;
        static_pose.clips[0].second.tracks[0].rotations.resize(1);
        check(decode_session(encode_session(static_pose)).clips[0].second.frames == 0,
            "Authored single-frame pose could not survive conversion");
        session transition = restored;
        transition.clips.push_back({"__neutral__", {1,30,false,{}}});
        transition.actors[0] = {{100,300,100,0,0,1,0}, {300,800,300,0,1,2,0}};
        const auto transition_saved = decode_session(encode_session(transition));
        const auto before = sample_expression(transition_saved,0,299,0.2f);
        const auto boundary = sample_expression(transition_saved,0,300,0.2f);
        const auto halfway = sample_expression(transition_saved,0,400,0.2f);
        const auto settled = sample_expression(transition_saved,0,501,0.2f);
        check(before.active && !before.outgoing && boundary.active && boundary.outgoing && boundary.outgoing_weight==1,
            "Shooting-to-idle boundary lost the outgoing pose");
        check(std::abs(halfway.outgoing_weight-0.5f)<1e-5f && !settled.outgoing,
            "Facial release did not reach the midpoint/idle endpoint");
        skeleton target{{"j_head",-1,{}},{"jnt_mandible",0,{}}};
        const std::array<correspondence,1> mapping{{{1,1}}};
        const auto pose_before = retarget_expression(transition_saved,before,299,0,target,0,mapping,1,{});
        const auto pose_boundary = retarget_expression(transition_saved,boundary,300,0,target,0,mapping,1,{});
        const auto pose_halfway = retarget_expression(transition_saved,halfway,400,0,target,0,mapping,1,{});
        const auto pose_idle = retarget_expression(transition_saved,settled,501,0,target,0,mapping,1,{});
        check(pose_before[1].rotation==pose_boundary[1].rotation,"Release snapped at the first idle frame");
        check(pose_halfway[1].rotation==interpolate(target[1].rest.rotation,pose_before[1].rotation,halfway.outgoing_weight),
            "Release advanced the shooting clip instead of fading its last recorded pose");
        check(pose_idle[1].rotation==target[1].rest.rotation,"Release failed to return to the target idle pose");
        for (const int time : {400,450,310,501,400})
        {
            const auto frame = sample_expression(transition_saved,0,time,0.2f);
            const auto output = retarget_expression(transition_saved,frame,time,0,target,0,mapping,1,{});
            if(time==400) check(output[1].rotation==pose_halfway[1].rotation,"Paused/rewound release depended on evaluation history");
        }
        check(!sample_expression(transition_saved,0,350,0).outgoing,"Zero blend duration still released a shooting pose");
        auto rifle_fire = transition_saved; rifle_fire.clips[0].first = "pt_rifle_fire";
        check(sample_expression(rifle_fire,0,400,0.2f).outgoing != nullptr,
            "Rifle-fire names did not use the firing release");
        auto reused = transition_saved; reused.actors[0][1].client=1;
        check(!sample_expression(reused,0,310,0.2f).outgoing,"Release leaked into a different client identity");
        auto death_transition=transition_saved; death_transition.clips[1].first="death_fixture";
        check(!sample_expression(death_transition,0,310,0.2f).outgoing,"Idle release delayed a death expression");
        auto removed=transition_saved; removed.actors[0].pop_back();
        check(!sample_expression(removed,0,310,0.2f).rig_state(),"Release leaked past terminal actor removal");
        auto legacy=transition_saved; legacy.actors[0][1].begin=700; legacy.actors[0][1].started=700;
        check(sample_expression(legacy,0,400,0.2f).outgoing!=nullptr,"Older facial recording lost its bounded release tail");
        check(!sample_expression(legacy,0,501,0.2f).rig_state(),"Legacy release persisted beyond its bounded duration");
        legacy.actors[0][1].client=1;
        check(!sample_expression(legacy,0,400,0.2f).rig_state(),"Legacy gap release crossed client reuse");
        check(argc == 3 || argc == 1, "Usage: test [source.dm_1 raw-directory]");
        if (argc == 3)
        {
            const auto bytes = read(argv[1]); const auto camera = cod4::decode(bytes);
            const auto world = cod4::decode_world(bytes); const auto players = cod4::decode_players(bytes);
            const auto data = build_session(world, players, camera.configstrings, cod4::make_client_slots(world),
                [&](std::string_view category, std::string_view name) { return read(std::filesystem::path(argv[2])/category/name); });
            const auto saved = encode_session(data); const auto reloaded = decode_session(saved);
            // Check every original stock model, including complete-body SAS
            // characters and head variants absent from these two recordings.
            unsigned tested_models{};
            for (const auto& [model, parts] : source_face_parts)
            {
                const auto rig = decode_rest(read(std::filesystem::path(argv[2])/"xmodelparts"/parts));
                if (std::none_of(rig.begin(),rig.end(),[](const bone& b){return facial_bone(b.name);})) continue;
                cod4::world_track fixture; fixture.client_num=0;
                cod4::world_frame frame; frame.time=100;
                cod4::client_state identity; identity.set(8,1); identity.set(12,model.starts_with("head_")?2:0);
                frame.clients.emplace(0,identity);
                cod4::entity_state actor; actor.set(4,1); actor.set(140,0); actor.set(204,185); actor.set(208,315);
                frame.entities.emplace(0,actor); fixture.frames.push_back(frame);
                std::map<int,std::string> config{{830,""},{831,std::string(model)},{832,std::string(model)}};
                auto converted=build_session(fixture,{},config,cod4::make_client_slots(fixture),
                    [&](auto category,auto name){return read(std::filesystem::path(argv[2])/category/name);});
                check(converted.actors.size()==1 && converted.rigs.size()==1
                    && converted.clips[converted.at(0,100)->animation].first=="pt_stand_shoot_auto",
                    "Stock head/complete-body character lost its automatic-fire curves");
                check(decode_session(encode_session(converted)).rigs.front().name==model,
                    "Stock source identity did not survive saved conversion");
                fixture.frames[0].entities.at(0).set(208,0);
                converted=build_session(fixture,{},config,cod4::make_client_slots(fixture),
                    [&](auto category,auto name){return read(std::filesystem::path(argv[2])/category/name);});
                check(converted.clips[converted.at(0,100)->animation].first=="pb_stand_alert",
                    "Root torso discarded the recorded full-body idle expression");
                bool missing_rejected{};
                fixture.frames[0].entities.at(0).set(208,315);
                try { build_session(fixture,{},config,cod4::make_client_slots(fixture),
                    [&](auto category,auto name){return name=="pt_stand_shoot_auto"?std::vector<std::uint8_t>{}
                        :read(std::filesystem::path(argv[2])/category/name);}); }
                catch(const std::runtime_error&){missing_rejected=true;}
                check(missing_rejected,"Missing original automatic-fire asset silently became neutral");
                ++tested_models;
            }
            check(tested_models==28,"Complete stock facial model coverage changed");
            for (const auto& character:cod4::source_characters)
            {
                cod4::world_track fixture; fixture.client_num=0;
                cod4::world_frame frame; frame.time=100;
                cod4::client_state identity; identity.set(8,1); identity.set(12,character.head.empty()?0:2);
                frame.clients.emplace(0,identity);
                cod4::entity_state actor; actor.set(4,1); actor.set(208,315);
                frame.entities.emplace(0,actor); fixture.frames.push_back(frame);
                const std::map<int,std::string> config{{830,""},{831,std::string(character.body)},{832,std::string(character.head)}};
                const auto converted=build_session(fixture,{},config,cod4::make_client_slots(fixture),
                    [&](auto category,auto name){return read(std::filesystem::path(argv[2])/category/name);});
                check(converted.actors.size()==1 && converted.rigs.front().name
                    == (character.head.empty()?character.body:character.head),
                    "Stock character class failed to retain its exact recorded face rig");
            }
            check(!data.actors.empty() && data.clips.size() >= 2, "No sample facial states decoded");
            unsigned intervals{}, death{}, shooting{}, restarts{}, neutral{}, releases{};
            for (const auto& [entity, entries] : data.actors)
                for (const auto& value : entries)
                {
                    const auto* actual = reloaded.at(entity, value.begin);
                    check(actual && actual->started == value.started && actual->animation == value.animation, "Saved facial phase changed");
                    check(reloaded.at(entity, value.end-1) != nullptr, "Saved facial lifetime changed");
                    ++intervals;
                    const auto& name = data.clips[value.animation].first;
                    if (name.find("death") != std::string::npos) ++death;
                    if (firing_expression(name)) ++shooting;
                    if (name=="__neutral__") ++neutral;
                    if (sample_expression(reloaded,entity,value.begin,0.2f).outgoing) ++releases;
                    if (value.token & 512) ++restarts;
                }
            check(shooting > 0, "Sample shooting expression missing");
            check(releases > 0,"Sample release boundaries missing");
            if (std::string(argv[1]).find("explosion") != std::string::npos)
            {
                check(data.rigs.size() == 5 && death > 0, "Multiple-head death sample incomplete");
                for(const unsigned client : {3u,9u})
                {
                    unsigned firing{};
                    for(const auto& [entity,entries]:data.actors) for(const auto& value:entries)
                        if(value.client==client && data.clips[value.animation].first=="pt_stand_shoot_auto") ++firing;
                    check(firing>0,"Arab/USMC LMG client has no saved automatic-fire expression");
                    std::cout<<"PASS LMG client="<<client<<" automatic_fire_intervals="<<firing<<'\n';
                }
            }
            std::cout<<"PASS all "<<tested_models<<" stock facial model bindings and 30 stock character definitions, full-body idle selection and missing-input rejection\n";
            std::cout << "PASS real demo rigs=" << data.rigs.size() << " clips=" << data.clips.size()
                << " actors=" << data.actors.size() << " intervals=" << intervals << " death=" << death
                << " shooting=" << shooting << " neutral=" << neutral << " releases=" << releases
                << " restarted=" << restarts << " saved_bytes=" << saved.size() << '\n';
        }
        std::cout << "PASS bounded facial codec, smooth release boundaries, terminal removal, identity reuse, death priority and stateless pause/rewind\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
