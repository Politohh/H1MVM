-- MWR LUI owns drawing, fonts, focus, mouse input and menu lifetime. Replay and
-- camera changes are queued by the C++ bridge onto the main game pipeline.
local MENU = "h1_theater_controls"
local EDITOR = "h1_theater_editor"
local editor = require("editor")
local WIDTH, TRACK, INSET = 480, 456, 12
local TEXT_HEIGHT = 10.5
local white = RegisterMaterial("white")
local colors = {
    text = {r = 0.92, g = 0.94, b = 0.92},
    muted = {r = 0.60, g = 0.64, b = 0.60},
    accent = {r = 0.72, g = 0.82, b = 0.55},
    dark = {r = 0.025, g = 0.035, b = 0.025},
    track = {r = 0.16, g = 0.19, b = 0.16},
    kill = {r = 0.38, g = 0.84, b = 0.42},
    death = {r = 0.94, g = 0.32, b = 0.28},
    key = {r = 0.96, g = 0.76, b = 0.35}
}
local speeds = {0.025, 0.1, 0.25, 0.5, 1, 2, 4}
local cameras = {"FIRST PERSON", "THIRD PERSON", "FREE CAMERA", "DOLLY"}
local hudPanel, menuRoot, requestOpen = nil, nil, false
local latest = {playing = false, open = false, elapsed = 0, duration = 0, speed = 1, camera = 0, events = {}, keys = {}}

local function state(x, y, w, h, color, alpha)
    return {leftAnchor = true, rightAnchor = false, topAnchor = true, bottomAnchor = false,
        left = x, top = y, width = w, height = h, color = color or colors.text, alpha = alpha or 1}
end
local function image(parent, id, x, y, w, h, color, alpha)
    local style = state(x, y, w, h, color, alpha)
    style.material = white
    local element = LUI.UIImage.new(style)
    element.id = id
    parent:addElement(element)
    return element
end
local function text(parent, id, value, x, y, w, h, color)
    local style = state(x, y, w, h, color)
    -- The in-game VM starts before CoD.TextSettings is populated. Resolve the
    -- native font when mp_hud is constructed, rather than while loading scripts.
    style.font = CoD.TextSettings.TitleFontTiny.Font
    style.alignment = LUI.Alignment.Left
    local element = LUI.UIText.new(style)
    element.id = id
    element:setText(value)
    parent:addElement(element)
    return element
end
local function setAlpha(element, alpha)
    element:registerAnimationState("theater_alpha", {alpha = alpha})
    -- No replay-scaled animation delay: closing a paused menu must be immediate.
    element:animateToState("theater_alpha", 0)
end
local function action(name, value)
    return game:theateraction(name, value or 0)
end
local function button(parent, id, label, x, y, w, callback, transparent, height)
    -- MWR's native nine-slice has two 12-unit caps. Keep the center positive.
    height = height or 26
    if not transparent then
        -- The same factory/style as MenuTemplate:AddButton and mp_pause_menu.
        -- Keep native art and focus behavior; only the layout/text size differs.
        local properties = {style = LUI.MenuTemplate.ButtonStyle,
            button_text = label, height = height, fadeIn = false,
            text_align_without_content = LUI.Alignment.Left,
            text_padding_without_content = 5, text_padding_focus_without_content = 5,
            button_action_func = function() return callback() end
        }
        -- BuildAddChild installs .properties, including button_action_func.
        -- BuildRegisteredType alone only constructs the art and native handlers.
        local element = LUI.MenuBuilder.BuildAddChild(parent, {
            type = "UIGenericButton", id = id, properties = properties
        })
        element:registerAnimationState("theater_layout", state(x, y, w, height))
        element:animateToState("theater_layout", 0)
        local style = properties.style
        local substyle = style.SubStyles.Default
        local function setting(name)
            if properties[name] ~= nil then return properties[name] end
            if substyle[name] ~= nil then return substyle[name] end
            if style[name] ~= nil then return style[name] end
            return GenericButtonSettings.Common[name]
        end
        local labelSettings = {align = LUI.Alignment.Left, padding = 5, paddingFocus = 5,
            yOffset = 0, overDisabledAnimationDuration = setting("over_disabled_animation_duration")}
        for _, name in ipairs({"text_focus_color", "text_lock_color", "text_default_color", "text_disabled_color",
            "text_over_disabled_color", "text_over_disabled_pulse_color", "pinned_color"}) do
            labelSettings[name] = setting(name)
        end
        element.textLabel:close()
        -- Native label factory keeps the same compact size in every focus state.
        -- Changing the current state afterward lets stock focus states grow it.
        element.textLabel = LUI.UIGenericButton.ButtonLabelFactory(labelSettings, label,
            element, nil, w - 10, {Font = CoD.TextSettings.TitleFontTiny.Font, Height = TEXT_HEIGHT / 0.8})
        element.label = element.textLabel
        element.label.id = id .. "_label"
        element:addElement(element.label)
        editor.fitButtonBackground(element)
        return element
    end
    local element = LUI.UIButton.new(state(x, y, w, height))
    element.id = id
    local background = image(element, id .. "_bg", 0, 0, w, height, colors.track, transparent and 0 or 0.65)
    image(element, id .. "_line", 0, height - 1, w, 1, colors.accent, transparent and 0 or 0.35)
    local padding = height < 20 and 1 or 5
    element.label = text(element, id .. "_label", label, padding, padding, w - 2 * padding, height < 20 and 8 or 9)
    element:registerEventHandler("button_over", function()
        setAlpha(background, transparent and 0 or 1)
    end)
    element:registerEventHandler("button_up", function()
        setAlpha(background, transparent and 0 or 0.75)
    end)
    element:registerEventHandler("button_action", function()
        if callback() ~= false then Engine.PlaySound(CoD.SFX.MenuAccept) end
        return true
    end)
    parent:addElement(element)
    return element
end
local function clock(ms)
    local seconds = math.floor(math.max(0, ms or 0) / 1000)
    return string.format("%02d:%02d", math.floor(seconds / 60), seconds % 60)
end
local function ratio(time, duration)
    if not duration or duration <= 0 then return 0 end
    return math.max(0, math.min(1, time / duration))
end
local function move(element, x, width)
    element:registerAnimationState("theater_position", {leftAnchor = true, rightAnchor = false,
        left = x, width = width})
    element:animateToState("theater_position", 0)
end

local function buildPanel(interactive)
    local height = interactive and 82 or 46
    local panel = LUI.UIElement.new({leftAnchor = false, rightAnchor = false, topAnchor = false,
        bottomAnchor = true, left = -WIDTH / 2, right = WIDTH / 2, top = -18 - height, bottom = -18})
    panel.id = interactive and "H1TheaterInteractive" or "H1TheaterTimeline"
    setAlpha(panel, 0)
    colors.accent = luiglobals.Colors.mw1_green
    colors.text = luiglobals.Colors.white
    colors.muted = GenericButtonSettings.Common.text_default_color
    local background = LUI.H1ButtonBackground.new()
    background.id = "theater_back"
    background:registerAnimationState("theater_panel_layout", state(0, 0, WIDTH, height))
    background:animateToState("theater_panel_layout", 0)
    panel:addElement(background)
    panel.status = text(panel, "theater_status", "", INSET, 8, 300, TEXT_HEIGHT, colors.muted)
    panel.time = text(panel, "theater_time", "00:00 / 00:00", 357, 8, 111, TEXT_HEIGHT)
    panel.time:registerAnimationState("theater_time_align", {alignment = LUI.Alignment.Right})
    panel.time:animateToState("theater_time_align", 0)
    image(panel, "theater_track", INSET, 25, TRACK, 2, colors.track)
    panel.fill = image(panel, "theater_fill", INSET, 25, 0, 2, colors.accent)
    panel.head = image(panel, "theater_playhead", INSET, 22, 1, 8, colors.text)
    for i = 0, 20 do
        image(panel, "theater_tick_" .. i, INSET + i * TRACK / 20, 30, 1, i % 5 == 0 and 3 or 1, colors.muted, 0.5)
    end
    panel.events = LUI.UIElement.new(state(INSET, 19, TRACK, 5))
    panel.keys = LUI.UIElement.new(state(INSET, 34, TRACK, 10))
    panel:addElement(panel.events)
    panel:addElement(panel.keys)
    if interactive then
        -- Native mouse-hit regions avoid assuming IW8's mouse coordinate APIs.
        for i = 0, 79 do
            local fraction = (i + 0.5) / 80
            local hit = button(panel, "theater_seek_" .. i, "", INSET + i * TRACK / 80, 18,
                TRACK / 80, function() return action("seek", math.floor(fraction * latest.duration)) end, true)
            hit:makeNotFocusable()
        end
        panel.pause = button(panel, "theater_pause", "PAUSE", 65, 49, 56, function() return action("pause") end)
        button(panel, "theater_rewind", "<< 5s", INSET, 49, 48, function() return action("skip", -5000) end)
        button(panel, "theater_forward", "5s >>", 126, 49, 48, function() return action("skip", 5000) end)
        panel.speed = button(panel, "theater_speed", "1x", 179, 49, 46, function()
            local index = 1
            for i, speed in ipairs(speeds) do if speed <= latest.speed + 0.001 then index = i end end
            return action("speed", math.floor(speeds[index % #speeds + 1] * 1000))
        end)
        panel.camera = button(panel, "theater_camera", "FIRST PERSON", 230, 49, 110,
            function() return action("camera", (latest.camera + 1) % 4) end)
        button(panel, "theater_editor", "EDITOR", 345, 49, 60, function() return action("editor") end)
        button(panel, "theater_close", "CLOSE", 410, 49, 58, function() action("close") end)
    end
    panel.interactive = interactive
    return panel
end

local function updatePanel(panel, data)
    local visible = data.playing and data.enabled and
        (panel.editor and data.editor or not data.editor and (panel.interactive or not data.open))
    setAlpha(panel, visible and 1 or 0)
    if not visible then return end
    panel.time:setText(clock(data.elapsed) .. " / " .. clock(data.duration))
    local progress = ratio(data.elapsed, data.duration)
    move(panel.fill, INSET, TRACK * progress)
    move(panel.head, INSET + TRACK * progress, 1)
    local busy = data.seeking and "SEEKING..." or ""
    if panel.interactive then
        panel.pause.label:setText(data.paused and "PLAY" or "PAUSE")
        panel.speed.label:setText(string.format("%.3gx", data.speed))
        panel.camera.label:setText(cameras[data.camera + 1] or cameras[1])
    end
    panel.status:setText(busy ~= "" and busy or string.format("%s   %.3gx", data.paused and "PAUSED" or "PLAYING", data.speed))
    local signature = tostring(data.session) .. ":" .. tostring(data.revision) .. ":" .. #data.events
    if signature ~= panel.eventSignature then
        panel.eventSignature = signature
        panel.events:closeChildren()
        local pixels = {}
        for _, event in ipairs(data.events) do
            local pixel = math.floor(ratio(event.time, data.duration) * (TRACK - 2))
            local key = pixel .. (event.death and "d" or "k")
            if not pixels[key] then
                image(panel.events, "theater_event_" .. key, pixel, 0, 2, 4,
                    event.death and colors.death or colors.kill)
                pixels[key] = true
            end
        end
    end
    local keySignature = tostring(data.session) .. ":" .. tostring(data.selected)
    for _, key in ipairs(data.keys) do keySignature = keySignature .. ":" .. key.time end
    if keySignature ~= panel.keySignature then
        panel.keySignature = keySignature
        panel.keys:closeChildren()
        for _, key in ipairs(data.keys) do
            local x = math.max(0, math.min(TRACK - 16, ratio(key.time, data.duration) * TRACK - 8))
            if panel.interactive then
                local index = key.index
                local marker = button(panel.keys, "theater_key_" .. index, tostring(index + 1), x, 0, 16,
                    function() return action("seek", key.time) end, true, 10)
                marker.label:registerAnimationState("key_color", {color = data.selected == index and colors.text or colors.key})
                marker.label:animateToState("key_color", 0)
            else
                text(panel.keys, "theater_key_" .. key.index, tostring(key.index + 1), x, 0, 16, 8, colors.key)
            end
        end
    end
end

LUI.MenuBuilder.registerType(MENU, function(controller)
    local root = LUI.UIElement.new({leftAnchor = true, rightAnchor = true, topAnchor = true, bottomAnchor = true})
    root.id = MENU
    root.theaterMenu = MENU
    root.panel = buildPanel(true)
    root:addElement(root.panel)
    menuRoot, requestOpen = root, false
    local bind = LUI.UIBindButton.new()
    bind:registerEventHandler("button_secondary", function() action("close"); return true end)
    root:addElement(bind)
    root:registerEventHandler("menu_close", function()
        if menuRoot == root then menuRoot = nil; action("close") end
    end)
    root:registerEventHandler("gain_focus", function(element, event)
        root.panel.pause:processEvent({name = "gain_focus", controller = event.controller or 0})
        return true
    end)
    updatePanel(root.panel, latest)
    return root
end)

LUI.MenuBuilder.registerType(EDITOR, function(controller)
    local root = editor.create(controller, function() action("timeline") end)
    root.theaterMenu = EDITOR
    root.panel = buildPanel(false)
    root.panel.editor = true
    root:addElement(root.panel)
    menuRoot, requestOpen = root, false
    root:addEventHandler("menu_close", function()
        if menuRoot == root then menuRoot = nil; action("close") end
    end)
    updatePanel(root.panel, latest)
    editor.update(root, latest)
    return root
end)

local function updateTheater(root)
    local previous = latest
    latest = game:gettheaterstate(previous.revision or -1, previous.keyRevision or -1)
    latest.events, latest.keys = latest.events or previous.events or {}, latest.keys or previous.keys or {}
    if hudPanel then updatePanel(hudPanel, latest) end
    local desired = latest.playing and latest.enabled and
        (latest.editor and EDITOR or latest.open and MENU) or nil
    if menuRoot and menuRoot.theaterMenu ~= desired then
        local closing = menuRoot
        -- A route transition must not run the user's close action.
        menuRoot = nil
        LUI.FlowManager.RequestLeaveMenu(closing)
    end
    if desired and not menuRoot and not requestOpen then
        if not root:AnyActiveMenusInStack() then
            requestOpen = true
            LUI.FlowManager.RequestAddMenu(nil, desired, true, 0)
        end
    end
    if menuRoot then
        updatePanel(menuRoot.panel, latest)
        if menuRoot.theaterMenu == EDITOR then editor.update(menuRoot, latest) end
    end
    -- Space belongs to playback in this menu and in the passive HUD. Other
    -- native menus, chat, the console and the advanced editor retain typing.
    game:theaterkeyboard(menuRoot ~= nil or not root:AnyActiveMenusInStack())
end

LUI.onmenuopen("mp_hud", function(hud)
    if Engine.InFrontend() then return end
    if hudPanel then hudPanel:close() end
    local root = Engine.GetLuiRoot()
    local panel = buildPanel(false)
    hudPanel = panel
    -- The root does not exist yet when the in-game HKS VM loads scripts.
    -- Bind after mp_hud is built, and rebind after every VM/map transition.
    root:registerEventHandler("h1_theater_update", updateTheater)
    root:addElement(panel)
    hud:addEventHandler("menu_close", function()
        panel:close()
        if hudPanel == panel then hudPanel = nil end
    end)
    game:theaterready(true)
    print("[theater] Native MWR timeline attached to HUD")
end)
print("[theater] Native MWR timeline script loaded")
