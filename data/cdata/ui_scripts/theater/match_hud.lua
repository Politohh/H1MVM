-- Keep native MWR HUD art while supplying the scores recorded by COD4.
if Engine.InFrontend() or not game.getcod4teamscore then return end
local function converted() return Engine.GetDvarBool("demo_cod4PlaybackHud") end
-- Select only this widget's gamepad artwork. The actual device/input setting
-- remains unchanged, including while native button configuration events run.
local function controllerLayout(callback,...)
    local native=Engine.IsGamepadEnabled
    Engine.IsGamepadEnabled=function() return true end
    local ok,result=pcall(callback,...)
    Engine.IsGamepadEnabled=native
    if not ok then error(result) end
    return result
end
LUI.overrideMenuBuilder("actionSlotHudDef",function(native,...)
    local widget=controllerLayout(native,...)
    local handler=widget and widget.m_eventHandlers and widget.m_eventHandlers.button_config_updated
    if handler then
        widget:registerEventHandler("button_config_updated",function(...)
            return controllerLayout(handler,...)
        end)
    end
    return widget
end)
-- Converted snapshots and seeks can omit native weapon_change notifications.
-- Refresh on Theater's wall clock (also while paused), using the same weapon
-- selector as the visible POV gun, including applied Weapon Swapper choices.
LUI.overrideMenuBuilder("weaponInfoHudDef",function(native,...)
    local widget=native(...)
    if not widget then return widget end
    widget:registerEventHandler("h1_theater_weapon_update",function(element,event)
        if type(event.label)~="string" or event.label=="" then return end
        local getName=Game.GetPlayerWeaponDisplayName
        Game.GetPlayerWeaponDisplayName=function() return Engine.Localize(event.label) end
        local ok,result=pcall(element.processEvent,element,{name="weapon_change"})
        Game.GetPlayerWeaponDisplayName=getName
        if not ok then error(result) end
    end)
    return widget
end)
local nativeScore=Game.GetTeamScore
Game.GetTeamScore=function(team,...)
    if converted() then
        local score=game:getcod4teamscore(team)
        if type(score)=="number" then return score end
    end
    return nativeScore(team,...)
end

local nativeBind=LUI.UIElement.setupUIBindText
LUI.UIElement.setupUIBindText=function(element,name,...)
    if converted() and (name=="TeamScoreFriendly" or name=="TeamScoreEnemy") then return end
    return nativeBind(element,name,...)
end
local function find(element,id)
    if not element then return nil end
    if element.id==id then return element end
    -- Native traversal also visits unnamed layout containers, which are absent
    -- from childRecord.
    if element.getFirstDescendentById then return element:getFirstDescendentById(id) end
    for _,child in pairs(element.childRecord or {}) do
        local found=find(child,id)
        if found then return found end
    end
end
-- registerType rejects existing native types. Wrap the registered builders in
-- place, just as LUI.onmenuopen does, so both hooks finish loading.
LUI.overrideMenuBuilder("teamScoresHudDef",function(nativeTeams,...)
    local widget=nativeTeams(...)
    if not widget or not converted() then return widget end
    local friendly,enemy=find(widget,"friendlyScore_id"),find(widget,"enemyScore_id")
    if not friendly or not enemy then return widget end
    local function update()
        local team=Game.GetPlayerTeam(Game.GetPlayerstateClientnum())
        if team~=1 and team~=2 then return end
        local first,second=game:getcod4teamscore(team),game:getcod4teamscore(3-team)
        friendly:setText(type(first)=="number" and tostring(first) or "--")
        enemy:setText(type(second)=="number" and tostring(second) or "--")
        if type(first)=="number" and type(second)=="number" then
            local fw,ew=find(widget,"scoreWatchFriendly"),find(widget,"scoreWatchEnemy")
            if fw then fw:processEvent({name="int_watch_alert",newValue=first}) end
            if ew then ew:processEvent({name="int_watch_alert",newValue=second}) end
        end
    end
    widget:registerEventHandler("cod4_scores_update",update)
    widget:addElement(LUI.UITimer.new(50,"cod4_scores_update"))
    update()
    return widget
end)

LUI.overrideMenuBuilder("pointsPopup",function(nativePopup,...)
    local popup=nativePopup(...)
    if not popup or not converted() then return popup end
    popup:registerAnimationState("cod4_center",{leftAnchor=false,rightAnchor=false,
        topAnchor=false,bottomAnchor=false,left=0,width=0,top=10,height=0})
    popup:animateToState("cod4_center",0)
    local points=find(popup,"points")
    if points then
        points:registerAnimationState("default",{leftAnchor=false,rightAnchor=false,
            topAnchor=true,bottomAnchor=false,left=-100,right=100,top=-85,height=30,
            alignment=LUI.Alignment.Center,alpha=1,scale=0})
        points:animateToState("default",0)
    end
    local flare=find(popup,"flareAnimation")
    if flare then
        flare:registerAnimationState("cod4_center",{left=0})
        flare:animateToState("cod4_center",0)
    end
    return popup
end)
