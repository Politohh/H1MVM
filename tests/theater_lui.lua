-- Contract harness for the Lua panel. Actual MWR rendering/input is tested in game.
local Element = {}
Element.__index = Element
function Element.new(style) return setmetatable({style = style or {}, children = {}, childRecord = {}, handlers = {}, states = {}}, Element) end
function Element:addElement(e)
    assert(not e.closed, "closed native elements cannot be reattached")
    if e.id then
        assert(not self.childRecord[e.id], "duplicate native child id: " .. e.id)
        self.childRecord[e.id] = e
    end
    table.insert(self.children, e); e.parent = self
end
function Element:registerAnimationState(name, style) self.states[name] = style end
function Element:animateToState(name, duration)
    assert(duration == 0, "paused theater controls must not depend on advancing LUI animations")
    for k, v in pairs(self.states[name]) do self.style[k] = v end
end
function Element:registerEventHandler(name, handler) self.handlers[name] = handler end
function Element:addEventHandler(name, handler)
    local previous = self.handlers[name]
    self.handlers[name] = function(...) if previous then previous(...) end; return handler(...) end
end
function Element:processEvent(event) if self.handlers[event.name] then return self.handlers[event.name](self, event) end end
-- The native bulk removal leaves Lua childRecord intact; closeChildren must be
-- used when rebuilding children with the same IDs.
function Element:removeAllChildren() self.children = {} end
function Element:closeChildren()
    while #self.children > 0 do self.children[1]:close() end
end
function Element:setHandleMouseButton(value) self.mouseButtons = value end
-- H1's MouseButtonEvent visits the parent first. A truthy leftmouseup
-- handler stops dispatch before children; reproduce that ordering here.
function Element:mouse(event)
    local l,t,r,b=self:getRect()
    local inside=event.x>=l and event.x<=r and event.y>=t and event.y<=b
    if self.mouseButtons then
        local normalized={root=event.root,x=event.x,y=event.y,inside=inside}
        if event.name=="mousedown" and inside then
            self.m_leftMouseDown=true; normalized.name="leftmousedown"; self:processEvent(normalized)
        elseif event.name=="mouseup" and self.m_leftMouseDown then
            self.m_leftMouseDown=nil; normalized.name="leftmouseup"
            if self:processEvent(normalized) then return true end
        elseif event.name=="mousedrag" and self.m_leftMouseDown then
            normalized.name="mousedrag"; self:processEvent(normalized)
        end
    end
    for _,child in ipairs(self.children) do if child:mouse(event) then return true end end
end
function Element:getRect()
    local x,y = self.style.left or 0,self.style.top or 0
    local parent=self.parent
    while parent do x=x+(parent.style.left or 0); y=y+(parent.style.top or 0); parent=parent.parent end
    return x,y,x+(self.style.width or 0),y+(self.style.height or 0)
end
function Element:pixelsToUnits(x,y) return x,y end
function Element:setText(value) self.text = value end
function Element:setTextStyle(value) self.textStyle = value end
function Element:makeNotFocusable() self.notFocusable = true end
function Element:close()
    self.closed = true
    if self.parent then
        if self.id then self.parent.childRecord[self.id] = nil end
        for i, child in ipairs(self.parent.children) do
            if child == self then table.remove(self.parent.children, i); break end
        end
        self.parent = nil
    end
end
function Element:AnyActiveMenusInStack() return self.menu ~= nil end
function Element:getChildById(id) return self.childRecord[id] end
function Element:setup9SliceImage(w, h, u, v) self.slice = {w, h, u, v} end
local root = Element.new()
local menus, callbacks, commands = {}, {}, {}
LUI = {UIElement = Element, UIImage = Element, UIText = Element, UIButton = Element,
    UIBindButton = Element, Alignment = {Left = 0, Right = 1, Center = 2}, MenuBuilder = {}, FlowManager = {},
    MenuTemplate = {spMenuOffset = 80, ButtonStyle = {SubStyles = {Default = {}}}}, Options = {},
    UIGenericButton = {}, H1ButtonBackground = {}}
LUI.H1ButtonBackground.EdgeW, LUI.H1ButtonBackground.EdgeH = 12, 12
function LUI.UIButton.LeftMouseDown(button) button:processEvent({name="button_over_down"}) end
function LUI.UIButton.LeftMouseUp(button,event)
    if event.inside and not button.disabled then
        local action={name="button_action",mouse=true}
        button:processEvent(action); event.handled=action.handled; return true
    end
end
LUI.H1ButtonBackground.EdgeU, LUI.H1ButtonBackground.EdgeV, LUI.H1ButtonBackground.ButtonHeight = .33, .33, 36
function LUI.H1ButtonBackground.new()
    local art = Element.new()
    local fill = Element.new(); fill.id = "fill"; art:addElement(fill)
    return art
end
GenericButtonSettings = {Common = {text_default_color = {r = .6, g = .6, b = .6}}, Variants = {Slider = "slider", Select = "select"}}
luiglobals = {Colors = {mw1_green = {r = .2, g = .6, b = .25}, white = {r = 1, g = 1, b = 1},
    h1 = {light_grey = {r = .9, g = .9, b = .9}}}}
function LUI.MenuBuilder.BuildRegisteredType(name, properties)
    assert(name == "UIGenericButton" and properties.style == LUI.MenuTemplate.ButtonStyle)
    local button = Element.new()
    button.mouseButtons=true
    button:registerEventHandler("leftmousedown",LUI.UIButton.LeftMouseDown)
    button:registerEventHandler("leftmouseup",LUI.UIButton.LeftMouseUp)
    button.properties = {} -- Native constructor does not install action properties.
    button.nativeProperties = properties
    button.textLabel = Element.new(); button.textLabel:setText(properties.button_text)
    button:addElement(button.textLabel)
    local inner = Element.new(); inner.id = "button"; button:addElement(inner)
    local art = Element.new(); art.id = "buttonContainer"; inner:addElement(art)
    local fill = Element.new(); fill.id = "fill"; art:addElement(fill)
    fill:registerEventHandler("button_over", function()
        local old = art:getChildById("focusedContainer")
        if old then old:close() end
        old = art:getChildById("selectionContour")
        if old then old:close() end
        local contour = Element.new(); contour.id = "selectionContour"; art:addElement(contour)
        local focused = Element.new(); focused.id = "focusedContainer"; art:addElement(focused)
        for _, id in ipairs({"stroke", "overlayGlow"}) do
            local image = Element.new(); image.id = id; focused:addElement(image)
            image:setup9SliceImage(12, 12, .33, .33)
        end
    end)
    button:registerEventHandler("button_action", function(element, event)
        local action=element.properties.button_action_func
        if not event.mouse and element.properties.buttonActionIsRightAction and element.properties.button_right_func then
            assert(action==nil,"native Select callback collision")
            action=element.properties.button_right_func
        end
        if action and not event.handled then action(element,event) end
    end)
    return button
end
function LUI.MenuBuilder.BuildAddChild(parent, definition)
    local button = LUI.MenuBuilder.BuildRegisteredType(definition.type, definition.properties)
    button.id = definition.id; button.properties = definition.properties
    parent:addElement(button)
    return button
end
function LUI.UIGenericButton.ButtonLabelFactory(_, label, _, _, width, font)
    assert(font.Font == "MWR_NATIVE_TITLE_FONT" and (font.Height * .8 == 10.5 or font.Height * .8 == 12))
    assert(width > 0)
    local element = Element.new({font = font.Font})
    for _, name in ipairs({"default", "focus", "disabled"}) do
        element:registerAnimationState(name, {height = font.Height * .8})
    end
    element:setText(label)
    element:animateToState("default", 0)
    return element
end
function LUI.MenuTemplate.new(_, options)
    -- Native MenuTemplate applies DesignGrid gutters to its full-window root.
    local menu = Element.new({left=30, top=30, width=1220, height=660})
    menu.nativeOptions = options
    menu.list = Element.new(); menu:addElement(menu.list)
    function menu:AddButton(label, callback, _, _, _, options)
        options.style = LUI.MenuTemplate.ButtonStyle
        options.button_text, options.button_action_func = label, callback
        return LUI.MenuBuilder.BuildAddChild(self.list,
            {type = "UIGenericButton", id = "native_" .. label, properties = options})
    end
    function menu:AddBackButton(callback)
        self:registerEventHandler("button_secondary", callback)
    end
    function menu:CreateBottomDivider() self.divider = true end
    return menu
end
function LUI.Options.InitScrollingList(list) list.scrolling = true end
function LUI.MenuBuilder.registerType(name, callback) menus[name] = callback end
function LUI.onmenuopen(name, callback) callbacks[name] = callback end
function LUI.FlowManager.RequestAddMenu(_, name)
    assert(not root.menu, "duplicate native menus")
    root.menu = menus[name]({type = name, properties = {exclusiveController = 0}})
end
function LUI.FlowManager.RequestLeaveMenu(menu)
    assert(menu == root.menu)
    root.menu = nil
    menu:processEvent({name = "menu_close"})
end
local rootReady = false
Engine = {GetLuiRoot = function() assert(rootReady, "in-game root accessed during VM startup"); return root end, InFrontend = function() return false end,
    PlaySound = function() end}
CoD = nil -- MWR populates fonts only after in-game VM startup.
RegisterMaterial = function(name) return name end
local data = {playing = false, open = false, enabled = true, visible = true, session = 1, paused = false, seeking = false,
    name = "test.dm_h1", elapsed = 0, duration = 10000, speed = 1, camera = 0, selected = -1,
    events = {}, keys = {}, revision = 0}
game = {}
function game:gettheaterstate()
    -- The real C++ bridge returns a new snapshot. Keep the prior poll's camera
    -- value stable when the simulated engine changes modes between updates.
    local snapshot = {}
    for key, value in pairs(data) do snapshot[key] = value end
    return snapshot
end
local editorData = {revision = 1, custom = {}, values = {demo_camera_speed = 500, cg_drawGun = 1}, controls = {
    {page = "camera", name = "demo_camera_speed", label = "FLY SPEED", kind = "float", min = 10, max = 20000, step = 50},
    {page = "weapon", name = "cg_drawGun", label = "VIEWMODEL", kind = "bool", min = 0, max = 1, step = 1}
}}
editorData.acks, editorData.session = {}, 1
local editTicket, delayed = 0, false
local function clone(value)
    if type(value)~="table" then return value end
    local result={}; for k,v in pairs(value) do result[k]=clone(v) end; return result
end
function game:gettheatereditor() return clone(editorData) end
function game:theatereditorset(name, value)
    assert(editorData.values[name] ~= nil, "unknown setting")
    editTicket=editTicket+1
    if not delayed then editorData.values[name] = value; editorData.acks[name]=editTicket end
    commands[#commands + 1] = {"setting", name, value}; return editTicket
end
function game:theatereditorcolor(name,r,g,b)
    assert(editorData.values[name]~=nil,"unknown color")
    editTicket=editTicket+1
    if not delayed then editorData.values[name]={r,g,b}; editorData.acks[name]=editTicket end
    commands[#commands+1]={"color",name,{r,g,b}}; return editTicket
end
function game:theatereditoraction(name, value) commands[#commands + 1] = {"editor", name, value}; return true end
function game:theaterready(value) self.ready = value end
function game:theaterkeyboard(value) self.keyboard = value end
function game:theateraction(name, value)
    commands[#commands + 1] = {name, value}
    if name == "close" then data.open = false; data.editor = false end
    if name == "editor" then data.open = false; data.editor = true end
    if name == "timeline" then data.open = true; data.editor = false end
    if name == "controls" then data.visible = not data.visible; data.open = data.visible and not data.editor end
    if name == "editortoggle" then data.editor = not data.editor; data.open = not data.editor and data.visible end
    return true
end
local function tick() root:processEvent({name = "h1_theater_update"}) end
local function find(element, id)
    if element.id == id then return element end
    for _, child in ipairs(element.children) do local found = find(child, id); if found then return found end end
end
local function click(id)
    local element = assert(find(root.menu, id), id)
    local count = #commands
    element:processEvent({name = "button_action"})
    assert(#commands == count + 1, "button must dispatch exactly one action: " .. id)
    return commands[#commands]
end
package.path = "data/cdata/ui_scripts/theater/?.lua;" .. package.path
assert(loadfile("data/cdata/ui_scripts/theater/timeline.lua"))()
assert(not game.ready, "the in-game root is not available at script startup")
local hud = Element.new({alpha = 1})
hud.id = "mp_hud"
root:addElement(hud)
hud.experienceBar = Element.new()
hud.experienceBar.widget = Element.new({alpha = 1})
hud.experienceBar.widget.id = "experienceBar_root"
hud.experienceBar:addElement(hud.experienceBar.widget)
hud:addElement(hud.experienceBar)
rootReady = true
CoD = {TextSettings = {FontBold110 = {Font = "MWR_NATIVE_FONT"}, TitleFontTiny = {Font = "MWR_NATIVE_TITLE_FONT"}},
    SFX = {MenuAccept = "native_accept", MenuBack = "native_back"}, TextStyle = {MW1Title = "MWR_TITLE_GLOW"}}
CoD.TextSettings.H1TitleFont = {Font="MWR_NATIVE_HEADING_FONT", Height=33}
callbacks.mp_hud(hud)
assert(game.ready)
tick()
local passive = assert(find(root, "H1TheaterTimeline"))
assert(passive.style.alpha == 0)
assert(hud.experienceBar.widget.style.alpha == 1, "normal matches retain the native XP widget")
data.playing = true; data.elapsed = 5000; data.paused = true
data.events = {{time = 2000, death = false}, {time = 2000, death = false}, {time = 7000, death = true}}
data.keys = {{time = 3000, index = 0}, {time = 8000, index = 1}}
tick()
assert(passive.style.alpha == 1)
assert(passive.style.bottom - passive.style.top == 82, "full transport layout stays visible without cursor capture")
assert(hud.experienceBar.widget.style.alpha == 0, "demo hides the XP track and both rank icons")
assert(passive.hudHint.text == "F1 Toggle Hud" and passive.controlsHint.text == "F4 Toggle Controls")
assert(passive.hudHint.style.top < 0 and passive.controlsHint.style.top < 0, "toggle hints sit above the whole box")
assert(passive.time.style.alignment == LUI.Alignment.Right)
assert(passive.flightHint.style.alpha == 0 and passive.cameraHint.style.alpha == 0)
for _, id in ipairs({"pause", "rewind", "forward", "speed", "camera", "editor", "close"}) do
    local transport = assert(find(passive, "theater_" .. id), "passive row must retain every button")
    assert(not transport.properties and not transport.handlers.button_action, "display-only art must not capture camera input")
    assert(transport:getChildById("fill").slice[2] == 0, "passive native fill must not acquire a texture split")
end
assert(not find(passive, "theater_seek_0"), "seek hitboxes exist only with controls enabled")
assert(passive.pause.label.text == "PLAY")
assert(passive.time.text == "00:05 / 00:10")
assert(passive.fill.style.width == 228 and passive.head.style.left == 240)
assert(#passive.events.children == 2 and #passive.keys.children == 2)
-- F4 hides the entire timeline and releases its menu, rather than refreshing it.
game:theateraction("controls", 0); tick(); tick()
assert(passive.style.alpha == 0 and not root.menu and game.keyboard)
game:theateraction("controls", 0); tick(); tick()
assert(root.menu and root.menu.panel.style.alpha == 1)
game:theateraction("close", 0); tick(); tick()
assert(passive.style.alpha == 1)
-- F3 can open and close the editor even when F4 has hidden the timeline.
game:theateraction("controls", 0); tick(); tick()
game:theateraction("editortoggle", 0); tick(); tick()
assert(root.menu and root.menu.id == "h1_theater_editor" and root.menu.panel.style.alpha == 0)
game:theateraction("editortoggle", 0); tick(); tick()
assert(not root.menu and passive.style.alpha == 0)
game:theateraction("controls", 0); tick(); tick()
game:theateraction("close", 0); tick(); tick()
-- Keep native widget states alive under a single hidden parent. Even stock
-- hud_on and popup animations cannot expose a child while its parent is hidden.
local hudWidgets = {}
for _, id in ipairs({"map_frame", "map_icons", "team_scores", "killstreaks", "weapon_frame", "grenades", "score_popup", "crosshair"}) do
    local widget = Element.new({alpha = 1}); widget.id = id; hud:addElement(widget)
    hudWidgets[#hudWidgets + 1] = widget
end
local function effectiveAlpha(element)
    local alpha = element.style.alpha or 1
    if element.parent then return alpha * effectiveAlpha(element.parent) end
    return alpha
end
data.hudHidden = true; tick()
assert(hud.style.alpha == 0 and passive.style.alpha == 1 and passive.parent == root)
for _, widget in ipairs(hudWidgets) do
    widget:registerAnimationState("native_hud_on", {alpha = 1})
    widget:animateToState("native_hud_on", 0)
    assert(effectiveAlpha(widget) == 0, "F1 must hide entire native HUD even after stock child animations")
end
assert(effectiveAlpha(passive.pause) == 1 and effectiveAlpha(passive.hudHint) == 1)
data.open = true; tick(); tick()
assert(root.menu and passive.style.alpha == 0)
assert(hud.style.alpha == 0 and root.menu.panel.style.alpha == 1, "HUD toggle must leave interactive theater controls visible")
data.hudHidden = false; tick()
assert(hud.style.alpha == 1 and hud.experienceBar.widget.style.alpha == 0)
for _, widget in ipairs(hudWidgets) do assert(effectiveAlpha(widget) == 1, "F1 must restore native HUD") end

assert(game.keyboard, "Space must remain available in native theater controls")
assert(find(root.menu, "theater_pause_label").text == "PLAY")
local pause = assert(find(root.menu, "theater_pause"))
local function checkBackground(button)
    local art = button:getChildById("button"):getChildById("buttonContainer")
    local function check(image)
        assert(image.slice[1] == 12 and image.slice[2] == 0 and image.slice[3] == .33 and image.slice[4] == 0,
            "compact native art must retain its corners and continuous vertical texture")
    end
    check(art:getChildById("fill"))
    for _ = 1, 2 do
        art:getChildById("fill"):processEvent({name = "button_over"})
        check(art:getChildById("selectionContour"))
        local focused = art:getChildById("focusedContainer")
        check(focused:getChildById("stroke")); check(focused:getChildById("overlayGlow"))
    end
end
checkBackground(pause)
assert(pause.nativeProperties.style == LUI.MenuTemplate.ButtonStyle)
assert(pause.properties.button_action_func)
pause.label:animateToState("focus", 0); assert(pause.label.style.height == 10.5)
pause.label:animateToState("default", 0); assert(pause.label.style.height == 10.5)
assert(passive.status.style.font == pause.label.style.font and passive.time.style.font == pause.label.style.font)
assert(click("theater_pause")[1] == "pause")
assert(click("theater_rewind")[2] == -5000)
assert(click("theater_forward")[2] == 5000)
assert(click("theater_seek_39")[2] == 4937)
assert(click("theater_speed")[2] == 2000)
assert(click("theater_camera")[2] == 1)
-- The camera button and F2 both publish the resulting mode through this
-- snapshot. Entering freecam must leave FlowManager's interactive menu.
data.camera = 2; tick()
assert(not root.menu and passive.style.alpha == 1,
    "entering freecam must release native controls and retain the passive bar")
assert(not data.open and game.keyboard, "freecam must receive gameplay input")
assert(passive.camera.label.text == "FREE CAMERA" and passive.pause.label.text == "PLAY")
assert(passive.flightHint.style.alpha == 1 and passive.cameraHint.style.alpha == 1)
assert(passive.cameraHint.text:find("Shift+Scroll Zoom", 1, true))
data.open = true; tick(); tick()
assert(root.menu and root.menu.id == "h1_theater_controls",
    "controls may be explicitly reopened while already in freecam")
data.camera = 1; tick()
data.paused = false; data.camera = 2; tick()
assert(not root.menu and game.keyboard, "running playback must also release controls for freecam")
data.paused = true; data.open = true; tick(); tick()
assert(click("theater_key_1")[1] == "seek" and commands[#commands][2] == 8000)
data.elapsed = 10000; tick()
assert(find(root.menu, "theater_playhead").style.left == 468)
assert(click("theater_editor")[1] == "editor")
tick(); tick()
assert(root.menu.id == "h1_theater_editor" and passive.style.alpha == 0)
assert(game.keyboard, "native editor keeps playback Space available")
assert(root.menu.nativeOptions.menu_width == 310)
local function checkEditorLayout(titleId)
    local title = assert(find(root.menu,titleId or "theater_editor_category"))
    assert(title.style.font==CoD.TextSettings.H1TitleFont.Font and
        title.style.height==CoD.TextSettings.H1TitleFont.Height and title.textStyle==CoD.TextStyle.MW1Title,
        "category and picker headings must use native Theater title styling")
    local _,titleTop,_,titleBottom=title:getRect()
    assert(titleTop>=30 and titleTop<80,"category heading stays beside the top native header")
    local _,helpTop=root.menu.help:getRect()
    assert(titleBottom+8<helpTop,"title glow/divider must stay clear of the description")
    -- getRect models top/left bounds; resolve this bottom-anchored timeline
    -- explicitly in MenuTemplate's 720-unit window with its native gutters.
    local panel=root.menu.panel
    local hintTop=root.menu.style.top+root.menu.style.height+panel.style.top+panel.hudHint.style.top
    for _,child in ipairs(root.menu.content.children) do
        local _,top,_,bottom=child:getRect()
        assert(top>=30 and bottom+12<hintTop,"editor content overlaps timeline hints: "..tostring(child.id))
    end
end
checkEditorLayout()
assert(not root.menu.list.scrolling and #root.menu.list.children == 12)
for _, button in ipairs(root.menu.list.children) do checkBackground(button) end
assert(root.menu.category == "main")
assert(find(root.menu, "theater_editor_demo").text == "test.dm_h1")
find(root.menu, "native_CAMERA"):processEvent({name = "button_action"})
assert(root.menu.category == "camera")
assert(find(root.menu, "theater_editor_category").text == "CAMERA")
assert(not find(root.menu, "theater_editor_demo"))
local fly = assert(find(root.menu, "setting_demo_camera_speed"))
checkBackground(fly)
fly:processEvent({name="button_right"})
assert(editorData.values.demo_camera_speed == 550, "native slider must change its display synchronously for drag loops")
fly:processEvent({name="button_left"})
assert(math.abs(editorData.values.demo_camera_speed - 500) < .001)
-- Stale snapshots and partial acknowledgements must not undo a drag or
-- recreate focused native controls. The newest request wins until acknowledged.
delayed=true
local function mouse(element,name,x,y)
    return element:mouse({name=name,x=x,y=y,root=root})
end
local function mouseClick(element,offset)
    local x,y=element:getRect()
    mouse(element,"mousedown",x+offset,y+14)
    return mouse(element,"mouseup",x+offset,y+14)
end
local function leftArrowClick() mouseClick(fly,212) end
local count=#commands; leftArrowClick()
assert(#commands==count+1 and commands[#commands][3]==450)
local firstTicket=editTicket
leftArrowClick()
local latestTicket=editTicket
local fill=find(root.menu,"setting_demo_camera_speed_fill")
local optimisticWidth=fill.style.width
assert(fill.style.left==2 and fill.style.leftAnchor and not fill.style.rightAnchor and fill.style.material=="white")
local thumb=find(root.menu,"setting_demo_camera_speed_thumb")
assert(math.abs(thumb.style.left-(optimisticWidth+1))<.001)
tick(); assert(fill.style.width==optimisticWidth and find(root.menu,"setting_demo_camera_speed")==fly)
editorData.values.demo_camera_speed=450; editorData.acks.demo_camera_speed=firstTicket
tick(); assert(fill.style.width==optimisticWidth,"an older acknowledgement must preserve the newer drag")
editorData.values.demo_camera_speed=400; editorData.acks.demo_camera_speed=latestTicket
tick(); assert(fill.style.width==optimisticWidth)
-- Pointer coordinates map directly to a bounded value; no asynchronous
-- native step loop, queue flood or cumulative rounding drift.
local track=find(root.menu,"setting_demo_camera_speed_track")
local tx,ty=track:getRect()
mouse(fly,"mousedown",tx+34,ty+4)
assert(commands[#commands][3]==10010)
mouse(fly,"mouseup",tx+34,ty+4)
local commandCount=#commands
mouse(fly,"mousedrag",tx+60,ty+4)
assert(#commands==commandCount,"released drag must stop editing")
mouse(fly,"mousedown",tx+34,ty+4)
mouse(fly,"mousedrag",tx-100,ty+4)
assert(commands[#commands][3]==10,"drag clamps to the minimum outside the track")
mouse(fly,"mousedrag",tx+200,ty+4)
assert(commands[#commands][3]==20000,"drag clamps to the maximum outside the row")
mouse(fly,"mouseup",tx+200,ty+4)
commandCount=#commands
mouse(fly,"mousedrag",tx+34,ty+4)
assert(#commands==commandCount,"release outside the row clears the drag")
local fx,fy=fly:getRect()
mouse(fly,"mousedown",fx+212,fy+14)
mouse(fly,"mouseup",fx+304,fy+14)
assert(#commands==commandCount,"release over the other arrow must not act")
fly.disabled=true; mouseClick(fly,212); fly.disabled=false
assert(#commands==commandCount,"disabled native rows reject arrows")
mouse(fly,"mousedown",tx+34,ty+4); mouse(fly,"mouseup",tx+34,ty+4)
editorData.values.demo_camera_speed=10010; editorData.acks.demo_camera_speed=editTicket
delayed=false; tick()

editorData.values.demo_sun_override=0
editorData.values.demo_sun_color={1,1,1}
editorData.controls[#editorData.controls+1]={page="sun",name="demo_sun_override",label="SUN LIGHT OVERRIDE",kind="bool",min=0,max=1,step=1}
editorData.controls[#editorData.controls+1]={page="sun",name="demo_sun_color",label="SUN COLOR",kind="color",min=0,max=1,channels={"demo_sun_r","demo_sun_g","demo_sun_b"}}
editorData.values.demo_sun_pitch=-45; editorData.values.demo_sun_timeofday=12
editorData.controls[#editorData.controls+1]={page="sun",name="demo_sun_pitch",label="SUN ELEVATION",kind="float",min=-90,max=90,step=1,inverted=true}
editorData.controls[#editorData.controls+1]={page="sun",name="demo_sun_timeofday",label="TIME OF DAY",kind="float",min=0,max=24,step=1/6}
editorData.revision=editorData.revision+1; tick()
find(root.menu,"native_SUN"):processEvent({name="button_action"}); tick()
local override=find(root.menu,"setting_demo_sun_override")
local elevation=find(root.menu,"setting_demo_sun_pitch")
mouseClick(elevation,304)
assert(editorData.values.demo_sun_pitch==-46,"positive elevation must raise the sun")
local clock=find(root.menu,"setting_demo_sun_timeofday")
mouseClick(clock,304)
assert(root.menu.help.text=="TIME OF DAY  /  12:10")
assert(find(root.menu,"theater_editor_category").style.alignment==LUI.Alignment.Center)
assert(click("editor_reset_all")[2]=="reset_all")
count=#commands; mouseClick(override,80)
assert(#commands==count+1 and editorData.values.demo_sun_override==1,"mouse label toggles ON exactly once")
count=#commands
mouseClick(override,304)
assert(#commands==count+1 and editorData.values.demo_sun_override==0,"handled child arrow must not also toggle its parent")
mouseClick(override,80)
assert(editorData.values.demo_sun_override==1)
mouseClick(override,212)
assert(editorData.values.demo_sun_override==0)
override:processEvent({name="button_action"})
assert(editorData.values.demo_sun_override==1,"keyboard select toggles ON without a native callback collision")
local editor=require("editor")
for _,rgb in ipairs({{1,0,0},{0,1,0},{0,0,1},{1,1,1},{0,0,0},{.2,.7,.4},{1.5,.3,.7}}) do
    local h,s,v=editor.rgbToHSV(rgb,123)
    local result=editor.hsvToRGB(h,s,v)
    for i=1,3 do assert(math.abs(result[i]-rgb[i])<1e-9) end
end
local retainedHue=editor.rgbToHSV({.5,.5,.5},123); assert(retainedHue==123)
find(root.menu,"setting_demo_sun_color"):processEvent({name="button_action"})
assert(root.menu.colorPicker and not find(root.menu,"setting_demo_sun_r"))
checkEditorLayout("color_title")
local colorCommand=click("color_preset_4")
assert(colorCommand[1]=="color" and colorCommand[2]=="demo_sun_color")
assert(colorCommand[3][1]==1 and colorCommand[3][2]==0 and colorCommand[3][3]==0)
delayed=true
click("color_cell_15_0")
local swatch=find(root.menu,"color_preview")
assert(swatch.style.color.r==0)
local previewState=swatch.states.color
assert(previewState.left==793 and previewState.top==310 and previewState.width==316 and previewState.height==44)
assert(previewState.leftAnchor and not previewState.rightAnchor and previewState.material=="white")
local marker=find(root.menu,"color_marker")
assert(marker.states.position.width==5 and marker.states.position.height==5)
assert(marker.style.left>=465 and marker.style.left<=684 and marker.style.top>=185 and marker.style.top<=404)
local cellArt=find(root.menu,"color_cell_15_0").children[1]
assert(cellArt.states.color.left==0 and cellArt.states.color.width==14 and cellArt.states.color.height==14)
tick(); assert(swatch.style.color.r==0,"stale RGB snapshot must not replace the chosen color")
editorData.values.demo_sun_color={0,0,0}; editorData.acks.demo_sun_color=editTicket
delayed=false; tick()
click("color_restore"); assert(editorData.values.demo_sun_color[1]==1 and editorData.values.demo_sun_color[2]==1)
find(root.menu,"color_back"):processEvent({name="button_action"}); assert(not root.menu.colorPicker and find(root.menu,"setting_demo_sun_color")); assert(marker.closed and not marker.parent and not find(root.menu,"color_marker"))

editorData.controls[#editorData.controls+1]={page="sun",name="demo_skybox",label="SKYBOX",kind="enum",min=0,max=23,step=1,choices={}}
local skyControl=editorData.controls[#editorData.controls]
for i=1,24 do skyControl.choices[i]=i==1 and "ORIGINAL MAP SKY" or "NATIVE SKY "..i end
editorData.values.demo_skybox=0;editorData.revision=editorData.revision+1;tick()
find(root.menu,"setting_demo_skybox"):processEvent({name="button_action"})
assert(root.menu.skyPicker and find(root.menu,"sky_option_20") and not find(root.menu,"sky_option_21"))
checkEditorLayout("sky_title")
local _,_,_,lastSkyBottom=find(root.menu,"sky_option_20"):getRect()
local _,navigationTop,_,navigationBottom=find(root.menu,"sky_next"):getRect()
local _,footerTop=find(root.menu,"sky_back"):getRect()
assert(lastSkyBottom+12<navigationTop and navigationBottom+12<footerTop,"sky grid, paging and footer must not overlap")
find(root.menu,"sky_next"):processEvent({name="button_action"});click("sky_option_24");assert(editorData.values.demo_skybox==23)
click("sky_original");assert(editorData.values.demo_skybox==0)
find(root.menu,"sky_back"):processEvent({name="button_action"});assert(not root.menu.skyPicker)

for _, label in ipairs({"MISC", "BONE CAMERA", "SSAO", "FOG", "DOF", "SUN", "LIGHTS", "STREAMS", "WEAPON", "SLIDERS"}) do
    find(root.menu, "native_" .. label):processEvent({name = "button_action"}); tick()
    checkEditorLayout()
end
find(root.menu, "native_WEAPON"):processEvent({name = "button_action"}); tick()
click("setting_cg_drawGun"); assert(editorData.values.cg_drawGun == 0)
find(root.menu, "native_MAIN"):processEvent({name = "button_action"}); tick()
assert(find(root.menu, "theater_editor_status").text == "PAUSED   1x")
assert(click("editor_save_preset")[2] == "save_preset")
assert(click("editor_load_preset")[2] == "load_preset")
assert(click("editor_reset_all")[2] == "reset_all")
-- Real pages can be much larger than the native viewport. Every item must
-- remain reachable and action buttons must not overlap the settings columns.
for _, page in ipairs({"fog", "lights", "dof"}) do
    for i=1,43 do
        local name = page .. "_fixture_" .. i
        editorData.controls[#editorData.controls+1] = {page=page,name=name,label=name,kind="float",min=0,max=1,step=.01}
        editorData.values[name] = .5
    end
end
editorData.revision = editorData.revision+1; tick()
for _, page in ipairs({"fog", "lights", "dof"}) do
    find(root.menu, "native_" .. string.upper(page)):processEvent({name="button_action"}); tick()
    local seen, pages = {}, page=="dof" and 3 or 5
    for _=1,pages do
        checkEditorLayout()
        for _, item in ipairs(root.menu.controls) do
            assert(not seen[item.control.name]); seen[item.control.name]=true
            assert(item.element.style.top+28<515)
            if page~="dof" then assert(item.element.style.left==465, "actions reserve the second column") end
        end
        find(root.menu,"editor_next_page"):processEvent({name="button_action"}); tick()
    end
    local count=0; for _ in pairs(seen) do count=count+1 end; assert(count==43)
    assert(root.menu.settingsPage==1, "pagination wraps and retains its category")
end
for _, spec in ipairs({{"BONE CAMERA","editor_bone_attach","bone_attach"}, {"BONE CAMERA","editor_bone_detach","bone_detach"},
    {"FOG","editor_fog_sample","fog_sample"},{"LIGHTS","editor_point_add","point_add"},
    {"LIGHTS","editor_point_next","point_next"},{"LIGHTS","editor_point_remove","point_remove"},
    {"LIGHTS","editor_point_clear","point_clear"},{"MISC","editor_toggle_hud","toggle_hud"}}) do
    find(root.menu,"native_"..spec[1]):processEvent({name="button_action"}); tick()
    assert(click(spec[2])[2]==spec[3])
end
root.menu:processEvent({name = "button_secondary"}); tick(); tick()
assert(root.menu.id == "h1_theater_controls", "editor Back returns to the timeline")
root.menu.children[2]:processEvent({name = "button_secondary"}); tick()
assert(not root.menu and passive.style.alpha == 1)
assert(game.keyboard, "closing native controls must restore the playback shortcut")
root.menu = Element.new(); tick()
assert(not game.keyboard, "other game menus must retain keyboard ownership")
root.menu = nil; tick(); assert(game.keyboard)
data.open = true; tick(); tick(); click("theater_editor"); tick(); tick()
data.hudHidden = true; tick(); assert(hud.style.alpha == 0)
data.playing = false; tick(); assert(hud.style.alpha == 1 and passive.style.alpha == 0 and not root.menu)
-- Stopping a demo restores only the XP widget, leaving normal HUD wrapper
-- visibility to MWR; replaying another session suppresses it again.
data.playing = false; tick()
assert(hud.experienceBar.widget.style.alpha == 1)
data.playing = true; data.session = data.session + 1; data.editor = false; data.open = false; tick()
assert(hud.experienceBar.widget.style.alpha == 0 and passive.style.alpha == 1)

data.playing = false; tick()
hud:processEvent({name = "menu_close"}); assert(passive.closed)
print("PASS parent-first mouse down/up arrows, disabled/cancelled clicks, bounded slider capture/release, left-anchored fill/thumb, elevation/clock display, Reset All, stale acknowledgements, HSV picker, HUD/timeline and editor lifecycle")

-- Frontend library selection must survive ordinary polls without rebuilding
-- its focused buttons; page changes must rebuild only the nine visible cards.
Engine.Localize=function(value) return value end
Engine.ToUpperCase=string.upper
luiglobals.GetTextDimensions=function(value,font,height) return 0,0,#value*height*.55 end
LUI.UIVerticalList={build=function(_,definition) return Element.new(definition.defaultState) end}
LUI.UIHorizontalList={new=Element.new}
LUI.UITimer={new=function(interval,event) return Element.new({interval=interval,event=event}) end}
local libraryEntries={}
for i=1,12 do
    libraryEntries[i]={id="demo_"..i,name="recording_"..i..".dm_h1",map="mp_crash",mapTitle="Crash",
        modeTitle="Team Deathmatch",date="06/10/2026  21:00",duration=61000,material="preview_mp_crash",source=i%2==0,error=""}
end
libraryEntries[2].name="a_very_long_recording_title_that_should_fit_the_card.dm_1"
local libraryRevision=1
local libraryActions={}
function game:gettheaterlibrary(page,filter)
    local visible={}
    for _,entry in ipairs(libraryEntries) do
        if filter=="all" or (filter=="ready" and not entry.source) or (filter=="source" and entry.source) then visible[#visible+1]=entry end
    end
    local pages=math.max(1,math.ceil(#visible/9)); page=math.max(1,math.min(page,pages))
    local entries={}; for i=(page-1)*9+1,math.min(page*9,#visible) do entries[#entries+1]=visible[i] end
    return {revision=libraryRevision,busy=false,converting=false,message="Library ready",entries=entries,page=page,pages=pages,total=#visible}
end
function game:theaterlibraryaction(name,id) libraryActions[#libraryActions+1]={name,id}; return true end
dofile("data/cdata/ui_scripts/theater/library.lua")
root.menu=menus.h1_theater_library({type="h1_theater_library"})
local library=root.menu
assert(#library.cards==9 and library.selected=="demo_1" and library.snapshot.pages==2)
assert(find(library.cards[1].element,"card_title").text=="recording_1","card heading uses the demo title without its extension")
local longTitle=find(library.cards[2].element,"card_title")
assert(longTitle.text:sub(-3)=="..." and #longTitle.text*longTitle.style.height*.55<=210,"long titles fit without shrinking below a readable size")
find(library,"library_convert"):processEvent({name="button_action"})
assert(libraryActions[#libraryActions][1]=="convertfile","conversion picker stays visible for H1 selections")
local fifth=library.cards[5].element
local previousGrid=library.grid
fifth:processEvent({name="button_over"})
assert(library.selected=="demo_5" and find(library,"preview_filename").text=="recording_5.dm_h1")
library:refresh()
assert(library.cards[5].element==fifth and library.selected=="demo_5","poll must not replace focus/selection")
libraryRevision=2; library:refresh()
assert(previousGrid.closed and library.selected=="demo_5","metadata refresh preserves selected recording")
find(library,"library_next"):processEvent({name="button_action"})
assert(#library.cards==3 and library.page==2 and library.selected=="demo_10")
find(library,"library_cod4"):processEvent({name="button_action"})
assert(library.page==1 and #library.cards==6 and library.cards[1].item.source)
find(library,"library_play"):processEvent({name="button_action"})
assert(libraryActions[#libraryActions][1]=="convert")
find(library,"library_h1"):processEvent({name="button_action"})
assert(#library.cards==6 and not library.cards[1].item.source)
find(library,"library_play"):processEvent({name="button_action"})
assert(libraryActions[#libraryActions][1]=="play" and root.menu==nil)
print("PASS nine-card native Theater library, stable focus/selection, paging, H1/COD4 filters and correct Convert/Play routing")
