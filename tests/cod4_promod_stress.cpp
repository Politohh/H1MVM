#include "component/demo/cod4_actor_translation.hpp"
#include "component/demo/cod4_action_translation.hpp"
#include "component/demo/cod4_model_translation.hpp"
#include "component/demo/cod4_grenade_events.hpp"
#include "component/demo/cod4_car_motion.hpp"
#include "component/demo/cod4_view_translation.hpp"
#include <chrono>
#include <iostream>
#include <set>
#include <sstream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<typename T, std::size_t N> T field(const std::array<std::uint8_t, N>& value, unsigned offset)
{ T out; std::memcpy(&out, value.data() + offset, sizeof(out)); return out; }

int main(int argc, char** argv)
{
    std::string context;
    try
    {
        check(argc == 3, "Pass Promod demo and independently captured native animation codecs");
        const auto started = std::chrono::steady_clock::now();
        std::ifstream input(argv[1], std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        const auto camera = cod4::decode(bytes);
        const auto players = cod4::decode_players(bytes);
        const auto world = cod4::decode_world(bytes);
        check(players.size() == world.frames.size(), "Player/world baseline handling differs");
        check(camera.map == "mp_crash" && world.client_num == 6, "Wrong stress fixture");
        unsigned bob_carries = 0;
        for (const auto& frame : camera.frames)
        {
            const auto bob = cod4::translate_bob_cycle(frame.bob_cycle);
            check(std::abs(bob / 65535.0 - (frame.bob_cycle & 255) / 255.0) < 1e-12,
                "Recorded camera bob phase differs from COD4's native low-byte consumer");
            bob_carries += frame.bob_cycle > 255;
        }
        check(bob_carries == 20, "Promod predicted bob carry coverage changed");
        check(world.discarded_deltas == 2 && world.frames.size() == 1136, "Promod baseline recovery changed");
        for (const auto& frame : world.frames)
            check(frame.sequence != 4780 && frame.sequence != 4783, "Invented state for a discarded source delta");
        std::vector<std::string> weapons{""};
        std::istringstream names(camera.configstrings.at(2258));
        for (std::string name; names >> name;) weapons.push_back(name);
        std::ifstream table(argv[2]);
        unsigned index; int leg, upper;
        std::map<std::string, unsigned> owned;
        cod4::actor_animation_codec codec;
        for (std::string name; table >> index >> leg >> upper >> name;)
        {
            check(index == codec.legs.size(), "Broken native animation reference");
            codec.legs.push_back(static_cast<std::int16_t>(leg));
            codec.torso.push_back(static_cast<std::int16_t>(upper));
            owned.emplace(name, index);
        }
        check(codec.legs.size() > 1000, "Missing native animation reference");
        std::map<std::string_view, unsigned> catalog;
        for (const auto& [name, id] : owned) catalog.emplace(name, id);
        std::map<int, unsigned> leg_decode, torso_decode;
        for (unsigned i = 0; i < codec.legs.size(); ++i)
        {
            if (codec.legs[i] >= 0) leg_decode.emplace(codec.legs[i], i);
            if (codec.torso[i] >= 0) torso_decode.emplace(codec.torso[i], i);
        }
        const auto events = cod4::collect_actor_events(world);
        const auto gun = cod4::collect_gun_events(players);
        const auto grenades = cod4::collect_grenade_events(world);
        std::map<unsigned, unsigned> counts, clients, teams;
        std::set<unsigned> remote, corpses, pickups, source_clips, native_clips;
        std::map<std::tuple<unsigned, unsigned, unsigned>, unsigned> packed_states;
        std::map<std::string, unsigned> failures;
        std::set<std::array<unsigned, 7>> characters;
        std::set<unsigned> brushes;
        unsigned peak_entities = 0, peak_remote = 0, midpoint_samples = 0, reordered_frames = 0;
        for (std::size_t f = 0; f < world.frames.size(); ++f)
        {
            const auto& frame = world.frames[f];
            check(frame.time == players[f].time && frame.sequence == players[f].sequence, "Source snapshot timelines differ");
            if (f) check(frame.time > world.frames[f-1].time, "Nonmonotonic snapshot chronology");
            unsigned visible_remote = 0;
            std::set<unsigned> native_numbers;
            cod4::entity_snapshot ordered;
            int last_inserted = -1;
            bool reordered = false;
            const auto stage = [&](const cod4::h1_entity& native)
            {
                const auto number = field<std::uint16_t>(native, 0);
                reordered |= number < last_inserted;
                last_inserted = number;
                ordered.add(native);
            };
            for (const auto& [id, client] : frame.clients)
            {
                clients[id] = client.get(4);
                std::array<unsigned, 7> identity{};
                for (unsigned i = 0; i < 7; ++i) identity[i] = client.get(8 + i * 4);
                if (characters.insert(identity).second)
                {
                    std::array<std::string_view, 6> parts{};
                    for (unsigned i = 0; i < 6; ++i) if (identity[i+1]) parts[i] = camera.configstrings.at(830 + identity[i+1]);
                    const auto mapped = cod4::map_character_models(camera.configstrings.at(830 + identity[0]), parts, camera.map);
                    check(!mapped.body.empty(), "Lost recorded character body");
                    std::cout << "CHARACTER " << camera.configstrings.at(830 + identity[0]) << " -> " << mapped.body << '\n';
                }
            }
            const auto& rings = cod4::actor_events_at(events, frame.time).actors;
            for (const auto& [number, source] : frame.entities)
            {
                const auto type = source.get(4);
                context = "time=" + std::to_string(frame.time) + " entity=" + std::to_string(number)
                    + " type=" + std::to_string(type) + " weapon=" + std::to_string(source.get(196))
                    + " item=" + std::to_string(source.get(136));
                ++counts[type];
                const auto target_number = type == 2 ? cod4::actor_entity_number(number) : cod4::translate_entity_number(number);
                check(native_numbers.insert(target_number).second, "Native corpse/entity slot collision");
                if (type == 1 || type == 2)
                {
                    check(frame.clients.contains(source.get(140)), "Actor has no recorded character identity");
                    source_clips.insert(source.get(204) & 511u); source_clips.insert(source.get(208) & 511u);
                    if (type == 1 && number != unsigned(world.client_num)) { remote.insert(number); ++visible_remote; }
                    if (type == 2) corpses.insert(number);
                    try
                    {
                        const auto key = std::tuple{source.get(204), source.get(208), source.get(196)};
                        if (!packed_states.contains(key)) packed_states[key] = cod4::actor_animation(source.get(204), source.get(208), weapons.at(source.get(196)), catalog, codec);
                        const auto packed = packed_states.at(key);
                        const auto legs = (packed >> 11) & 4095u, torso = (packed >> 1) & 1023u;
                        native_clips.insert(leg_decode.at(legs & 2047u)); native_clips.insert(torso_decode.at(torso & 511u));
                        check((legs & 2048u) == ((source.get(204) & 512u) << 2), "Lost recorded animation restart");
                        const auto event = rings.find(number);
                        const auto native = cod4::translate_actor(source, cod4::sample_actor_motion(world, number, frame.time),
                            source.get(196) ? 17u : 0u, packed, event == rings.end() ? nullptr : &event->second);
                        check(native[12] == source.get(140) && native[10] == type, "Lost native actor identity/type");
                        check(field<unsigned>(native, 84) == packed, "Native packed animation offset mismatch");
                        check(field<unsigned>(native, 76) == (source.get(196) ? 17u : 0u), "Lost native actor weapon");
                        if (type == 2)
                            check(bool(field<unsigned>(native, 92) & 0x100000u) == bool(source.get(8) & 0x80000u),
                                "Promod death phase lost its native playback flag");
                        stage(native);
                    }
                    catch (const std::exception& error)
                    {
                        auto message = std::string(error.what());
                        if (message == "Native actor animation has incompatible body channel")
                            message += " legs=" + std::string(cod4::source_body_animations.at(source.get(204) & 511u))
                                + " torso=" + std::string(cod4::source_body_animations.at(source.get(208) & 511u))
                                + " weapon=" + weapons.at(source.get(196));
                        ++failures[message];
                    }
                    if (type == 1 && f + 1 < world.frames.size())
                    {
                        const auto pose = cod4::sample_actor_motion(world, number, (frame.time + world.frames[f+1].time) / 2);
                        for (float v : pose.origin) check(std::isfinite(v), "Invalid interpolated actor position");
                        for (float v : pose.angles) check(std::isfinite(v), "Invalid interpolated actor angle");
                        ++midpoint_samples;
                    }
                }
                else if (type == 3)
                {
                    pickups.insert(number);
                    const auto weapon_index = cod4::item_weapon_index(source);
                    check(!weapons.at(weapon_index).empty(), "Lost pickup weapon name");
                    cod4::map_weapon(weapons.at(weapon_index));
                    const auto native = cod4::translate_item(source, 17, frame.time);
                    check(native[10] == 3 && field<unsigned>(native, 76) == 17, "Lost pickup translation");
                    stage(native);
                }
                else if (type == 4)
                {
                    cod4::map_weapon(weapons.at(source.get(196)));
                    const auto native = cod4::translate_missile(source, 17, frame.time);
                    check(native[10] == 4, "Lost projectile translation");
                    stage(native);
                }
                else if (cod4::script_model_is_brush(source))
                {
                    if (brushes.insert(number).second)
                        std::cout << "UNSUPPORTED_BRUSH entity=" << number << " index=" << source.get(136) << '\n';
                }
                else if (type == 6 && source.get(136))
                {
                    check(camera.configstrings.contains(830 + source.get(136)), "Missing XModel identity");
                    if (source.get(228) || source.get(232) || source.get(236) || source.get(240))
                        check(!cod4::source_bones(camera.configstrings.at(830 + source.get(136))).empty(),
                            "Masked source model has no verified bone profile");
                }
            }
            peak_entities = std::max(peak_entities, static_cast<unsigned>(frame.entities.size()));
            peak_remote = std::max(peak_remote, visible_remote);
            int previous = -1;
            for (const auto& [number, native] : ordered.entities)
            {
                check(static_cast<int>(number) > previous && field<std::uint16_t>(native, 0) == number,
                    "Native entity emission order invalid after corpse-slot remapping");
                previous = static_cast<int>(number);
            }
            reordered_frames += reordered;
        }
        std::cout << "NATIVE_ENTITY_REORDER snapshots=" << reordered_frames << '\n';
        cod4::entity_snapshot mixed_fx;
        for (const auto number : {1050u, 1044u, 60u, 42u, 1u})
        {
            cod4::h1_entity native{};
            cod4::write_native(native, 0, static_cast<std::uint16_t>(number));
            mixed_fx.add(native);
        }
        check(mixed_fx.entities.begin()->first == 1 && mixed_fx.entities.rbegin()->first == 1050,
            "Later grenade identity emitted after a higher car FX identity");
        bool duplicate_rejected = false;
        try { mixed_fx.add(mixed_fx.entities.begin()->second); } catch (const std::exception&) { duplicate_rejected = true; }
        check(duplicate_rejected, "Duplicate native entity identity accepted");
        for (const auto& [id, team] : clients) ++teams[team];
        check(clients.size() == 10 && teams.at(1) == 5 && teams.at(2) == 5 && remote.size() == 9, "5v5 player coverage changed");
        check(counts[1] == 7594 && counts[2] == 2758 && counts[3] == 2281 && counts[4] == 1255,
            "Promod player/corpse/pickup/projectile coverage changed");
        check(events.actions == 333 && gun.events.size() == 33 && grenades.events.size() == 21,
            "Promod native FX event counts changed");
        auto camouflaged = world.frames.front().entities.begin()->second;
        camouflaged.set(4, 3); camouflaged.set(136, 648);
        check(cod4::item_weapon_index(camouflaged) == 8, "Camo bits became a pickup weapon ID");
        camouflaged.set(136, 776);
        check(cod4::item_weapon_index(camouflaged) == 8, "Second camo pickup lost its AK identity");
        camouflaged.set(136, 2048);
        bool rejected = false;
        try { cod4::item_weapon_index(camouflaged); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Out-of-protocol pickup index accepted");
        for (const auto& frame : events.frames) for (const auto& [id, ring] : frame.actors)
            for (const auto& event : ring.ring) check(event[0] == 0 || event[0] == 39 || event[0] == 40 || event[0] == 42 || event[0] == 43, "Unmapped remote gun event");
        for (const auto& event : grenades.events)
        {
            context = "grenade entity=" + std::to_string(event.entity) + " weapon=" + std::to_string(event.weapon);
            check(cod4::grenade_weapon_matches(event.source_event, weapons.at(event.weapon)), "Grenade event weapon mismatch");
            const auto native = cod4::translate_grenade_event(event, 1024, 17);
            check(native[10] != 0, "Lost grenade event");
        }
        for (const auto& player : players) cod4::translate_weapon_state(player.integer("weaponstate"));
        const auto car_fx = cod4::collect_car_fx(world, camera.configstrings);
        const auto car_motion = cod4::collect_car_animations(world, camera.configstrings, car_fx.events);
        check(brushes.contains(365), "Promod inline brush classification changed");
        const auto destroyed_sedan = cod4::source_bones("vehicle_80s_sedan1_red_destroyed");
        check(destroyed_sedan.size() == 41 && destroyed_sedan[17] == "tag_driver",
            "Crash destroyed sedan lost its distinct source bone order");
        // The converter writes at archive frequency, including repeated world
        // snapshots. Exercise every real archive in forward and reverse order;
        // seeking must produce exactly the same native state bytes.
        std::vector<std::uint64_t> hashes;
        unsigned archive_actor_states = 0;
        if (failures.empty())
        {
            const auto sample = [&](int time)
            {
                std::uint64_t hash = 14695981039346656037ull;
                if (time < world.frames.front().time) return hash;
                const auto& frame = world.sample(time);
                const auto& rings = cod4::actor_events_at(events, time).actors;
                for (const auto& [number, source] : frame.entities)
                {
                    if ((source.get(4) != 1 && source.get(4) != 2) || number == unsigned(world.client_num)) continue;
                    const auto ring = rings.find(number);
                    const auto packed = packed_states.at({source.get(204), source.get(208), source.get(196)});
                    const auto native = cod4::translate_actor(source, cod4::sample_actor_motion(world, number, time),
                        source.get(196) ? 17u : 0u, packed, ring == rings.end() ? nullptr : &ring->second);
                    for (auto byte : native) { hash ^= byte; hash *= 1099511628211ull; }
                    ++archive_actor_states;
                }
                return hash;
            };
            for (const auto& frame : camera.frames) hashes.push_back(sample(frame.time));
            for (unsigned pass = 0; pass < 20; ++pass)
                for (std::size_t i = camera.frames.size(); i-- > 0;)
                    check(sample(camera.frames[i].time) == hashes[i], "Seek changed native actor pose/events");
        }
        std::cout << "SUMMARY camera=" << camera.frames.size() << " snapshots=" << world.frames.size()
            << " discarded_delta_packets=" << world.discarded_deltas << " duration_ms=" << camera.frames.back().time-camera.frames.front().time
            << " clients=" << clients.size() << " teams=" << teams[1] << ':' << teams[2]
            << " remote_players=" << remote.size() << " peak_remote=" << peak_remote << " peak_entities=" << peak_entities
            << " corpse_slots=" << corpses.size() << " pickups=" << pickups.size() << " source_clips=" << source_clips.size()
            << " native_clips=" << native_clips.size() << " packed_states=" << packed_states.size()
            << " remote_gun_actions=" << events.actions << " local_gun_actions=" << gun.events.size()
            << " grenades=" << grenades.events.size() << " midpoint_samples=" << midpoint_samples
            << " archive_actor_translations=" << archive_actor_states << " reverse_passes=20"
            << " car_fx=" << car_fx.events.size() << " car_animations=" << car_motion.size()
            << " unsupported_brush_entities=" << brushes.size() << '\n';
        for (const auto& [type, count] : counts) std::cout << "TYPE " << type << " states=" << count << '\n';
        for (const auto& [message, count] : failures) std::cout << "FAIL " << message << " states=" << count << '\n';
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        std::cout << (failures.empty() ? "PASS" : "FAIL") << " Promod stress elapsed_ms=" << elapsed << " mapping_failures=" << failures.size() << '\n';
        return failures.empty() ? 0 : 1;
    }
    catch (const std::exception& error) { std::cerr << context << ": " << error.what() << '\n'; return 1; }
}
