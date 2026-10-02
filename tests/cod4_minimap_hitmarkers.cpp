#include "component/demo/cod4_hud_translation.hpp"
#include <iostream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char** argv)
{
    try
    {
        // Exercise both native naming conventions without a map allowlist.
        for (const std::string map : {"backlot", "bloc", "bog", "cargoship", "citystreets", "convoy",
            "countdown", "crash", "crossfire", "farm", "overgrown", "pipeline", "shipment", "showdown",
            "strike", "vacant", "broadcast", "carentan", "creek", "killhouse", "crash_snow", "custom_map"})
        {
            const auto material = "compass_map_mp_" + map;
            const auto config = "\"" + material + "\" 2735.000000 2528 -1959 -2166";
            const auto exact = cod4::resolve_minimap_material(config, [&](const auto& name) { return name == material; });
            check(exact && *exact == config, "Existing native minimap changed");
            const auto dlc = cod4::resolve_minimap_material(config, [&](const auto& name) { return name == material + "_dlc"; });
            check(dlc && *dlc == "\"" + material + "_dlc\" 2735.000000 2528 -1959 -2166", "Native DLC bounds/name mismatch");
            check(cod4::resolve_minimap_material(*dlc, [&](const auto& name) { return name == material; }) == exact,
                "DLC reverse resolution failed");
            check(!cod4::resolve_minimap_material(config, [](const auto&) { return false; }), "Missing asset accepted");
        }
        check(!cod4::resolve_minimap_material("\"compass_map_default\" 1 2 3 4", [](const auto&) { return true; }),
            "Checkerboard default accepted");
        check(cod4::resolve_minimap_material(" compass_map_mp_crash 1 2 3 4", [](const auto& n) {
            return n == "compass_map_mp_crash_dlc";
        }) == " compass_map_mp_crash_dlc 1 2 3 4", "Unquoted config failed");

        const std::map<int, std::string> strings{{2003, "damage_feedback"}, {2004, "damage_feedback_j"}};
        cod4::world_track world;
        cod4::wire_state<160> hit;
        hit.set(0, 6); hit.set(64, 1); hit.set(36, 0x00ffffff);
        hit.set(40, 0xf8ffffff); hit.set(48, 1000); hit.set(44, 900);
        world.hud_frames.push_back({1000, {{{hit}, {}}}}); // initial history
        hit.set(44, 1100);
        world.hud_frames.push_back({1150, {{{hit}, {hit}}}}); // duplicate banks
        world.hud_frames.push_back({1200, {{{hit}, {hit}}}}); // unchanged snapshot
        hit.set(44, 1250); hit.set(64, 2);
        world.hud_frames.push_back({1300, {{{}, {hit}}}}); // another hit, other bank
        hit.set(44, 1150);
        world.hud_frames.push_back({1800, {{{hit}, {}}}}); // edited stale state
        const auto events = cod4::collect_hitmarkers(world, strings);
        check(events.size() == 2 && events[0].time == 1100 && !events[0].juggernaut
            && events[1].time == 1250 && events[1].juggernaut, "Hit timing/history/deduplication mismatch");
        std::cout << "PASS: generic minimap resolution across 22 names; recorded hit edges and stale history\n";

        check(argc == 4, "Pass Warz, Promod Crash and Strike fixtures");
        const std::array<std::vector<int>, 3> expected{{
            {105160800, 105170100, 105184950, 105196400},
            {52444000, 52444500, 52445300, 52472350, 52472450},
            {11071700, 11073650, 11075300, 11082600, 11083900}}};
        for (int arg = 1; arg < argc; ++arg)
        {
            std::ifstream file(argv[arg], std::ios::binary);
            check(file.good(), "Fixture unavailable");
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
            const auto camera = cod4::decode(bytes);
            const auto recorded = cod4::decode_world(bytes);
            const auto hits = cod4::collect_hitmarkers(recorded, camera.configstrings);
            std::vector<int> times;
            for (const auto& event : hits) times.push_back(event.time);
            check(times == expected[arg - 1], "Recorded hit missing, delayed or duplicated");
            const auto& compass = camera.configstrings.at(823);
            const auto resolved = cod4::resolve_minimap_material(compass, [&](const std::string& name) {
                return name == "compass_map_" + camera.map + (camera.map == "mp_crash" ? "_dlc" : "");
            });
            check(resolved.has_value(), "Recorded minimap could not resolve native name");
            std::cout << "PASS: " << camera.map << ", " << hits.size() << " exact recorded hit times; " << *resolved << '\n';
        }
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
