#include "component/demo/cod4_facial_animation.hpp"
#include <fstream>
#include <iostream>
#include <limits>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv)
{
    using namespace cod4::face;
    try
    {
        const auto opposite = interpolate({0,0,0,1}, {0,0,0,-1}, 0.5f);
        check(std::abs(opposite[3]-1) < 0.00001f, "Quaternion hemisphere interpolation failed");
        clip curve{10,10,false,{}};
        track t{"j_jaw",{{0,{0,0,0,1}},{10,{0,0,1,0}}},{{0,{0,0,0}},{10,{2,4,6}}}};
        const auto middle = sample(curve,t,0.5,{}), end = sample(curve,t,100,{});
        check(std::abs(middle.translation[2]-3) < 0.00001f && end.translation[2] == 6,
            "Facial interpolation or non-looping death end clamp failed");
        const auto repeat = sample(curve,t,0.5,{});
        check(repeat.translation == middle.translation && repeat.rotation == middle.rotation,
            "Facial sampling depends on evaluation history");
        curve.loop = true;
        check(sample(curve,t,1.5,{}).translation == middle.translation, "Loop phase failed");
        check(!facial_bone("tag_eye") && !facial_bone("j_head") && facial_bone("j_levator_le"),
            "Non-facial transforms entered the layer");
        bool rejected{};
        try { sample(curve,t,std::numeric_limits<double>::quiet_NaN(),{}); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected,"NaN sample time accepted");
        unsigned inputs{};
        for (int i = 1; i < argc; ++i)
        {
            std::ifstream file(argv[i],std::ios::binary);
            const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(file)),{});
            const auto c = decode(data);
            check(c.tracks.size() == 20, "Stock facial track coverage changed");
            unsigned moving{};
            for (const auto& bone : c.tracks)
            {
                const auto first = sample(c,bone,0,{});
                bool changed{};
                for (unsigned f = 1; f <= c.frames; ++f)
                {
                    const auto p = sample(c,bone,static_cast<double>(f)/c.fps,{});
                    float distance{}, opposite_distance{};
                    for(unsigned j=0;j<4;++j)
                    {
                        const float difference = p.rotation[j]-first.rotation[j];
                        const float opposite_component = p.rotation[j]+first.rotation[j];
                        distance += difference*difference;
                        opposite_distance += opposite_component*opposite_component;
                    }
                    changed |= std::min(distance,opposite_distance) > 1e-12f || p.translation != first.translation;
                }
                moving += changed;
            }
            check(moving >= 17, "Authored facial motion was lost");
            auto truncated = data; truncated.pop_back(); rejected=false;
            try { decode(truncated); } catch (const std::runtime_error&) { rejected=true; }
            check(rejected,"Truncated raw asset accepted");
            auto trailing = data; trailing.push_back(0); rejected=false;
            try { decode(trailing); } catch (const std::runtime_error&) { rejected=true; }
            check(rejected,"Trailing raw asset bytes accepted");
            std::cout << "PASS " << argv[i] << " frames=" << c.frames << " fps=" << c.fps
                << " facial_tracks=" << c.tracks.size() << " moving=" << moving << '\n';
            ++inputs;
        }
        std::cout << "PASS facial interpolation, pause/seek determinism, loop/end handling and " << inputs << " authored inputs\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
