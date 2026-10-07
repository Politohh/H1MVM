-- Native frontend demo library. The scanner only supplies recordings and metadata;
-- selection, focus, paging and preview rendering stay in MWR's LUI menu system.
local Editor = require("editor")
local green = {r = 0.35, g = 0.75, b = 0.4}
local white = {r = 1, g = 1, b = 1}
local dark = {r = 0.025, g = 0.035, b = 0.025}
local function layout(x,y,w,h)
    return {leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
        left=x,top=y,width=w,height=h}
end
local function image(parent,id,x,y,w,h,color,alpha,material)
    local style=layout(x,y,w,h)
    style.color,style.alpha,style.material=color,alpha or 1,RegisterMaterial(material or "white")
    local element=LUI.UIImage.new(style)
    element.id=id; element.libraryLayout=style; parent:addElement(element)
    return element
end
local function text(parent,id,value,x,y,w,h,color)
    local style=layout(x,y,w,h)
    style.font,style.alignment,style.color=CoD.TextSettings.TitleFontTiny.Font,LUI.Alignment.Left,color or white
    local element=LUI.UIText.new(style)
    element.id=id; element:setText(value); parent:addElement(element)
    return element
end
local function localized(value,fallback)
    if not value or value=="" then return fallback end
    return Engine.Localize(value)
end
local function cardTitle(name)
    local title=(name or ""):gsub("%.[dD][mM]_[hH]1$",""):gsub("%.[dD][mM]_1$","")
    if title=="" then title="Untitled demo" end
    local font=CoD.TextSettings.TitleFontTiny.Font
    local function width(value,height)
        local _,_,measured=luiglobals.GetTextDimensions(value,font,height)
        return measured
    end
    local height=15
    if width(title,height)>210 then height=13 end
    if width(title,height)>210 then
        -- Keep filenames legible and leave the complete name in the preview.
        while title~="" and width(title.."...",height)>210 do
            title=title:gsub("[%z\1-\127\194-\244][\128-\191]*$","")
        end
        title=title.."..."
    end
    return title,height
end
local function duration(ms)
    local seconds=math.floor((ms or 0)/1000)
    return string.format("%02d:%02d",math.floor(seconds/60),seconds%60)
end
local function button(parent,id,label,x,y,w,callback)
    local element=LUI.MenuBuilder.BuildAddChild(parent,{type="UIGenericButton",id=id,properties={
        style=LUI.MenuTemplate.ButtonStyle,button_text=label,height=30,fadeIn=false,
        text_align_without_content=LUI.Alignment.Left,text_padding_without_content=8,
        text_padding_focus_without_content=8,button_action_func=callback}})
    element:registerAnimationState("library_layout",layout(x,y,w,30))
    element:animateToState("library_layout",0)
    Editor.fitButtonBackground(element)
    return element
end
local function alpha(element,value)
    local state={}
    for k,v in pairs(element.libraryLayout) do state[k]=v end
    state.alpha=value
    element:registerAnimationState("library_alpha",state); element:animateToState("library_alpha",0)
end

LUI.MenuBuilder.registerType("h1_theater_library",function(parameters)
    local menu=LUI.MenuTemplate.new(parameters,{menu_title="THEATER",menu_width=720,
        do_not_add_friends_helper=true,showTopRightSmallBar=true,uppercase_title=true,skipAnim=true})
    menu.id="h1_theater_library"
    menu.page,menu.filter,menu.revision=1,"all",-1
    menu.cards={}
    local function request(name,id) return game:theaterlibraryaction(name,id or "") end
    local function activate(item)
        if menu.snapshot.busy or menu.snapshot.converting or (item.error and item.error~="") then return end
        if request(item.source and "convert" or "play",item.id) and not item.source then
            menu.playRequested=true
        end
    end
    menu.list:registerAnimationState("library_list",layout(38,136,704,434))
    menu.list:animateToState("library_list",0)
    menu.preview=LUI.UIElement.new(layout(774,136,350,440)); menu.preview.id="library_preview"; menu:addElement(menu.preview)
    image(menu,"library_separator",754,136,1,434,green,.5)
    menu.status=text(menu,"library_status","Reading demo library...",38,614,1086,14)
    menu.counter=text(menu,"library_count","",38,580,420,16,green)
    menu.empty=text(menu,"library_empty","",38,215,704,20)
    menu.actions=LUI.UIElement.new(layout(0,0,1150,650)); menu.actions.id="library_actions"; menu:addElement(menu.actions)

    function menu:select(item)
        self.selected=item and item.id or nil
        for _,card in ipairs(self.cards) do
            for _,edge in ipairs(card.edges) do alpha(edge,card.item.id==self.selected and 1 or .25) end
        end
        self.preview:closeChildren()
        if not item then
            text(self.preview,"preview_empty","IMPORT A DEMO TO GET STARTED",0,0,350,20,green)
            text(self.preview,"preview_empty_help","Native H1 and COD4 recordings",0,34,350,16)
            return
        end
        local map=localized(item.mapTitle,item.map~="" and item.map or "UNKNOWN MAP")
        text(self.preview,"preview_title",Engine.ToUpperCase(map),0,0,350,24,green)
        text(self.preview,"preview_mode",localized(item.modeTitle,"DEMO RECORDING"),0,31,350,17)
        image(self.preview,"preview_background",0,62,350,197,dark,.92)
        if item.material and item.material~="" then image(self.preview,"preview_image",0,62,350,197,white,1,item.material) end
        text(self.preview,"preview_date",item.date or "",0,273,246,16)
        text(self.preview,"preview_duration",duration(item.duration),273,273,77,16,green)
        text(self.preview,"preview_filename",item.name,0,305,350,16)
        text(self.preview,"preview_format",item.source and "COD4 RECORDING  /  CONVERSION REQUIRED" or "H1 RECORDING  /  READY TO PLAY",0,331,350,15,green)
        if item.error and item.error~="" then
            text(self.preview,"preview_error",item.error,0,361,350,16)
        else
            button(self.preview,"library_play",item.source and "CONVERT DEMO" or "PLAY DEMO",0,366,350,function()
                activate(item)
            end)
        end
    end

    function menu:render(data)
        self.snapshot=data; self.page=data.page; self.revision=data.revision
        if self.grid then self.grid:close() end
        local gridState=layout(0,0,704,434); gridState.spacing=10
        self.grid=LUI.UIVerticalList.build(nil,{defaultState=gridState,noWrap=true})
        self.grid.id="library_grid"; self.list:addElement(self.grid)
        self.cards={}
        local selected=nil
        for row=1,3 do
            local state=layout(0,0,704,138); state.spacing=12
            local line=LUI.UIHorizontalList.new(state); line.id="library_row_"..row; self.grid:addElement(line)
            for col=1,3 do
                local item=data.entries[(row-1)*3+col]
                if item then
                    local card=LUI.UIButton.new(layout(0,0,226,138)); card.id="library_card_"..((row-1)*3+col)
                    line:addElement(card)
                    image(card,"card_background",0,0,226,138,dark,.95)
                    if item.material and item.material~="" then image(card,"card_map",1,1,224,111,white,1,item.material) end
                    image(card,"card_caption",1,1,224,42,dark,.8)
                    local title,titleHeight=cardTitle(item.name)
                    text(card,"card_title",title,8,7,210,titleHeight)
                    text(card,"card_mode",localized(item.modeTitle,"DEMO RECORDING"),8,27,210,14)
                    image(card,"card_footer",1,112,224,25,dark,.95)
                    text(card,"card_date",item.date or "",8,118,155,13)
                    text(card,"card_duration",duration(item.duration),175,118,45,13,green)
                    text(card,"card_format",item.error and item.error~="" and "UNAVAILABLE" or (item.source and "COD4" or "H1"),8,91,210,14,green)
                    local edges={image(card,"edge_top",0,0,226,1,green,.25),image(card,"edge_bottom",0,137,226,1,green,.25),
                        image(card,"edge_left",0,0,1,138,green,.25),image(card,"edge_right",225,0,1,138,green,.25)}
                    self.cards[#self.cards+1]={item=item,element=card,edges=edges}
                    card:addEventHandler("button_over",function() self:select(item) end)
                    card:registerEventHandler("button_action",function(_,event)
                        self:select(item)
                        if not event.mouse then activate(item); return end
                        local now=game:theaterclock()
                        if self.lastClickId==item.id and now>=self.lastClickTime and now-self.lastClickTime<=400 then
                            self.lastClickId=nil; activate(item)
                        else self.lastClickId,self.lastClickTime=item.id,now end
                    end)
                    if item.id==self.selected then selected=item end
                end
            end
        end
        self:select(selected or data.entries[1])
        self.empty:setText(#data.entries==0 and "No demos here. Use IMPORT DEMOS to add a recording." or "")
        self.counter:setText(string.format("PAGE %d / %d    ·    %d RECORDINGS",data.page,data.pages,data.total))
    end

    function menu:refresh()
        local data=game:gettheaterlibrary(self.page,self.filter)
        self.snapshot=data
        if self.playRequested and data.playing then
            self.playRequested=false
            LUI.FlowManager.RequestLeaveMenu(self)
            return
        end
        if data.revision~=self.revision or data.page~=self.page or self.redraw then
            self.redraw=false; self:render(data)
        end
        self.status:setText(data.busy and "Reading demo library..." or (data.conversionMessage or data.message or ""))
        if self.wasConverting and not data.converting then request("refresh") end
        self.wasConverting=data.converting
    end
    local function filter(value)
        menu.filter=value; menu.page=1; menu.redraw=true; menu:refresh()
    end
    button(menu.actions,"library_all","ALL DEMOS",38,92,150,function() filter("all") end)
    button(menu.actions,"library_h1","H1 DEMOS",200,92,150,function() filter("ready") end)
    button(menu.actions,"library_cod4","COD4 DEMOS",362,92,150,function() filter("source") end)
    button(menu.actions,"library_import","IMPORT DEMOS",774,92,215,function() request("import") end)
    button(menu.actions,"library_refresh","REFRESH",1001,92,123,function() request("refresh") end)
    button(menu.actions,"library_convert","CONVERT COD4 DEMO",774,544,350,function()
        if not menu.snapshot.busy and not menu.snapshot.converting then request("convertfile") end
    end)
    button(menu.actions,"library_previous","< PREVIOUS",470,577,130,function()
        if menu.page>1 then menu.page=menu.page-1; menu.redraw=true; menu:refresh() end
    end)
    button(menu.actions,"library_next","NEXT >",612,577,130,function()
        if menu.page<menu.snapshot.pages then menu.page=menu.page+1; menu.redraw=true; menu:refresh() end
    end)
    menu:AddBackButton()
    menu:registerEventHandler("library_poll",function() menu:refresh() end)
    menu:addElement(LUI.UITimer.new(500,"library_poll"))
    menu:refresh()
    request("refresh")
    return menu
end)

-- A demo exits into a new frontend VM. Consume the return request only after
-- its native lobby is ready, then open the same library as the THEATER button.
if Engine.InFrontend() and game.theaterlibraryreturn then
    local function attachReturn(menu)
        menu:registerEventHandler("theater_return_poll",function()
            if game:theaterlibraryreturn() then
                LUI.FlowManager.RequestAddMenu(nil,"h1_theater_library",true,menu.exclusiveController)
            end
        end)
        menu:addElement(LUI.UITimer.new(250,"theater_return_poll"))
    end
    LUI.onmenuopen("mp_main_menu",attachReturn)
    LUI.onmenuopen("menu_xboxlive",attachReturn)
end
