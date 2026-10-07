#include "component/demo/demo_splitter.hpp"
#include "component/demo/theater_library_metadata.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
#include <fstream>

static std::span<const std::uint8_t> bytes(const std::string& text)
{ return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}; }

static void network(std::ostream& file, int sequence, const std::vector<std::uint8_t>& packet)
{
    std::vector<std::uint8_t> chunk(8 + compressBound(static_cast<uLong>(packet.size())));
    std::memcpy(chunk.data(), &sequence, 4); chunk[7] = 128;
    uLongf length = static_cast<uLongf>(chunk.size() - 8);
    assert(compress2(chunk.data() + 8, &length, packet.data(), static_cast<uLong>(packet.size()), 1) == Z_OK);
    chunk.resize(length + 8); demo_splitter::write_chunk(file, 2, chunk);
}

static std::string fixture(bool delta = false)
{
    std::ostringstream file;
    std::string header;
    for (const auto& field : {"", "H1 COD4 conversion v5", "v0.0.5", "1", "", "COD4", "", "mp_crash", "war", "@model:123:body"})
    { header += field; header.push_back(0); }
    demo_splitter::write_chunk(file, 5, bytes(header));
    demo_splitter::write_chunk(file, 1, bytes(std::string("mp_crash\0war\0", 13)));
    demo_splitter::write_chunk(file, 7, bytes("facial metadata"));
    demo_splitter::write_chunk(file, 8, bytes("corpse metadata"));
    network(file, 1, {0, 1, 2, 3});
    demo_splitter::write_chunk(file, 4, bytes("persistent gamestate"));
    for (int i = 0; i <= 10; ++i)
    {
        const int time = 10000 + i * 1000;
        std::vector<std::uint8_t> prediction(50, static_cast<std::uint8_t>(i));
        prediction[0] = 0; prediction[1] = static_cast<std::uint8_t>(i + 100);
        std::memcpy(prediction.data() + 2, &time, 4);
        demo_splitter::write_chunk(file, 3, prediction);
        std::vector<std::uint8_t> packet(12, static_cast<std::uint8_t>(i));
        packet[0] = 6; std::memcpy(packet.data() + 1, &time, 4); packet[5] = delta ? 1 : 0; packet[6] = 8;
        network(file, 50 + i, packet);
    }
    const int first = 10000, last = 20000;
    std::string footer(reinterpret_cast<const char*>(&first), 4); footer.append(reinterpret_cast<const char*>(&last), 4);
    footer += std::string("[00:00:10]\0", 11);
    const std::array<demo_timeline::event, 3> events{{{11000, 0, 1}, {15000, 0, 2}, {19000, 1, 0}}};
    footer += demo_timeline::encode(0, events, first, last);
    demo_splitter::write_chunk(file, 6, bytes(footer)); demo_splitter::write_chunk(file, 31, {});
    return file.str();
}

static void validate_clip(std::istream& input, std::istream& output, int first, int last, std::size_t frames)
{
    const auto tail = demo_file::read_footer(output);
    assert(tail && tail->first == first && tail->last == last);
    const auto info = theater_library::read_native(output);
    assert(info.converted && info.map == "mp_crash" && info.duration == last - first);
    // Retained compressed snapshot payloads must be byte-identical to source.
    std::map<int, std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>>> originals;
    const auto collect = [&](std::istream& stream, bool verify)
    {
        stream.clear(); stream.seekg(0);
        std::vector<std::uint8_t> prediction;
        std::size_t count{}; int expected_sequence = 1;
        while (stream)
        {
            std::uint8_t tag{}; std::uint32_t size{};
            stream.read(reinterpret_cast<char*>(&tag), 1);
            stream.read(reinterpret_cast<char*>(&size), tag & 128 ? 1 : 4);
            assert(stream && size <= 131072);
            std::vector<std::uint8_t> data(size); stream.read(reinterpret_cast<char*>(data.data()), size);
            const auto id = tag & 31;
            if (id == 31) break;
            if (id == 3) prediction = data;
            if (id != 2) continue;
            const auto packet = demo_splitter::unpack(data);
            if (verify)
            {
                int sequence{}; std::memcpy(&sequence, data.data(), 4);
                assert(sequence == expected_sequence++);
            }
            if ((packet[0] & 7) != 6) continue;
            int time{}; std::memcpy(&time, packet.data() + 1, 4);
            assert(prediction.size() == 50);
            if (verify)
            {
                assert(time >= first && time <= last && prediction[1] == static_cast<std::uint8_t>(count));
                const auto& original = originals.at(time);
                assert(std::equal(data.begin() + 4, data.end(), original.first.begin() + 4, original.first.end()));
                assert(prediction[0] == original.second[0]);
                assert(std::equal(prediction.begin() + 2, prediction.end(), original.second.begin() + 2));
            }
            else originals.emplace(time, std::pair{data, prediction});
            prediction.clear(); ++count;
        }
        if (verify) assert(count == frames);
    };
    collect(input, false); collect(output, true);
}

int main(int argc, char** argv)
{
    const auto original = fixture();
    std::stringstream input(original), output;
    const auto cut = demo_splitter::split_native(input, output, 3500, 8500);
    assert(cut.first == 14000 && cut.last == 18000 && cut.frames == 5);
    validate_clip(input, output, cut.first, cut.last, cut.frames);
    auto tail = demo_file::read_footer(output);
    const auto timeline = demo_timeline::decode(tail->bytes, tail->first, tail->last);
    assert(timeline && timeline->pov == 0 && timeline->events.size() == 1 && timeline->events[0].time == 15000);
    // Repeated splitting works with the saved clip's local zero-based range.
    std::stringstream nested; output.clear(); output.seekg(0);
    const auto second = demo_splitter::split_native(output, nested, 1000, 3000);
    assert(second.first == 15000 && second.last == 17000 && second.frames == 3);
    auto reject = [](const std::string& data, int start, int end, bool cancel = false)
    {
        std::stringstream src(data), dst; bool rejected{};
        try { (void)demo_splitter::split_native(src, dst, start, end, [=](int) { return !cancel; }); }
        catch (const std::runtime_error&) { rejected = true; }
        assert(rejected);
    };
    reject(original, -1, 4000); reject(original, 4000, 4000); reject(original, 0, 10001);
    reject(original, 1, 999); reject(original.substr(0, original.size() - 1), 0, 5000);
    reject(original + "junk", 0, 5000); reject(fixture(true), 0, 5000); reject(original, 0, 5000, true);
    auto corrupted = original; corrupted[0] |= 32; reject(corrupted, 0, 5000);
    std::stringstream full_in(original), full_out;
    assert(demo_splitter::split_native(full_in, full_out, 0, 10000).frames == 11);
    const std::vector<cod4::camera_frame> camera{{10000}, {11000}, {12000}, {13000}};
    const auto clipped = demo_splitter::clip_frames(camera, 500, 2200);
    assert(clipped.size() == 2 && clipped.front().time == 11000 && clipped.back().time == 12000);
    std::cout << "PASS standalone clips, byte-preserved snapshots/cameras, packet renumbering, footer/kill markers, repeated/full cuts, malformed/delta rejection and cancellation\n";
    if (argc > 2)
    {
        const auto source = cod4::load(argv[2]);
        const auto source_clip = demo_splitter::clip_frames(source.frames, 10 * 60 * 1000, 10 * 60 * 1000 + 5000);
        assert(source_clip.front().time >= source.frames.front().time + 600000);
        assert(source_clip.back().time <= source.frames.front().time + 605000);
        std::cout << "PASS original COD4 range reconstruction: " << source_clip.size() << " camera frames\n";
    }
    if (argc > 1)
    {
        std::ifstream source(argv[1], std::ios::binary);
        std::stringstream saved;
        const auto actual = demo_splitter::split_native(source, saved, 600000, 605000);
        validate_clip(source, saved, actual.first, actual.last, actual.frames);
        std::cout << "PASS real 27-minute replay cut at 10:00-10:05: " << actual.frames << " frames, "
            << saved.str().size() << " bytes, " << actual.last - actual.first << " ms\n";
    }
}
