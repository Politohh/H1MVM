#include "component/demo/cod4_car_fx.hpp"
#include "component/demo/cod4_grenade_events.hpp"
#include "component/demo/cod4_model_metadata.hpp"
#include <iostream>
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<typename F> void rejects(F&& function, const char* message)
{ bool failed = false; try { function(); } catch (const std::exception&) { failed = true; } check(failed, message); }
template<typename T> T native(const cod4::h1_entity& data, unsigned offset)
{ T result; std::memcpy(&result, data.data() + offset, sizeof(result)); return result; }
cod4::entity_state effect(unsigned sequence, unsigned event = 55)
{
    cod4::entity_state result;
    result.set(0, 95); result.set(160, sequence);
    result.set(164 + ((sequence - 1) & 3) * 4, event);
    result.set(180 + ((sequence - 1) & 3) * 4, 14);
    result.set(24, std::bit_cast<unsigned>(-600.0f));
    result.set(60, std::bit_cast<unsigned>(270.0f));
    return result;
}
int main(int argc, char** argv)
{
    try
    {
        const std::map<int, std::string> configs{{1612, std::string(cod4::car_explosion_fx)}, {1613, "props/car_glass_large"}};
        cod4::world_track synthetic;
        synthetic.frames = {{1000,1,{{95,effect(255,44)}},{}}, {1050,2,{{95,effect(0)}},{}},
            {1100,3,{{95,effect(0)}},{}}, {1150,4,{},{}}, {1200,5,{{95,effect(1)}},{}}};
        const auto events = cod4::collect_car_fx(synthetic, configs).events;
        check(events.size() == 2 && events[0].time == 1050 && events[1].time == 1200,
            "Event wrap, duplicate suppression or entity reuse failed");
        auto temporary = effect(0); temporary.set(4, 72); temporary.set(156, 14);
        synthetic.frames = {{1000,1,{{95,temporary}},{}}, {1050,2,{{95,temporary}},{}}};
        synthetic.frames[1].entities.at(95).set(24, std::bit_cast<unsigned>(-650.0f));
        check(cod4::collect_car_fx(synthetic, configs).events.size() == 1, "Temporary pose change retriggered FX");
        temporary.set(156, 15); synthetic.frames = {{1000,1,{{95,temporary}},{}}};
        const auto other = cod4::collect_car_fx(synthetic, configs);
        check(other.events.empty() && other.other_effects.at("props/car_glass_large") == 1, "Other FX became a car explosion");
        temporary.set(156, 99); synthetic.frames = {{1000,1,{{95,temporary}},{}}};
        rejects([&] { cod4::collect_car_fx(synthetic, configs); }, "Unknown effect identity accepted");
        temporary = {}; temporary.set(4,6); temporary.set(136,355);
        synthetic.frames = {{1000,1,{{95,temporary}},{}}};
        check(cod4::collect_car_fx(synthetic, configs).events.empty(), "Model swap invented a car explosion");
        const auto translated = cod4::translate_car_fx(events[0],1500,511);
        check(translated[10] == 131 && native<unsigned>(translated,36) == 511 && native<unsigned>(translated,76) == 0
            && native<std::array<float,3>>(translated,108) == events[0].origin
            && native<std::array<float,3>>(translated,144) == events[0].angles, "Native FX layout or pose is wrong");
        rejects([&] { cod4::translate_car_fx(events[0],2046,511); }, "Entity overflow accepted");
        rejects([&] { cod4::translate_car_fx(events[0],1500,512); }, "Native FX index overflow accepted");
        auto bad = events[0]; bad.angles[1] = std::numeric_limits<float>::infinity();
        rejects([&] { cod4::translate_car_fx(bad,1500,511); }, "Nonfinite orientation accepted");
        check(cod4::car_fx_at(events,1049) == 0 && cod4::car_fx_at(events,1050) == 1
            && cod4::car_fx_at(events,1050) == 1 && cod4::car_fx_at(events,5000) == 2
            && cod4::car_fx_at(events,1000) == 0, "Pause/seek presence calculation failed");
        const auto metadata = cod4::parse_model_metadata({"@grenade:1499:1000", "@carfx:1500:1050",
            "@fx:511:fx/explosions/small_vehicle_explosion"});
        check(metadata.car_fx_times.at(1500) == 1050 && metadata.fx_bindings.at(511) == cod4::native_car_explosion_fx
            && metadata.names.empty() && metadata.grenade_times.at(1499) == 1000, "Standalone FX metadata was lost");
        rejects([] { cod4::parse_model_metadata({"@carfx:1500:1050"}); }, "Missing native FX binding accepted");
        rejects([] { cod4::parse_model_metadata({"@grenade:1500:1000", "@carfx:1500:1050"}); }, "FX identity collision accepted");
        rejects([] { cod4::parse_model_metadata({"@fx:512:fx/car"}); }, "FX binding overflow accepted");
        rejects([] { cod4::parse_model_metadata({"@fx:511:fx/car", "@fx:511:fx/other"}); }, "Duplicate FX binding accepted");
        check(argc == 2, "Pass the dedicated car explosion recording");
        std::ifstream file(argv[1], std::ios::binary);
        check(static_cast<bool>(file), "Cannot open car fixture");
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
        const auto camera = cod4::decode(bytes); const auto world = cod4::decode_world(bytes);
        const auto recorded = cod4::collect_car_fx(world,camera.configstrings);
        check(camera.map == "mp_backlot" && camera.frames.size() == 7990 && recorded.events.size() == 4,
            "Car recording inventory changed");
        constexpr std::array<int,4> times{106850,117800,124800,124800};
        constexpr std::array<unsigned,4> entities{95,149,250,342};
        for (std::size_t i=0; i<4; ++i)
        {
            const auto& event = recorded.events[i];
            check(event.time == times[i] && event.entity == entities[i] && event.effect == 14
                && event.angles == std::array<float,3>{270,0,0}, "Recorded explosion clock, identity or orientation changed");
            check(cod4::car_fx_at(recorded.events,event.time-1) == (i==3 ? 2 : i), "Chain reaction or clock ordering lost");
        }
        check(cod4::collect_grenade_events(world).events.size() == 3, "Grenade fixture events changed");
        unsigned readable{}, skipped{}, explosions{};
        for (const auto& item : std::filesystem::directory_iterator(std::filesystem::path(argv[1]).parent_path()))
        {
            if (item.path().extension() != ".dm_1") continue;
            std::ifstream input(item.path(),std::ios::binary);
            const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(input)),{});
            cod4::camera_track source_camera; cod4::world_track source_world;
            try { source_camera = cod4::decode(data); source_world = cod4::decode_world(data); }
            catch (const std::exception&) { ++skipped; continue; }
            const auto fx = cod4::collect_car_fx(source_world,source_camera.configstrings);
            explosions += static_cast<unsigned>(fx.events.size()); ++readable;
            std::cout << "FX fixture: " << item.path().filename().string() << " car explosions=" << fx.events.size()
                << " other scripted FX identities=" << fx.other_effects.size() << '\n';
        }
        std::cout << "PASS: car FX inventory across " << readable << " readable demos; " << explosions
            << " explicit car FX events; " << skipped << " existing reader failures separate\n";
        std::cout << "PASS: 4 recorded car explosion FX; 3 grenade detonations retained; event wrap/reuse, native pose/index, pause/seek presence, standalone bindings and malformed input checks\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
