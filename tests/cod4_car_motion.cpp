#include "component/demo/cod4_car_motion.hpp"
#include "component/demo/cod4_model_metadata.hpp"
#include <iostream>
#include <set>

void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class T> T value(const cod4::h1_entity& state, unsigned offset)
{ T result; std::memcpy(&result, state.data() + offset, sizeof(result)); return result; }
int main(int argc, char** argv)
{
    try
    {
        check(argc == 2, "Pass the dedicated source car demo");
        std::ifstream file(argv[1], std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        const auto camera = cod4::decode(bytes);
        const auto world = cod4::decode_world(bytes);
        const auto fx = cod4::collect_car_fx(world, camera.configstrings);
        const auto cars = cod4::collect_car_animations(world, camera.configstrings, fx.events);
        check(cars.size() == 4, "Native car animation association missing or duplicated");
        std::set<std::pair<unsigned, unsigned>> launches;
        unsigned physics_states{}, animated_states{};
        for (const auto& frame : world.frames) for (const auto& [number, entity] : frame.entities)
        {
            if (entity.get(4) != 6 || !entity.get(136)) continue;
            // Visibility is checked separately against resident native skeletons.
            auto visible = entity;
            for (unsigned i = 0; i < 4; ++i) visible.set(228 + i * 4, 0);
            auto native = cod4::translate_script_model(visible, 768);
            if (entity.get(12) == 8)
            {
                check(entity.get(48) == 8 && value<unsigned>(native, 96) == 9
                    && value<unsigned>(native, 132) == 9, "Debris did not select native client physics");
                check(value<unsigned>(native, 100) == entity.get(16)
                    && value<unsigned>(native, 104) == INT_MAX, "Native physics launch clock changed");
                for (unsigned i = 0; i < 3; ++i)
                    check(value<unsigned>(native, 120 + i * 4) == entity.get(36 + i * 4)
                        && value<unsigned>(native, 156 + i * 4) == entity.get(72 + i * 4),
                        "Physics launch point or force altered");
                launches.emplace(number, entity.get(16)); ++physics_states;
            }
            for (const auto& car : cars)
            {
                if (car.entity != number || car.model != entity.get(136) || car.time > frame.time) continue;
                cod4::apply_car_animation(native, car);
                check(value<unsigned>(native, 96) == 11 && value<int>(native, 100) == car.time
                    && value<unsigned>(native, 104) == 2000 && value<unsigned>(native, 132) == 0
                    && value<unsigned>(native, 184) == 254 && value<int>(native, 188) == car.time,
                    "Native animation index/start time or root trajectory wrong");
                check(value<std::array<float, 3>>(native, 108) == car.origin
                    && value<std::array<float, 3>>(native, 144) == car.angles,
                    "Recorded pop was applied underneath native animation");
                ++animated_states;
            }
        }
        check(physics_states > 0 && launches.size() >= 20 && animated_states > 0, "Fixture motion coverage empty");
        check(cod4::collect_car_animations(world, camera.configstrings, {}).empty(),
            "Scenery model name inferred an explosion without a recorded event");
        check(cod4::native_trajectory_type(6) == 7 && cod4::native_trajectory_type(7) == 8,
            "Eased native trajectories confused with low gravity");
        bool rejected{};
        try { (void)cod4::native_trajectory_type(9); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Unknown source trajectory was passed to native engine");
        auto metadata = cod4::parse_model_metadata({"@anim:254:vehicle_80s_sedan1_destroy"});
        check(metadata.animation_bindings.at(254) == cod4::native_car_destroy_animation
            && metadata.names.empty(), "Native animation became an XModel request");
        check(cod4::script_animation_configstring(254) == 4556
            && std::string_view(cod4::script_animation_binding(metadata.animation_bindings, 4556))
                == cod4::native_car_destroy_animation, "Native last animation configstring is not bound");
        for (const auto config : {-1, 0, 1240, 4302, 4303, 4557, 4558, 4565, 5616})
            check(cod4::script_animation_binding(metadata.animation_bindings, config) == nullptr,
                "Car animation leaked into an unbound slot or custom weapon lookup");
        for (const auto index : {0u, 255u, 256u, UINT_MAX})
        {
            rejected = false;
            try { (void)cod4::script_animation_configstring(index); } catch (const std::exception&) { rejected = true; }
            check(rejected, "Native animation configstring accepted an out-of-range index");
        }
        for (const auto& entry : {"@anim:0:a", "@anim:255:a", "@anim:256:a", "@anim:x:a", "@anim:254:", "@anim:254:a:b"})
        {
            rejected = false;
            try { (void)cod4::parse_model_metadata({entry}); } catch (const std::exception&) { rejected = true; }
            check(rejected, "Malformed animation metadata accepted");
        }
        rejected = false;
        try { (void)cod4::parse_model_metadata({"@anim:254:a", "@anim:254:b"}); }
        catch (const std::exception&) { rejected = true; }
        check(rejected, "Duplicate animation metadata accepted");
        std::cout << "PASS: " << cars.size() << " native destruction clips, " << launches.size()
            << " recorded debris launches, " << physics_states << " physics states, " << animated_states
            << " native animation states; launch payloads, stable placement, animation/weapon configstring isolation and metadata validation\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
