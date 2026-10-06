#include "game/ui_scripting/json_conversion.hpp"
#include <cassert>
#include <iostream>

// Model HKS table indexing while exercising the exact production converter.
struct lua_table
{
    nlohmann::json fields = nlohmann::json::object();
    nlohmann::json& operator[](const std::string& key) { return fields[key]; }
    nlohmann::json& operator[](int key) { return fields[std::to_string(key)]; }
    operator nlohmann::json() const { return fields; }
};

int main()
{
    const nlohmann::json snapshot = {
        {"revision", 7}, {"values", {{"sun_intensity", .75}}},
        {"controls", nlohmann::json::array({
            {{"page", "sun"}, {"name", "sun_intensity"}, {"kind", "float"}},
            {{"page", "weapon"}, {"name", "cg_drawGun"}, {"choices", {"OFF", "ON"}}}
        })},
        {"custom", {"sun_intensity", "fog_density"}},
        {"events", nlohmann::json::array({{{"time", 1250}, {"death", true}}})},
        {"keys", nlohmann::json::array({{{"time", 900}, {"index", 0}}})},
        {"empty", nlohmann::json::array()}, {"null", nullptr}
    };
    const auto converted = ui_scripting::convert_json<nlohmann::json, lua_table>(snapshot);
    assert(converted["revision"] == 7 && converted["values"]["sun_intensity"] == .75);
    assert(converted["controls"].size() == 2 && !converted["controls"].contains("0"));
    assert(converted["controls"]["1"]["name"] == "sun_intensity");
    assert(converted["controls"]["2"]["choices"]["2"] == "ON");
    assert(converted["custom"]["1"] == "sun_intensity" && converted["custom"]["2"] == "fog_density");
    assert(converted["events"]["1"]["death"] == true);
    assert(converted["keys"]["1"]["index"] == 0);
    assert(converted["empty"].is_object() && converted["empty"].empty());
    assert(converted["null"].is_null());
    std::cout << "PASS production JSON bridge: populated one-based controls, choices, custom sliders, events and camera keys\n";
}
