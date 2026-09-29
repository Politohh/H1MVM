#include "component/demo/cod4_actor_translation.hpp"
#include <iostream>
#include <set>
#include <sstream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<typename T> T field(const cod4::h1_entity& value, unsigned offset)
{ T out; std::memcpy(&out, value.data() + offset, sizeof(out)); return out; }
int main(int argc, char** argv)
{
    try
    {
        check(argc == 3, "Pass City demo and native animation codecs");
        std::ifstream input(argv[1], std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        const auto world = cod4::decode_world(bytes);
        const auto camera = cod4::decode(bytes);
        std::vector<std::string> weapons{""};
        std::istringstream names(camera.configstrings.at(2258));
        for (std::string name; names >> name;) weapons.push_back(name);
        std::ifstream table(argv[2]);
        unsigned index; int leg, upper;
        std::map<std::string, unsigned> owned;
        cod4::actor_animation_codec codec;
        for (std::string name; table >> index >> leg >> upper >> name;)
        {
            check(index == codec.legs.size(), "Broken native animation codec fixture");
            codec.legs.push_back(static_cast<std::int16_t>(leg));
            codec.torso.push_back(static_cast<std::int16_t>(upper));
            owned.emplace(name, index);
        }
        std::map<std::string_view, unsigned> catalog;
        for (const auto& [name, id] : owned) catalog.emplace(name, id);
        std::map<int, unsigned> leg_decode, torso_decode;
        for (unsigned i = 0; i < codec.legs.size(); ++i)
        {
            if (codec.legs[i] >= 0) leg_decode.emplace(codec.legs[i], i);
            if (codec.torso[i] >= 0) torso_decode.emplace(codec.torso[i], i);
        }
        const auto events = cod4::collect_actor_events(world);
        unsigned actors = 0, corpses = 0, items = 0, death_frames = 0, settled_frames = 0;
        std::set<unsigned> identities, clips;
        for (const auto& frame : world.frames)
        {
            std::set<unsigned> native_numbers;
            for (const auto& [number, source] : frame.entities)
            {
                if (source.get(4) == 1 || source.get(4) == 2)
                {
                    const auto packed = cod4::actor_animation(source.get(204), source.get(208), weapons.at(source.get(196)), catalog, codec);
                    const auto legs = (packed >> 11) & 4095u, torso = (packed >> 1) & 1023u;
                    clips.insert(leg_decode.at(legs & 2047u)); clips.insert(torso_decode.at(torso & 511u));
                    check((legs & 2048u) == ((source.get(204) & 512u) << 2), "Lost actor leg restart");
                    const auto& ring = cod4::actor_events_at(events, frame.time).actors;
                    const auto event = ring.find(number);
                    const auto native = cod4::translate_actor(source, cod4::sample_actor_motion(world, number, frame.time),
                        source.get(196) ? 17u : 0u, packed, event == ring.end() ? nullptr : &event->second);
                    check(native[12] == source.get(140), "Lost character identity");
                    check(field<unsigned>(native, 84) == packed, "Native animation wire offset mismatch");
                    check(native_numbers.insert(field<std::uint16_t>(native, 0)).second, "Actor number collision");
                    if (source.get(4) == 1)
                    {
                        ++actors; identities.insert(number);
                        check(native[10] == 1 && field<unsigned>(native, 76) == 17, "Lost remote weapon");
                    }
                    else
                    {
                        ++corpses;
                        check(native[10] == 2 && field<std::uint16_t>(native, 0) >= 42
                            && field<std::uint16_t>(native, 0) < 50, "Corpse outside native range");
                        check(field<unsigned>(native, 96) == (source.get(12) >= 9 ? source.get(12) + 3 : source.get(12)), "Lost native corpse physics");
                        const bool play_death = source.get(8) & 0x80000u;
                        check(bool(field<unsigned>(native, 92) & 0x100000u) == play_death,
                            "Corpse skipped its death clip or restarted a settled death");
                        if (play_death) ++death_frames; else ++settled_frames;
                    }
                }
                else if (source.get(4) == 3)
                {
                    check(native_numbers.insert(static_cast<std::uint16_t>(number)).second, "Pickup collided with corpse");
                    check(!weapons.at(source.get(136)).empty(), "Pickup has no recorded weapon identity");
                    const auto native = cod4::translate_item(source, 17, frame.time);
                    check(native[10] == 3 && field<unsigned>(native, 76) == 17, "Wrong pickup world weapon");
                    ++items;
                }
                else check(native_numbers.insert(static_cast<std::uint16_t>(number)).second, "Corpse collided with world entity");
            }
        }
        check(actors == 814 && corpses == 969 && items == 662, "City actor coverage changed");
        check(identities == std::set<unsigned>{1}, "Friend identity changed");
        check(events.actions == 13, "City friend gun action count changed");
        check(death_frames && settled_frames, "City fixture lost death-to-ragdoll coverage");
        check(cod4::actor_flags(0xA0002) == 0x140002, "New corpse cannot play its native death clip");
        check(cod4::actor_flags(0x20002) == 0x40002, "Settled corpse replays its death clip");
        check(cod4::actor_flags(0x220002) == 0x40002, "COD4 voice icon incorrectly enables death playback");
        check(cod4::actor_flags(0x40040) == 0x80040, "Friend ADS/firing flags not translated");
        // Smooth the recorded 20 Hz actor snapshots without blending teleports,
        // visibility removals, or a different player into the same entity slot.
        cod4::world_track motion;
        for (unsigned i = 0; i < 2; ++i)
        {
            cod4::world_frame frame;
            frame.time = static_cast<int>(1000 + i * 50);
            cod4::entity_state actor;
            actor.set(0, 1); actor.set(4, 1); actor.set(140, 1);
            actor.set(24, std::bit_cast<unsigned>(static_cast<float>(i * 10)));
            actor.set(64, std::bit_cast<unsigned>(i ? 1.0f : 359.0f));
            frame.entities.emplace(1, actor);
            motion.frames.push_back(frame);
        }
        const auto midpoint = cod4::sample_actor_motion(motion, 1, 1025);
        check(midpoint.origin[0] == 5 && midpoint.angles[1] == 360, "Actor interpolation or yaw seam failed");
        motion.frames.back().entities.at(1).set(8, 2);
        check(cod4::sample_actor_motion(motion, 1, 1025).origin[0] == 0, "Interpolated a teleport");
        motion.frames.back().entities.clear();
        check(cod4::sample_actor_motion(motion, 1, 1025).origin[0] == 0, "Interpolated across actor removal");
        cod4::world_track shots;
        for (unsigned i = 0; i <= 301; ++i)
        {
            cod4::world_frame frame;
            frame.time = static_cast<int>(i * 50);
            cod4::entity_state actor;
            actor.set(0, 1); actor.set(4, 1); actor.set(140, 1); actor.set(196, 16);
            actor.set(160, i & 255u);
            for (unsigned slot = 0; slot < 4; ++slot) actor.set(164 + slot * 4, 26);
            frame.entities.emplace(1, actor);
            shots.frames.push_back(frame);
        }
        auto firing = cod4::collect_actor_events(shots);
        check(firing.actions == 301 && firing.frames.back().actors.at(1).sequence == 45, "Actor event counter wrap failed");
        auto absent = shots.frames.back(); absent.time += 50; absent.entities.clear(); shots.frames.push_back(absent);
        auto respawn = shots.frames.front(); respawn.time = absent.time + 50; shots.frames.push_back(respawn);
        firing = cod4::collect_actor_events(shots);
        const auto& respawn_ring = firing.frames.back().actors.at(1);
        check(firing.actions == 301 && respawn_ring.sequence == 45, "Respawn replayed source event history");
        check(respawn_ring.ring == std::array<std::array<unsigned, 2>, 4>{}, "Respawn retained stale native shots");
        // Independent native decoding preserves the packed restart bits and IDs.
        check(codec.pack(2048 | owned.at("mp_stand_idle"), 2048 | owned.at("mp_stand_reload"))
            == (((static_cast<unsigned>(codec.legs[owned.at("mp_stand_idle")]) | 2048) << 11)
                | ((static_cast<unsigned>(codec.torso[owned.at("mp_stand_reload")]) | 512) << 1)), "Packed animation restart overlap");
        for (const auto& frame : events.frames) for (const auto& [id, state] : frame.actors)
            for (const auto& event : state.ring) check(event[0] == 0 || event[0] == 39 || event[0] == 40 || event[0] == 42 || event[0] == 43, "Copied unrelated actor event ordinal");
        std::cout << "PASS City actors=" << actors << " corpses=" << corpses << " items=" << items
            << " clips=" << clips.size() << " remote_gun_actions=" << events.actions << '\n';
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
