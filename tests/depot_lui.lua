unpack = unpack or table.unpack
local function copy(value)
    if type(value) ~= "table" then return value end
    local result = {}; for key, item in pairs(value) do result[key] = copy(item) end; return result
end
local function equal(left, right)
    if type(left) ~= type(right) then return false end
    if type(left) ~= "table" then return left == right end
    for key, value in pairs(left) do if not equal(value, right[key]) then return false end end
    for key in pairs(right) do if left[key] == nil then return false end end
    return true
end
local Element = {}; Element.__index = Element
function Element.new() return setmetatable({handlers = {}, children = {}}, Element) end
function Element:registerEventHandler(name, handler) self.handlers[name] = handler end
function Element:addElement(child) self.children[#self.children + 1] = child end
function Element:processEvent(event)
    if self.handlers[event.name] then self.handlers[event.name](self, event) end
    for _, child in ipairs(self.children) do if child.processEvent then child:processEvent(event) end end
end
local unlock, frontend, replay, fail_save = false, true, false, false
local persisted, save_count, collections, cache_ready
local loot = {
    {"head", "costume", "head_guid", "3", "100", "buy_head", "0", "LootDrop1"},
    {"camo", "charactercamo", "camo_guid", "4", "200", "buy_camo", "0", "LootDrop1"},
    {"native_head", "costume", "native_guid", "3", "100", "buy_native", "0", "LootDrop1"},
    {"set_reward", "costume", "reward_guid", "2", "0", "", "0", "LootDrop1"},
    {"season_reward", "charactercamo", "season_guid", "2", "0", "", "0", "LootDrop1"},
    {"locked_reward", "costume", "locked_reward_guid", "2", "0", "", "0", "LootDrop1"}
}
local bundle = {"bundle_id", "bundle_name", "buy_bundle", "name", "bundle_limit", "1", "2", "10", "3", "50",
    "crate_guid", "2", "", "0", "", "0", "", "0"}
local native_items = {native_guid = 1}
local native_tab = function(menu, categories)
    menu.tabManager = {categories = categories}; menu.populated = true
end
local native_calls = 0
local function setup(in_frontend)
    frontend = in_frontend
    LUI = {UIElement = Element, UITimer = {new = function(interval, event) return {interval = interval, event = event} end},
        DeepCopy = copy, MenuBuilder = {m_types_build = {}, m_definitions = {}}, FlowManager = {}, Options = {},
        MPDepotBase = {AddLootDropTabSelector = native_tab},
        InventoryUtils = {TaskType = {INV_TASK_PURCHASE_ITEM = 23}, EventType = {ELUIInventoryEvent_TaskCompleted = 3}},
        MPDepot = {BundlesTable = {File = "bundles", Cols = {ID = 0, PurchaseID = 2, PurchaseLimitItem = 4,
            PurchaseLimitQuantity = 5, PurchaseCurrency = 6, PurchaseAmount = 7, CurrencyAwardID = 8, CurrencyAwardAmount = 9,
            Item0ID = 10, Item0Amount = 11, Item1ID = 12, Item1Amount = 13, Item2ID = 14, Item2Amount = 15,
            Item3ID = 16, Item3Amount = 17}}, SuppyDropLootStream = {aw_basic = "stream"},
            LootDropsData = {stream = {lootTableColName = "LootDrop1"}}, crateData = {}},
        MPLootDropsBase = {GetGenericItemList = function() return collections end}}
    function LUI.MenuBuilder.registerType(name, fn) LUI.MenuBuilder.m_types_build[name] = fn end
    dofile("src/client/resources/ui_scripts/common.lua")
    Engine = {
        InFrontend = function() return frontend end,
        GetDvarBool = function(name) return name == "cg_unlockall_loot" and unlock end,
        Inventory_GetItemQuantity = function(controller, guid) return native_items[guid] or 0 end,
        GetItemLockState = function(controller, guid) return native_items[guid] and "Unlocked" or "Locked", 15, "reason" end,
        GetItemLockStateByItemRef = function() return "Locked", 15, "reason" end,
        Inventory_IsItemUsableForPlayer = function() return false end,
        Inventory_PurchaseItem = function() native_calls = native_calls + 1; return false end,
        Loot_GetSupplyDropCount = function() return 0 end,
        LookupStatsTableColumnForGUID = function(guid) return guid == "crate_guid" and "aw_basic_1" or "" end,
        TableLookup = function(file, column, value, result)
            local rows = file == "loot" and loot or {bundle}
            for _, row in ipairs(rows) do if row[column + 1] == tostring(value) then return row[result + 1] or "" end end
            return ""
        end
    }
    LootTable = {File = "loot", Cols = {Ref = 0, Type = 1, GUID = 2, Rarity = 3, Value = 4, Purchase = 5}}
    StatsTable = {Cols = {Ref = 4}}
    Cac = {InventoryItemType = {Loot = 1}}
    game = {issingleplayer = function() return false end}
    json = dofile("src/client/resources/json.lua")
    customdepot = {
        load = function() return persisted and copy(persisted) or nil end,
        save = function(encoded)
            if fail_save then return false end
            persisted = json.decode(encoded); save_count = (save_count or 0) + 1; return true
        end,
        isreplay = function() return replay end
    }
    collections = {{guid = "head_guid", inventoryItemType = 1}}
    LUI.MPDepot.GetSupplyDropPrice = function() return {type = 2, amount = 10} end
    cache_ready = false
    -- Representative native count/claim contract: all ownership goes through
    -- Inventory_GetItemQuantity, including already-owned native items.
    GetItemSets = function()
        cache_ready = true
        local owned = Engine.Inventory_GetItemQuantity(0, "head_guid", false) > 0
        local set = {setID = 1, setItems = {{guid = "head_guid"}, {guid = "native_guid"}},
            setReward = {guid = "reward_guid"}, numOwned = owned and 2 or 1, completed = owned}
        local season = {sets = {set}, completedSets = owned and 1 or 0,
            rewardData = {isSeasonReward = true, setReward = {guid = "season_guid"}, completed = owned}}
        return {seasons = {season}, completedSets = season.completedSets, numSets = 1, idToSetMap = {[1] = set}}
    end
    GetItemSet = function(id) return GetItemSets().idToSetMap[id] end
    InvalidateItemSets = function() assert(cache_ready, "native cache must exist before invalidation") end
    package.loaded.inventory, package.loaded.depot_override, package.loaded.scoreboard_override = nil, nil, nil
    package.path = "data/cdata/ui_scripts/custom_depot/?.lua;" .. package.path
    dofile("data/cdata/ui_scripts/custom_depot/__init__.lua")
end
setup(true)
local f = custom_depot.functions
assert(not f.has_accepted_mod_eula() and not f.has_seen_mod_eula())
assert(LUI.MPDepotBase.AddLootDropTabSelector == native_tab, "stock selectors must remain intact")
for _, kind in ipairs({"characters", "character_camos", "collections", "armory"}) do
    local menu = {loot_type = kind}; native_tab(menu, {"season"}); assert(menu.tabManager and menu.populated)
end
assert(GetItemSets().completedSets == 0 and GetItemSets().seasons[1].sets[1].numOwned == 1)
assert(not f.has_item("false_item")); f.add_item("false_item", false); assert(not f.has_item("false_item"))
assert(f.add_currency(3, 1000) and not f.remove_currency(3, 1001))
assert(not f.add_currency(3, -1) and not f.add_currency(3, 1.5) and not f.add_currency(99, 1))
assert(not Engine.Inventory_PurchaseItem(0, "unknown_sku", 1))
assert(not Engine.Inventory_PurchaseItem(0, "buy_head", 2))
assert(Engine.Inventory_PurchaseItem(0, "buy_head", 1))
assert(f.has_item("head_guid") and not f.has_item("buy_head") and f.get_currency(3) == 900)
assert(persisted.items.head_guid and persisted.currencies.parts == 900)
assert(Engine.GetItemLockState(0, "head_guid") == "Unlocked")
assert(Engine.GetItemLockStateByItemRef(0, "head") == "Unlocked")
assert(Engine.Inventory_IsItemUsableForPlayer(0, "head"))
assert(GetItemSets().completedSets == 1 and GetItemSet(1).numOwned == 2)
local function event_menu(root, params)
    local menu = Element.new(); menu.properties = params; menu.exclusiveController = params.exclusiveController
    menu:registerEventHandler("inventory", function(element, event)
        assert(event.inventoryTaskType == 23 and event.inventoryEventType == 3)
        element.completed = (element.completed or 0) + 1; element.success = event.success
    end)
    return menu
end
LUI.MenuBuilder.registerType("popup_fabricating_item", event_menu) -- deferred registration
local popup = LUI.MenuBuilder.m_types_build.popup_fabricating_item(nil, {guid = "head_guid", exclusiveController = 0})
assert(not popup.completed and popup.children[1].interval > 0)
popup:processEvent({name = "custom_depot_tasks"}); assert(popup.completed == 1 and popup.success)
popup:processEvent({name = "custom_depot_tasks"}); assert(popup.completed == 1)
-- An owned item retry is idempotent, insufficient funds and disk failures roll back.
assert(Engine.Inventory_PurchaseItem(0, "buy_head", 1) and f.get_currency(3) == 900)
custom_depot.pending = {}
fail_save = true
assert(not Engine.Inventory_PurchaseItem(0, "buy_camo", 1))
assert(not f.has_item("camo_guid") and f.get_currency(3) == 900 and #custom_depot.pending == 0)
fail_save = false
f.remove_currency(3, 900)
assert(not Engine.Inventory_PurchaseItem(0, "buy_camo", 1) and not f.has_item("camo_guid"))
assert(Engine.Loot_CanClaimItemSetReward(0, 1))
assert(Engine.Loot_ClaimItemSetReward(0, 1) and f.has_item("reward_guid"))
assert(not Engine.Loot_CanClaimItemSetReward(0, 1) and not Engine.Loot_ClaimItemSetReward(0, 1))
assert(Engine.Inventory_PurchaseItem(0, "season_guid", 1) and f.has_item("season_guid"))
assert(not Engine.Inventory_PurchaseItem(0, "locked_reward_guid", 1))
custom_depot.pending = {}
f.set_has_seen_mod_eula(true)
local expected = copy(custom_depot.data)
setup(true); f = custom_depot.functions
assert(f.has_item("head_guid") and f.has_item("reward_guid") and f.has_seen_mod_eula())
assert(equal(expected, custom_depot.data))
-- Unlock option counts known loot only, without creating saved item ownership.
unlock = true
assert(Engine.Inventory_GetItemQuantity(0, "camo_guid", false) == 1)
assert(Engine.Inventory_GetItemQuantity(0, "unknown_entitlement", false) == 0)
assert(Engine.Inventory_GetItemQuantity(0, "aw_basic", false) == 0)
assert(not f.has_item("camo_guid")); unlock = false
-- Both frontend and match VMs expose local ownership; replay grants no income.
setup(false); f = custom_depot.functions
assert(Engine.GetItemLockStateByItemRef(0, "head") == "Unlocked")
assert(Engine.Inventory_IsItemUsableForPlayer(0, "head"))
LUI.MenuBuilder.registerType("scoreboard", function()
    local menu = Element.new(); function menu:registerOmnvarHandler(name, handler) self.round_end = handler end
    return menu
end)
GameX = {IsRankedMatch = function() return true end, GetGameMode = function() return "war" end}
Game = {GetPlayerTeam = function() return 1 end, GetPlayerScoreRanking = function() return 1 end,
    GetPlayerScoreInfoAtRank = function() return {score = 1000} end,
    GetOmnvar = function() return 3 end}
IsGameTypeRoundBased = function() return true end
local scoreboard = LUI.MenuBuilder.m_types_build.scoreboard()
replay = true; scoreboard:round_end({}); assert(f.get_currency(3) == 0)
replay = false; scoreboard:round_end({}); local credits = f.get_currency(2)
assert(f.get_currency(3) == 100 and credits >= 2)
scoreboard:round_end({}); assert(f.get_currency(3) == 100 and f.get_currency(2) == credits)
-- Local drops: bounded pools, no forced success, no duplicate charges, no
-- double debit when opening a crate acquired from a bundle.
persisted = nil
setup(true); f = custom_depot.functions; f.add_currency(2, 100)
collections = {}
assert(not Engine.Loot_OpenSupplyDrop(0, "aw_basic", 100) and f.get_currency(2) == 100)
collections = {{guid = "camo_guid", inventoryItemType = 1}, {guid = "bad", inventoryItemType = 0}}
assert(Engine.Loot_OpenSupplyDrop(0, "aw_basic", 101) and f.get_currency(2) == 90)
assert(f.has_item("camo_guid") and #custom_depot.pending[#custom_depot.pending].items == 3)
local saved_count = save_count
assert(Engine.Loot_OpenSupplyDrop(0, "aw_basic", 101) and f.get_currency(2) == 90 and saved_count == save_count)
assert(Engine.Inventory_PurchaseItem(0, "buy_bundle", 1) and f.get_quantity("crate_guid") == 2)
assert(Engine.Loot_GetSupplyDropCount(0, "aw_basic") == 2 and not f.has_item("aw_basic"))
assert(f.get_currency(2) == 80)
assert(not Engine.Inventory_PurchaseItem(0, "buy_bundle", 1) and f.get_currency(2) == 80)
assert(Engine.Loot_OpenSupplyDrop(0, "aw_basic", 102) and f.get_quantity("crate_guid") == 1 and f.get_currency(2) == 80)
fail_save = true
assert(not Engine.Loot_OpenSupplyDrop(0, "aw_basic", 103) and f.get_quantity("crate_guid") == 1)
fail_save = false
assert(native_calls == 0, "local purchases must not wait for the empty marketplace backend")
print("PASS Depot native selectors, ownership, restart, crafting, rewards, event delivery, drops and replay exclusion")
