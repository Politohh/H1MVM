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
    };
    inline model_metadata parse_model_metadata(const std::vector<std::string>& entries)
    {
        if (entries.size() > 512) throw std::runtime_error("Too many converted model records");
        model_metadata result;
        for (const auto& entry : entries)
        {
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
        return result;
    }
}
