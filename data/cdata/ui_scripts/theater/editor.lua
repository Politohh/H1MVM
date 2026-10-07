-- Category hub built with MWR's MenuTemplate. Individual settings pages can
-- register their own builder without changing the timeline or menu lifecycle.
local Editor = {pages = {}}
local settings = {revision = -1, controls = {}, custom = {}, values = {}}
local pending = {}
-- Move the whole editor column, including picker hit boxes and animation
-- states, above the independent bottom-anchored timeline and its hints.
local CONTENT_OFFSET_Y = -80
local categories = {
    {"main", "MAIN"}, {"camera", "CAMERA"}, {"misc", "MISC"},
    {"bone_camera", "BONE CAMERA"}, {"ssao", "SSAO"}, {"fog", "FOG"},
    {"dof", "DOF"}, {"sun", "SUN"}, {"lights", "LIGHTS"},
    {"streams", "STREAMS"}, {"weapon", "WEAPON"}, {"models", "MODEL SWAPPER"}, {"sliders", "SLIDERS"}
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
    button:registerAnimationState("editor_layout", layout(x, y, w, properties.height))
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
        option and (option.maxLabelWidth or w - 14) or w - 14, {Font = CoD.TextSettings.TitleFontTiny.Font, Height = 15})
    button.textLabel.id = id .. "_label"
    button:addElement(button.textLabel)
    Editor.fitButtonBackground(button)
    return button
end

local function request(name, argument)
    local accepted = game:theatereditoraction(name, argument or "")
    if accepted and (name == "reset" or name == "reset_all" or name == "load_preset") then pending = {} end
    return accepted
end

local function display(control)
    local value = settings.values[control.name] or control.min
    if control.kind == "bool" then return value ~= 0 and "ON" or "OFF" end
    if control.choices then return control.choices[math.floor(value) + 1] or tostring(value) end
    return string.format("%.4g", value)
end

local openColorPicker, openSkyPicker
local function image(parent, id, x, y, w, h, color, material)
    local style = layout(x, y, w, h)
    style.color, style.material = color, RegisterMaterial(material or "white")
    local element = LUI.UIImage.new(style)
    element.id = id; element.editorImageLayout = style; parent:addElement(element)
    return element
end
local function tint(element, rgb)
    -- Native animation states need complete image bounds. A color-only state
    -- expands the preview/cells to their parent's default rectangle.
    local state = {}
    for key,value in pairs(element.editorImageLayout) do state[key] = value end
    state.color = {r = math.min(1,rgb[1]), g = math.min(1,rgb[2]), b = math.min(1,rgb[3])}
    element:registerAnimationState("color", state)
    element:animateToState("color", 0)
end
local function submit(control, value)
    local ticket
    if control.kind == "color" then ticket = game:theatereditorcolor(control.name, value[1], value[2], value[3])
    else ticket = game:theatereditorset(control.name, value) end
    if type(ticket) == "number" and ticket > 0 then
        pending[control.name] = {ticket = ticket, value = value}
        settings.values[control.name] = value
        return true
    end
    return false
end

-- Keep MWR's native button shell and slider artwork, but size every hit area
-- explicitly for a 28-unit row. The stock Select click region has 30-unit
-- top/bottom margins, and cannot be used at this compact height.
local function valueRow(content, id, label, x, y, control, getter, setter, menu)
    local slider = control.kind == "float" and control.max > control.min
    local function readout()
        if control.format then return control.format(getter()) end
        if control.name == "demo_sun_timeofday" then
            local minutes = math.floor(getter()*60+.5)
            return string.format("%02d:%02d", math.floor(minutes/60), minutes%60)
        end
        return string.format("%.4g", getter())
    end
    local refresh
    local function change(direction)
        local nextValue = getter() + direction * control.step
        if slider then nextValue = math.max(control.min, math.min(control.max, nextValue))
        elseif nextValue > control.max then nextValue = control.min
        elseif nextValue < control.min then nextValue = control.max end
        if setter(nextValue) and refresh then refresh(); menu.help:setText(label .. "  /  " .. readout()) end
    end
    local button = nativeButton(content, id, label, x, y, 316,
        not slider and function() change(1) end or nil, {maxLabelWidth = 178})
    button:registerEventHandler("button_left", function(_,event) change(-1); event.handled=true; return true end)
    button:registerEventHandler("button_right", function(_,event) change(1); event.handled=true; return true end)
    button:addEventHandler("button_over", function()
        menu.help:setText(label .. "  /  " .. readout())
    end)
    for _, spec in ipairs({{"left", 202, "<", -1}, {"right", 294, ">", 1}}) do
        -- Decorative children: the native parent sees mouse release first.
        -- Nested UIButton actions are swallowed by that parent in MWR.
        local arrow = LUI.UIElement.new(layout(spec[2], 2, 20, 24))
        arrow.id = id .. "_" .. spec[1]
        local caption = LUI.UIText.new({leftAnchor = true, rightAnchor = true, topAnchor = true,
            bottomAnchor = false, left = 0, right = 0, top = 6, height = 12,
            font = CoD.TextSettings.TitleFontTiny.Font, alignment = LUI.Alignment.Center})
        caption:setText(spec[3]); arrow:addElement(caption)
        button:addElement(arrow)
    end
    if slider then
        local track = LUI.UIElement.new(layout(224, 10, 68, 8))
        track.id = id .. "_track"
        image(track, id .. "_track_border", 0, 0, 68, 8, {r=.18,g=.3,b=.2})
        image(track, id .. "_track_art", 1, 1, 66, 6, {r=.025,g=.045,b=.03})
        local fill = image(track, id .. "_fill", 2, 2, 0, 4, {r=.35,g=.7,b=.43})
        local thumb = image(track, id .. "_thumb", 1, -1, 2, 10, {r=.8,g=.9,b=.82})
        refresh = function()
            local fraction = math.max(0, math.min(1, (getter()-control.min)/(control.max-control.min)))
            fill:registerAnimationState("value", layout(2, 2, 64 * fraction, 4)); fill:animateToState("value", 0)
            thumb:registerAnimationState("value", layout(1 + 64 * fraction, -1, 2, 10)); thumb:animateToState("value", 0)
        end
        button:addElement(track)
    else
        local style = layout(224,8,68,12)
        style.font, style.alignment = CoD.TextSettings.TitleFontTiny.Font, LUI.Alignment.Center
        local value = LUI.UIText.new(style); button:addElement(value)
        refresh = function()
            local v = getter()
            value:setText(control.format and control.format(v) or control.kind == "bool" and (v ~= 0 and "ON" or "OFF") or
                control.choices and control.choices[math.floor(v)+1] or string.format("%.4g",v))
        end
    end
    local function pointer(event)
        if not event.root or not event.x or not event.y then return end
        local px,py = event.x,event.y
        if event.rootName then px,py=ProjectRootCoordinate(event.rootName,px,py) end
        local left,top=button:getRect()
        local start=event.root:pixelsToUnits(left,top)
        return event.root:pixelsToUnits(px,py)-start
    end
    local function region(px)
        if not px then return end
        if px>=202 and px<224 then return "left" end
        if px>=294 then return "right" end
        if slider and px>=224 and px<294 then return "track" end
        return "label"
    end
    local function drag(event)
        local px=pointer(event); if not px then return end
        local fraction=math.max(0,math.min(1,(px-226)/64))
        local value=control.min+fraction*(control.max-control.min)
        value=math.max(control.min,math.min(control.max,
            control.min+math.floor((value-control.min)/control.step+.5)*control.step))
        if value~=getter() and setter(value) then
            refresh(); menu.help:setText(label .. "  /  " .. readout())
        end
    end
    local pressed
    button:registerEventHandler("leftmousedown", function(element,event)
        LUI.UIButton.LeftMouseDown(element,event)
        if element.disabled then return end
        pressed=region(pointer(event))
        if pressed=="track" then drag(event) end
    end)
    button:registerEventHandler("mousedrag", function(_,event) if pressed=="track" then drag(event) end end)
    button:registerEventHandler("leftmouseup", function(element,event)
        local was=pressed; pressed=nil
        if not element.disabled and event.inside and was==region(pointer(event)) then
            if was=="left" or was=="right" then
                change(was=="left" and -1 or 1)
                element:processEvent({name="button_over",controller=event.controller})
                event.handled=true; return true
            elseif was=="label" and not slider then
                return LUI.UIButton.LeftMouseUp(element,event)
            end
        end
        element:processEvent({name=event.inside and "button_over" or "button_up",controller=event.controller})
        event.handled=true; return true
    end)
    button:addEventHandler("content_refresh", refresh)
    refresh()
    return button
end

Editor.valueRow = valueRow

local function controlButton(content, control, index, menu)
    local x, y = 465 + math.floor((index - 1) / 10) * 328,
        (menu.category == "sliders" and 205 or 185) + ((index - 1) % 10) * 31
    local element
    if control.name == "demo_skybox" then
        element = nativeButton(content, "setting_" .. control.name, "SKYBOX: "..display(control), x,y,316,
            function() openSkyPicker(menu,control,1) end)
        element:addEventHandler("content_refresh",function() element.textLabel:setText("SKYBOX: "..display(control)) end)
    elseif control.kind == "color" then
        element = nativeButton(content, "setting_" .. control.name, control.label, x, y, 316,
            function() openColorPicker(menu, control) end, {maxLabelWidth=178})
        local swatch = image(element, "swatch", 238, 6, 64, 16, {r=1,g=1,b=1})
        element:addEventHandler("content_refresh", function() tint(swatch,settings.values[control.name] or {1,1,1}) end)
        element:processEvent({name="content_refresh"})
    else
        element = valueRow(content, "setting_" .. control.name, control.label, x, y, control,
            function()
                local value = settings.values[control.name] or control.min
                return control.inverted and -value or value
            end,
            function(v) return submit(control,control.inverted and -v or v) end, menu)
    end
    menu.controls[#menu.controls + 1] = {element = element, control = control}
end

local notes = {
    camera = "PATH OFFSETS APPLY IN DOLLY MODE.  SHIFT = BOOST / ALT = PRECISE FLIGHT.",
    misc = "NATIVE HUD, EFFECT AND FILM CONTROLS.",
    bone_camera = "SELECT AN ACTOR AND BONE, THEN ATTACH. OFFSET IS PRESERVED; DETACH RETAINS FREECAM.",
    ssao = "H1 SSAO MODES AND OVERRIDES. ENABLE SSAO OVERRIDE TO USE TWEAK VALUES.",
    fog = "SAMPLE THE MAP FIRST, THEN ENABLE ATMOSPHERE OVERRIDE. COLOR, HEIGHT AND HAZE ARE LIVE.",
    dof = "H1 PHYSICAL AND GAUSSIAN DOF. PARAMETERS DEPEND ON THE SELECTED MODE.",
    sun = "SUN LIGHT DIRECTION / COLOR / INTENSITY AND FLARE. BAKED LIGHTING STAYS BAKED.",
    lights = "EDIT MAP LIGHTS OR PLACE UP TO 16 POINT LIGHTS AT THE CAMERA. SELECTED POINT SETTINGS ARE LIVE.",
    streams = "CAPTURE CURRENT SETTINGS OR OMIT PLAYERS. OUTPUT: DEMOS/AVI. REQUIRES FFMPEG.",
    weapon = "VIEWMODEL VISIBILITY. ADDITIONAL OFFSETS / SWAY REQUIRE H1 VIEWMODEL HOOKS.",
    sliders = "BUILD A PALETTE FROM REGISTERED H1 RENDER / CAMERA / EFFECT SETTINGS. SAVED WITH MAIN PRESETS."
}

local function text(parent, id, value, y, height)
    local element = LUI.UIText.new({leftAnchor = true, rightAnchor = false,
        topAnchor = true, bottomAnchor = false, left = 465, top = y,
        width = 644, height = height, font = CoD.TextSettings.FontBold110.Font,
        alignment = LUI.Alignment.Center, color = {r = 0.9, g = 0.93, b = 0.9}})
    element.id = id
    element:setText(value)
    parent:addElement(element)
    return element
end

local function categoryTitle(parent, id, value)
    -- Same font, height, base color and MW1 glow treatment as the native
    -- generic_menu_title header used by MenuTemplate's THEATER EDITOR title.
    local font = CoD.TextSettings.H1TitleFont
    local style = layout(465, 95, 644, font.Height)
    style.font, style.alignment = font.Font, LUI.Alignment.Center
    style.color = luiglobals.Colors.h1.light_grey
    local title = LUI.UIText.new(style)
    title.id = id
    title:setText(value)
    title:setTextStyle(CoD.TextStyle.MW1Title)
    parent:addElement(title)
    local divider = layout(465, 95 + font.Height + 8, 644, 1)
    divider.material = RegisterMaterial("h1_ui_divider_gradient_left")
    divider.color, divider.alpha = luiglobals.Colors.mw1_green, 0.5
    local rule = LUI.UIImage.new(divider)
    rule.id = id .. "_divider"
    parent:addElement(rule)
    return title
end

function Editor.rgbToHSV(rgb, fallbackHue)
    local r,g,b = rgb[1],rgb[2],rgb[3]
    local high, low = math.max(r,g,b), math.min(r,g,b)
    local delta, hue = high-low, fallbackHue or 0
    if delta > 0 then
        if high == r then hue = 60*((g-b)/delta % 6)
        elseif high == g then hue = 60*((b-r)/delta+2)
        else hue = 60*((r-g)/delta+4) end
    end
    return hue, high > 0 and delta/high or 0, high
end
function Editor.hsvToRGB(h,s,v)
    h = h%360/60
    local c = v*s
    local x = c*(1-math.abs(h%2-1))
    local m = v-c
    local combinations = {{c,x,0},{x,c,0},{0,c,x},{0,x,c},{x,0,c},{c,0,x}}
    local rgb = combinations[math.floor(h)+1]
    return {rgb[1]+m,rgb[2]+m,rgb[3]+m}
end

openColorPicker = function(menu, control)
    menu.content:closeChildren(); menu.controls = {}
    categoryTitle(menu.content,"color_title",control.label)
    menu.help = text(menu.content,"color_help","PICK A COLOR. CHANGES APPLY LIVE; ALPHA IS PRESERVED.",145,10)
    menu.feedback = text(menu.content,"theater_editor_feedback","",595,10)
    local initial = settings.values[control.name] or {1,1,1}
    local h,s,v = Editor.rgbToHSV(initial)
    local cells = {}
    local preview = image(menu.content,"color_preview",793,310,316,44,{r=1,g=1,b=1})
    local readout = LUI.UIText.new({leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
        left=793,top=360,width=316,height=12,font=CoD.TextSettings.TitleFontTiny.Font})
    menu.content:addElement(readout)
    local marker
    local picker = {control=control, last=initial}
    menu.colorPicker = picker
    local lastHue
    local function redraw()
        local rgb = Editor.hsvToRGB(h,s,v)
        tint(preview,rgb)
        readout:setText(string.format("#%02X%02X%02X   H %.0f / S %.0f%% / V %.3g",
            math.floor(math.min(1,rgb[1])*255+.5),math.floor(math.min(1,rgb[2])*255+.5),
            math.floor(math.min(1,rgb[3])*255+.5),h,s*100,v))
        local markerState = layout(465+s*219,185+(1-math.min(1,v))*219,5,5)
        markerState.color,markerState.material = {r=1,g=1,b=1},RegisterMaterial("white")
        marker:registerAnimationState("position",markerState)
        marker:animateToState("position",0)
        if lastHue ~= h then
            for _, cell in ipairs(cells) do tint(cell.image,Editor.hsvToRGB(h,cell.s,cell.v)) end
            lastHue = h
        end
        for _, row in ipairs(picker.rows or {}) do row:processEvent({name="content_refresh"}) end
    end
    local function send()
        local rgb = Editor.hsvToRGB(h,s,v)
        if submit(control,rgb) then picker.last=rgb; redraw(); return true end
        return false
    end
    for row=0,15 do for col=0,15 do
        local sat, brightness = col/15, 1-row/15
        local cell = LUI.UIButton.new(layout(465+col*14,185+row*14,14,14))
        cell.id="color_cell_"..row.."_"..col
        local art = image(cell,"fill",0,0,14,14,{r=1,g=1,b=1})
        cell:registerEventHandler("button_action",function(_,event) s,v=sat,brightness; send(); event.handled=true; return true end)
        menu.content:addElement(cell); cells[#cells+1]={image=art,s=sat,v=brightness}
    end end
    for i=0,35 do
        local hue = i*10
        local cell = LUI.UIButton.new(layout(707,185+i*224/36,20,224/36))
        cell.id="color_hue_"..i
        local rgb=Editor.hsvToRGB(hue,1,1)
        image(cell,"fill",0,0,20,224/36,{r=rgb[1],g=rgb[2],b=rgb[3]})
        cell:registerEventHandler("button_action",function(_,event) h=hue; send(); event.handled=true; return true end)
        menu.content:addElement(cell)
    end
    -- Marker is decorative and must not consume color-square mouse input.
    -- Create last for draw order. Closing and re-adding a native element can
    -- orphan its renderer handle after the picker is dismissed.
    marker = image(menu.content,"color_marker",465,185,5,5,{r=1,g=1,b=1})
    picker.rows = {}
    for i,spec in ipairs({{"HUE",0,360,1},{"SATURATION",0,1,.01},{"BRIGHTNESS",0,control.max,.01}}) do
        local row = valueRow(menu.content,"color_component_"..i,spec[1],793,185+(i-1)*38,
            {kind="float",min=spec[2],max=spec[3],step=spec[4]},
            function() return i==1 and h or i==2 and s or v end,
            function(value) if i==1 then h=value elseif i==2 then s=value else v=value end; return send() end,menu)
        picker.rows[#picker.rows+1]=row
    end
    local swatches = {{1,1,1},{.5,.5,.5},{0,0,0},{1,0,0},{0,1,0},{0,0,1},{1,.7,.4},{.4,.7,1}}
    for i,rgb in ipairs(swatches) do
        local swatch=LUI.UIButton.new(layout(793+(i-1)*39,395,32,28)); swatch.id="color_preset_"..i
        image(swatch,"fill",1,1,30,26,{r=rgb[1],g=rgb[2],b=rgb[3]})
        swatch:registerEventHandler("button_action",function(_,event) h,s,v=Editor.rgbToHSV(rgb,h); send(); event.handled=true; return true end)
        menu.content:addElement(swatch)
    end
    nativeButton(menu.content,"color_restore","RESTORE ORIGINAL COLOR",793,450,316,function()
        h,s,v=Editor.rgbToHSV(initial,h); return send()
    end)
    nativeButton(menu.content,"color_back","BACK TO "..string.upper(menu.category:gsub("_"," ")),793,490,316,function()
        menu:selectCategory(menu.category,string.upper(menu.category:gsub("_"," ")))
    end)
    picker.refresh = function()
        local rgb=settings.values[control.name]
        if rgb and (rgb[1]~=picker.last[1] or rgb[2]~=picker.last[2] or rgb[3]~=picker.last[3]) then
            h,s,v=Editor.rgbToHSV(rgb,h); picker.last=rgb; redraw()
        end
    end
    redraw()
end

-- A native button grid keeps full sky names readable and all choices
-- reachable. Choosing Original immediately restores this map's own sky.
openSkyPicker = function(menu,control,page)
    menu.content:closeChildren();menu.controls={};menu.colorPicker=nil;menu.skyPicker=true
    categoryTitle(menu.content,"sky_title","SKYBOX")
    menu.help=text(menu.content,"sky_help","CHOOSE A GAME SKY. LIGHTING AND ATMOSPHERE KEEP YOUR CURRENT SETTINGS.",145,10)
    menu.feedback=text(menu.content,"theater_editor_feedback","",595,10)
    local pages=math.max(1,math.ceil(#control.choices/20))
    page=(page-1)%pages+1
    for i=(page-1)*20+1,math.min(page*20,#control.choices) do
        local index=i
        local offset=i-(page-1)*20-1
        nativeButton(menu.content,"sky_option_"..i,control.choices[i],465+math.floor(offset/10)*328,
            185+(offset%10)*31,316,function()
                if submit(control,index-1) then menu.help:setText("SKYBOX  /  "..control.choices[index]);return true end
            end)
    end
    if pages>1 then
        nativeButton(menu.content,"sky_previous","< PREVIOUS",465,515,316,function() openSkyPicker(menu,control,page-1) end)
        nativeButton(menu.content,"sky_next","NEXT >  "..page.."/"..pages,793,515,316,function() openSkyPicker(menu,control,page+1) end)
    end
    nativeButton(menu.content,"sky_original","RESTORE MAP SKY",465,560,316,function() return submit(control,0) end)
    nativeButton(menu.content,"sky_back","BACK TO SUN",793,560,316,function() menu:selectCategory("sun","SUN") end)
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
        topAnchor = true, bottomAnchor = true, left = 0, right = 0,
        top = CONTENT_OFFSET_Y, bottom = CONTENT_OFFSET_Y})
    menu:addElement(menu.content)
    function menu:populateCategories()
        self.list:closeChildren()
        for _, category in ipairs(categories) do
            local id, label = category[1], category[2]
            local button = self:AddButton(label, function() self:selectCategory(id, label) end,
                nil, true, nil, {height = 26, fadeIn = false})
            Editor.fitButtonBackground(button)
        end
    end
    function menu:selectCategory(id, label)
        if self.category ~= id then self.settingsPage = 1 end
        if id == "models" then self.list:closeChildren()
        elseif self.category == "models" then self:populateCategories() end
        self.category = id
        self.colorPicker = nil
        self.skyPicker = nil
        self.modelBrowser = nil
        -- closeChildren clears MWR's Lua childRecord as well as the native tree.
        self.content:closeChildren()
        self.controls = {}
        categoryTitle(self.content, "theater_editor_category", label)
        self.help = text(self.content, "theater_editor_help", notes[id] or "", 145, 10)
        self.feedback = text(self.content, "theater_editor_feedback", "", 595, 10)
        local builder = Editor.pages[id]
        if builder then builder(self.content, self) end
    end
    -- AddButton supplies MWR's slanted borders, focus animation, native font,
    -- sounds and controller navigation; these are not custom transport buttons.
    menu:populateCategories()
    menu:AddBackButton(function()
        Engine.PlaySound(CoD.SFX.MenuBack)
        if menu.category == "models" then menu:selectCategory("main", "MAIN")
        elseif menu.colorPicker or menu.skyPicker then menu:selectCategory(menu.category, string.upper(menu.category:gsub("_", " ")))
        else back() end
    end)
    -- All thirteen categories fit in 362 units. The options helper overrides
    -- this list with a nine-row viewport and adds a scrollbar; do not use it.
    menu:CreateBottomDivider()
    menu:selectCategory("main", "MAIN")
    return menu
end

Editor.registerPage("main", function(content, menu)
    menu.demoName = text(content, "theater_editor_demo", "", 170, 12)
    menu.demoStatus = text(content, "theater_editor_status", "", 200, 12)
    menu.slot = menu.slot or 1
    local slot = nativeButton(content, "editor_preset_slot", "PRESET SLOT  " .. menu.slot, 629, 240, 316, function()
        menu.slot = menu.slot % 8 + 1
        menu.slotButton.textLabel:setText("PRESET SLOT  " .. menu.slot)
    end)
    menu.slotButton = slot
    nativeButton(content, "editor_save_preset", "SAVE ALL AS CFG", 629, 278, 316, function() return request("save_preset", tostring(menu.slot)) end)
    nativeButton(content, "editor_load_preset", "LOAD CFG", 629, 316, 316, function() return request("load_preset", tostring(menu.slot)) end)
    text(content, "editor_preset_path", "DEMOS/THEATER/PRESET_1.CFG ... PRESET_8.CFG", 362, 11)
    nativeButton(content, "editor_reset_all", "RESET ALL EDITOR SETTINGS", 629, 410, 316, function() return request("reset_all") end)
end)

local function settingsPage(id)
    Editor.registerPage(id, function(content, menu)
        local items = {}
        for _, control in ipairs(settings.controls) do
            if control.page == id then items[#items + 1] = control end
        end
        local actions = id == "camera" or id == "streams" or id == "bone_camera" or id == "fog" or id == "lights" or id == "misc"
        local capacity = actions and 10 or 20
        local pages = math.max(1, math.ceil(#items / capacity))
        menu.settingsPage = math.max(1, math.min(menu.settingsPage or 1, pages))
        local first = (menu.settingsPage - 1) * capacity
        for i = first + 1, math.min(first + capacity, #items) do
            controlButton(content, items[i], i - first, menu)
        end
        if #items > 0 then
            nativeButton(content, "editor_reset", "RESET THIS TAB", 793, 560, 316, function() return request("reset", id) end)
        end
        nativeButton(content, "editor_reset_all", "RESET ALL EDITOR SETTINGS", 465, 560, 316, function() return request("reset_all") end)
        if pages > 1 then
            local function turn(direction)
                menu.settingsPage = (menu.settingsPage - 1 + direction) % pages + 1
                menu:selectCategory(id, string.upper(id:gsub("_", " ")))
            end
            nativeButton(content, "editor_previous_page", "< PREVIOUS", 465, 515, 153, function() turn(-1) end)
            nativeButton(content, "editor_next_page", "NEXT >  " .. menu.settingsPage .. "/" .. pages, 628, 515, 153, function() turn(1) end)
        end
        if id == "camera" then
            nativeButton(content, "editor_add_key", "ADD DOLLY MARKER [K]", 793, 250, 316, function() return game:theateraction("add", 0) end)
            nativeButton(content, "editor_play_path", "START / STOP DOLLY [J]", 793, 285, 316, function() return game:theateraction("cinematic", 0) end)
            nativeButton(content, "editor_clear_keys", "CLEAR ALL MARKERS", 793, 320, 316, function() return game:theateraction("clear", 0) end)
            nativeButton(content, "editor_save_path", "EXPORT THIS DEMO'S CAMERA PATH", 793, 355, 316, function() return request("save_path") end)
            nativeButton(content, "editor_load_path", "IMPORT THIS DEMO'S CAMERA PATH", 793, 390, 316, function() return request("load_path") end)
        elseif id == "bone_camera" then
            nativeButton(content, "editor_bone_next", "NEXT ACTIVE ACTOR", 793, 185, 316, function() return request("bone_next") end)
            nativeButton(content, "editor_bone_attach", "ATTACH CAMERA TO BONE", 793, 220, 316, function() return request("bone_attach") end)
            nativeButton(content, "editor_bone_detach", "DETACH TO FREE CAMERA", 793, 255, 316, function() return request("bone_detach") end)
            menu.sceneStatus = text(content, "editor_scene_status", "", 480, 11)
        elseif id == "fog" then
            nativeButton(content, "editor_fog_sample", "COPY MAP ATMOSPHERE", 793, 185, 316, function() return request("fog_sample") end)
        elseif id == "lights" then
            nativeButton(content, "editor_point_add", "PLACE POINT AT CAMERA", 793, 185, 316, function() return request("point_add") end)
            nativeButton(content, "editor_point_next", "NEXT EDITOR POINT", 793, 220, 316, function() return request("point_next") end)
            nativeButton(content, "editor_point_remove", "REMOVE SELECTED POINT", 793, 255, 316, function() return request("point_remove") end)
            nativeButton(content, "editor_point_clear", "CLEAR EDITOR POINTS", 793, 290, 316, function() return request("point_clear") end)
            menu.sceneStatus = text(content, "editor_scene_status", "", 480, 11)
        elseif id == "misc" then
            nativeButton(content, "editor_toggle_hud", "TOGGLE ENTIRE GAME HUD [F1]", 793, 185, 316, function() return request("toggle_hud") end)
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
    if category[1] ~= "main" and category[1] ~= "sliders" and category[1] ~= "models" then settingsPage(category[1]) end
end
require("model_swapper")(Editor,nativeButton,image,settings,request)

Editor.registerPage("sliders", function(content, menu)
    nativeButton(content, "editor_reset_all", "RESET ALL EDITOR SETTINGS", 465, 560, 316, function() return request("reset_all") end)
    local candidates, byName = {}, {}
    for _, control in ipairs(settings.controls) do
        if control.kind == "float" or control.kind == "color" then candidates[#candidates + 1] = control end
        byName[control.name] = control
    end
    menu.sliderPick = math.max(1, math.min(menu.sliderPick or 1, #candidates))
    local function selection() return candidates[menu.sliderPick] end
    local picker
    picker = nativeButton(content, "editor_slider_pick", selection() and selection().label or "NO EDITABLE SETTINGS", 465, 165, 316, function()
        if #candidates > 0 then menu.sliderPick = menu.sliderPick % #candidates + 1; picker.textLabel:setText(selection().label) end
    end)
    nativeButton(content, "editor_slider_add", "ADD SELECTED CONTROL", 793, 165, 153, function() if selection() then return request("add_slider", selection().name) end end)
    nativeButton(content, "editor_slider_remove", "REMOVE SELECTED", 956, 165, 153, function() if selection() then return request("remove_slider", selection().name) end end)
    local index, seen = 0, {}
    for _, name in ipairs(settings.custom) do
        if not byName[name] then
            for _, candidate in ipairs(candidates) do
                for _, channel in ipairs(candidate.channels or {}) do if channel == name then name=candidate.name; break end end
            end
        end
        if byName[name] and not seen[name] then seen[name]=true; index = index + 1; controlButton(content, byName[name], index, menu) end
    end
end)

function Editor.update(menu, data)
    local received = game:gettheatereditor(settings.revision)
    local changed = received.revision ~= settings.revision
    local previous = settings.values
    if received.session ~= settings.session then pending = {} end
    for name, edit in pairs(pending) do
        if (received.acks and received.acks[name] or 0) >= edit.ticket then pending[name] = nil
        else received.values[name] = edit.value end
    end
    for k, v in pairs(received) do settings[k] = v end
    if Editor.updateModels then Editor.updateModels(menu) end
    if changed then
        for _, category in ipairs(categories) do
            if menu.category == category[1] then menu:selectCategory(category[1], category[2]); break end
        end
    end
    local function equal(a,b)
        if type(a) ~= "table" then return a==b end
        return type(b)=="table" and a[1]==b[1] and a[2]==b[2] and a[3]==b[3]
    end
    for _, item in ipairs(menu.controls) do
        if not equal(previous[item.control.name], settings.values[item.control.name]) then item.element:processEvent({name = "content_refresh"}) end
    end
    if menu.colorPicker then menu.colorPicker.refresh() end
    menu.feedback:setText(settings.message ~= "" and settings.message or
        (menu.category=="models" and settings.modelMessage or settings.sceneMessage) or "")
    if (menu.category == "bone_camera" or menu.category == "lights") and menu.sceneStatus then
        menu.sceneStatus:setText(menu.category == "bone_camera" and (settings.boneAttached and "ATTACHED" or "DETACHED") or
            "POINT " .. tostring(settings.selectedLight or 0) .. " / " .. tostring(settings.lightCount or 0))
    end
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
