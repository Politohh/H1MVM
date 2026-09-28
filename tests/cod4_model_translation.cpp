#include "component/demo/cod4_model_translation.hpp"
#include "component/demo/cod4_demo_reader.hpp"
#include "component/demo/cod4_world_reader.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    try
    {
        if (argc < 3) throw std::runtime_error("Usage: test native-model-names.txt fixture.dm_1...");
        std::set<std::string> native;
        std::ifstream catalog(argv[1]);
        for (std::string name; std::getline(catalog, name); )
        {
            if (!name.empty() && name.back() == '\r') name.pop_back();
            native.insert(name);
        }
        if (native.empty()) throw std::runtime_error("Native catalog is empty");
        // Character transients are absent from the resident Crash map export.
        for (const auto& character : cod4::native_characters)
            for (const auto name : {character.body, character.head, character.hands}) native.emplace(name);
        const auto available = [&](const std::string& name) { return native.contains(name); };
        if (cod4::select_model("mil_tntbomb_mp", cod4::model_usage::prop, [](auto&) { return false; }) != "")
            throw std::runtime_error("Missing prop was not omitted");
        if (cod4::select_model("mil_tntbomb_mp", cod4::model_usage::prop, [](auto&) { return true; }) != "mil_tntbomb_mp")
            throw std::runtime_error("Available prop changed identity");
        bool rejected = false;
        try { cod4::select_model("body_custom", cod4::model_usage::character, [](auto&) { return false; }); }
        catch (const std::runtime_error&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Required character was silently omitted");
        rejected = false;
        try { cod4::select_model("../bad:model", cod4::model_usage::prop, [](auto&) { return false; }); }
        catch (const std::runtime_error&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Malformed model was treated as a missing prop");
        for (int fixture = 2; fixture < argc; ++fixture)
        {
            std::ifstream input(argv[fixture], std::ios::binary);
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
            const auto camera = cod4::decode(bytes);
            const auto world = cod4::decode_world(bytes);
            std::set<unsigned> props;
            std::set<std::string> characters;
            for (const auto& frame : world.frames)
            {
                for (const auto& [number, entity] : frame.entities)
                    if (entity.get(4) == 6 && entity.get(136)) props.insert(entity.get(136));
                for (const auto& [number, client] : frame.clients)
                {
                    if (!client.get(8)) continue;
                    const auto& body = camera.configstrings.at(830 + client.get(8));
                    std::array<std::string_view, 6> attachments{};
                    for (unsigned i = 0; i < 6; ++i)
                    {
                        const auto index = client.get(12 + i * 4);
                        if (index) attachments[i] = camera.configstrings.at(830 + index);
                    }
                    const auto mapped = cod4::map_character_models(body, attachments, camera.map);
                    characters.insert(mapped.body);
                    for (const auto& name : mapped.attachments) if (!name.empty()) characters.insert(name);
                }
            }
            unsigned omitted = 0;
            for (const auto index : props)
                omitted += cod4::select_model(camera.configstrings.at(830 + index), cod4::model_usage::prop, available).empty();
            for (const auto& name : characters)
                if (!available(name)) throw std::runtime_error("Native character model missing: " + name);
            std::cout << "PASS: " << camera.map << " props=" << props.size() << " omitted=" << omitted
                << " required character models=" << characters.size() << " matched native catalog snapshot\n";
        }
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
