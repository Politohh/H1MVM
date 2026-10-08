-- Contract harness for the Lua panel. Actual MWR rendering/input is tested in game.
unpack = unpack or table.unpack -- MWR HKS uses the Lua 5.1 global.
local Element = {}
Element.__index = Element
local elementCount=0
function Element.new(style)
    elementCount=elementCount+1
    return setmetatable({style = style or {}, children = {}, childRecord = {}, handlers = {}, states = {}}, Element)
end
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
local activeModelAction
function Element:processEvent(event)
    local previous=activeModelAction
    if event.name=="button_action" and self.id and (self.id:sub(1,6)=="model_" or self.id:sub(1,7)=="weapon_") then activeModelAction=self end
    local result
    if self.handlers[event.name] then result=self.handlers[event.name](self,event) end
    activeModelAction=previous
    return result
end
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
function Element:setUseStencil(value) self.stencil = value end
function Element:makeNotFocusable() self.notFocusable = true end
function Element:close()
    local active=activeModelAction
    while active do
        assert(active~=self,"Model Swapper must not destroy its active input subtree")
        active=active.parent
    end
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
function Element:getFirstChild() return self.children[1] end
function Element:getNextSibling()
    if self.parent then
        for i,child in ipairs(self.parent.children) do if child==self then return self.parent.children[i+1] end end
    end
end
function Element:getFirstDescendentById(id)
    for _,child in ipairs(self.children) do
        if child.id==id then return child end
        local found=child:getFirstDescendentById(id); if found then return found end
    end
end
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
luiglobals.GetTextDimensions=function(value,font,height) return 0,0,#value*height*.55 end
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
function LUI.UIGenericButton.ButtonLabelFactory(settings, label, _, _, width, font)
    assert(font.Font == "MWR_NATIVE_TITLE_FONT" and (font.Height * .8 == 10.5 or font.Height * .8 == 12))
    assert(width > 0)
    local element = Element.new({font = font.Font})
    for _, name in ipairs({"default", "focus", "disabled", "over_disabled", "over_disabled_pulse"}) do
        element:registerAnimationState(name, {height = font.Height * .8,
            leftAnchor=true,rightAnchor=true,topAnchor=false,bottomAnchor=false,
            left=name=="focus" and settings.paddingFocus or settings.padding,
            top=settings.yOffset-font.Height*.4})
    end
    for event,state in pairs({button_over="focus",button_up="default",button_disable="disabled",
        button_over_disable="over_disabled",visual_focused_lowlighted="focus",visual_lowlighted="disabled",
        visual_unlighted="default"}) do
        element:registerEventHandler(event,function(self) self:animateToState(state,0) end)
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
        options=options or {}
        options.style = LUI.MenuTemplate.ButtonStyle
        options.button_text, options.button_action_func = label, callback
        local button=LUI.MenuBuilder.BuildAddChild(self.list,
            {type = "UIGenericButton", id = "native_" .. label, properties = options})
        button.menu=self; return button
    end
    function menu:AddBackButton(callback)
        self:registerEventHandler("button_secondary", callback)
    end
    function menu:CreateBottomDivider() self.divider = true end
    function menu:AddBottomDividerToList(button) self.lastDivider=button end
    return menu
end
function LUI.Options.InitScrollingList(list) list.scrolling = true end
function LUI.MenuBuilder.registerType(name, callback)
    assert(menus[name]==nil,"native registerType rejects duplicate builder: "..name)
    menus[name] = callback
end
function LUI.onmenuopen(name, callback) callbacks[name] = callback end
function LUI.overrideMenuBuilder(name, callback)
    local native=assert(menus[name],name)
    menus[name]=function(...) return callback(native,...) end
end
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
local missingMaterials={}
RegisterMaterial = function(name)
    assert(not missingMaterials[name],"unavailable portraits must not register the checkerboard: "..name)
    return name
end
local data = {playing = false, open = false, enabled = true, visible = true, session = 1, paused = false, seeking = false,
    name = "test.dm_h1", elapsed = 0, duration = 10000, speed = 1, camera = 0, selected = -1,
    events = {}, keys = {}, revision = 0}
game = {}
function game:isdefaultmaterial(name) return missingMaterials[name] or false end
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
function game:gettheatereditor()
    local data=clone(editorData);data.modelCatalog=nil;return data
end
function game:gettheatermodelcatalog(kind,faction,role,collection,page)
    local matches,collections,seen={},{"All collections"},{["All collections"]=true}
    for _,c in ipairs(editorData.modelCatalog or {}) do
        if c.kind==kind then
            if not seen[c.collection] then collections[#collections+1]=c.collection;seen[c.collection]=true end
            if (faction=="" or c.faction==faction) and (role=="" or c.role==role) and
                (collection=="All collections" or c.collection==collection) then matches[#matches+1]=c end
        end
    end
    local pages=math.max(1,math.ceil(#matches/4));page=math.min(math.max(1,page),pages)
    local items={};for i=(page-1)*4+1,math.min(page*4,#matches) do items[#items+1]=matches[i] end
    return {items=items,collections=collections,total=#matches,pages=pages,page=page}
end
function game:theatereditorset(name, value)
    assert(editorData.values[name] ~= nil, "unknown setting")
    editTicket=editTicket+1
    if not delayed then editorData.values[name] = value; editorData.acks[name]=editTicket end
    commands[#commands + 1] = {"setting", name, value}; return editTicket
end
function game:gettheaterweaponcatalog(source,weapon,page)
    local guns={{weapon="h1_remington700_mp",label="R700"},{weapon="h1_junsnp_mp",label="S-Tac Aggressor"}}
    if weapon=="" then weapon="h1_remington700_mp" end
    local all={}
    for i=1,6 do all[i]={id=weapon.."|kit"..i,weapon=weapon,label=weapon==guns[1].weapon and "R700" or "S-Tac Aggressor",
        kitLabel=i==1 and "No weapon kit" or "Neanderthal "..i,icon=i==1 and "" or "weapon_kit_icon"} end
    page=math.max(1,math.min(2,page));local items={}
    for i=(page-1)*4+1,math.min(page*4,#all) do items[#items+1]=all[i] end
    return {items=items,weapons=guns,total=#all,page=page,pages=2,category="Sniper rifles"}
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
Engine.GetLuiRoot() -- Recover a HUD constructed before extension callbacks.
assert(game.ready and find(root,"H1TheaterTimeline"),"late attachment needs no lui_restart")
Engine.GetLuiRoot()
callbacks.mp_hud(hud) -- Repeated root access/menu notification is idempotent.
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
data.visible = false; tick()
assert(hud.style.alpha == 0 and passive.style.alpha == 0, "F4 must hide controls independently while F1 hides the HUD")
data.visible = true; tick()
assert(hud.style.alpha == 0 and effectiveAlpha(passive.pause) == 1,
    "F4 must restore controls without restoring the game HUD")
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
assert(not root.menu.list.scrolling and #root.menu.list.children == 14)
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
-- Character browser contracts: stable player identities, catalog paging,
-- native portraits/default cards, filters and apply/load guards.
editorData.modelPlayers={{key="actor:100",name="Alice",slot=18,visible=true},{key="actor:200",name="Bob",slot=19,visible=false}}
editorData.modelSelected="actor:100"
editorData.modelDetail={name="Alice",head="head_original",body="body_original"}
editorData.modelCatalogRevision=1;editorData.modelLoading=false
editorData.modelCatalog={}
for i=1,15 do
    editorData.modelCatalog[i]={id=i,kind="head",model="head_sas_ct_test_"..i,label="SAS head "..i,
        icon=i==1 and "" or "h1_character_icon_010",faction="SAS / Urban",role="assault",collection="Default"}
end
editorData.modelCatalog[16]={id=100,kind="body",model="body_sas_ct_assault_mp_camo",label="SAS Urban assault",icon="",faction="SAS / Urban",role="assault",collection="Default"}
tick();root.menu:selectCategory("models","MODEL SWAPPER");tick()
checkEditorLayout()
assert(find(root.menu,"model_default_card") and find(root.menu,"model_choice_2"):getChildById("model_thumbnail"))
assert(#root.menu.list.children==0, "the full-width browser hides the editor category list")
assert(click("model_player_2")[2]=="model_select" and commands[#commands][3]=="actor:200")
assert(click("model_apply")[2]=="model_head" and commands[#commands][3]=="1")
find(root.menu,"model_next"):processEvent({name="button_action"});tick();assert(root.menu.modelPage==2 and find(root.menu,"model_choice_1").modelChoice==5)
mouseClick(find(root.menu,"model_choice_2"),90);tick()
assert(click("model_apply")[3]=="6","reused card callback selects the new page's model")
find(root.menu,"model_next"):processEvent({name="button_action"});tick();assert(root.menu.modelPage==3 and find(root.menu,"model_choice_1").modelChoice==9)
find(root.menu,"model_next"):processEvent({name="button_action"});tick();assert(root.menu.modelPage==4 and find(root.menu,"model_choice_3").modelChoice==15)
find(root.menu,"model_next"):processEvent({name="button_action"});tick();assert(root.menu.modelPage==1)
find(root.menu,"model_bodies"):processEvent({name="button_action"});tick();assert(find(root.menu,"model_choice_1").modelChoice==100 and find(root.menu,"model_choice_2").modelHidden)
assert(click("model_apply")[2]=="model_body" and commands[#commands][3]=="100")
editorData.modelLoading=true;tick()
local beforeLoad=#commands;mouseClick(find(root.menu,"model_apply"),70);assert(#commands==beforeLoad,"loading must block repeated model apply")
mouseClick(find(root.menu,"model_save"),70);mouseClick(find(root.menu,"model_load"),70)
assert(#commands==beforeLoad,"loading must block save/load of incomplete selections")
editorData.modelLoading=false;tick()
assert(click("model_focus")[2]=="model_focus")
assert(click("model_restore")[2]=="model_restore")
assert(click("model_save")[2]=="model_save" and click("model_load")[2]=="model_load")
find(root.menu,"model_faction"):processEvent({name="button_action"});tick();assert(find(root.menu,"model_no_matches") and find(root.menu,"model_choice_1").modelHidden,"faction filter hides nonmatching cards")
assert(find(root.menu,"model_count").text:find("0 APPEARANCES"))
find(root.menu,"model_clear_filters"):processEvent({name="button_action"});tick();assert(find(root.menu,"model_choice_1"))
-- Real mouse callbacks keep their button alive through release, then redraw.
for _,id in ipairs({"model_faction_next","model_faction_previous","model_class","model_collection",
    "model_heads","model_bodies","model_next","model_previous","model_players_next","model_players_previous","model_clear_filters"}) do
    local old=assert(find(root.menu,id),id)
    mouseClick(old,15)
    assert(not old.closed and root.menu.pendingCategory,"click queues refresh until release finishes")
    tick()
    assert(not old.closed and find(root.menu,id)==old and not root.menu.pendingCategory,"next poll refreshes the existing native button")
end
find(root.menu,"model_bodies"):processEvent({name="button_action"});tick()
find(root.menu,"model_clear_filters"):processEvent({name="button_action"});tick()
local selectedCard=find(root.menu,"model_choice_1")
mouseClick(selectedCard,90);assert(not selectedCard.closed);tick();assert(not selectedCard.closed and find(root.menu,"model_choice_1")==selectedCard)
local stableCard=find(root.menu,"model_choice_1")
tick();tick();assert(find(root.menu,"model_choice_1")==stableCard,"ordinary replay polls preserve browser focus")
local stableWidgets=elementCount
for i=1,200 do
    find(root.menu,"model_faction_next"):processEvent({name="button_action"});tick()
end
assert(elementCount<=stableWidgets+8,"repeated filters must reuse native widgets instead of exhausting the UI pool")
find(root.menu,"model_clear_filters"):processEvent({name="button_action"});tick()
assert(find(root.menu,"model_choice_1").style.height==42,"appearance cards reserve space for readable metadata")
assert(find(root.menu,"model_choice_1").textLabel.style.alpha~=0,"native card labels remain visible during reuse")
local function checkModelCard(row)
    local thumbnail=find(row,"model_thumbnail")
    local copy=assert(find(row,"model_copy"),"card text needs its own bounded column")
    local title=assert(find(copy,"model_name"))
    local meta=assert(find(copy,"model_variant"))
    assert(copy.stencil and row.textLabel.text=="","native label must not paint under the thumbnail")
    local original=title.text
    for _,event in ipairs({"button_over","button_up","button_disable","button_over_disable",
        "visual_focused_lowlighted","visual_lowlighted","visual_unlighted"}) do
        row.textLabel:processEvent({name=event})
        assert(title.text==original and title.style.left==2,"native states must not move card copy")
        for _,resolution in ipairs({{1024,768},{1280,720},{1920,1080},{2560,1440},{3440,1440},{3840,2160}}) do
            local scale=math.min(resolution[1]/1280,resolution[2]/720)
            assert(copy.style.left*scale>(thumbnail.style.left+thumbnail.style.width)*scale)
            assert((copy.style.left+copy.style.width)*scale<row.style.width*scale)
            assert((title.style.top+title.style.height)*scale<meta.style.top*scale)
            assert((meta.style.top+meta.style.height)*scale<copy.style.height*scale)
        end
    end
    for _,line in ipairs({title,meta}) do
        local _,_,width=luiglobals.GetTextDimensions(line.text,line.style.font,line.style.height)
        assert(width<=line.style.width+.001,"card text must fit after reuse")
        assert(line.style.left>0 and line.style.left+line.style.width<copy.style.width,
            "text needs a small inset from the clipping edges")
    end
end
checkModelCard(find(root.menu,"model_choice_1"))
find(root.menu,"model_heads"):processEvent({name="button_action"});tick()
checkModelCard(find(root.menu,"model_choice_1")) -- HEAD placeholder
checkModelCard(find(root.menu,"model_choice_2")) -- native portrait
local reused=find(root.menu,"model_choice_1")
local reusedCopy=find(reused,"model_copy")
local oldLabel=editorData.modelCatalog[5].label
editorData.modelCatalog[5].label="SAS "..string.rep("Woodland ",24).."assault"
find(root.menu,"model_next"):processEvent({name="button_action"});tick()
assert(find(root.menu,"model_choice_1")==reused and find(reused,"model_copy")==reusedCopy)
checkModelCard(reused)
assert(find(reused,"model_name").text:sub(1,5)=="> SAS" and find(reused,"model_name").text:sub(-3)=="...")
editorData.modelCatalog[5].label=oldLabel
find(root.menu,"model_previous"):processEvent({name="button_action"});tick()
checkModelCard(reused)
assert(find(reused,"model_name").text=="> SAS HEAD 1" and find(reused,"model_name").style.height==15,
    "reused short names regain normal size and complete text")
-- A valid catalog icon is not necessarily resident in the replay. Exercise
-- valid -> missing -> valid -> missing without replacing the card or preview.
local portraitCard=find(root.menu,"model_choice_2")
mouseClick(portraitCard,90);tick()
local preview=assert(find(root.menu,"model_portrait"))
assert(not preview.modelHidden and preview.editorImageLayout.material=="h1_character_icon_010")
local portraitWidgets=elementCount
for _,missing in ipairs({true,false,true,false}) do
    missingMaterials.h1_character_icon_010=missing
    root.menu:requestCategory("models","MODEL SWAPPER");tick()
    assert(find(root.menu,"model_choice_2")==portraitCard and find(root.menu,"model_portrait")==preview)
    assert(find(portraitCard,"model_thumbnail").editorImageLayout.material==
        (missing and "white" or "h1_character_icon_010"))
    assert(find(portraitCard,"model_placeholder_text").text==(missing and "HEAD" or ""))
    assert((preview.modelHidden or false)==missing)
    assert((find(root.menu,"model_default_card").modelHidden or false)==not missing)
    if missing then
        assert(find(root.menu,"model_default_hint").text=="HEAD / PORTRAIT UNAVAILABLE")
        assert(click("model_apply")[3]=="2","missing portrait must not hide or disable a usable model")
    end
end
assert(elementCount==portraitWidgets,"portrait availability changes must reuse the native widget tree")
editorData.modelCatalog[16].icon="missing_body_portrait"
missingMaterials.missing_body_portrait=true
find(root.menu,"model_bodies"):processEvent({name="button_action"});tick()
assert(find(find(root.menu,"model_choice_1"),"model_placeholder_text").text=="BODY")
assert(find(root.menu,"model_default_hint").text=="BODY / PORTRAIT UNAVAILABLE")
assert(click("model_apply")[3]=="100")
editorData.modelCatalog[16].icon=""
checkEditorLayout()
print("PASS detailed character browser: player identity, portraits/default cards, head/body apply, paging, faction filter, load guard, save/restore and layout")
root.menu:processEvent({name = "button_secondary"}); tick(); tick()
assert(root.menu.category=="main" and #root.menu.list.children==14, "browser Back restores the category hub")
editorData.weaponSwapSource="h1_remington700_mp_a#none_f#base"
editorData.weaponSwapLabel="R700";editorData.weaponSwapLoading=false;editorData.weaponSwapCount=0
tick();root.menu:requestCategory("weapons","WEAPON SWAPPER");tick()
assert(root.menu.category=="weapons" and #root.menu.list.children==0)
local weaponCard=find(root.menu,"weapon_card_1")
assert(weaponCard.textLabel.text=="" and find(weaponCard,"weapon_text_1").stencil)
click("weapon_apply")
assert(commands[#commands][2]=="weapon_swap_apply" and commands[#commands][3]==editorData.weaponSwapSource.."|h1_remington700_mp|kit1")
find(root.menu,"weapon_next"):processEvent({name="button_action"});tick()
assert(find(root.menu,"weapon_card_1")==weaponCard and find(root.menu,"weapon_card_3").style.alpha==0)
find(root.menu,"weapon_next_gun"):processEvent({name="button_action"});tick()
assert(find(root.menu,"weapon_filter").text=="S-Tac Aggressor")
local allocatedWeapons=elementCount
for i=1,80 do find(root.menu,"weapon_next"):processEvent({name="button_action"});tick();find(root.menu,"weapon_previous"):processEvent({name="button_action"});tick() end
assert(elementCount==allocatedWeapons,"weapon paging must reuse all native widgets")
editorData.weaponSwapLoading=true;tick()
local beforeApply=#commands;find(root.menu,"weapon_apply"):processEvent({name="button_action"});assert(#commands==beforeApply,"no competing apply while a fastfile loads")
editorData.weaponSwapLoading=false;tick();click("weapon_save")
assert(commands[#commands][2]=="weapon_swap_save")
click("weapon_restore");assert(commands[#commands][2]=="weapon_swap_restore")
find(root.menu,"weapon_back"):processEvent({name="button_action"});assert(root.menu.category=="weapons","Back must defer destroying the focused button")
tick();assert(root.menu.category=="main" and #root.menu.list.children==14)
print("PASS Weapon Swapper catalog paging, kit selection, same-category gun selection, bounded widget reuse, loading guard and deferred Back")
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
        modeTitle="Team Deathmatch",date="06/10/2026  21:00",duration=61000,material="preview_mp_crash",source=i%2==0,error="",splittable=true}
end
libraryEntries[2].name="a_very_long_recording_title_that_should_fit_the_card.dm_1"
local libraryRevision=1
local libraryActions={}
local libraryClock=1000
local libraryPlaying=false
local libraryBusy=false
local librarySplitCalls={}
function game:theaterclock() return libraryClock end
function game:gettheaterlibrary(page,filter)
    local visible={}
    for _,entry in ipairs(libraryEntries) do
        if filter=="all" or (filter=="ready" and not entry.source) or (filter=="source" and entry.source) then visible[#visible+1]=entry end
    end
    local pages=math.max(1,math.ceil(#visible/9)); page=math.max(1,math.min(page,pages))
    local entries={}; for i=(page-1)*9+1,math.min(page*9,#visible) do entries[#entries+1]=visible[i] end
    return {revision=libraryRevision,busy=libraryBusy,splitProgress=25,converting=false,playing=libraryPlaying,message="Library ready",entries=entries,page=page,pages=pages,total=#visible}
end
function game:theaterlibraryaction(name,id) libraryActions[#libraryActions+1]={name,id}; return true end
function game:theaterlibrarysplit(id,start,finish)
    librarySplitCalls[#librarySplitCalls+1]={id,start,finish}; libraryBusy=true; return true
end
dofile("data/cdata/ui_scripts/theater/library.lua")
root.menu=menus.h1_theater_library({type="h1_theater_library"})
local library=root.menu
assert(#library.cards==9 and library.selected=="demo_1" and library.snapshot.pages==2)
local sourceCard=library.cards[2].element
sourceCard:processEvent({name="button_action",mouse=true})
assert(libraryActions[#libraryActions][1]=="refresh","single click only selects")
libraryClock=1500; sourceCard:processEvent({name="button_action",mouse=true})
assert(libraryActions[#libraryActions][1]=="refresh","slow clicks are separate selections")
libraryClock=1700; sourceCard:processEvent({name="button_action",mouse=true})
assert(libraryActions[#libraryActions][1]=="convert","double-click activates the selected source")
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
assert(libraryActions[#libraryActions][1]=="play" and root.menu==library,"queued Play must retain Theater until playback starts")
library:refresh()
assert(root.menu==library,"a deferred or failed Play request must not drop the user into the online menu")
libraryPlaying=true; library:refresh()
assert(root.menu==nil,"confirmed playback closes Theater")
libraryPlaying=false
print("PASS nine-card native Theater library, stable focus/selection, paging, H1/COD4 filters and correct Convert/Play routing")

for _,format in ipairs({"ready","source"}) do
    root.menu=menus.h1_theater_library({})
    local owner=root.menu
    owner.filter=format; owner.redraw=true; owner:refresh()
    local selected=owner.selected
    find(owner,"library_split"):processEvent({name="button_action"})
    local split=root.menu
    assert(split.id=="h1_theater_split" and split.start==0 and split.finish==61)
    assert(find(split,"split_format").text==(format=="source" and "COD4 → H1 CLIP" or "H1 CLIP"))
    find(split,"split_start"):processEvent({name="button_right"})
    find(split,"split_finish"):processEvent({name="button_left"})
    assert(split.start==1 and split.finish==60)
    find(split,"split_save"):processEvent({name="button_action"})
    local request=librarySplitCalls[#librarySplitCalls]
    assert(request[1]==selected and request[2]==1000 and request[3]==60000,"split sends precise local milliseconds and stable selection")
    local requests=#librarySplitCalls
    find(split,"split_save"):processEvent({name="button_action"})
    find(split,"split_start"):processEvent({name="button_right"})
    assert(#librarySplitCalls==requests and split.start==1,"pending split prevents duplicate saves/range edits")
    split:processEvent({name="split_poll"})
    assert(split.help.text=="Saving clip... 25%")
    libraryBusy=false; split:processEvent({name="split_poll"})
    assert(not split.submitted and split.help.text=="Library ready")
    split:processEvent({name="button_secondary"})
    assert(root.menu.id=="h1_theater_library" and root.menu.selected==selected and root.menu.filter==format,
        "Back returns to the selected recording/filter without stacking duplicate menus")
end
libraryEntries[1].splittable=false
root.menu=menus.h1_theater_library({})
assert(not find(root.menu,"library_split"),"unsupported native H1 recordings must not offer independent-snapshot cuts")
libraryEntries[1].splittable=true
print("PASS Theater splitter: both inputs, range adjustments, millisecond requests, duplicate/busy protection, progress and unsupported-format gating")

root.menu=menus.h1_theater_library({})
local playbackCard=root.menu.cards[1].element
libraryClock=2000; playbackCard:processEvent({name="button_action",mouse=true})
libraryClock=2200; playbackCard:processEvent({name="button_action",mouse=true})
assert(libraryActions[#libraryActions][1]=="play" and root.menu,"double-click queues H1 recording without leaving before launch")
libraryPlaying=true; root.menu:refresh(); libraryPlaying=false
assert(root.menu==nil,"double-click leaves Theater once playback starts")

local convertedHud=true
Engine.GetDvarBool=function() return convertedHud end
local recordedScores={[1]=3,[2]=14}
function game:getcod4teamscore(team) return recordedScores[team] end
Game={GetTeamScore=function() return 99 end,GetPlayerTeam=function() return 2 end,GetPlayerstateClientnum=function() return 0 end}
local nativeBinds=0
function Element:setupUIBindText() nativeBinds=nativeBinds+1 end
LUI.MenuBuilder.m_types_build=menus
menus.pointsPopup=function()
    local popup=Element.new({left=20})
    local points=Element.new({alignment=LUI.Alignment.Left}); points.id="points"; popup:addElement(points)
    local flare=Element.new({left=-25}); flare.id="flareAnimation"; popup:addElement(flare)
    return popup
end
menus.teamScoresHudDef=function()
    local widget=Element.new()
    widget:setupUIBindText("TeamScoreFriendly"); widget:setupUIBindText("TeamScoreEnemy")
    for _,id in ipairs({"friendlyScore_id","enemyScore_id","scoreWatchFriendly","scoreWatchEnemy"}) do
        local child=Element.new(); child.id=id; widget:addElement(child)
        child:registerEventHandler("int_watch_alert",function(_,event) child.newValue=event.newValue end)
    end
    return widget
end
dofile("data/cdata/ui_scripts/theater/match_hud.lua")
assert(Game.GetTeamScore(1)==3 and Game.GetTeamScore(2)==14)
-- Fresh HUD construction queries free/spectator teams and may precede the
-- recorded scores. An unavailable bridge result must not reach native < / >.
local savedRecordedScore=game.getcod4teamscore
game.getcod4teamscore=function(_,team)
    if team==0 or team==3 then return {} end -- opaque bridge sentinel regression
    return savedRecordedScore(game,team)
end
assert(Game.GetTeamScore(0)==99 and Game.GetTeamScore(3)==99,
    "unavailable converted scores must fall back to numeric native scores")
game.getcod4teamscore=function() return {} end
local earlyMatchWidget=menus.teamScoresHudDef()
assert(find(earlyMatchWidget,"friendlyScore_id").text=="--" and
    find(earlyMatchWidget,"scoreWatchFriendly").newValue==nil,
    "fresh HUD must construct before recorded scores are available")
game.getcod4teamscore=savedRecordedScore
local matchWidget=menus.teamScoresHudDef()
assert(nativeBinds==0 and find(matchWidget,"friendlyScore_id").text=="14" and find(matchWidget,"enemyScore_id").text=="3")
recordedScores[2]=15; matchWidget:processEvent({name="cod4_scores_update"})
assert(find(matchWidget,"friendlyScore_id").text=="15" and find(matchWidget,"scoreWatchFriendly").newValue==15)
recordedScores[2]=14; matchWidget:processEvent({name="cod4_scores_update"})
assert(find(matchWidget,"friendlyScore_id").text=="14","HUD rewind restores recorded score")
local popup=menus.pointsPopup()
assert(popup.style.left==0 and find(popup,"points").style.alignment==LUI.Alignment.Center)
find(popup,"points"):animateToState("default",0)
assert(find(popup,"points").style.alignment==LUI.Alignment.Center,"native popup reset keeps centering")
convertedHud=false
assert(Game.GetTeamScore(1)==99 and menus.pointsPopup().style.left==20)
menus.teamScoresHudDef(); assert(nativeBinds==2,"native matches keep their own score bindings")
print("PASS double-click playback/conversion, centered popup reset, recorded native HUD scores and native-match isolation")

local nativePauseClosed,optionsOpened=0,0
LUI.MenuTemplate.OnOptions=function() optionsOpened=optionsOpened+1 end
menus.mp_pause_menu=function()
    local menu=LUI.MenuTemplate.new({type="mp_pause_menu"},{})
    menu.title=Element.new(); menu.title:setText("MULTIPLAYER MENU"); menu:addElement(menu.title)
    menu.map=Element.new(); menu.map.id="stock_pause_map"; menu:addElement(menu.map)
    local mode=Element.new(); mode.id="gamemode_Id"; menu:addElement(mode)
    local helper=Element.new(); helper.id="native_back_helper"; menu.list:addElement(helper)
    menu:AddButton("CHANGE LOADOUT",function() error("live match action called") end)
    menu:AddButton("CHANGE TEAM",function() error("live match action called") end)
    menu:AddButton("MUTE PLAYERS",function() error("live match action called") end)
    menu:registerEventHandler("menu_close",function() nativePauseClosed=nativePauseClosed+1 end)
    return menu
end
dofile("data/cdata/ui_scripts/theater/pause_menu.lua")
data.playing=false
local stockPause=menus.mp_pause_menu()
assert(stockPause.title.text=="MULTIPLAYER MENU" and not stockPause.map.closed)
assert(find(stockPause,"native_CHANGE LOADOUT"),"live match menu remains native")
data.playing=true; data.paused=true; data.name=string.rep("long_demo_",30)..".dm_h1"
data.elapsed=38000; data.duration=47000; data.speed=.5; data.camera=2
data.mapTitle="Crash"; data.modeTitle="Search and Destroy"
root.menu=menus.mp_pause_menu()
local demoPause=root.menu
assert(demoPause.title.text=="DEMO PLAYBACK" and demoPause.map.closed)
assert(not find(demoPause,"native_CHANGE LOADOUT") and not find(demoPause,"gamemode_Id"))
assert(find(demoPause,"native_back_helper"),"native back helper survives button rebuild")
assert(find(demoPause,"demo_pause_filename").text:sub(-3)=="...","long recording title fits details panel")
assert(find(demoPause,"demo_pause_map").text=="Crash" and find(demoPause,"demo_pause_mode").text=="Search and Destroy")
assert(find(demoPause,"demo_pause_status").text=="PAUSED   00:38 / 00:47   0.5x   FREE CAMERA")
local beforeBack=#commands
find(demoPause,"native_BACK TO DEMO"):processEvent({name="button_action"})
assert(root.menu==nil and data.paused and #commands==beforeBack and nativePauseClosed==1,
    "Back preserves playback pause and the native close handler")
for _,entry in ipairs({{"PLAY DEMO","pause"},{"PLAYBACK CONTROLS","timeline"},{"THEATER EDITOR","editor"},{"RESTART DEMO","seek"}}) do
    root.menu=menus.mp_pause_menu()
    find(root.menu,"native_"..entry[1]):processEvent({name="button_action"})
    assert(root.menu==nil and commands[#commands][1]==entry[2],"demo action must close the native pause menu")
    if entry[2]=="seek" then assert(commands[#commands][2]==0,"restart seeks to the beginning") end
end
root.menu=menus.mp_pause_menu()
find(root.menu,"native_@MENU_OPTIONS"):processEvent({name="button_action"})
assert(optionsOpened==1 and root.menu,"Options uses its native route and retains the pause menu")
find(root.menu,"native_RETURN TO THEATER"):processEvent({name="button_action"})
assert(root.menu==nil and libraryActions[#libraryActions][1]=="return")
data.paused=false
root.menu=menus.mp_pause_menu()
assert(find(root.menu,"native_PAUSE DEMO"),"playing recordings offer Pause")
root.menu:processEvent({name="demo_pause_poll"})
assert(find(root.menu,"demo_pause_status").text:sub(1,7)=="PLAYING")
root.menu=nil
print("PASS demo-only Escape menu, preserved native helpers/options/close, playback actions, recording details and return routing")

function game:issingleplayer() return false end
dofile("data/cdata/ui_scripts/patches/pausequit.lua")
local noExtraQuit=menus.mp_pause_menu()
local pauseButtonCount=#noExtraQuit.list.children
callbacks.mp_pause_menu(noExtraQuit)
assert(#noExtraQuit.list.children==pauseButtonCount,"patch load order must not append duplicate demo quit buttons")
data.playing=false
local liveQuit=menus.mp_pause_menu(); callbacks.mp_pause_menu(liveQuit)
assert(find(liveQuit,"native_@MENU_QUIT_TO_DESKTOP"),"live matches retain the normal quit patch")

Engine.InFrontend=function() return true end
local returnPending,frontendReady=true,false
function game:theaterlibraryreturn()
    if not frontendReady then return false end
    local requested=returnPending; returnPending=false; return requested
end
menus.h1_theater_library=nil
menus.h1_theater_split=nil
dofile("data/cdata/ui_scripts/theater/library.lua")
local frontend=Element.new()
callbacks.mp_main_menu(frontend)
frontend:processEvent({name="theater_return_poll"})
assert(not root.menu and returnPending,"return waits for the native frontend lobby")
frontendReady=true; frontend:processEvent({name="theater_return_poll"})
assert(root.menu and root.menu.id=="h1_theater_library" and not returnPending)
frontend:processEvent({name="theater_return_poll"})
assert(root.menu.id=="h1_theater_library","return request is consumed once")
print("PASS script-order-safe quit controls and one-shot library reopening after frontend readiness")

dofile("src/client/resources/ui_scripts/common.lua")
local lateCalls,nativeCalls=0,0
LUI.onmenuopen("late_hud",function(menu) lateCalls=lateCalls+1; menu.attached=true end)
LUI.overrideMenuBuilder("late_hud",function(native,...)
    local menu=native(...); menu.patched=true; return menu
end)
LUI.MenuBuilder.registerType("late_hud",function()
    nativeCalls=nativeCalls+1; return Element.new()
end)
local lateHud=menus.late_hud()
assert(lateHud.attached and lateHud.patched and lateCalls==1 and nativeCalls==1,
    "callbacks survive extension loading before native HUD registration")
LUI.onmenuopen("late_hud",function(menu) menu.second=true end)
lateHud=menus.late_hud()
assert(lateHud.second and lateCalls==2 and nativeCalls==2,"late hooks do not duplicate builders")
print("PASS existing HUD bootstrap, idempotent attachment and deferred native builder hooks")
