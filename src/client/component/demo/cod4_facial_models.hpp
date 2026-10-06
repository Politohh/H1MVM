#pragma once
#include "cod4_model_metadata.hpp"
#include <optional>

namespace cod4
{
    inline bool is_lobby_facial_head(std::string_view name)
    {
        return name.starts_with("head_") && (name.ends_with("_mp_lobby") || name.ends_with("_mp_lobby_patch"));
    }
    // Read the native costume table rather than inventing a _lobby suffix:
    // patched sniper/assault heads have distinct authored identities.
    template<typename Cell>
    std::optional<std::string> lobby_head_for(std::string_view head, int rows, int columns, Cell&& cell)
    {
        if (!valid_model_name(head) || !head.starts_with("head_") || rows < 0 || rows > 16384 || columns <= 6)
            return std::nullopt;
        std::optional<std::string> result;
        for (int row = 0; row < rows; ++row)
        {
            const auto name = cell(row, 0), kind = cell(row, 1), stock = cell(row, 2);
            if (!name || !kind || !stock || head != name || std::string_view(kind) != "head"
                || std::string_view(stock) != "1") continue;
            const auto lobby = cell(row, 6);
            if (!lobby || !valid_model_name(lobby) || !std::string_view(lobby).starts_with("head_"))
                throw std::runtime_error("Invalid native lobby head mapping: " + std::string(head));
            if (result && *result != lobby)
                throw std::runtime_error("Conflicting native lobby head mapping: " + std::string(head));
            result = lobby;
        }
        return result;
    }
}
