#include "component/demo/cod4_client_slots.hpp"
#include "component/demo/cod4_replay_agents.hpp"
#include "component/demo/cod4_gun_events.hpp"
#include "component/demo/cod4_obituary_events.hpp"
#include <iostream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

void reject_all(std::span<const std::uint8_t> bytes)
{
    for (int decoder = 0; decoder < 3; ++decoder)
    {
        bool rejected = false;
        try
        {
            if (decoder == 0) (void)cod4::decode(bytes);
            else if (decoder == 1) (void)cod4::decode_players(bytes);
            else (void)cod4::decode_world(bytes);
        }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Malformed or unsupported protocol recording accepted");
    }
}

int main(int argc, char** argv)
{
    try
    {
        check(argc == 2, "Pass 60_AMBUSH_ACOG_C4S.dm_1");
        std::ifstream input(argv[1], std::ios::binary);
        check(input.good(), "Missing protocol-19 fixture");
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        const auto camera = cod4::decode(bytes);
        const auto players = cod4::decode_players(bytes);
        const auto world = cod4::decode_world(bytes);
        check(camera.protocol == 19 && camera.map == "mp_convoy" && camera.configstrings.size() == 3712
            && camera.configstrings.at(2) == "cod19 ", "Protocol-19 gamestate changed");
        check(camera.packets == 317 && camera.archives == 1667 && camera.replaced_frames == 243
            && camera.frames.size() == 1424 && camera.frames.front().time == 245599170
            && camera.frames.back().time == 245619372, "Protocol-19 camera chronology changed");
        check(players.size() == 316 && world.frames.size() == players.size() && world.baselines.size() == 82
            && world.client_num == 27 && world.discarded_deltas == 0
            && players.front().time == 245602400 && players.back().time == 245619450,
            "Protocol-19 snapshot framing changed");
        check(cod4::collect_obituary_events(world).size() == 47
            && cod4::collect_gun_events(players).events.size() == 15, "Protocol-19 recorded events lost");

        std::set<unsigned> identities;
        std::size_t fractional_origins = 0, peak_identities = 0;
        for (std::size_t i = 0; i < players.size(); ++i)
        {
            const auto& frame = world.frames[i];
            check(frame.time == players[i].time, "Player and world timelines diverged");
            check(frame.client_names.at(27) == "dutchchedda", "Recorded POV name changed");
            std::set<unsigned> current;
            for (const auto& [number, client] : frame.clients)
                if (client.get(8)) { identities.insert(number); current.insert(number); }
            for (const auto& [number, entity] : frame.entities)
            {
                if (entity.get(4) != 1 && entity.get(4) != 2) continue;
                identities.insert(entity.get(140)); current.insert(entity.get(140));
                for (unsigned offset : {24u, 28u, 32u})
                {
                    const auto value = std::bit_cast<float>(entity.get(offset));
                    check(std::isfinite(value) && std::abs(value) < 10000000, "Invalid recorded actor origin");
                    if (value != std::trunc(value)) ++fractional_origins;
                }
            }
            peak_identities = std::max(peak_identities, current.size());
        }
        check(identities.size() == 39 && peak_identities == 39 && fractional_origins > 100,
            "Recorded actor identity or fractional precision lost");

        const auto slots = cod4::make_client_slots(world);
        std::set<unsigned> destinations;
        unsigned native_players = 0, native_agents = 0;
        for (const auto& [source, destination] : slots.slots)
        {
            check(destination < 42 && destinations.insert(destination).second, "Recorded identity collision");
            if (cod4::agent_slot(destination)) ++native_agents; else ++native_players;
        }
        check(native_players == 18 && native_agents == 21 && slots.client(27) == 0,
            "39-player native player/agent partition changed");
        const auto names = cod4::collect_agent_names(world, slots);
        check(names.size() >= 21, "Native agents lost their recorded player names");
        for (const auto& frame : world.frames)
            for (const auto& [number, client] : frame.clients)
            {
                if (!client.get(8) || !cod4::agent_slot(slots.client(number))) continue;
                const auto name = cod4::recorded_actor_name(client, frame.client_names.at(number));
                const auto agent = cod4::translate_agent(client, slots.client(number), 768, {}, names.at(name));
                unsigned team{}, token{}; std::memcpy(&team, agent.data() + 76, 4); std::memcpy(&token, agent.data() + 124, 4);
                check(team == client.get(4) && token == names.at(name), "Agent identity/team changed during replay");
            }

        for (int bits : {-92, -91, -90})
        {
            const float value = -127.125f;
            std::array<std::uint8_t, 4> payload{};
            std::memcpy(payload.data(), &value, sizeof(value));
            cod4::detail::reader msg{payload}; msg.protocol = 19;
            check(cod4::detail::player_delta(msg, 0, {"origin", bits, 2}, 0) == std::bit_cast<unsigned>(value)
                && msg.pos == 4, "Protocol-19 raw-float origin codec changed");
        }
        for (const auto length : {std::size_t{1}, std::size_t{8}, std::size_t{16}, bytes.size() - 1})
            reject_all(std::span(bytes).first(length));
        for (unsigned unsupported : {0u, 17u, 18u, 20u, 22u})
        {
            auto corrupt = bytes;
            std::memcpy(corrupt.data() + 1, &unsupported, sizeof(unsupported));
            reject_all(corrupt);
        }
        for (std::size_t offset : {5u, 9u})
        {
            auto corrupt = bytes; corrupt[offset] ^= 1;
            reject_all(corrupt);
        }
        auto misplaced = bytes;
        misplaced.insert(misplaced.begin() + 17, bytes.begin(), bytes.begin() + 17);
        reject_all(misplaced);
        std::cout << "PASS: protocol 19; 1424 camera frames, 316 snapshots, 47 kills, 15 gun events; "
            << "names, raw origins and malformed-input checks; 39 identities mapped to 18 native players and 21 agents\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
