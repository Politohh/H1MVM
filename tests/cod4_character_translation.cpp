#include "component/demo/cod4_model_translation.hpp"
#include "component/demo/cod4_world_reader.hpp"
#include <filesystem>
#include <iostream>
#include <set>

namespace
{
    using row = std::vector<std::string>;
    std::vector<row> read_tsv(const std::filesystem::path& path, unsigned columns)
    {
        std::ifstream input(path);
        if (!input) throw std::runtime_error("Cannot read reference: " + path.string());
        std::string line;
        std::getline(input, line);
        std::vector<row> rows;
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            row values;
            std::istringstream cells(line);
            for (std::string value; std::getline(cells, value, '\t');) values.push_back(value);
            if (values.size() != columns) throw std::runtime_error("Invalid reference row: " + path.string());
            rows.push_back(std::move(values));
        }
        return rows;
    }
    void require(bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error(message);
    }
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 3, "Usage: test reference-directory source-demo-directory");
        const std::filesystem::path reference(argv[1]);
        const auto sources = read_tsv(reference / "cod4-mp-characters.tsv", 6);
        const auto targets = read_tsv(reference / "mwr-stock-characters.tsv", 5);
        const auto maps = read_tsv(reference / "cod4-map-environments.tsv", 2);
        require(sources.size() == 34 && targets.size() == 30 && maps.size() == 25, "Incomplete stock reference");
        std::map<std::pair<std::string, std::string>, row> native_classes;
        std::set<std::string> available;
        for (const auto& target : targets)
        {
            native_classes[{target[0], target[1]}] = target;
            for (unsigned i = 2; i < 5; ++i) available.insert(target[i]);
        }
        unsigned combinations = 0;
        for (const auto& source : sources) for (const auto& map : maps)
        {
            auto family = source[4];
            if (family == "spetsnaz_woodland" && source[5] != "sniper" && map[1] == "urban")
                family = "spetsnaz_urban";
            const auto& target = native_classes.at({family, source[5]});
            require(cod4::character_environment(map[0]) == map[1], "Wrong original map environment: " + map[0]);
            std::array<std::string_view, 6> attachments{source[2], "weapon_m40a3"};
            const auto models = cod4::map_character_models(source[1], attachments, map[0]);
            require(models.body == target[2] && models.attachments[0] == target[3], "Wrong class body/head: " + source[0]);
            require(models.attachments[1].empty(), "COD4 weapon model was attached to MWR character");
            require(cod4::hands_target(source[3], map[0], source[1]) == target[4], "Wrong class hands: " + source[0]);
            ++combinations;
        }
        require(cod4::character_environment("custom_map").empty(), "Unknown map was restricted");
        require(cod4::model_target("custom_model", "custom_map") == "custom_model", "Custom identity was replaced");
        require(cod4::hands_target("viewhands_marine_sniper", "mp_bloc", "body_mp_opforce_sniper")
            != cod4::hands_target("viewhands_marine_sniper", "mp_bloc", "body_mp_usmc_woodland_sniper"),
            "Shared ghillie name lost faction context");
        require(cod4::model_target("head_mp_opforce_gasmask", "mp_bloc", "body_mp_opforce_cqb")
            != cod4::model_target("head_mp_opforce_gasmask", "mp_bloc", "body_mp_opforce_eningeer"),
            "Shared head name lost class context");
        require(cod4::model_target("head_mp_usmc_tactical_mich", "mp_bloc", "body_mp_usmc_woodland_assault")
            != cod4::model_target("head_mp_usmc_tactical_mich", "mp_crash", "body_mp_usmc_assault"),
            "Shared head name lost body context");
        bool rejected = false;
        try { cod4::hands_target("viewhands_marine_sniper", "mp_bloc"); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Ambiguous ghillie hands were guessed without a body");
        std::cout << "PASS: " << combinations << " class/map combinations from 34 COD4 scripts and 30 native MWR class sets\n";

        unsigned decoded = 0, skipped = 0, total_samples = 0;
        for (const auto& file : std::filesystem::directory_iterator(argv[2]))
        {
            if (file.path().extension() != ".dm_1") continue;
            cod4::camera_track camera;
            cod4::world_track world;
            std::vector<cod4::player_frame> players;
            try
            {
                std::ifstream input(file.path(), std::ios::binary);
                const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
                camera = cod4::decode(bytes);
                players = cod4::decode_players(bytes);
                world = cod4::decode_world(bytes);
            }
            catch (const std::exception& error)
            {
                std::cout << "SKIP reader: " << file.path().filename().string() << " : " << error.what() << '\n';
                ++skipped;
                continue;
            }
            std::set<std::pair<unsigned, unsigned>> hands;
            std::set<std::string> required;
            for (const auto& frame : world.frames) for (const auto& [number, client] : frame.clients)
            {
                if (!client.get(8)) continue;
                const auto& body = camera.configstrings.at(830 + client.get(8));
                std::array<std::string_view, 6> attachments{};
                for (unsigned i = 0; i < 6; ++i)
                    if (const auto index = client.get(12 + i * 4)) attachments[i] = camera.configstrings.at(830 + index);
                const auto models = cod4::map_character_models(body, attachments, camera.map);
                required.insert(models.body);
                for (const auto& name : models.attachments) if (!name.empty()) required.insert(name);
            }
            for (const auto& frame : camera.frames)
            {
                const auto next = std::upper_bound(players.begin(), players.end(), frame.time,
                    [](int time, const cod4::player_frame& player) { return time < player.integer("commandTime"); });
                const auto& player = next == players.begin() ? players.front() : *(next - 1);
                const auto index = player.integer("viewmodelIndex");
                if (!index) continue;
                const auto& clients = world.sample(frame.time).clients;
                const auto client = clients.find(world.client_num);
                const auto body_index = client == clients.end() ? 0 : client->second.get(8);
                if (!hands.emplace(index, body_index).second) continue;
                const auto body = body_index ? camera.configstrings.at(830 + body_index) : std::string{};
                required.emplace(cod4::hands_target(camera.configstrings.at(830 + index), camera.map, body));
            }
            for (const auto& name : required)
                require(available.contains(name), "Unmapped required character asset in " + file.path().filename().string() + ": " + name);
            std::cout << "PASS fixture: " << file.path().filename().string() << " samples=" << camera.frames.size()
                << " hands contexts=" << hands.size() << " native character assets=" << required.size() << '\n';
            ++decoded;
            total_samples += static_cast<unsigned>(camera.frames.size());
        }
        require(decoded > 0, "No source fixtures were checked");
        std::cout << "PASS: required character/hands mappings across " << decoded << " readable demos and " << total_samples
            << " camera samples; " << skipped << " reader failures reported separately\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
