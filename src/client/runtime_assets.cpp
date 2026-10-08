#include <std_include.hpp>
#include "runtime_assets.hpp"
#include "resource.hpp"
#include <utils/cryptography.hpp>
#include <utils/io.hpp>
#include <utils/properties.hpp>
#include <zlib.h>
#include <fstream>
#include <set>

namespace runtime_assets
{
    namespace
    {
        std::filesystem::path cache;
        nlohmann::json report;

        std::string lowercase(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
            { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        std::string sha256(const std::string& bytes)
        { return lowercase(utils::cryptography::sha256::compute(bytes, true)); }

        bool safe(const std::string& name)
        {
            if (name.empty() || name.size() > 240 || name.front() == '/' || name.back() == '/'
                || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_./-") != name.npos)
                return false;
            std::istringstream stream(name);
            std::string part;
            while (std::getline(stream, part, '/')) if (part.empty() || part == "." || part == "..") return false;
            return true;
        }

        bool matches(const std::filesystem::path& file, std::size_t size, const std::string& hash)
        {
            std::error_code error;
            if (std::filesystem::file_size(file, error) != size || error) return false;
            std::ifstream input(file, std::ios::binary);
            std::string bytes(size, '\0');
            input.read(bytes.data(), static_cast<std::streamsize>(size));
            return input.good() && sha256(bytes) == hash;
        }

        void atomic_write(const std::filesystem::path& destination, const std::string& bytes)
        {
            std::filesystem::create_directories(destination.parent_path());
            auto temporary = destination;
            temporary += L".tmp";
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                output.close();
                if (!output) throw std::runtime_error("Cannot write bundled asset: " + destination.string());
            }
            if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot install bundled asset: " + destination.string());
        }
    }

    void initialize()
    {
        const auto module = GetModuleHandleW(nullptr);
        const auto resource = FindResourceW(module, MAKEINTRESOURCEW(RUNTIME_ASSETS), MAKEINTRESOURCEW(10));
        const auto size = resource ? SizeofResource(module, resource) : 0;
        const auto loaded = resource ? LoadResource(module, resource) : nullptr;
        const auto* data = loaded ? static_cast<const unsigned char*>(LockResource(loaded)) : nullptr;
        if (!data || size < 12 || std::memcmp(data, "H1PACK01", 8))
            throw std::runtime_error("Missing or invalid embedded client bundle. Download the complete EXE again.");
        std::uint32_t header_size;
        std::memcpy(&header_size, data + 8, sizeof(header_size));
        if (!header_size || header_size > 8 * 1024 * 1024 || header_size > size - 12)
            throw std::runtime_error("Invalid client bundle header size");
        const std::string header(reinterpret_cast<const char*>(data + 12), header_size);
        const auto manifest = nlohmann::json::parse(header);
        if (manifest.at("version") != 1 || !manifest.at("files").is_array()
            || manifest.at("files").empty() || manifest.at("files").size() > 10000)
            throw std::runtime_error("Unsupported client bundle manifest");
        const auto id = sha256(header);
        wchar_t override_path[32768]{};
        const auto length = GetEnvironmentVariableW(L"H1_RUNTIME_CACHE", override_path, 32768);
        auto directory = utils::properties::get_appdata_path() / "bundles";
        if (length)
        {
            if (length >= 32768 || !std::filesystem::path(override_path).is_absolute())
                throw std::runtime_error("H1_RUNTIME_CACHE must be an absolute directory");
            directory = override_path;
        }
        cache = directory / id;
        const auto mutex = CreateMutexW(nullptr, FALSE, (L"Local\\H1Bundle-" + std::wstring(id.begin(), id.end())).c_str());
        if (!mutex) throw std::runtime_error("Cannot lock the client bundle cache");
        const auto close = gsl::finally([&] { CloseHandle(mutex); });
        const auto wait = WaitForSingleObject(mutex, 120000);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) throw std::runtime_error("Client bundle cache is busy");
        const auto unlock = gsl::finally([&] { ReleaseMutex(mutex); });
        const auto payload_size = static_cast<std::size_t>(size) - 12 - header_size;
        const auto* payload = data + 12 + header_size;
        std::set<std::string> names;
        std::size_t expected_offset = 0, extracted = 0, total = 0;
        for (const auto& entry : manifest.at("files"))
        {
            const auto name = entry.at("path").get<std::string>();
            const auto bytes = entry.at("size").get<std::size_t>();
            const auto offset = entry.at("offset").get<std::size_t>();
            const auto compressed = entry.at("compressed_size").get<std::size_t>();
            const auto hash = entry.at("sha256").get<std::string>();
            if (!safe(name) || !names.insert(lowercase(name)).second || bytes > 512 * 1024 * 1024
                || offset != expected_offset || offset > payload_size || compressed > payload_size - offset
                || !compressed || hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != hash.npos)
                throw std::runtime_error("Invalid client bundle entry: " + name);
            expected_offset += compressed;
            total += bytes;
            if (total > 2ULL * 1024 * 1024 * 1024) throw std::runtime_error("Client bundle exceeds size limit");
            const auto destination = cache / name;
            if (matches(destination, bytes, hash)) continue;
            std::string output(bytes, '\0');
            auto output_size = static_cast<uLongf>(bytes);
            if (uncompress(reinterpret_cast<Bytef*>(output.data()), &output_size, payload + offset,
                static_cast<uLong>(compressed)) != Z_OK || output_size != bytes || sha256(output) != hash)
                throw std::runtime_error("Corrupt embedded client asset: " + name + ". Download the EXE again.");
            atomic_write(destination, output);
            ++extracted;
        }
        if (expected_offset != payload_size) throw std::runtime_error("Unexpected client bundle trailing data");
        atomic_write(cache / "manifest.json", header);
        report = {{"ok", true}, {"bundle", id}, {"root", cache.generic_string()},
            {"files", names.size()}, {"bytes", total}, {"extracted_or_repaired", extracted}};
    }

    const std::filesystem::path& root()
    {
        if (cache.empty()) throw std::runtime_error("Client bundle has not been initialized");
        return cache;
    }

    std::filesystem::path path(const std::string& relative)
    {
        if (!safe(relative)) throw std::runtime_error("Invalid bundled asset path: " + relative);
        return root() / relative;
    }

    bool read(const std::string& relative, std::string* bytes)
    {
        std::ifstream input(path(relative), std::ios::binary | std::ios::ate);
        if (!input) return false;
        const auto size = input.tellg();
        if (size < 0 || size > 512 * 1024 * 1024) return false;
        bytes->resize(static_cast<std::size_t>(size));
        input.seekg(0);
        input.read(bytes->data(), size);
        return input.good();
    }

    bool obsolete_ui_package(const std::string& script, bool rawfile)
    {
        auto normalized = std::filesystem::path(script).generic_string();
        const auto start = normalized.find("ui_scripts/");
        if (start == normalized.npos) return false;
        const auto relative = normalized.substr(start);
        const auto bundled = path("cdata/" + relative);
        if (!std::filesystem::exists(bundled / "__init__.lua")) return false;
        return rawfile || lowercase(std::filesystem::weakly_canonical(script).generic_string())
            != lowercase(std::filesystem::weakly_canonical(bundled).generic_string());
    }

    void write_check(const std::filesystem::path& destination)
    { atomic_write(std::filesystem::absolute(destination), report.dump(2) + "\n"); }
}
