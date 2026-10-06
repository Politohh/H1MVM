#include "component/demo/theater_library_metadata.hpp"
#include <cassert>
#include <sstream>
#include <iostream>

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
    std::stringstream oversized;
    oversized.put(1); const std::uint32_t huge = UINT32_MAX;
    oversized.write(reinterpret_cast<const char*>(&huge), 4);
    rejected = false;
    try { (void)theater_library::read_native(oversized); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    for (int i = 1; i < argc; ++i)
    {
        const std::filesystem::path path(argv[i]);
        theater_library::metadata actual;
        if (path.extension() == ".dm_1") actual = theater_library::read_source(path);
        else { std::ifstream native(path, std::ios::binary); actual = theater_library::read_native(native); }
        assert(!actual.map.empty() && actual.duration > 0);
        std::cout << path.filename().string() << ": " << actual.map << " / " << actual.gametype << " / " << actual.duration << " ms\n";
    }
    std::cout << "PASS native H1 and COD4 library metadata; truncated and oversized chunks rejected\n";
}
