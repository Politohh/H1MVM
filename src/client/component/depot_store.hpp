#pragma once

#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <mutex>
#include <stdexcept>
#ifdef _WIN32
#ifndef _WINDOWS_
#define NOMINMAX
#include <Windows.h>
#endif
#endif

// Local Depot data follows the active stats profile. Never truncate the live
// file: serialize/validate first, then replace it with the completed temp file.
namespace depot_store
{
    inline constexpr std::size_t max_size = 8 * 1024 * 1024;
    inline std::mutex store_mutex;

    inline nlohmann::json defaults()
    {
        return {{"currencies", {{"launchCredits", 0}, {"credits", 0}, {"parts", 0},
            {"codPoints", 0}, {"bonus", 0}}}, {"items", nlohmann::json::object()},
            {"reward_splashes", nlohmann::json::object()},
            {"has_accepted_mod_eula", false}, {"has_seen_mod_eula", false}};
    }

    inline nlohmann::json normalize(nlohmann::json data)
    {
        if (!data.is_object()) throw std::runtime_error("Depot data must be an object");
        const auto base = defaults();
        for (const auto& key : {"currencies", "items", "reward_splashes"})
        {
            // The old Lua encoder represented empty maps as [].
            if (!data.contains(key) || (data[key].is_array() && data[key].empty())) data[key] = base[key];
            if (!data[key].is_object()) throw std::runtime_error("Invalid Depot map");
        }
        for (const auto& [key, value] : base["currencies"].items())
        {
            if (!data["currencies"].contains(key)) data["currencies"][key] = value;
            const auto& amount = data["currencies"][key];
            if (!amount.is_number_integer() || amount.get<double>() < 0 || amount.get<double>() > 2147483647.0)
                throw std::runtime_error("Invalid Depot currency balance");
        }
        for (const auto& [key, value] : data["items"].items())
        {
            if (key.empty() || (!value.is_boolean() && !value.is_number_integer()) ||
                (value.is_number_integer() && (value.get<double>() < 0 || value.get<double>() > 2147483647.0)))
                throw std::runtime_error("Invalid Depot item quantity");
        }
        for (const auto& [key, value] : data["reward_splashes"].items())
            if (key.empty() || !value.is_boolean()) throw std::runtime_error("Invalid Depot reward state");
        for (const auto& key : {"has_accepted_mod_eula", "has_seen_mod_eula"})
        {
            if (!data.contains(key)) data[key] = false;
            if (!data[key].is_boolean()) throw std::runtime_error("Invalid Depot preference");
        }
        return data;
    }

    inline nlohmann::json read(const std::filesystem::path& path)
    {
        if (std::filesystem::file_size(path) > max_size) throw std::runtime_error("Depot file exceeds size limit");
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("Cannot read Depot file");
        return normalize(nlohmann::json::parse(stream));
    }

    inline nlohmann::json load(const std::filesystem::path& path)
    {
        const std::lock_guard lock(store_mutex);
        return std::filesystem::exists(path) ? read(path) : defaults();
    }

    inline void save(const std::filesystem::path& path, const std::string& input)
    {
        if (input.size() > max_size) throw std::runtime_error("Depot data exceeds size limit");
        const auto encoded = normalize(nlohmann::json::parse(input)).dump(2);
        if (encoded.size() > max_size) throw std::runtime_error("Encoded Depot data exceeds size limit");
        const std::lock_guard lock(store_mutex);
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        auto temporary = path;
        temporary += ".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            stream.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
            stream.flush();
            if (!stream) throw std::runtime_error("Cannot write Depot temporary file");
        }
        if (std::filesystem::exists(path))
        {
            auto backup = path;
            try { (void)read(path); backup += ".bak"; }
            catch (const std::exception&)
            {
                backup += ".corrupt." + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
            }
            std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing);
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace Depot file");
#else
        std::filesystem::rename(temporary, path);
#endif
    }
}
