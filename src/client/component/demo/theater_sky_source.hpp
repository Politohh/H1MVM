#pragma once
#include "theater_sky_format.hpp"
#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

namespace theater_sky::source
{
    inline bool identifier(const std::string& value)
    { return !value.empty() && value.size() <= 128 && value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == value.npos; }

    // Independent 64 KiB LZ4 blocks in the authenticated stock S1 stream.
    // Every read, match distance and output length is bounded before copying.
    inline std::vector<std::uint8_t> decode(std::span<const std::uint8_t> input, std::size_t expected)
    {
        if (input.empty() || input.size() > 0x10200 || !expected || expected > 0x10000)
            throw std::runtime_error("Invalid sky compression block bounds");
        std::vector<std::uint8_t> output;
        output.reserve(expected);
        std::size_t cursor = 0;
        const auto length = [&](std::size_t initial) {
            auto result = initial;
            if (initial == 15)
            {
                unsigned extra;
                do {
                    if (cursor >= input.size()) throw std::runtime_error("Truncated sky compression length");
                    extra = input[cursor++]; result += extra;
                    if (result > expected) throw std::runtime_error("Oversized sky compression length");
                } while (extra == 255);
            }
            return result;
        };
        while (cursor < input.size())
        {
            const auto token = input[cursor++];
            const auto literals = length(token >> 4);
            if (literals > input.size() - cursor || literals > expected - output.size())
                throw std::runtime_error("Truncated sky literal data");
            output.insert(output.end(), input.begin() + cursor, input.begin() + cursor + literals);
            cursor += literals;
            if (cursor == input.size()) break;
            if (input.size() - cursor < 2) throw std::runtime_error("Truncated sky match offset");
            const auto distance = input[cursor] | (static_cast<unsigned>(input[cursor + 1]) << 8);
            cursor += 2;
            const auto count = length(token & 15) + 4;
            if (!distance || distance > output.size() || count > expected - output.size())
                throw std::runtime_error("Invalid sky match bounds");
            for (std::size_t i = 0; i < count; ++i) output.push_back(output[output.size() - distance]);
        }
        if (output.size() != expected) throw std::runtime_error("Incorrect sky decoded size");
        return output;
    }

    inline header description(const nlohmann::json& entry)
    {
        header h{}; std::memcpy(h.magic, "H1SKY001", 8);
        h.format = entry.at("format"); h.width = entry.at("width"); h.height = entry.at("height");
        h.mips = entry.at("mips"); h.flags = entry.at("flags"); h.size = entry.at("size");
        (void)layout(h);
        return h;
    }

    inline void validate(const nlohmann::json& entry)
    {
        if (!identifier(entry.at("name")) || !identifier(entry.at("zone")))
            throw std::runtime_error("Invalid sky source identity");
        const auto hash = entry.at("sha256").get<std::string>();
        if (hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != hash.npos)
            throw std::runtime_error("Invalid sky content hash");
        const auto h = description(entry);
        const auto source_size = entry.at("source_size").get<std::uint64_t>();
        if (source_size < 64 || source_size > 8ULL * 1024 * 1024 * 1024)
            throw std::runtime_error("Invalid sky source size");
        const auto& blocks = entry.at("blocks");
        if (!blocks.is_array() || blocks.empty() || blocks.size() > 8192)
            throw std::runtime_error("Invalid sky block count");
        std::size_t total = 0;
        for (const auto& block : blocks)
        {
            const auto decoded = block.at("decoded").get<std::size_t>();
            const auto skip = block.at("skip").get<std::size_t>(), take = block.at("take").get<std::size_t>();
            if (!decoded || decoded > 0x10000 || skip > decoded || !take || take > decoded - skip)
                throw std::runtime_error("Invalid sky pixel slice");
            const auto& segments = block.at("segments");
            if (!segments.is_array() || segments.empty() || segments.size() > 2)
                throw std::runtime_error("Invalid sky source segments");
            std::size_t encoded = 0;
            for (const auto& segment : segments)
            {
                if (!segment.is_array() || segment.size() != 2) throw std::runtime_error("Invalid sky segment");
                const auto offset = segment[0].get<std::uint64_t>(), size = segment[1].get<std::uint64_t>();
                if (offset < 64 || offset > source_size || !size || size > 0x10200 || size > source_size - offset)
                    throw std::runtime_error("Sky block exceeds source file");
                encoded += static_cast<std::size_t>(size);
            }
            if (encoded > 0x10200) throw std::runtime_error("Oversized compressed sky block");
            total += take;
        }
        if (total != h.size) throw std::runtime_error("Sky index does not cover all pixels");
    }

    inline std::string extract(const nlohmann::json& entry, const std::filesystem::path& game_directory)
    {
        validate(entry);
        const auto filename = entry.at("zone").get<std::string>() + ".ff";
        const auto file = game_directory / filename;
        std::ifstream input(file, std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("Sky unavailable: install the MWR game file " + filename);
        if (input.tellg() != static_cast<std::streamoff>(entry.at("source_size").get<std::uint64_t>()))
            throw std::runtime_error("Sky unavailable: unsupported MWR file version (" + filename + ")");
        std::array<char, 12> prefix{};
        input.seekg(0); input.read(prefix.data(), prefix.size());
        std::uint32_t version; std::memcpy(&version, prefix.data() + 8, 4);
        if (!input || (std::memcmp(prefix.data(), "S1ff0100", 8) && std::memcmp(prefix.data(), "S1ffu100", 8)) || version != 0x42)
            throw std::runtime_error("Sky unavailable: unsupported MWR archive (" + filename + ")");
        const auto h = description(entry);
        std::string result(reinterpret_cast<const char*>(&h), sizeof(h));
        result.reserve(sizeof(h) + h.size);
        for (const auto& block : entry.at("blocks"))
        {
            std::vector<std::uint8_t> encoded;
            for (const auto& segment : block.at("segments"))
            {
                const auto offset = segment[0].get<std::uint64_t>(), size = segment[1].get<std::size_t>();
                const auto start = encoded.size(); encoded.resize(start + size);
                input.seekg(static_cast<std::streamoff>(offset));
                input.read(reinterpret_cast<char*>(encoded.data() + start), static_cast<std::streamsize>(size));
                if (!input) throw std::runtime_error("Truncated MWR sky archive: " + filename);
            }
            const auto decoded = decode(encoded, block.at("decoded"));
            result.append(reinterpret_cast<const char*>(decoded.data() + block.at("skip").get<std::size_t>()), block.at("take"));
        }
        return result;
    }
}
