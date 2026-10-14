-- Original Private Match bridge. Research bundle only until gameplay and UI
-- acceptance are complete. Current tested gametype is Team Deathmatch.
if game:issingleplayer() then return end
if not Engine.InFrontend() then require("spai_scoreboard");return end
if not LUI.GameSetup or not LUI.GameSetup.GameSetupMenuOptionsFeeder then
    error("SP Bots: native Game Setup is unavailable")
end
if rawget(LUI.GameSetup, "spai_bridge_installed") then return end
rawset(LUI.GameSetup, "spai_bridge_installed", true)

local function restore_reservation(reason)
    local name = Engine.GetDvarString("spai_reserved_dvar") or ""
    if name:match("^scr_[%w_]+_maxagents$") then
        local previous = Engine.GetDvarString("spai_reserved_text") or ""
        print("[SPAI_MENU] Restore reservation: reason="..tostring(reason).." pool="..name.." previous='"..previous.."'")
        Engine.SetDvarString(name, previous)
    end
    Engine.SetDvarString("spai_reserved_dvar", "")
    Engine.SetDvarInt("spai_from_lobby", 0)
    Engine.SetDvarInt("spai_enabled", 0)
end

local function private_host()
    return Engine.IsCoreMode() and Lobby.IsGameHost()
        and Engine.GetDvarBool("xblive_privatematch")
end

local function supported()
    return private_host() and Engine.GetDvarString("ui_gametype") == "war"
end

local function bounded(value, ceiling)
    return math.floor(math.max(0, math.min(ceiling, tonumber(value) or 0)))
end

local function publish_settings()
    local friendly = bounded(Lobby.GetBotsTeamLimit(Lobby.BotTeams.Friendly), 17)
    local enemy = bounded(Lobby.GetBotsTeamLimit(Lobby.BotTeams.Enemy), 17)
    local fd = bounded(Lobby.GetBotsDifficulty(Lobby.BotTeams.Friendly), 4)
    local ed = bounded(Lobby.GetBotsDifficulty(Lobby.BotTeams.Enemy), 4)
    local pool = "scr_war_maxagents"
    restore_reservation("republish")
    -- The native allocator reads a string and treats empty/missing as its
    -- normal fallback. Preserve that sentinel rather than turning it into 0.
    Engine.SetDvarString("spai_reserved_text", Engine.GetDvarString(pool) or "")
    Engine.SetDvarString("spai_reserved_dvar", pool)
    -- Agent capacity must exist before the map initializes the native pool.
    Engine.SetDvarString(pool, tostring(math.min(17, friendly + enemy)))
    Engine.SetDvarInt("spai_friendly", friendly)
    Engine.SetDvarInt("spai_enemy", enemy)
    Engine.SetDvarInt("spai_friendly_difficulty", fd)
    Engine.SetDvarInt("spai_enemy_difficulty", ed)
    Engine.SetDvarInt("spai_from_lobby", 1)
    Engine.SetDvarInt("spai_enabled", 1)
    print("[SPAI_MENU] Start settings: friendly="..friendly.." enemy="..enemy.." enabled=1")
end

-- HKS also reloads in frontend mode between Start and native map allocation.
-- Do not clear published settings during script initialization. Restore the
-- previous reservation when configuring another match or changing connect
-- mode; publish_settings also restores before reserving for a repeated Start.

local original_connect_type = SetBotsConnectType
function SetBotsConnectType(connect_type)
    print("[SPAI_MENU] Connect type="..tostring(connect_type).." supported="..tostring(supported()).." selected="..tostring(Engine.GetDvarBool("spai_bots")))
    if connect_type == 2 and supported() and Engine.GetDvarBool("spai_bots") then
        publish_settings()
        -- Keep native menu counts, but suppress the normal MP client-bot fill.
        -- The scripted population takes these slots when the host picks a team.
        return original_connect_type(0)
    end
    restore_reservation("connect_type_"..tostring(connect_type))
    return original_connect_type(connect_type)
end

local original_feeder = LUI.GameSetup.GameSetupMenuOptionsFeeder
function LUI.GameSetup:GameSetupMenuOptionsFeeder(...)
    if private_host() then restore_reservation("game_setup") end
    original_feeder(self, ...)
    if not supported() then return end
    local function toggle(element)
        Engine.SetDvarBool("spai_bots", not Engine.GetDvarBool("spai_bots"))
        if element and element.processEvent then
            element:processEvent({name = "content_refresh"})
        end
    end
    -- Select owns mouse clicks on its value/arrows. A second row action can
    -- toggle again when that click reaches the parent. Route keyboard/controller
    -- accept through the right action, as supported by UIGenericButton.
    self:AddButton("SP BOTS", nil, nil, nil, nil, {
        variant = GenericButtonSettings.Variants.Select,
        buttonActionIsRightAction = true,
        delay = 0,
        button_display_func = function()
            return Engine.GetDvarBool("spai_bots") and "ON" or "OFF"
        end,
        button_left_func = toggle,
        button_right_func = toggle,
        desc_text = "Use campaign animations and tactical behavior. Friendly/enemy counts and difficulties apply."
    })
end
print("[SPAI_MENU] native frontend bridge installed")
