#pragma once
#include <json.hpp>

namespace ui_scripting
{
    // Shared by the HKS bridge and its regression test. Lua arrays are one-based.
    template<class Value, class Table>
    Value convert_json(const nlohmann::json& json)
    {
        if (json.is_object())
        {
            Table object;
            for (const auto& [key, value] : json.items())
                object[key] = convert_json<Value, Table>(value);
            return object;
        }
        if (json.is_array())
        {
            Table array;
            int index = 1;
            // json::array() constructs a NEW empty array, even called on an instance.
            for (const auto& value : json)
                array[index++] = convert_json<Value, Table>(value);
            return array;
        }
        if (json.is_boolean()) return json.get<bool>();
        if (json.is_number_integer()) return json.get<int>();
        if (json.is_number_float()) return json.get<float>();
        if (json.is_string()) return json.get<std::string>();
        return {};
    }
}
