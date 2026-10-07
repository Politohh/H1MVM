-- Native equip validation needs ownership in the frontend and active matches.
local functions = custom_depot.functions
local native_quantity = Engine.Inventory_GetItemQuantity
local native_lock = Engine.GetItemLockState
local native_ref_lock = Engine.GetItemLockStateByItemRef
local native_usable = Engine.Inventory_IsItemUsableForPlayer
function custom_depot.guid_for_ref(ref)
    if not ref or not LootTable then return nil end
    local guid = Engine.TableLookup(LootTable.File, LootTable.Cols.Ref, tostring(ref), LootTable.Cols.GUID)
    return guid ~= "" and guid or nil
end
function custom_depot.is_unlocked(guid)
    if functions.has_item(guid) then return true end
    if guid and LootTable and Engine.GetDvarBool("cg_unlockall_loot") then
        local ref = Engine.TableLookup(LootTable.File, LootTable.Cols.GUID, tostring(guid), LootTable.Cols.Ref)
        return ref ~= nil and ref ~= ""
    end
    return false
end
Engine.Inventory_GetItemQuantity = function(controller, guid, ...)
    local native = tonumber(native_quantity(controller, guid, ...)) or 0
    local local_count = functions.get_quantity(guid)
    -- Unlock All Loot already unlocks native lock state. Keep collections
    -- consistent without fabricating entitlement or crate quantities.
    if custom_depot.is_unlocked(guid) then local_count = math.max(1, local_count) end
    return math.max(native, local_count)
end
Engine.GetItemLockState = function(controller, guid, ...)
    if custom_depot.is_unlocked(guid) then return "Unlocked", 0, "" end
    return native_lock(controller, guid, ...)
end
Engine.GetItemLockStateByItemRef = function(controller, ref, ...)
    if custom_depot.is_unlocked(custom_depot.guid_for_ref(ref)) then return "Unlocked", 0, "" end
    return native_ref_lock(controller, ref, ...)
end
Engine.Inventory_IsItemUsableForPlayer = function(controller, ref, ...)
    if custom_depot.is_unlocked(ref) or custom_depot.is_unlocked(custom_depot.guid_for_ref(ref)) then return true end
    return native_usable(controller, ref, ...)
end
