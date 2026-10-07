#include "component/demo/theater_library_metadata.hpp"
#include "component/demo/theater_launch.hpp"
#include "component/demo/demo_timeline.hpp"
#include <cassert>
#include <sstream>
#include <iostream>
#include <chrono>

class counted_buffer : public std::stringbuf
{
public:
    std::size_t bytes{};
    explicit counted_buffer(const std::string& data) : std::stringbuf(data) {}
    std::streamsize xsgetn(char* out, std::streamsize count) override
    { bytes += static_cast<std::size_t>(count); return std::stringbuf::xsgetn(out, count); }
};

static void chunk(std::ostream& out, unsigned char id, const std::string& data)
{
    const auto size = static_cast<std::uint32_t>(data.size());
    out.put(id); out.write(reinterpret_cast<const char*>(&size), 4); out.write(data.data(), size);
}

int main(int argc, char** argv)
{
    std::stringstream file(std::ios::in | std::ios::out | std::ios::binary);
    chunk(file, 1, std::string("mp_crash\0war\0", 13));
    chunk(file, 2, "network data need not be decompressed");
    const std::int32_t bounds[]{2500, 11750};
    chunk(file, 6, std::string(reinterpret_cast<const char*>(bounds), sizeof(bounds)));
    file.put(31);
    const auto info = theater_library::read_native(file);
    assert(info.map == "mp_crash" && info.gametype == "war" && info.duration == 9250);
    std::stringstream broken(file.str().substr(0, file.str().size() - 3));
    bool rejected = false;
    try { (void)theater_library::read_native(broken); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    // Thousands of irrelevant packets must not become UI/Play startup work.
    std::ostringstream large;
    chunk(large, 1, std::string("mp_crash\0sd\0", 12));
    for (unsigned i = 0; i < 10000; ++i) chunk(large, 2, std::string(256, 'x'));
    const std::string label("[00:27:41]\0", 11);
    const std::array<demo_timeline::event, 1> events{{{3000, 0, 1}}};
    const auto index = demo_timeline::encode(0, events, bounds[0], bounds[1]);
    const auto payload = std::string(reinterpret_cast<const char*>(bounds), sizeof(bounds)) + label + index;
    chunk(large, 6, payload); large.put(static_cast<char>(159)); large.put(0);
    counted_buffer counted(large.str()); std::istream indexed(&counted);
    const auto fast = theater_library::read_native(indexed);
    assert(fast.map == "mp_crash" && fast.gametype == "sd" && fast.duration == 9250 && counted.bytes < 140000);
    indexed.clear(); indexed.seekg(13);
    const auto tail = demo_file::read_footer(indexed);
    assert(tail && tail->bytes == std::vector<std::uint8_t>(payload.begin(), payload.end()) && indexed.tellg() == 13);
    assert(demo_timeline::decode(tail->bytes, tail->first, tail->last)->events.size() == 1);
    std::stringstream missing_eof(large.str().substr(0, large.str().size() - 1));
    assert(!demo_file::read_footer(missing_eof));
    std::stringstream wide_eof(large.str().substr(0, large.str().size() - 2) + std::string("\x1f\0\0\0\0", 5));
    assert(demo_file::read_footer(wide_eof));
    assert(theater_library::can_launch(false, false, false, false));
    assert(theater_library::can_launch(false, false, true, true));
    assert(!theater_library::can_launch(false, false, true, false));
    assert(!theater_library::can_launch(true, false, false, false));
    assert(!theater_library::can_launch(false, true, false, false));
    std::stringstream oversized;
    oversized.put(1); const std::uint32_t huge = UINT32_MAX;
    oversized.write(reinterpret_cast<const char*>(&huge), 4);
    rejected = false;
    try { (void)theater_library::read_native(oversized); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    for (int i = 1; i < argc; ++i)
    {
        const std::filesystem::path path(argv[i]);
        const auto started = std::chrono::steady_clock::now();
        theater_library::metadata actual;
        if (path.extension() == ".dm_1") actual = theater_library::read_source(path);
        else { std::ifstream native(path, std::ios::binary); if (!native) throw std::runtime_error("Cannot open native fixture"); actual = theater_library::read_native(native); }
        assert(!actual.map.empty() && actual.duration > 0);
        std::cout << path.filename().string() << ": " << actual.map << " / " << actual.gametype << " / " << actual.duration
            << " ms; metadata read " << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() << " ms\n";
    }
    std::cout << "PASS native H1 and COD4 library metadata; truncated and oversized chunks rejected\n";
}
