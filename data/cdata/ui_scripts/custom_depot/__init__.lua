if game:issingleplayer() then return end

InventoryCurrencyType = {LaunchCredits = 1, Credits = 2, Parts = 3, CoDPoints = 4, Bonus = 5, Max = 6}
local currency_names = {"launchCredits", "credits", "parts", "codPoints", "bonus"}
custom_depot = {
    data = {
        currencies = {launchCredits = 0, credits = 0, parts = 0, codPoints = 0, bonus = 0},
        items = {}, reward_splashes = {},
        has_accepted_mod_eula = false, has_seen_mod_eula = false
    }, functions = {}, pending = {}
}
function custom_depot.get_function(name) return custom_depot.functions[name] end
local functions = custom_depot.functions
functions.save_depot_data = function()
    local ok, saved = pcall(function() return customdepot.save(json.encode(custom_depot.data)) end)
    if not ok or saved ~= true then
        print("[Depot] Could not save local inventory; transaction cancelled.")
        return false
    end
    return true
end
functions.load_depot_data = function()
    local loaded = customdepot.load()
    if type(loaded) == "table" then custom_depot.data = loaded end
end
local function amount(value)
    value = tonumber(value)
    if not value or value ~= value or value < 0 or value > 2147483647 or value ~= math.floor(value) then return nil end
    return value
end
functions.get_currency = function(currency)
    return custom_depot.data.currencies[currency_names[currency]] or 0
end
functions.add_currency = function(currency, value)
    local key, count = currency_names[currency], amount(value)
    if not key or not count or functions.get_currency(currency) + count > 2147483647 then return false end
    custom_depot.data.currencies[key] = functions.get_currency(currency) + count
    return true
end
functions.remove_currency = function(currency, value)
    local key, count = currency_names[currency], amount(value)
    if not key or not count or functions.get_currency(currency) < count then return false end
    custom_depot.data.currencies[key] = functions.get_currency(currency) - count
    return true
end
functions.get_quantity = function(item)
    if item == nil then return 0 end
    local value = custom_depot.data.items[tostring(item)]
    return value == true and 1 or (amount(value) or 0)
end
functions.add_item = function(item, value)
    if item == nil or tostring(item) == "" then return false end
    if value ~= true and value ~= false and not amount(value) then return false end
    custom_depot.data.items[tostring(item)] = value
    return true
end
functions.has_item = function(item) return functions.get_quantity(item) > 0 end
functions.add_reward_splash = function(item, value) custom_depot.data.reward_splashes[tostring(item)] = value == true end
functions.has_reward_splash = function(item) return custom_depot.data.reward_splashes[tostring(item)] == true end
functions.has_accepted_mod_eula = function() return custom_depot.data.has_accepted_mod_eula == true end
functions.has_seen_mod_eula = function() return custom_depot.data.has_seen_mod_eula == true end
functions.set_has_accepted_mod_eula = function(value)
    custom_depot.data.has_accepted_mod_eula = value == true
    return functions.save_depot_data()
end
functions.set_has_seen_mod_eula = function(value)
    custom_depot.data.has_seen_mod_eula = value == true
    return functions.save_depot_data()
end
-- Publish inventory changes only after the complete state reaches disk.
function custom_depot.commit(change)
    local before = LUI.DeepCopy(custom_depot.data)
    local ok, changed = pcall(change)
    if not ok or changed ~= true or not functions.save_depot_data() then
        custom_depot.data = before
        return false
    end
    -- Native invalidation assumes the collection cache has been initialized.
    if GetItemSets then GetItemSets() end
    if InvalidateItemSets then InvalidateItemSets() end
    return true
end
functions.load_depot_data()
require("inventory")
if Engine.InFrontend() then require("depot_override") else require("scoreboard_override") end
print("[Depot] Local inventory loaded; frontend=" .. tostring(Engine.InFrontend())
    .. ", parts=" .. tostring(functions.get_currency(InventoryCurrencyType.Parts)))
