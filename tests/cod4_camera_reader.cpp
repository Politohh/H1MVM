#include "component/demo/cod4_demo_reader.hpp"
#include <iostream>

void check(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }

int main(int argc, char** argv)
{
    try
    {
        check(argc == 2, "Pass a COD4 demo path");
        const auto track = cod4::load(argv[1]);
        check(track.map == "mp_crash", "Fixture map mismatch");
        check(track.configstrings.contains(12) && track.configstrings.contains(2258), "Incomplete source gamestate");
        check(track.configstrings.at(2258).find("deserteagle_mp") != std::string::npos
            && track.configstrings.at(2258).find("m40a3_mp") != std::string::npos, "Fixture weapon names missing");
        check(track.archives == 2281 && track.packets == 406, "Fixture framing mismatch");
        check(track.replaced_frames == 40 && track.frames.size() == 2241, "Pre-roll replacement mismatch");
        check(track.frames.front().time == 33585 && track.frames.back().time == 56385, "Source time bounds mismatch");
        for (std::size_t i = 1; i < track.frames.size(); ++i) check(track.frames[i].time > track.frames[i-1].time, "Nonmonotonic track");
        for (const auto& f : track.frames)
        {
            const auto sampled = track.sample(f.time);
            check(sampled.origin == f.origin && sampled.angles == f.angles, "Exact source sample mismatch");
        }
        cod4::camera_track synthetic;
        cod4::camera_frame a, b;
        a.time = 100; a.angles[1] = 179;
        b.time = 200; b.origin[0] = 10; b.angles[1] = -179;
        synthetic.frames = {a,b};
        const auto mid = synthetic.sample(150);
        check(mid.origin[0] == 5 && std::abs(std::abs(mid.angles[1])-180) < .001f, "Interpolation or angle wrap failed");
        check(synthetic.sample(-1).time == 100 && synthetic.sample(999).time == 200, "Endpoint clamping failed");
        synthetic.frames[1].origin[0] = 1000;
        check(synthetic.sample(150).origin[0] == 0, "Teleport was interpolated");
        std::ifstream file(argv[1], std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        for (const auto n : {std::size_t(0), std::size_t(1), std::size_t(8), std::size_t(100), bytes.size()-1})
        {
            bool rejected = false;
            try { cod4::decode(std::span(bytes).first(n)); } catch (const std::exception&) { rejected = true; }
            check(rejected, "Truncated file accepted");
        }
        auto corrupt = bytes; corrupt[0] = 9;
        bool rejected = false;
        try { cod4::decode(corrupt); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Unknown block accepted");
        std::cout << "PASS: map=" << track.map << " packets=" << track.packets << " frames=" << track.frames.size()
            << " duration_ms=" << track.frames.back().time-track.frames.front().time
            << "; exact-sample fidelity, interpolation, wrap, teleport, pre-roll and malformed-input checks passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
