-- Adapt the native pause menu only during recorded playback. Retain its menu
-- lifetime, back handler, options route and quit confirmation.
if Engine.InFrontend() or not game.gettheaterstate then return end
local Editor=require("editor")
local green={r=.35,g=.75,b=.4}
local white={r=.92,g=.94,b=.92}
local cameras={"FIRST PERSON","THIRD PERSON","FREE CAMERA","DOLLY"}
local function layout(x,y,w,h)
    return {leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
        left=x,top=y,width=w,height=h}
end
local function text(parent,id,value,x,y,w,h,color)
    local style=layout(x,y,w,h)
    style.font=CoD.TextSettings.TitleFontTiny.Font
    style.alignment=LUI.Alignment.Left; style.color=color or white
    local element=LUI.UIText.new(style); element.id=id
    element:setText(value); parent:addElement(element)
    return element
end
local function clock(ms)
    local seconds=math.floor((ms or 0)/1000)
    return string.format("%02d:%02d",math.floor(seconds/60),seconds%60)
end
local function filename(value)
    local font=CoD.TextSettings.TitleFontTiny.Font
    local function width(label) local _,_,w=luiglobals.GetTextDimensions(label,font,18); return w end
    if width(value)<=520 then return value end
    while value~="" and width(value.."...")>520 do
        value=value:gsub("[%z\1-\127\194-\244][\128-\191]*$","")
    end
    return value.."..."
end

LUI.overrideMenuBuilder("mp_pause_menu",function(nativePause,...)
    local menu=nativePause(...)
    local data=game:gettheaterstate(-1,-1)
    if not data.playing or not menu or not menu.list then return menu end

    if menu.title then menu.title:setText("DEMO PLAYBACK") end
    -- Keep the reference so the stock POV-change handler cannot recreate it.
    if menu.map then menu.map:close() end
    local mode=menu:getFirstDescendentById("gamemode_Id")
    if mode then mode:close() end
    -- Keep the native helper/back controls. Only AddButton-owned entries go.
    local child=menu.list:getFirstChild()
    while child do
        local nextChild=child:getNextSibling()
        if child.menu==menu then child:close() end
        child=nextChild
    end
    menu.list.buttonCount=0; menu.list.bottomDivider=nil
    local function back() LUI.FlowManager.RequestLeaveMenu(menu) end
    local function action(name,value)
        if game:theateraction(name,value or 0) then back() end
    end
    local function button(label,callback)
        local element=menu:AddButton(label,callback,nil,true,nil,{height=28,fadeIn=false},nil,true)
        Editor.fitButtonBackground(element)
        return element
    end
    button("BACK TO DEMO",back)
    button(data.paused and "PLAY DEMO" or "PAUSE DEMO",function() action("pause") end)
    button("PLAYBACK CONTROLS",function() action("timeline") end)
    button("THEATER EDITOR",function() action("editor") end)
    button("RESTART DEMO",function() action("seek",0) end)
    button("@MENU_OPTIONS",LUI.MenuTemplate.OnOptions)
    button("RETURN TO THEATER",function()
        if game:theaterlibraryaction("return","") then game:theateraction("close",0); back() end
    end)
    local last=button("@MENU_QUIT_TO_DESKTOP",function(button,event)
        LUI.FlowManager.RequestAddMenu(button,"quit_popmenu",true,event and event.controller)
    end)
    menu:CreateBottomDivider(); menu:AddBottomDividerToList(last)

    local panel=LUI.UIElement.new(layout(350,100,570,320)); panel.id="demo_pause_details"
    menu:addElement(panel)
    text(panel,"demo_pause_heading","DEMO RECORDING",0,0,550,24,green)
    text(panel,"demo_pause_filename",filename(data.name or ""),0,44,520,18)
    local map=Engine.Localize(data.mapTitle or "")
    local modeTitle=Engine.Localize(data.modeTitle or "")
    text(panel,"demo_pause_map",map,0,89,550,20)
    text(panel,"demo_pause_mode",modeTitle,0,119,550,17)
    local status=text(panel,"demo_pause_status","",0,171,550,17,green)
    text(panel,"demo_pause_keys","F1  Toggle HUD     F3  Theater Editor     F4  Toggle Controls",0,232,550,14)
    text(panel,"demo_pause_help","Back keeps your current playback and camera settings.",0,263,550,14)
    local function update()
        local state=game:gettheaterstate(-1,-1)
        status:setText((state.paused and "PAUSED" or "PLAYING").."   "..clock(state.elapsed).." / "..
            clock(state.duration).."   "..tostring(state.speed or 1).."x   "..(cameras[(state.camera or 0)+1] or ""))
    end
    menu:registerEventHandler("demo_pause_poll",update)
    menu:addElement(LUI.UITimer.new(250,"demo_pause_poll")); update()
    return menu
end)
