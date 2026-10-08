#pragma once
#include <filesystem>
#include <string>

namespace runtime_assets
{
    // Initialized before any component or game code starts. Never depends on CWD.
    void initialize();
    const std::filesystem::path& root();
    std::filesystem::path path(const std::string& relative);
    bool read(const std::string& relative, std::string* bytes);
    bool obsolete_ui_package(const std::string& script, bool rawfile);
    void write_check(const std::filesystem::path& destination);
}
