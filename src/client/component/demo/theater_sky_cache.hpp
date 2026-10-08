#pragma once
#include "theater_sky_source.hpp"
#include <functional>

namespace theater_sky::source
{
    using hash_function = std::function<std::string(const std::string&)>;

    inline std::string load(const nlohmann::json& entry, const std::filesystem::path& game_directory,
        const std::filesystem::path& cache, const hash_function& hash)
    {
        validate(entry);
        const auto expected = entry.at("sha256").get<std::string>();
        const auto file = cache / expected / (entry.at("name").get<std::string>() + ".h1sky");
        const auto size = sizeof(header) + description(entry).size;
        std::ifstream existing(file, std::ios::binary | std::ios::ate);
        if (existing && existing.tellg() == static_cast<std::streamoff>(size))
        {
            std::string bytes(size, '\0'); existing.seekg(0);
            existing.read(bytes.data(), static_cast<std::streamsize>(size));
            if (existing && hash(bytes) == expected) return bytes;
        }
        existing.close();
        auto bytes = extract(entry, game_directory);
        if (hash(bytes) != expected) throw std::runtime_error("Sky data differs from the supported MWR preset: " + entry.at("name").get<std::string>());
        std::filesystem::create_directories(file.parent_path());
        // Separate temporary names allow two game instances to populate the same cache.
        auto temporary = file;
        temporary += "." + std::to_string(GetCurrentProcessId()) + ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); output.close();
            if (!output) throw std::runtime_error("Cannot cache the selected sky");
        }
        if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot finish caching the selected sky");
        return bytes;
    }
}
