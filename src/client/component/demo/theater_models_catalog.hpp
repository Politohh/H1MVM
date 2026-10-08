#pragma once
#include <algorithm>
#include <json.hpp>
#include <string>
#include <vector>

namespace theater_models
{
    // H1's small LUI heap must not own thousands of catalog tables. Filter
    // in native memory and transfer only the visible cards plus filter labels.
    inline nlohmann::json catalog_page(const nlohmann::json& catalog,
        const std::string& kind, const std::string& faction, const std::string& role,
        const std::string& collection, int requested_page,
        const std::string& head, const std::string& body)
    {
        std::vector<const nlohmann::json*> matches;
        auto collections = nlohmann::json::array({"All collections"});
        nlohmann::json result = {{"headLabel", "Recorded appearance"}, {"bodyLabel", "Recorded appearance"}};
        for (const auto& c : catalog)
        {
            const auto model = c.value("model", "");
            if (model == head) result["headLabel"] = c.value("label", model);
            if (model == body) result["bodyLabel"] = c.value("label", model);
            if (c.value("kind", "") != kind) continue;
            const auto set = c.value("collection", "Default");
            if (std::find(collections.begin(), collections.end(), set) == collections.end()) collections.push_back(set);
            if ((faction.empty() || c.value("faction", "") == faction) &&
                (role.empty() || c.value("role", "") == role) &&
                (collection == "All collections" || set == collection)) matches.push_back(&c);
        }
        const auto pages = std::max(1, static_cast<int>((matches.size() + 3) / 4));
        const auto page = std::clamp(requested_page, 1, pages);
        auto items = nlohmann::json::array();
        const auto first = static_cast<std::size_t>(page - 1) * 4;
        for (auto i = first; i < std::min(first + 4, matches.size()); ++i) items.push_back(*matches[i]);
        result.update({{"items", items}, {"collections", collections}, {"total", matches.size()}, {"pages", pages}, {"page", page}});
        return result;
    }
}
