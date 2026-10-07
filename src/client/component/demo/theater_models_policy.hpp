#pragma once
#include <array>
#include <string>
#include <string_view>
#include <optional>

namespace theater_models
{
    // The native characterInfo stores a body and eight independently tagged
    // attachments. Keep weapons/equipment intact when replacing a head.
    struct composition
    {
        std::string body;
        std::array<std::string, 8> models{}, tags{};
        bool operator==(const composition&) const = default;
    };
    inline std::optional<composition> replace(const composition& original,
        std::string_view head, std::string_view body)
    {
        auto result = original;
        if (!body.empty()) result.body = body;
        if (!head.empty())
        {
            std::size_t slot = 8;
            for (std::size_t i = 0; i < 8; ++i)
                if (original.models[i].starts_with("head_")) { slot = i; break; }
            if (slot == 8)
                for (std::size_t i = 0; i < 8; ++i)
                    if (original.models[i].empty()) { slot = i; break; }
            if (slot == 8) return std::nullopt;
            result.models[slot] = head;
            // An empty tag is intentional: native character parts merge
            // their shared skeleton. Attaching a skinned head to j_head
            // applies its authored offset twice and makes it float.
        }
        return result;
    }
}
