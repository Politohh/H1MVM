-- Category hub built with MWR's MenuTemplate. Individual settings pages can
-- register their own builder without changing the timeline or menu lifecycle.
local Editor = {pages = {}}
local settings = {revision = -1, controls = {}, custom = {}, values = {}}
local categories = {
    {"main", "MAIN"}, {"camera", "CAMERA"}, {"misc", "MISC"},
    {"bone_camera", "BONE CAMERA"}, {"ssao", "SSAO"}, {"fog", "FOG"},
    {"dof", "DOF"}, {"sun", "SUN"}, {"lights", "LIGHTS"},
    {"streams", "STREAMS"}, {"weapon", "WEAPON"}, {"sliders", "SLIDERS"}
}

function Editor.registerPage(id, builder)
    Editor.pages[id] = builder
end

local function layout(x, y, w, h)
    return {leftAnchor = true, rightAnchor = false, topAnchor = true,
        bottomAnchor = false, left = x, top = y, width = w, height = h}
end

function Editor.fitButtonBackground(button)
    local art = button:getChildById("button"):getChildById("buttonContainer")
    local function fit(image)
        if image then
            -- Preserve the native horizontal corner caps, but sample the full
            -- texture height continuously. Stock 12-unit top/bottom slices
            -- compress the gradient into a band on our compact menu rows.
            image:setup9SliceImage(LUI.H1ButtonBackground.EdgeW, 0,
                LUI.H1ButtonBackground.EdgeU, 0)
        end
    end
    local function refresh()
        fit(art:getChildById("fill"))
        fit(art:getChildById("selectionContour"))
        local focused = art:getChildById("focusedContainer")
        if focused then
            fit(focused:getChildById("stroke"))
            fit(focused:getChildById("overlayGlow"))
        end
    end
    refresh()
    -- MWR recreates the contour, stroke and glow when focus/lock changes.
    -- Run after its handlers so freshly created images use the same mapping.
    local fill = art:getChildById("fill")
    for _, event in ipairs({"button_over", "button_up", "enable", "disable",
        "button_disable", "button_over_disable"}) do
        fill:addEventHandler(event, refresh)
    end
end

local function nativeButton(parent, id, label, x, y, w, callback, option)
    local properties = {style = LUI.MenuTemplate.ButtonStyle, button_text = label,
        height = 28, fadeIn = false, text_align_without_content = LUI.Alignment.Left,
        text_padding_without_content = 7, text_padding_focus_without_content = 7,
        button_action_func = callback}
    if option then for k, v in pairs(option) do properties[k] = v end end
    local button = LUI.MenuBuilder.BuildAddChild(parent, {type = "UIGenericButton", id = id, properties = properties})
    button:registerAnimationState("editor_layout", layout(x, y, w, 28))
    button:animateToState("editor_layout", 0)
    local style, substyle = properties.style, properties.style.SubStyles.Default
    local function setting(name)
        if properties[name] ~= nil then return properties[name] end
        if substyle[name] ~= nil then return substyle[name] end
        if style[name] ~= nil then return style[name] end
        return GenericButtonSettings.Common[name]
    end
    local labelSettings = {align = LUI.Alignment.Left, padding = 7, paddingFocus = 7,
        yOffset = 0, overDisabledAnimationDuration = setting("over_disabled_animation_duration")}
    for _, name in ipairs({"text_focus_color", "text_lock_color", "text_default_color", "text_disabled_color",
        "text_over_disabled_color", "text_over_disabled_pulse_color", "pinned_color"}) do labelSettings[name] = setting(name) end
    button.textLabel:close()
    button.textLabel = LUI.UIGenericButton.ButtonLabelFactory(labelSettings, label, button, nil,
        option and 178 or w - 14, {Font = CoD.TextSettings.TitleFontTiny.Font, Height = 15})
    button.textLabel.id = id .. "_label"
    button:addElement(button.textLabel)
    Editor.fitButtonBackground(button)
    return button
end

local function request(name, argument)
    return game:theatereditoraction(name, argument or "")
end

local function display(control)
    local value = settings.values[control.name] or control.min
    if control.kind == "bool" then return value ~= 0 and "ON" or "OFF" end
    if control.choices then return control.choices[math.floor(value) + 1] or tostring(value) end
    return string.format("%.4g", value)
end

local function controlButton(content, control, index, menu)
    local x, y = 465 + math.floor((index - 1) / 12) * 328,
        (menu.category == "sliders" and 205 or 185) + ((index - 1) % 12) * 31
    local slider = control.kind == "float" and control.max > control.min
    local function change(direction)
        local value = settings.values[control.name] or control.min
        local step = slider and math.max(control.step, (control.max - control.min) / 100) or control.step
        local nextValue = value + direction * step
        if not slider then
            if nextValue > control.max then nextValue = control.min end
            if nextValue < control.min then nextValue = control.max end
        else nextValue = math.max(control.min, math.min(control.max, nextValue)) end
        if game:theatereditorset(control.name, nextValue) then settings.values[control.name] = nextValue end
    end
    local options = {variant = slider and GenericButtonSettings.Variants.Slider or GenericButtonSettings.Variants.Select,
        H1OptionButton = true, content_width = 110, content_slider_width = 92, maxLabelWidth = 178,
        text_align_with_content = LUI.Alignment.Left,
        button_display_func = function()
            if slider then return ((settings.values[control.name] or control.min) - control.min) / (control.max - control.min) end
            return display(control)
        end,
        button_left_func = function() change(-1) end,
        button_right_func = function() change(1) end,
        button_over_func = function() menu.help:setText(control.label .. "  /  " .. display(control)) end,
        buttonActionIsRightAction = not slider, muteAction = slider}
    local element = nativeButton(content, "setting_" .. control.name, control.label, x, y, 316,
        function() change(1) end, options)
    menu.controls[#menu.controls + 1] = {element = element, control = control}
end

local notes = {
    camera = "PATH OFFSETS APPLY IN DOLLY MODE.  SHIFT = BOOST / ALT = PRECISE FLIGHT.",
    misc = "NATIVE HUD, EFFECT AND FILM CONTROLS.",
    bone_camera = "BONE ATTACHMENT NEEDS AN H1 WORLD-TAG CAMERA HOOK; IT IS NOT AVAILABLE YET.",
    ssao = "H1 SSAO MODES AND OVERRIDES. ENABLE SSAO OVERRIDE TO USE TWEAK VALUES.",
    fog = "H1 FOG AND SKY FOG. T6 HEIGHT/COLOR PRESETS DO NOT MATCH H1'S ATMOSPHERE.",
    dof = "H1 PHYSICAL AND GAUSSIAN DOF. PARAMETERS DEPEND ON THE SELECTED MODE.",
    sun = "SUN FLARE AND SHADOWS. THESE DO NOT CHANGE THE MAP'S SUN DIRECTION OR COLOR.",
    lights = "EXISTING H1 LIGHTING. PLACING CUSTOM OMNI/SPOT LIGHTS NEEDS A NEW ENGINE HOOK.",
    streams = "SINGLE BEAUTY VIDEO CAPTURE. OUTPUT: DEMOS/AVI. REQUIRES FFMPEG.",
    weapon = "REGISTERED H1 VIEWMODEL CONTROLS. T6 CAMO/HOLDGUN/ANIMATION HOOKS ARE NOT PORTABLE.",
    sliders = "BUILD A PALETTE FROM SUPPORTED H1 SETTINGS. SAVE IT WITH A MAIN PRESET."
}

local function text(parent, id, value, y, height)
    local element = LUI.UIText.new({leftAnchor = true, rightAnchor = false,
        topAnchor = true, bottomAnchor = false, left = 465, top = y,
        width = 650, height = height, font = CoD.TextSettings.FontBold110.Font,
        alignment = LUI.Alignment.Left, color = {r = 0.9, g = 0.93, b = 0.9}})
    element.id = id
    element:setText(value)
    parent:addElement(element)
    return element
end

function Editor.create(parameters, back)
    local menu = LUI.MenuTemplate.new(parameters, {
        menu_title = "THEATER EDITOR",
        menu_width = 310, menu_top_indent = LUI.MenuTemplate.spMenuOffset,
        menu_height = 420, spacing = 2, do_not_add_friends_helper = true,
        showTopRightSmallBar = true, uppercase_title = true, skipAnim = true
    })
    menu.id = "h1_theater_editor"
    menu.content = LUI.UIElement.new({leftAnchor = true, rightAnchor = true,
        topAnchor = true, bottomAnchor = true})
    menu:addElement(menu.content)
    function menu:selectCategory(id, label)
        self.category = id
        -- closeChildren clears MWR's Lua childRecord as well as the native tree.
        self.content:closeChildren()
        self.controls = {}
        text(self.content, "theater_editor_category", label, 110, 20)
        self.help = text(self.content, "theater_editor_help", notes[id] or "", 145, 10)
        self.feedback = text(self.content, "theater_editor_feedback", "", 595, 10)
        local builder = Editor.pages[id]
        if builder then builder(self.content, self) end
    end
    -- AddButton supplies MWR's slanted borders, focus animation, native font,
    -- sounds and controller navigation; these are not custom transport buttons.
    for _, category in ipairs(categories) do
        local id, label = category[1], category[2]
        local button = menu:AddButton(label, function() menu:selectCategory(id, label) end,
            nil, true, nil, {height = 26, fadeIn = false})
        Editor.fitButtonBackground(button)
    end
    menu:AddBackButton(function()
        Engine.PlaySound(CoD.SFX.MenuBack)
        back()
    end)
    -- All twelve categories fit in 334 units. The options helper overrides
    -- this list with a nine-row viewport and adds a scrollbar; do not use it.
    menu:CreateBottomDivider()
    menu:selectCategory("main", "MAIN")
    return menu
end

Editor.registerPage("main", function(content, menu)
    menu.demoName = text(content, "theater_editor_demo", "", 170, 12)
    menu.demoStatus = text(content, "theater_editor_status", "", 200, 12)
    menu.slot = menu.slot or 1
    local slot = nativeButton(content, "editor_preset_slot", "PRESET SLOT  " .. menu.slot, 465, 240, 316, function()
        menu.slot = menu.slot % 8 + 1
        menu.slotButton.textLabel:setText("PRESET SLOT  " .. menu.slot)
    end)
    menu.slotButton = slot
    nativeButton(content, "editor_save_preset", "SAVE ALL AS CFG", 465, 278, 316, function() return request("save_preset", tostring(menu.slot)) end)
    nativeButton(content, "editor_load_preset", "LOAD CFG", 465, 316, 316, function() return request("load_preset", tostring(menu.slot)) end)
    text(content, "editor_preset_path", "DEMOS/THEATER/PRESET_1.CFG ... PRESET_8.CFG", 362, 11)
end)

local function settingsPage(id)
    Editor.registerPage(id, function(content, menu)
        local index = 0
        for _, control in ipairs(settings.controls) do
            if control.page == id then index = index + 1; controlButton(content, control, index, menu) end
        end
        if index > 0 then
            nativeButton(content, "editor_reset", "RESET TO ENGINE DEFAULTS", 793, 560, 316, function() return request("reset", id) end)
        end
        if id == "camera" then
            nativeButton(content, "editor_add_key", "ADD DOLLY MARKER [K]", 793, 250, 316, function() return game:theateraction("add", 0) end)
            nativeButton(content, "editor_play_path", "START / STOP DOLLY [J]", 793, 285, 316, function() return game:theateraction("cinematic", 0) end)
            nativeButton(content, "editor_clear_keys", "CLEAR ALL MARKERS", 793, 320, 316, function() return game:theateraction("clear", 0) end)
            nativeButton(content, "editor_save_path", "EXPORT THIS DEMO'S CAMERA PATH", 793, 355, 316, function() return request("save_path") end)
            nativeButton(content, "editor_load_path", "IMPORT THIS DEMO'S CAMERA PATH", 793, 390, 316, function() return request("load_path") end)
        elseif id == "streams" then
            menu.range = text(content, "editor_capture_range", "", 410, 12)
            nativeButton(content, "editor_capture_in", "SET IN AT PLAYHEAD", 793, 185, 316, function() return request("capture_in") end)
            nativeButton(content, "editor_capture_out", "SET OUT AT PLAYHEAD", 793, 220, 316, function() return request("capture_out") end)
            nativeButton(content, "editor_capture_clear", "CLEAR CAPTURE RANGE", 793, 255, 316, function() return request("capture_clear") end)
            nativeButton(content, "editor_capture_start", "START CAPTURE", 793, 290, 316, function() return request("capture_start") end)
            nativeButton(content, "editor_capture_stop", "DISARM CAPTURE", 793, 325, 316, function() return request("capture_stop") end)
        end
    end)
end
for _, category in ipairs(categories) do
    if category[1] ~= "main" and category[1] ~= "sliders" then settingsPage(category[1]) end
end

Editor.registerPage("sliders", function(content, menu)
    local candidates, byName = {}, {}
    for _, control in ipairs(settings.controls) do
        if control.kind == "float" then candidates[#candidates + 1] = control end
        byName[control.name] = control
    end
    menu.sliderPick = math.max(1, math.min(menu.sliderPick or 1, #candidates))
    local function selection() return candidates[menu.sliderPick] end
    local picker
    picker = nativeButton(content, "editor_slider_pick", selection() and selection().label or "NO FLOAT SETTINGS", 465, 165, 316, function()
        if #candidates > 0 then menu.sliderPick = menu.sliderPick % #candidates + 1; picker.textLabel:setText(selection().label) end
    end)
    nativeButton(content, "editor_slider_add", "ADD SELECTED SLIDER", 793, 165, 153, function() if selection() then return request("add_slider", selection().name) end end)
    nativeButton(content, "editor_slider_remove", "REMOVE SELECTED", 956, 165, 153, function() if selection() then return request("remove_slider", selection().name) end end)
    local index = 0
    for _, name in ipairs(settings.custom) do
        if byName[name] then index = index + 1; controlButton(content, byName[name], index, menu) end
    end
end)

function Editor.update(menu, data)
    local received = game:gettheatereditor(settings.revision)
    local changed = received.revision ~= settings.revision
    for k, v in pairs(received) do settings[k] = v end
    if changed then
        for _, category in ipairs(categories) do
            if menu.category == category[1] then menu:selectCategory(category[1], category[2]); break end
        end
    end
    for _, item in ipairs(menu.controls) do item.element:processEvent({name = "content_refresh"}) end
    menu.feedback:setText(settings.message or "")
    if menu.category == "main" then
        menu.demoName:setText(data.name or "")
        menu.demoStatus:setText(string.format("%s   %.3gx",
            data.paused and "PAUSED" or "PLAYING", data.speed or 1))
    elseif menu.category == "streams" and menu.range then
        local function point(ms) if not ms or ms < 0 then return "--:--" end; return string.format("%02d:%02d", math.floor(ms / 60000), math.floor(ms / 1000) % 60) end
        menu.range:setText("IN " .. point(settings.captureIn) .. "   OUT " .. point(settings.captureOut) ..
            "   " .. (settings.armed and "ARMED" or "READY") .. "   " .. tostring(settings.frames or 0) .. " FRAMES")
    end
end

return Editor
