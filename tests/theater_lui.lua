-- Contract harness for the Lua panel. Actual MWR rendering/input is tested in game.
local Element = {}
Element.__index = Element
function Element.new(style) return setmetatable({style = style or {}, children = {}, childRecord = {}, handlers = {}, states = {}}, Element) end
function Element:addElement(e)
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
function Element:setText(value) self.text = value end
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
    UIBindButton = Element, Alignment = {Left = 0, Right = 1}, MenuBuilder = {}, FlowManager = {},
    MenuTemplate = {spMenuOffset = 80, ButtonStyle = {SubStyles = {Default = {}}}}, Options = {},
    UIGenericButton = {}, H1ButtonBackground = Element}
Element.EdgeW, Element.EdgeH, Element.EdgeU, Element.EdgeV, Element.ButtonHeight = 12, 12, .33, .33, 36
GenericButtonSettings = {Common = {text_default_color = {r = .6, g = .6, b = .6}}, Variants = {Slider = "slider", Select = "select"}}
luiglobals = {Colors = {mw1_green = {r = .2, g = .6, b = .25}, white = {r = 1, g = 1, b = 1}}}
function LUI.MenuBuilder.BuildRegisteredType(name, properties)
    assert(name == "UIGenericButton" and properties.style == LUI.MenuTemplate.ButtonStyle)
    local button = Element.new()
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
        if element.properties.button_action_func then element.properties.button_action_func(element, event) end
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
    local menu = Element.new()
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
local data = {playing = false, open = false, enabled = true, session = 1, paused = false, seeking = false,
    name = "test.dm_h1", elapsed = 0, duration = 10000, speed = 1, camera = 0, selected = -1,
    events = {}, keys = {}, revision = 0}
game = {}
function game:gettheaterstate() return data end
local editorData = {revision = 1, custom = {}, values = {demo_camera_speed = 500, cg_drawGun = 1}, controls = {
    {page = "camera", name = "demo_camera_speed", label = "FLY SPEED", kind = "float", min = 10, max = 20000, step = 50},
    {page = "weapon", name = "cg_drawGun", label = "VIEWMODEL", kind = "bool", min = 0, max = 1, step = 1}
}}
function game:gettheatereditor() return editorData end
function game:theatereditorset(name, value)
    assert(editorData.values[name] ~= nil, "unknown setting")
    editorData.values[name] = value; commands[#commands + 1] = {"setting", name, value}; return true
end
function game:theatereditoraction(name, value) commands[#commands + 1] = {"editor", name, value}; return true end
function game:theaterready(value) self.ready = value end
function game:theaterkeyboard(value) self.keyboard = value end
function game:theateraction(name, value)
    commands[#commands + 1] = {name, value}
    if name == "close" then data.open = false; data.editor = false end
    if name == "editor" then data.open = false; data.editor = true end
    if name == "timeline" then data.open = true; data.editor = false end
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
local hud = Element.new()
rootReady = true
CoD = {TextSettings = {FontBold110 = {Font = "MWR_NATIVE_FONT"}, TitleFontTiny = {Font = "MWR_NATIVE_TITLE_FONT"}},
    SFX = {MenuAccept = "native_accept", MenuBack = "native_back"}}
callbacks.mp_hud(hud)
assert(game.ready)
tick()
local passive = assert(find(root, "H1TheaterTimeline"))
assert(passive.style.alpha == 0)
data.playing = true; data.elapsed = 5000; data.paused = true
data.events = {{time = 2000, death = false}, {time = 2000, death = false}, {time = 7000, death = true}}
data.keys = {{time = 3000, index = 0}, {time = 8000, index = 1}}
tick()
assert(passive.style.alpha == 1)
assert(passive.time.text == "00:05 / 00:10")
assert(passive.fill.style.width == 228 and passive.head.style.left == 240)
assert(#passive.events.children == 2 and #passive.keys.children == 2)
data.open = true; tick(); tick()
assert(root.menu and passive.style.alpha == 0)
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
assert(click("theater_key_1")[1] == "seek" and commands[#commands][2] == 8000)
data.elapsed = 10000; tick()
assert(find(root.menu, "theater_playhead").style.left == 468)
assert(click("theater_editor")[1] == "editor")
tick(); tick()
assert(root.menu.id == "h1_theater_editor" and passive.style.alpha == 0)
assert(game.keyboard, "native editor keeps playback Space available")
assert(root.menu.nativeOptions.menu_width == 310)
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
fly.properties.button_right_func()
assert(editorData.values.demo_camera_speed == 699.9, "native slider must change its display synchronously for drag loops")
fly.properties.button_left_func()
assert(math.abs(editorData.values.demo_camera_speed - 500) < .001)
for _, label in ipairs({"MISC", "BONE CAMERA", "SSAO", "FOG", "DOF", "SUN", "LIGHTS", "STREAMS", "WEAPON", "SLIDERS"}) do
    find(root.menu, "native_" .. label):processEvent({name = "button_action"}); tick()
end
find(root.menu, "native_WEAPON"):processEvent({name = "button_action"}); tick()
click("setting_cg_drawGun"); assert(editorData.values.cg_drawGun == 0)
find(root.menu, "native_MAIN"):processEvent({name = "button_action"}); tick()
assert(find(root.menu, "theater_editor_status").text == "PAUSED   1x")
assert(click("editor_save_preset")[2] == "save_preset")
assert(click("editor_load_preset")[2] == "load_preset")
root.menu:processEvent({name = "button_secondary"}); tick(); tick()
assert(root.menu.id == "h1_theater_controls", "editor Back returns to the timeline")
root.menu.children[2]:processEvent({name = "button_secondary"}); tick()
assert(not root.menu and passive.style.alpha == 1)
assert(game.keyboard, "closing native controls must restore the playback shortcut")
root.menu = Element.new(); tick()
assert(not game.keyboard, "other game menus must retain keyboard ownership")
root.menu = nil; tick(); assert(game.keyboard)
data.open = true; tick(); tick(); click("theater_editor"); tick(); tick()
data.playing = false; tick(); assert(passive.style.alpha == 0 and not root.menu)
hud:processEvent({name = "menu_close"}); assert(passive.closed)
print("PASS Lua paused updates, event markers, transport wiring, native editor categories, route transitions, Back and playback cleanup")
