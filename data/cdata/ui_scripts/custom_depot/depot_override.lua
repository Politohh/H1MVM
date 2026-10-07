local functions = custom_depot.functions
GetCurrencyBalance = function(currency, controller) return functions.get_currency(currency) end
IsContentPromoUnlocked = function() return true end

-- Keep native AddLootDropTabSelector, GetItemSet(s) and Personalize menus.
-- The old override replaced every selector with Collections/Armory and never
-- created a tabManager or populated the head/body camouflage lists.
local function queue_inventory(controller, guid, success)
    table.insert(custom_depot.pending, {
        name = "inventory", controller = controller, guid = guid, success = success,
        inventoryTaskType = LUI.InventoryUtils.TaskType.INV_TASK_PURCHASE_ITEM,
        inventoryEventType = LUI.InventoryUtils.EventType.ELUIInventoryEvent_TaskCompleted
    })
end
local function completed_reward(guid)
    local sets = GetItemSets()
    for _, season in ipairs(sets.seasons) do
        if season.rewardData and season.rewardData.setReward.guid == guid then return season.rewardData.completed == true end
        for _, set in ipairs(season.sets) do
            if set.setReward and set.setReward.guid == guid then return set.completed == true end
        end
    end
    return false
end
Engine.Loot_CanClaimItemSetReward = function(controller, id)
    local set = GetItemSet(id)
    return set ~= nil and set.completed == true and set.setReward ~= nil
        and Engine.Inventory_GetItemQuantity(controller, set.setReward.guid, false) == 0
end
Engine.Loot_ClaimItemSetReward = function(controller, id)
    if not Engine.Loot_CanClaimItemSetReward(controller, id) then return false end
    local guid = GetItemSet(id).setReward.guid
    local success = custom_depot.commit(function() return functions.add_item(guid, true) end)
    queue_inventory(controller, guid, success)
    return success
end
local function lookup(info, column, value, result)
    return Engine.TableLookup(info.File, column, tostring(value), result)
end
local function purchase_bundle(controller, sku)
    local info = LUI.MPDepot.BundlesTable
    local id = lookup(info, info.Cols.PurchaseID, sku, info.Cols.ID)
    if not id or id == "" then return nil end
    local function field(column) return lookup(info, info.Cols.ID, id, column) end
    local currency = tonumber(field(info.Cols.PurchaseCurrency))
    local cost = tonumber(field(info.Cols.PurchaseAmount))
    local limit_item = field(info.Cols.PurchaseLimitItem)
    local limit = tonumber(field(info.Cols.PurchaseLimitQuantity)) or 0
    if limit > 0 and Engine.Inventory_GetItemQuantity(controller, limit_item, false) >= limit then return false end
    return custom_depot.commit(function()
        if not functions.remove_currency(currency, cost) then return false end
        for index = 0, 3 do
            local guid = field(info.Cols["Item" .. index .. "ID"])
            local count = tonumber(field(info.Cols["Item" .. index .. "Amount"])) or 0
            if guid and guid ~= "" and count > 0 then
                if not functions.add_item(guid, functions.get_quantity(guid) + count) then return false end
            end
        end
        if limit_item and limit_item ~= "" then
            if not functions.add_item(limit_item, functions.get_quantity(limit_item) + 1) then return false end
        end
        local award = tonumber(field(info.Cols.CurrencyAwardAmount)) or 0
        if award > 0 and not functions.add_currency(tonumber(field(info.Cols.CurrencyAwardID)), award) then return false end
        return true
    end)
end
Engine.Inventory_PurchaseItem = function(controller, sku, count)
    -- Crafting passes a purchase SKU; season reward claims pass a GUID.
    if count ~= 1 or sku == nil then return false end
    local guid = lookup(LootTable, LootTable.Cols.Purchase, sku, LootTable.Cols.GUID)
    if not guid or guid == "" then
        if completed_reward(sku) then
            local success = custom_depot.commit(function() return functions.add_item(sku, true) end)
            queue_inventory(controller, sku, success)
            return success
        end
        local success = purchase_bundle(controller, sku)
        if success ~= nil then queue_inventory(controller, sku, success) end
        return success == true
    end
    if Engine.Inventory_GetItemQuantity(controller, guid, false) > 0 then
        queue_inventory(controller, guid, true)
        return true
    end
    local cost = tonumber(lookup(LootTable, LootTable.Cols.GUID, guid, LootTable.Cols.Value))
    if not cost or cost <= 0 then return false end
    local success = custom_depot.commit(function()
        return functions.remove_currency(InventoryCurrencyType.Parts, cost) and functions.add_item(guid, true)
    end)
    -- Failed synchronous crafting never opens the native wait popup.
    if success then queue_inventory(controller, guid, true) end
    return success
end

local native_drop_count = Engine.Loot_GetSupplyDropCount
local crate_aliases = {}
local function crate_for_item(guid)
    if crate_aliases[guid] ~= nil then return crate_aliases[guid] or nil end
    if LUI.MPDepot.SuppyDropLootStream[guid] then return guid end
    local ref = Engine.LookupStatsTableColumnForGUID(guid, StatsTable.Cols.Ref)
    if ref and ref ~= "" then
        local base = string.gsub(ref, "_1$", "")
        if LUI.MPDepot.SuppyDropLootStream[base] then crate_aliases[guid] = base; return base end
    end
    -- Event crates use catalog references such as ex_promo_one, while the
    -- menu uses ex_promo_1. Resolve them through its native bundle metadata.
    for crate, data in pairs(LUI.MPDepot.crateData) do
        if data.bundleID then
            local info = LUI.MPDepot.BundlesTable
            for index = 0, 3 do
                local item = lookup(info, info.Cols.ID, data.bundleID, info.Cols["Item" .. index .. "ID"])
                if item == guid then
                    local canonical = data.items and data.items[1] or crate
                    crate_aliases[guid] = canonical
                    return canonical
                end
            end
        end
    end
    crate_aliases[guid] = false
end
local function crate_stock(crate)
    local total, keys = 0, {}
    for guid in pairs(custom_depot.data.items) do
        local count = functions.get_quantity(guid)
        if count > 0 and crate_for_item(guid) == crate then
            total = total + count
            keys[#keys + 1] = guid
        end
    end
    table.sort(keys)
    return total, keys
end
Engine.Loot_GetSupplyDropCount = function(controller, crate)
    local stock = crate_stock(crate)
    return math.max(tonumber(native_drop_count(controller, crate)) or 0, stock)
end
local function eligible_items(crate)
    local stream = LUI.MPDepot.SuppyDropLootStream[crate]
    local data = stream and LUI.MPDepot.LootDropsData[stream]
    local items = data and LUI.MPLootDropsBase.GetGenericItemList(nil, data.lootTableColName) or {}
    local eligible = {}
    for _, item in ipairs(items) do
        if item.guid and item.inventoryItemType == Cac.InventoryItemType.Loot then eligible[#eligible + 1] = item end
    end
    return eligible
end
local drop_results = {}
Engine.Loot_OpenSupplyDrop = function(controller, crate, transaction)
    local key = tostring(controller) .. ":" .. tostring(transaction) .. ":" .. tostring(crate)
    -- A repeated engine/UI event must not debit or roll rewards twice.
    if drop_results[key] then return drop_results[key].success end
    local event = {name = "supply_drop", controller = controller, transaction = transaction,
        success = false, duplicateRefund = false, items = {}, currencies = {}, replacements = {}, cards = {}}
    local eligible = eligible_items(crate)
    if #eligible > 0 then
        event.success = custom_depot.commit(function()
            local stock, keys = crate_stock(crate)
            if stock > 0 then functions.add_item(keys[1], functions.get_quantity(keys[1]) - 1)
            else
                local price = LUI.MPDepot.GetSupplyDropPrice(crate)
                if not price or not functions.remove_currency(price.type, price.amount) then return false end
            end
            for index = 1, 3 do
                local item = eligible[math.random(#eligible)]
                event.items[index] = item.guid
                if Engine.Inventory_GetItemQuantity(controller, item.guid, false) == 0 then functions.add_item(item.guid, true)
                else
                    -- Local duplicate salvage; official drop odds aren't emulated.
                    local salvage = math.max(1, math.floor((tonumber(lookup(LootTable, LootTable.Cols.GUID,
                        item.guid, LootTable.Cols.Value)) or 10) / 10))
                    if not functions.add_currency(InventoryCurrencyType.Parts, salvage) then return false end
                    event.replacements[#event.replacements + 1] = {item_index = index,
                        currency = {type = InventoryCurrencyType.Parts, amount = salvage}}
                end
            end
            return true
        end)
    end
    if not event.success then event.items, event.replacements = {}, {} end
    drop_results[key] = event
    table.insert(custom_depot.pending, event)
    return event.success
end

-- Native handlers are installed during construction, sometimes on a child.
-- Deliver completion after construction using the native task/event contract.
local function attach_events(menu, params, kind)
    local controller = menu.exclusiveController or menu.controller or (params and params.exclusiveController) or 0
    menu:registerEventHandler("custom_depot_tasks", function(element)
        local pending, events = custom_depot.pending, {}
        for index = #pending, 1, -1 do
            local event = pending[index]
            local accepts = event.controller == controller
            if kind == "drop" then
                accepts = accepts and ((event.name == "supply_drop" and event.transaction == element.supplyDropTransaction)
                    or (event.name == "inventory" and element.supplyDropPurchase ~= nil))
            else
                local guid = (element.properties and element.properties.guid) or (params and params.guid)
                    or (params and params.collectionData and params.collectionData.setReward.guid)
                accepts = accepts and event.name == "inventory" and (not guid or guid == event.guid)
            end
            if accepts then table.insert(events, 1, table.remove(pending, index)) end
        end
        for _, event in ipairs(events) do element:processEvent(event) end
    end)
    menu:addElement(LUI.UITimer.new(50, "custom_depot_tasks"))
    return menu
end
for _, name in ipairs({"popup_fabricating_item", "popup_purchasing_bundle", "MPDepotCollectionRewardSplash"}) do
    LUI.overrideMenuBuilder(name, function(native, root, params)
        return attach_events(native(root, params), params, "inventory")
    end)
end
LUI.overrideMenuBuilder("MPDepotOpenLootMenu", function(native, root, params)
    return attach_events(native(root, params), params, "drop")
end)
