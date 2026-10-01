#include "component/demo/cod4_actor_translation.hpp"
#include <iostream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char** argv)
{
    try
    {
        check(argc == 2, "Pass the Diablo demo path");
        std::ifstream file(argv[1], std::ios::binary);
        check(bool(file), "Cannot read demo");
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        const auto world = cod4::decode_world(bytes);
        unsigned recovered{};
        for (const auto& frame : world.frames)
        {
            const auto clients = cod4::clients_with_corpse_identities(world, frame.time);
            for (const auto& [number, entity] : frame.entities)
            {
                if (entity.get(4) != 1 && entity.get(4) != 2) continue;
                const auto client = clients.find(entity.get(140));
                check(client != clients.end() && client->second.get(8), "Actor still has no identity");
                if (!frame.clients.contains(entity.get(140)))
                {
                    check(entity.get(4) == 2, "Restored a disconnected live player");
                    ++recovered;
                }
            }
        }
        check(recovered == 932, "Diablo missing-corpse coverage changed");
        cod4::world_track synthetic;
        cod4::world_frame first; first.time = 100;
        cod4::entity_state corpse; corpse.set(4, 2); corpse.set(140, 4);
        cod4::client_state identity; identity.set(0, 4); identity.set(8, 333); identity.set(16, 27);
        first.entities.emplace(69, corpse); first.clients.emplace(4, identity);
        auto second = first; second.time = 200; second.clients.clear();
        synthetic.frames = {first, second};
        check(cod4::clients_with_corpse_identities(synthetic, 250).at(4).words == identity.words,
            "Corpse body or attachments changed");
        check(cod4::clients_with_corpse_identities(synthetic, 100).at(4).words == identity.words,
            "Backward seek changed identity");
        synthetic.frames[1].entities.at(69).set(4, 1);
        check(cod4::clients_with_corpse_identities(synthetic, 200).empty(), "Inherited identity for live actor");
        synthetic.frames[1].entities.at(69) = corpse;
        synthetic.frames[0].entities.clear();
        check(cod4::clients_with_corpse_identities(synthetic, 200).empty(), "Inherited across absent corpse");
        synthetic.frames[0].entities.emplace(69, corpse);
        synthetic.frames[0].entities.at(69).set(140, 5);
        check(cod4::clients_with_corpse_identities(synthetic, 200).empty(), "Inherited across reused corpse slot");
        std::cout << "PASS: " << world.frames.size() << " snapshots; " << recovered
            << " disconnected corpse states recovered; lifetime boundaries and backward lookup verified\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
