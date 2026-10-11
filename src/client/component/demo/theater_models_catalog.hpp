#pragma once
#include <algorithm>
#include <json.hpp>
#include <string>
#include <vector>
#include <map>
#include <cctype>

namespace theater_models
{
    // A named headgear look has faction/class variants, not thousands of
    // independent outfits. Pair with the base uniform for that exact class.
    inline const nlohmann::json* wardrobe_body(const nlohmann::json& catalog, const nlohmann::json& head)
    {
        for (const auto& c : catalog)
            if (c.value("kind", "") == "body" && c.value("collection", "") == "Default" &&
                c.value("label", "") == head.value("faction", "") + " " + head.value("role", "")) return &c;
        return nullptr;
    }
    inline nlohmann::json wardrobe_page(const nlohmann::json& catalog, const std::string& kind,
        const std::string& category, const std::string& group, int requested_page, const std::string& query = "",
        const std::string& team = "")
    {
        auto lower = [](std::string s) { for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); return s; };
        const auto needle = lower(query);
        std::map<std::string, std::vector<const nlohmann::json*>> groups;
        auto categories = nlohmann::json::array({"All looks", "Default"});
        for (const auto& c : catalog)
        {
            if (c.value("kind", "") != (kind == "body" ? "body" : "head")) continue;
            if (kind == "look" && !wardrobe_body(catalog, c)) continue;
            const auto collection = c.value("collection", "Default");
            if (std::find(categories.begin(), categories.end(), collection) == categories.end()) categories.push_back(collection);
            if (category != "All looks" && category != collection) continue;
            if (group.empty() && !needle.empty() && lower(c.value("searchLabel", c.value("label", "")) + " " +
                c.value("faction", "") + " " + c.value("role", "")).find(needle) == std::string::npos) continue;
            const auto name = collection == "Default" ? c.value("faction", "Other") : c.value("label", "");
            // Preserve distinct catalog labels even if localization is unavailable.
            groups[collection + "|" + name].push_back(&c);
        }
        auto all = nlohmann::json::array();
        for (const auto& [key, variants] : groups)
        {
            if (!group.empty() && key != group) continue;
            if (group.empty())
            {
                const auto& c = *variants.front();
                auto card = c;
                card["group"] = key;
                card["label"] = c.value("collection", "") == "Default" ? c.value("faction", "Other") : c.value("label", "");
                card["variants"] = variants.size();
                all.push_back(std::move(card));
            }
            else if (kind != "body" && team.empty())
            {
                std::map<std::string, const nlohmann::json*> teams;
                for (const auto* c : variants) teams.try_emplace(c->value("faction", "Other"), c);
                for (const auto& [name, c] : teams)
                {
                    auto card = *c;
                    card["team"] = name;
                    card["group"] = key;
                    all.push_back(std::move(card));
                }
            }
            else for (const auto* c : variants)
            {
                if (kind != "body" && c->value("faction", "") != team) continue;
                auto card = *c;
                card["group"] = key;
                card["bodyId"] = kind == "look" ? wardrobe_body(catalog, *c)->value("id", 0) : 0;
                all.push_back(std::move(card));
            }
        }
        const auto pages = std::max(1, static_cast<int>((all.size() + 7) / 8));
        const auto page = std::clamp(requested_page, 1, pages);
        auto items = nlohmann::json::array();
        const auto first = static_cast<std::size_t>(page - 1) * 8;
        for (auto i = first; i < std::min(first + 8, all.size()); ++i) items.push_back(all[i]);
        const auto stage = group.empty() ? "looks" : kind == "body" ? "variants" : team.empty() ? "teams" : "classes";
        return {{"items", items}, {"categories", categories}, {"page", page}, {"pages", pages}, {"total", all.size()}, {"stage", stage}};
    }
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
