local gametypes = {
    ["dm"] = true,
    ["dom"] = true,
    ["sd"] = true,
    ["war"] = true,
    ["conf"] = true,
    ["vlobby"] = true,
    ["koth"] = true,
    ["sab"] = true,
    ["ctf"] = true,
    ["dd"] = true,
    ["hp"] = true,
    ["gun"] = true
}

local func = LUI.mp_hud.Scoreboard.DetermineIfSingleTeamGameType
LUI.mp_hud.Scoreboard.DetermineIfSingleTeamGameType = function()
    -- Converted COD4 demos have no MWR server scoreboard data. The native
    -- two-team builder adds duplicate headers in that state and aborts mp_hud,
    -- hiding ammo and every other HUD widget along with the scoreboard.
    if Engine.GetDvarBool("demo_cod4PlaybackHud") then
        return true
    end

    local gametype = Engine.GetDvarString("ui_gametype")
    if (gametypes[gametype]) then
        return func()
    end

    return Game.GetPlayerTeam() == Teams.free
end

local updateicon = LUI.FactionIcon.Update
LUI.FactionIcon.Update = function(element, icon, a3)
    local scale = LUI.FactionIcon.BackgroundScale
    if (game:isdefaultmaterial(icon .. "_soft")) then
        LUI.FactionIcon.BackgroundScale = 0
    end

    updateicon(element, icon, a3)
    LUI.FactionIcon.BackgroundScale = scale
end
