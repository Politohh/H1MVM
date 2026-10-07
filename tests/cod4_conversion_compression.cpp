#include "component/demo/cod4_conversion_compression.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

void roundtrip(const std::vector<std::uint8_t>& native)
{
    const auto compressed = cod4::compress_conversion_packet(native);
    std::vector<std::uint8_t> restored(native.size() + 1);
    uLongf length = static_cast<uLongf>(restored.size());
    check(uncompress(restored.data(), &length, compressed.data(), static_cast<uLong>(compressed.size())) == Z_OK,
        "Cannot decode compressed packet");
    restored.resize(length);
    check(restored == native && compressed.size() + 8 <= 131072,
        "Conversion changed packet bytes or exceeded native chunk bounds");
}

int main(int argc, char** argv)
{
    try
    {
        for (const auto size : {0u, 1u, 255u, 8192u, 65536u, 131072u})
        {
            roundtrip(std::vector<std::uint8_t>(size));
            std::vector<std::uint8_t> structured(size);
            for (unsigned i = 0; i < size; ++i) structured[i] = static_cast<std::uint8_t>((i / 64 + i % 7) % 256);
            roundtrip(structured);
        }
        std::uint32_t random_state = 1;
        std::vector<std::uint8_t> noise(131072);
        for (auto& byte : noise)
        {
            random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5;
            byte = static_cast<std::uint8_t>(random_state);
        }
        roundtrip(std::vector<std::uint8_t>(noise.begin(), noise.begin() + 65536));
        bool rejected = false;
        try { (void)cod4::compress_conversion_packet(noise); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Incompressible packet exceeded the native chunk limit");
        rejected = false;
        try { (void)cod4::compress_conversion_packet(std::vector<std::uint8_t>(131073)); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Oversized conversion input was accepted");
        std::cout << "PASS conversion packet roundtrips, empty and maximum-size packets, and native size limits\n";
        if (argc == 1) return 0;
        check(argc == 2, "Pass at most one completed native conversion");
        std::ifstream file(argv[1], std::ios::binary);
        check(bool(file), "Cannot open conversion");
        unsigned packets{}, sampled{};
        double old_ms{}, fast_ms{};
        std::size_t old_bytes{}, fast_bytes{};
        for (;;)
        {
            std::uint8_t tag{}; file.read(reinterpret_cast<char*>(&tag), 1);
            check(bool(file), "Missing native EOF");
            if ((tag & 31) == 31) break;
            std::uint32_t size{}; file.read(reinterpret_cast<char*>(&size), tag & 128 ? 1 : 4);
            check(bool(file) && size <= 131072, "Invalid native chunk");
            if ((tag & 31) != 2 || (++packets > 32 && packets % 512 != 0))
            { file.seekg(size, std::ios::cur); continue; }
            std::vector<std::uint8_t> data(size);
            file.read(reinterpret_cast<char*>(data.data()), size);
            check(bool(file) && size > 8 && (data[7] & 128), "Invalid converted network data");
            std::vector<std::uint8_t> native(131072);
            uLongf length = static_cast<uLongf>(native.size());
            check(uncompress(native.data(), &length, data.data() + 8, size - 8) == Z_OK, "Cannot decode native packet");
            native.resize(length);
            auto start = std::chrono::steady_clock::now();
            const auto compressed = cod4::compress_conversion_packet(native);
            fast_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::vector<std::uint8_t> old(compressBound(static_cast<uLong>(native.size())));
            uLongf old_length = static_cast<uLongf>(old.size());
            start = std::chrono::steady_clock::now();
            check(compress2(old.data(), &old_length, native.data(), static_cast<uLong>(native.size()), Z_BEST_COMPRESSION) == Z_OK,
                "Baseline compression failed");
            old_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::vector<std::uint8_t> restored(native.size());
            length = static_cast<uLongf>(restored.size());
            check(uncompress(restored.data(), &length, compressed.data(), static_cast<uLong>(compressed.size())) == Z_OK
                && length == native.size() && restored == native && compressed.size() + 8 <= 131072,
                "Fast conversion changed packet bytes or exceeded native chunk bounds");
            ++sampled; old_bytes += old_length; fast_bytes += compressed.size();
        }
        check(sampled > 0, "Recording contains no converted packets");
        std::cout << "PASS: " << sampled << " actual packets across " << packets << " network chunks; byte-identical decode. Level 9 "
            << old_ms << " ms, fast " << fast_ms << " ms; compressed bytes " << old_bytes << " -> " << fast_bytes << '\n';
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
