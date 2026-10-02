#include "component/demo/cod4_world_translation.hpp"
#include "component/demo/cod4_gun_events.hpp"
#include "component/demo/cod4_client_slots.hpp"
#include "component/demo/cod4_obituary_events.hpp"
#include <iostream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char** argv)
{
    try
    {
        check(argc == 2, "Pass demo0011.dm_1");
        std::ifstream input(argv[1], std::ios::binary);
        check(input.good(), "Missing protocol-21 fixture");
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        const auto camera = cod4::decode(bytes);
        const auto players = cod4::decode_players(bytes);
        const auto world = cod4::decode_world(bytes);
        check(camera.protocol == 21 && camera.map == "mp_crash" && camera.configstrings.size() == 3723,
            "Protocol/header/configstrings not decoded");
        check(camera.packets == 2037 && camera.archives == 3098 && camera.replaced_frames == 176
            && camera.frames.size() == 2922 && camera.frames.front().time == 105893896
            && camera.frames.back().time == 106005066, "Camera framing/chronology changed");
        check(players.size() == 2036 && world.frames.size() == players.size() && world.baselines.size() == 62
            && world.client_num == 5 && players.front().time == 105903200 && players.back().time == 106005150,
            "Player/world framing changed");
        check(cod4::make_client_slots(world).slots.size() == 16
            && cod4::collect_obituary_events(world).size() == 69
            && cod4::collect_gun_events(players).events.size() == 81, "Recorded actors/events lost");
        for (std::size_t i = 0; i < players.size(); ++i)
        {
            check(world.frames[i].time == players[i].time, "Player and world timelines diverged");
            check(world.frames[i].client_names.at(5) == "Polito", "Recorded POV name lost");
            const auto& client = world.frames[i].clients.at(5);
            const auto native = cod4::translate_client(client, 1, {}, world.frames[i].client_names.at(5));
            check(std::string(reinterpret_cast<const char*>(native.data() + 42)) == "Polito", "Native client name lost");
        }
        // The protocol transmits raw float origins, not legacy quantized deltas.
        for (int bits : {-92, -91, -90})
        {
            std::array<std::uint8_t, 4> payload{};
            const float value = -127.125f;
            std::memcpy(payload.data(), &value, sizeof(value));
            cod4::detail::reader msg{payload}; msg.protocol = 21;
            check(cod4::detail::player_delta(msg, 0, {"origin", bits, 2}, 0) == std::bit_cast<unsigned>(value)
                && msg.pos == 4, "Full precision CoD4x origin changed");
        }
        for (auto bad : {std::size_t{1}, std::size_t{8}, std::size_t{16}, bytes.size() - 1})
        {
            for (int decoder = 0; decoder < 3; ++decoder)
            {
                bool rejected = false;
                try
                {
                    const auto truncated = std::span(bytes).first(bad);
                    if (decoder == 0) cod4::decode(truncated);
                    else if (decoder == 1) cod4::decode_players(truncated);
                    else cod4::decode_world(truncated);
                }
                catch (const std::exception&) { rejected = true; }
                check(rejected, "Truncated protocol-21 file accepted");
            }
        }
        for (std::size_t offset : {1u, 5u, 9u})
        {
            auto corrupt = bytes; corrupt[offset] ^= 1;
            bool rejected = false;
            try { cod4::decode(corrupt); } catch (const std::exception&) { rejected = true; }
            check(rejected, "Unknown protocol or corrupt protocol header accepted");
        }
        std::cout << "PASS: protocol 21; 2922 camera frames, 2036 snapshots, 16 active identities, 69 kills, 81 gun events; native names, precision and malformed-input checks\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
