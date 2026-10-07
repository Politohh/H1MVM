#include "component/demo/demo_seek_index.hpp"
#include <zlib.h>
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>

static void check_index()
{
    demo_seek::index index;
    assert(!index.before(100));
    index.observe({-1, 100, 1});
    index.observe({100, -1, 1});
    index.observe({100, 100, 0});
    assert(index.size() == 0);
    // A 27-minute replay at 100 Hz, with file offsets beyond 4 GiB.
    constexpr int first = 234567, duration = 27 * 60 * 1000;
    for (int t = 0; t <= duration; t += 10)
        index.observe({5'000'000'000LL + t * 500LL, first + t, t / 10 + 1});
    assert(index.size() == 1621);
    assert(!index.before(first - 1));
    for (int t = 0; t <= duration; t += 37)
    {
        const auto value = index.before(first + t);
        assert(value && value->time <= first + t);
        assert(first + t - value->time <= 2000);
        assert(value->offset >= 5'000'000'000LL);
    }
    // Repeated Back / paused scrubs must reuse anchors, including after EOF.
    for (int t = duration - 5000; t >= 0; t -= 5000)
    {
        const auto value = index.before(first + t);
        assert(value);
        index.observe(*value);
    }
    assert(index.size() == 1621);
    assert(!index.before(first + duration + 5000));
    index.clear();
    index.observe({100, 10, 1});
    index.observe({200, 20000, 2});
    assert(!index.before(10000)); // gap; must use fallback
    assert(!index.before(20000)); // no usable warm-up snapshot across this gap
    index.observe({300, 21000, 3});
    assert(index.before(21000)->time == 20000);
    index.clear();
    index.observe({1, std::numeric_limits<int>::max() - 1000, 1});
    assert(index.before(std::numeric_limits<int>::max()));
    std::cout << "PASS bounded seeks, start/end, repeated rewinds, gaps, large offsets and session reset\n";
}

static void check_file(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    demo_seek::index index;
    std::vector<demo_seek::anchor> snapshots;
    std::size_t predictions{}, gamestates{};
    int last_sequence{};
    std::optional<int> predicted_time;
    while (input.good())
    {
        const auto offset = static_cast<std::int64_t>(input.tellg());
        std::uint8_t tag{};
        input.read(reinterpret_cast<char*>(&tag), 1);
        assert(input.good() && !(tag & 0x60));
        if ((tag & 31) == 31) break;
        std::uint32_t size{};
        input.read(reinterpret_cast<char*>(&size), tag & 128 ? 1 : 4);
        assert(input.good() && size <= 16 * 1024 * 1024);
        const auto id = tag & 31;
        if (id != 2 && id != 3 && id != 4)
        { input.seekg(size, std::ios::cur); continue; }
        std::vector<std::uint8_t> bytes(size);
        input.read(reinterpret_cast<char*>(bytes.data()), size);
        assert(input.good());
        if (id == 4)
        { assert(snapshots.empty()); ++gamestates; continue; }
        if (id == 3)
        {
            assert(size == 50 && bytes[0] == 0 && !predicted_time);
            int time{};
            std::memcpy(&time, bytes.data() + 2, 4);
            predicted_time = time;
            ++predictions;
            continue;
        }
        assert(size > 8);
        int sequence{};
        std::uint32_t flags{};
        std::memcpy(&sequence, bytes.data(), 4);
        std::memcpy(&flags, bytes.data() + 4, 4);
        assert(sequence > last_sequence && flags & 0x80000000);
        last_sequence = sequence;
        // Decode only the native packet header, not all entity payloads.
        std::array<std::uint8_t, 7> header{};
        z_stream stream{};
        stream.next_in = bytes.data() + 8;
        stream.avail_in = size - 8;
        stream.next_out = header.data();
        stream.avail_out = static_cast<uInt>(header.size());
        assert(inflateInit(&stream) == Z_OK);
        const auto result = inflate(&stream, Z_SYNC_FLUSH);
        assert(result == Z_OK || result == Z_STREAM_END);
        assert(stream.total_out == header.size());
        inflateEnd(&stream);
        if ((header[0] & 7) != 6) { assert(snapshots.empty()); continue; } // initial gamestate
        // Native MSG_WriteLong/Byte append aligned bytes after the opcode's
        // bit byte; they do not continue packing at bit offset three.
        int time{};
        std::memcpy(&time, header.data() + 1, 4);
        assert(header[5] == 0); // no delta base
        assert(header[6] == 8); // independent entity baseline
        assert(predicted_time && *predicted_time == time);
        predicted_time.reset();
        const demo_seek::anchor value{offset, time, sequence};
        assert(snapshots.empty() || time >= snapshots.back().time);
        snapshots.push_back(value);
        index.observe(value);
    }
    assert(gamestates == 1 && predictions == snapshots.size() && !snapshots.empty());
    int max_catchup{};
    for (const auto& frame : snapshots)
    {
        const auto value = index.before(frame.time);
        if (!value)
        { std::cerr << "Missing anchor at " << frame.time << " sequence " << frame.sequence << '\n'; }
        assert(value && value->time <= frame.time);
        max_catchup = (std::max)(max_catchup, frame.time - value->time);
        // Read the selected real file offset and confirm its packet sequence.
        if ((frame.sequence % 500) != 0) continue;
        input.clear(); input.seekg(value->offset);
        std::uint8_t tag{}; std::uint32_t size{}, sequence{};
        input.read(reinterpret_cast<char*>(&tag), 1);
        input.read(reinterpret_cast<char*>(&size), tag & 128 ? 1 : 4);
        input.read(reinterpret_cast<char*>(&sequence), 4);
        assert(input.good() && (tag & 31) == 2 && sequence == static_cast<std::uint32_t>(value->sequence));
    }
    std::cout << "PASS real converted file: " << snapshots.size() << " independent snapshots, "
        << index.size() << " anchors, max catch-up " << max_catchup << " ms\n";
}

int main(int argc, char** argv)
{
    check_index();
    if (argc > 1) check_file(argv[1]);
}
