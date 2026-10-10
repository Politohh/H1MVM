local Editor=require("editor")
local Splitter={}
local green={r=.35,g=.75,b=.4}
local white={r=1,g=1,b=1}
local amber={r=1,g=.72,b=.27}
local blue={r=.35,g=.75,b=1}
local dark={r=.025,g=.035,b=.025}
local function layout(x,y,w,h)
    return {leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,left=x,top=y,width=w,height=h}
end
local function image(parent,id,x,y,w,h,color,alpha)
    local style=layout(x,y,w,h);style.color,style.alpha,style.material=color,alpha or 1,RegisterMaterial("white")
    local element=LUI.UIImage.new(style);element.id=id;element.splitLayout=style;parent:addElement(element);return element
end
local function update(element,name,x,w,alpha)
    local state={};for k,v in pairs(element.splitLayout) do state[k]=v end
    state.left,state.width,state.alpha=x or state.left,w or state.width,alpha or state.alpha
    element:registerAnimationState(name,state);element:animateToState(name,0)
end
local function text(parent,id,value,x,y,w,h,color)
    local style=layout(x,y,w,h);style.font,style.alignment,style.color=CoD.TextSettings.TitleFontTiny.Font,LUI.Alignment.Left,color or white
    local element=LUI.UIText.new(style);element.id=id;element:setText(value);parent:addElement(element);return element
end
local function fitted(value,width,height)
    local function size(v) local _,_,w=luiglobals.GetTextDimensions(v,CoD.TextSettings.TitleFontTiny.Font,height);return w end
    if size(value)<=width then return value end
    while value~="" and size(value.."...")>width do value=value:gsub("[%z\1-\127\194-\244][\128-\191]*$","") end
    return value.."..."
end
local function time(seconds)
    local ms=math.floor(seconds*1000+.5)
    return string.format("%02d:%02d.%03d",math.floor(ms/60000),math.floor(ms/1000)%60,ms%1000)
end
local function button(parent,id,label,x,y,w,callback)
    local element=LUI.MenuBuilder.BuildAddChild(parent,{type="UIGenericButton",id=id,properties={
        style=LUI.MenuTemplate.ButtonStyle,button_text="",height=30,fadeIn=false,button_action_func=callback}})
    element:registerAnimationState("split_layout",layout(x,y,w,30));element:animateToState("split_layout",0)
    Editor.fitButtonBackground(element)
    element.caption=text(element,id.."_caption",fitted(label,w-18,14),9,7,w-18,14)
    return element
end

function Splitter.build(parameters,item)
    local menu=LUI.MenuTemplate.new(parameters,{menu_title="SPLIT DEMO",menu_width=1124,
        do_not_add_friends_helper=true,showTopRightSmallBar=true,skipAnim=true})
    menu.id="h1_theater_split"
    menu:AddBackButton(function() menu.leaveRequested=true end)
    if not item then return menu end
    local total=(item.duration or 0)/1000
    menu.start,menu.finish,menu.cursor=0,total,0
    menu.events,menu.eventPage,menu.eventFilter={},1,"all"
    -- MenuTemplate.list is a UIVerticalList: it rewrites child offsets and
    -- counts decorative backgrounds as rows. Use a plain, clipped container.
    image(menu,"split_clip_background",30,294,368,314,dark,.9)
    local controls=LUI.UIElement.new(layout(38,304,352,270))
    controls.id="split_controls";controls:setUseStencil(true);menu:addElement(controls)
    text(menu,"split_filename",fitted(item.name,1060,20),38,104,1086,20,green)
    text(menu,"split_format",item.source and "COD4 → H1 CLIP" or "H1 CLIP",38,134,330,14)
    menu.summary=text(menu,"split_summary","",414,134,710,16,green)
    image(menu,"split_timeline_background",38,168,1086,104,dark,.94)
    text(menu,"split_legend_kills","KILLS",54,179,125,13,green)
    text(menu,"split_legend_rounds","ROUND BOUNDARIES",180,179,210,13,blue)
    text(menu,"split_legend_marks","BOOKMARKS",392,179,180,13,amber)
    text(menu,"split_zero","00:00",54,253,100,12)
    text(menu,"split_total",time(total),1008,253,102,12)
    image(menu,"split_feed_background",414,294,710,314,dark,.9)
    text(menu,"split_clip_title","CLIP RANGE",38,279,352,14,green)
    text(menu,"split_feed_title","EVENTS / KILLFEED",430,279,400,14,green)
    text(menu,"split_event_order","EARLIEST FIRST",978,281,130,11)
    text(menu,"split_heading_time","TIME",439,338,101,10)
    text(menu,"split_heading_killer","KILLER",541,338,153,10,green)
    text(menu,"split_heading_weapon","WEAPON",700,338,72,10)
    text(menu,"split_heading_victim","VICTIM",782,338,220,10)
    menu.help=text(menu,"split_help","Choose an event or click the timeline, then set IN and OUT. Original recording kept.",38,153,1086,12)
    menu.eventNote=text(menu,"split_event_note","Reading recorded events...",414,615,710,12)
    local function editable()
        local data=game:gettheaterlibrary(1,"all")
        return not menu.submitted and not data.busy and not data.converting
    end
    local bar=LUI.UIButton.new(layout(54,204,1054,43));bar.id="split_timeline";menu:addElement(bar);bar:setHandleMouseButton(true)
    image(bar,"split_track",0,0,1054,43,{r=.1,g=.12,b=.1},1)
    local selection=image(bar,"split_range",0,0,1054,43,green,.18)
    -- Fixed bins bound widget allocation regardless of match length. The event
    -- list retains individual kills even when several share an overview pixel.
    local bins={}
    for lane=1,3 do
        bins[lane]={}
        for i=1,160 do bins[lane][i]=image(bar,"split_tick_"..lane.."_"..i,(i-1)*1052/160,4+(lane-1)*12,2,8,
            lane==1 and green or lane==2 and blue or amber,0) end
    end
    local inHandle=image(bar,"split_in",0,0,3,43,green,1)
    local outHandle=image(bar,"split_out",1051,0,3,43,green,1)
    local cursor=image(bar,"split_cursor",0,0,2,43,white,1)
    local function fraction(value) return total>0 and math.max(0,math.min(1,value/total)) or 0 end
    local function summary()
        menu.summary:setText("IN "..time(menu.start).."   OUT "..time(menu.finish).."   LENGTH "..time(menu.finish-menu.start))
        update(selection,"range",fraction(menu.start)*1052,math.max(1,fraction(menu.finish-menu.start)*1052))
        update(inHandle,"in",fraction(menu.start)*1051,3);update(outHandle,"out",fraction(menu.finish)*1051,3)
        update(cursor,"cursor",fraction(menu.cursor)*1052,2)
        for _,key in ipairs({"cursor","start","finish"}) do
            if menu[key.."Readout"] then menu[key.."Readout"]:setText(time(menu[key]));menu[key.."Row"]:processEvent({name="content_refresh"}) end
        end
    end
    local function setTime(key,value)
        if not editable() then return false end
        value=math.floor(value*1000+.5)/1000
        if key=="start" then value=math.max(0,math.min(value,menu.finish-.1))
        elseif key=="finish" then value=math.min(total,math.max(value,menu.start+.1))
        else value=math.max(0,math.min(total,value)) end
        menu[key]=value;summary();return true
    end
    local function pointer(event)
        if not event.root or not event.x or not event.y then return end
        local px,py=event.x,event.y
        if event.rootName then px,py=ProjectRootCoordinate(event.rootName,px,py) end
        local left,top=bar:getRect()
        return math.max(0,math.min(1052,event.root:pixelsToUnits(px,py)-event.root:pixelsToUnits(left,top)))
    end
    local dragging
    bar:registerEventHandler("leftmousedown",function(_,event)
        if not editable() then return end
        local x=pointer(event);if not x then return end
        dragging=math.abs(x-fraction(menu.start)*1052)<7 and "start" or math.abs(x-fraction(menu.finish)*1052)<7 and "finish" or "cursor"
        local value=x/1052*total
        if dragging=="cursor" then
            local distance=6
            for _,e in ipairs(menu.events) do
                local d=math.abs(x-fraction(e.time/1000)*1052)
                if d<distance then distance=d;value=e.time/1000 end
            end
        end
        setTime(dragging,value)
    end)
    bar:registerEventHandler("mousedrag",function(_,event) local x=pointer(event);if dragging and x then setTime(dragging,x/1052*total) end end)
    bar:registerEventHandler("leftmouseup",function() dragging=nil;return true end)
    bar:registerEventHandler("button_left",function() setTime("cursor",menu.cursor-.1) end)
    bar:registerEventHandler("button_right",function() setTime("cursor",menu.cursor+.1) end)
    for _,spec in ipairs({{"cursor","EVENT CURSOR",0},{"start","IN / START",64},{"finish","OUT / END",128}}) do
        local key=spec[1]
        menu[key.."Row"]=Editor.valueRow(controls,"split_"..key,spec[2],0,spec[3],
            {kind="float",min=0,max=total,step=key=="cursor" and .1 or 1,format=time},function() return menu[key] end,
            function(value) return setTime(key,value) end,menu)
        menu[key.."Readout"]=text(controls,"split_"..key.."_time","",8,spec[3]+32,308,20,key=="cursor" and amber or green)
    end
    button(controls,"split_set_in","SET IN AT CURSOR",0,194,154,function() setTime("start",menu.cursor) end)
    button(controls,"split_set_out","SET OUT AT CURSOR",162,194,154,function() setTime("finish",menu.cursor) end)
    button(controls,"split_bookmark","ADD / REMOVE BOOKMARK",0,232,316,function()
        if editable() then
            if game:theaterbookmark(item.id,math.floor(menu.cursor*1000+.5)) then menu.eventsLoaded=false
            else menu.help:setText("Cannot save bookmark. Check the recording folder is writable.") end
        end
    end)
    button(menu,"split_save",item.source and "CONVERT AND SAVE CLIP" or "SAVE CLIP",38,577,352,function()
        if not editable() then return end
        local accepted=game:theaterlibrarysplit(item.id,math.floor(menu.start*1000+.5),math.floor(menu.finish*1000+.5))
        if accepted then menu.submitted=true;menu.help:setText("Saving your clip...")
        else menu.help:setText("Cannot start the split. Refresh the library and check your range.") end
    end)
    local rows={}
    local empty=text(menu,"split_events_empty","",438,392,650,16)
    local function renderEvents()
        local visible={}
        for _,event in ipairs(menu.events) do
            if menu.eventFilter=="all" or menu.eventFilter==event.kind or menu.eventFilter=="round" and event.kind:sub(1,6)=="round_" then visible[#visible+1]=event end
        end
        menu.eventPages=math.max(1,math.ceil(#visible/7));menu.eventPage=math.max(1,math.min(menu.eventPages,menu.eventPage))
        empty:setText(#visible==0 and (menu.eventsLoaded and "No events in this filter. Add a bookmark at the cursor." or "Reading recorded events...") or "")
        for i,row in ipairs(rows) do
            row.event=visible[(menu.eventPage-1)*7+i];local e=row.event
            local kill=e and e.kind=="kill" and e.attacker and e.attacker~="" and e.victim and e.victim~=""
            row.button.caption:setText(e and not kill and fitted(time(e.time/1000).."  "..e.label,655,14) or "")
            row.clock:setText(kill and time(e.time/1000) or "")
            row.attacker:setText(kill and fitted(e.attacker,153,14) or "")
            row.victim:setText(kill and fitted(e.victim,220,14) or "")
            local hasIcon=kill and type(e.icon)=="string" and e.icon~=""
            local ratio=hasIcon and math.max(1,math.min(4,e.iconRatio or 2)) or 1
            local iconWidth=18*ratio
            local iconLeft=270+(72-iconWidth)/2
            -- Mirror using reversed image edges, as the native obituary does.
            local state=layout(iconLeft+iconWidth,6,iconWidth,18)
            state.width=nil;state.right=iconLeft
            state.alpha,state.material=hasIcon and 1 or 0,RegisterMaterial(hasIcon and e.icon or "white")
            state.color=white;row.icon:registerAnimationState("icon",state);row.icon:animateToState("icon",0)
            row.weaponText:setText(kill and not hasIcon and fitted(e.weapon and e.weapon~="" and e.weapon:gsub("_mp$","") or "DEATH",72,10) or "")
            local state=layout(430,350+(i-1)*32,678,30);state.alpha=e and 1 or 0
            row.button:registerAnimationState("event",state);row.button:animateToState("event",0);row.button.disabled=not e
            row.button:processEvent({name=e and "enable" or "disable"})
        end
        menu.eventCounter:setText(string.format("%d EVENTS   /   PAGE %d OF %d",#visible,menu.eventPage,menu.eventPages))
    end
    for i=1,7 do
        local row={};rows[i]=row
        row.button=button(menu,"split_event_"..i,"",430,350+(i-1)*32,678,function()
            if row.event then setTime("cursor",row.event.time/1000) end
        end)
        row.clock=text(row.button,"event_time_"..i,"",9,8,101,13)
        row.attacker=text(row.button,"event_attacker_"..i,"",111,7,153,14,green)
        row.icon=image(row.button,"event_icon_"..i,270,6,72,18,white,0)
        row.weaponText=text(row.button,"event_weapon_"..i,"",270,10,72,10)
        row.victim=text(row.button,"event_victim_"..i,"",352,7,220,14)
        row.button:addEventHandler("button_over",function()
            if row.event then menu.help:setText(fitted(time(row.event.time/1000).."  "..row.event.label..
                (row.event.weapon and row.event.weapon~="" and "  /  "..row.event.weapon or "").."  /  Select, then SET IN or SET OUT.",1086,12)) end
        end)
    end
    for i,spec in ipairs({{"all","ALL"},{"kill","KILLS"},{"round","ROUNDS"},{"bookmark","BOOKMARKS"}}) do
        button(menu,"split_filter_"..spec[1],spec[2],430+(i-1)*172,307,162,function()
            menu.eventFilter=spec[1];menu.eventPage=1;renderEvents()
        end)
    end
    menu.eventCounter=text(menu,"split_event_count","",430,587,390,13,green)
    button(menu,"split_event_previous","< PREV",878,579,109,function() menu.eventPage=menu.eventPage-1;renderEvents() end)
    button(menu,"split_event_next","NEXT >",999,579,109,function() menu.eventPage=menu.eventPage+1;renderEvents() end)
    menu:registerEventHandler("split_poll",function()
        if menu.leaveRequested then
            menu.leaveRequested=false;LUI.FlowManager.RequestLeaveMenu(menu)
            LUI.FlowManager.RequestAddMenu(nil,"h1_theater_library",true,menu.exclusiveController);return
        end
        if not menu.eventsLoaded then
            local data=game:gettheaterevents(item.id)
            if data.ready then
                menu.events=data.events or {};menu.eventsLoaded=true
                table.sort(menu.events,function(a,b) return a.time<b.time end)
                menu.eventNote:setText(fitted(data.message or "",710,12))
                local active={{},{},{}}
                for _,e in ipairs(menu.events) do
                    local lane=e.kind=="kill" and 1 or e.kind=="bookmark" and 3 or 2
                    local bin=math.min(160,math.floor(fraction(e.time/1000)*160)+1);active[lane][bin]=true
                end
                for lane=1,3 do for i=1,160 do update(bins[lane][i],"tick",nil,nil,active[lane][i] and 1 or 0) end end
                renderEvents()
            end
        end
        local data=game:gettheaterlibrary(1,"all")
        if menu.submitted then
            menu.help:setText(data.converting and (data.conversionMessage or "Converting COD4 clip...")
                or data.busy and string.format("Saving clip... %d%%",math.max(0,data.splitProgress or 0)) or data.message or "")
            if not data.busy and not data.converting then menu.submitted=false end
        end
        summary()
    end)
    menu:addElement(LUI.UITimer.new(250,"split_poll"))
    renderEvents();menu:processEvent({name="split_poll"});summary()
    return menu
end
return Splitter
