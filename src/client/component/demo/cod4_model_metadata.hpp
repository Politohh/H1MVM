#pragma once
#include <charconv>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cod4
{
    inline bool valid_model_name(std::string_view name)
    {
        if (name.empty() || name.size() > 128) return false;
        for (const auto c : name)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                || c == '_' || c == '/' || c == '.' || c == '-')) return false;
        return true;
    }
    struct model_metadata
    {
        std::set<std::string> names;
        std::map<unsigned, std::string> bindings;
        std::map<unsigned, int> grenade_times;
    };
    inline model_metadata parse_model_metadata(const std::vector<std::string>& entries)
    {
        if (entries.size() > 2048) throw std::runtime_error("Too many converted metadata records");
        model_metadata result;
        for (const auto& entry : entries)
        {
            if (entry.starts_with("@grenade:"))
            {
                const std::string_view text(entry);
                const auto separator = text.find(':', 9);
                if (separator == text.npos) throw std::runtime_error("Incomplete grenade timing record");
                unsigned number{};
                int time{};
                const auto entity = std::from_chars(text.data() + 9, text.data() + separator, number);
                const auto clock = std::from_chars(text.data() + separator + 1, text.data() + text.size(), time);
                if (entity.ec != std::errc{} || entity.ptr != text.data() + separator || number < 1024 || number >= 2046
                    || clock.ec != std::errc{} || clock.ptr != text.data() + text.size() || time < 0
                    || !result.grenade_times.emplace(number, time).second)
                    throw std::runtime_error("Invalid grenade timing record");
                continue;
            }
            if (!entry.starts_with("@model:"))
            {
                if (!valid_model_name(entry)) throw std::runtime_error("Invalid converted model name");
                result.names.insert(entry);
                continue;
            }
            const std::string_view text(entry);
            const auto separator = text.find(':', 7);
            if (separator == text.npos) throw std::runtime_error("Incomplete model binding");
            unsigned index{};
            const auto parsed = std::from_chars(text.data() + 7, text.data() + separator, index);
            const auto name = text.substr(separator + 1);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + separator || index < 768
                || index >= 1024 || !valid_model_name(name)) throw std::runtime_error("Invalid model binding");
            if (!result.bindings.emplace(index, name).second) throw std::runtime_error("Duplicate model binding");
            result.names.emplace(name);
        }
        if (result.names.size() > 512) throw std::runtime_error("Too many converted model names");
        return result;
    }
}
