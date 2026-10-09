-- Reused native widgets; navigation is deferred outside button dispatch.
return function(Editor, createButton, createImage, settings, request)
    local green={r=.35,g=.8,b=.4}
    local grey={r=.82,g=.86,b=.82}
    local used
    local function show(element)
        used[element]=true
        element.weaponState.alpha=1
        element:registerAnimationState("weapon_visible",element.weaponState)
        element:animateToState("weapon_visible",0)
        if element.weaponHidden then element.weaponHidden=nil;element:processEvent({name="enable"}) end
        return element
    end
    -- Optional native getChildById lookups assert on absent children. Keep our
    -- own cache, and update callbacks through a dispatcher stock buttons retain.
    local function widgets(parent)
        parent.weaponWidgets=parent.weaponWidgets or {}
        return parent.weaponWidgets
    end
    local function button(parent,id,value,x,y,w,action,options)
        local cache=widgets(parent)
        local e=cache[id]
        if not e then
            e=createButton(parent,id,value,x,y,w,function(element,event)
                return cache[id].weaponAction(element,event)
            end,options)
            cache[id]=e
        else e.textLabel:setText(value) end
        e.weaponAction=action
        e.weaponState={leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
            left=x,top=y,width=w,height=options and options.height or 28}
        return show(e)
    end
    local function image(parent,id,x,y,w,h,color,material)
        local cache=widgets(parent)
        local e=cache[id]
        if not e then e=createImage(parent,id,x,y,w,h,color,material);cache[id]=e end
        e.editorImageLayout={leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
            left=x,top=y,width=w,height=h,color=color,material=RegisterMaterial(material or "white")}
        e.weaponState=e.editorImageLayout
        return show(e)
    end
    local function text(parent,id,value,x,y,w,h,color,font)
        font=font or CoD.TextSettings.FontBold110.Font
        local cache=widgets(parent)
        local t=cache[id]
        if not t then
            t=LUI.UIText.new({leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
                left=x,top=y,width=w,height=h,font=font,
                alignment=LUI.Alignment.Left,color=color or grey})
            t.id=id;parent:addElement(t);cache[id]=t
        end
        t.weaponState={leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
            left=x,top=y,width=w,height=h,font=font,
            alignment=LUI.Alignment.Left,color=color or grey}
        t:setText(value);return show(t)
    end
    local function textArea(parent,id,x,y,w,h)
        local cache=widgets(parent)
        local area=cache[id]
        if not area then
            area=LUI.UIElement.new()
            area.id=id;area:setUseStencil(true);parent:addElement(area);cache[id]=area
        end
        area.weaponState={leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
            left=x,top=y,width=w,height=h}
        return show(area)
    end
    local function fittedText(parent,id,value,x,y,w,h,minimum,color,font)
        font=font or CoD.TextSettings.FontBold110.Font
        local function width(s,size)
            local _,_,measured=luiglobals.GetTextDimensions(s,font,size)
            return measured
        end
        -- Recompute for every reused card; the native label factory only fits
        -- the first value. Preserve the beginning and shorten only the tail.
        local measured=width(value,h)
        local size=measured>w and math.max(minimum,h*w/measured) or h
        if width(value,size)>w then
            while value~="" and width(value.."...",size)>w do
                value=value:gsub("[%z\1-\127\194-\244][\128-\191]*$","")
            end
            value=value.."..."
        end
        return text(parent,id,value,x,y,w,size,color,font)
    end
    local function localize(value)
        value=value or ""
        if (value:match("^[A-Z]+_") or value:sub(1,1)=="@") and Engine.Localize then return Engine.Localize(value) end
        return value
    end
    local function signature()
        return (settings.weaponSwapSource or "")..(settings.weaponSwapApplied or "")..
            tostring(settings.weaponSwapLoading)..tostring(settings.weaponSwapCount)..tostring(settings.weaponSwapCatalogCount)
    end
    local function build(content,menu)
        used={}
        local source=settings.weaponSwapSource or ""
        if menu.weaponSource~=source then
            menu.weaponSource=source;menu.weaponFilter="";menu.weaponPage=1;menu.weaponPick=nil
        end
        menu.weaponPage=menu.weaponPage or 1
        local catalog=game:gettheaterweaponcatalog(source,menu.weaponFilter or "",menu.weaponPage)
        menu.weaponPage=catalog.page or 1
        local function redraw() menu:requestCategory("weapons","WEAPON SWAPPER") end
        local function changeWeapon(direction)
            local weapons=catalog.weapons or {}
            if #weapons==0 then return end
            local selected=1
            local current=menu.weaponFilter~="" and menu.weaponFilter or source:match("^(.-)_a#") or source
            for i,w in ipairs(weapons) do if w.weapon==current then selected=i end end
            menu.weaponFilter=weapons[(selected-1+direction)%#weapons+1].weapon
            menu.weaponPage=1;menu.weaponPick=nil;redraw()
        end
        local picked
        for _,c in ipairs(catalog.items or {}) do if c.id==menu.weaponPick then picked=c end end
        if not picked then picked=(catalog.items or {})[1];menu.weaponPick=picked and picked.id end
        local busy=settings.weaponSwapLoading
        for _,e in ipairs({menu.help,menu.feedback}) do
            e:registerAnimationState("weapon_wide",{leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
                left=75,top=e==menu.help and 145 or 595,width=1044,height=10})
            e:animateToState("weapon_wide",0)
        end
        menu.help:setText("CHANGE THE POV AND WORLD GUN OR KIT. RECORDED SHOTS, AMMO AND DEMO FILE STAY UNCHANGED.")
        button(content,"weapon_back","< BACK TO EDITOR",75,174,250,function() menu:requestCategory("main","MAIN") end)
        text(content,"weapon_source_heading","1. RECORDED WEAPON",75,228,250,16,green)
        fittedText(content,"weapon_source",localize(settings.weaponSwapLabel),75,270,250,22,14,grey)
        text(content,"weapon_category",string.upper(catalog.category or ""),75,309,250,12,green)
        text(content,"weapon_scope","POV AND WORLD MODELS",75,355,250,12,grey)
        text(content,"weapon_scope_detail","Applies whenever this recorded gun appears.",75,382,250,10,grey)
        text(content,"weapon_attachment","Kits keep the recorded attachment.",75,408,250,10,grey)
        text(content,"weapon_other_gun","Other guns use their standard setup.",75,432,250,10,grey)
        button(content,"weapon_restore","RESTORE THIS GUN",75,510,250,function() request("weapon_swap_restore") end)
        button(content,"weapon_reset","RESTORE ALL GUNS",75,550,250,function() request("weapon_swap_reset") end)
        text(content,"weapon_choices_heading","2. CHOOSE GUN AND KIT",365,174,405,16,green)
        button(content,"weapon_previous_gun","<",365,210,30,function() changeWeapon(-1) end)
        fittedText(content,"weapon_filter",picked and localize(picked.label) or "No compatible weapons",405,216,325,18,12,grey)
        button(content,"weapon_next_gun",">",740,210,30,function() changeWeapon(1) end)
        for i,c in ipairs(catalog.items or {}) do
            local y=254+(i-1)*55
            local card=button(content,"weapon_card_"..i,"",365,y,405,function() menu.weaponPick=c.id;redraw() end,{height=49})
            local area=textArea(card,"weapon_text_"..i,62,3,330,43)
            fittedText(area,"weapon_name_"..i,(menu.weaponPick==c.id and "> " or "")..localize(c.kitLabel),2,3,326,17,12,
                menu.weaponPick==c.id and green or grey)
            fittedText(area,"weapon_detail_"..i,localize(c.label),2,27,326,10,9,grey)
            local hasIcon=c.icon and c.icon~="" and not game:isdefaultmaterial(c.icon)
            image(card,"weapon_icon_"..i,4,4,48,41,hasIcon and {r=1,g=1,b=1} or {r=.08,g=.12,b=.08},hasIcon and c.icon or "white")
            if not hasIcon then text(card,"weapon_fallback_"..i,"KIT",14,18,35,10,green) end
        end
        text(content,"weapon_pages",tostring(catalog.total or 0).." KITS   /   PAGE "..menu.weaponPage.." OF "..(catalog.pages or 1),365,485,405,10,green)
        local function page(direction)
            menu.weaponPage=(menu.weaponPage-1+direction)%(catalog.pages or 1)+1;menu.weaponPick=nil;redraw()
        end
        button(content,"weapon_previous","< PREVIOUS",365,510,197,function() page(-1) end)
        button(content,"weapon_next","NEXT >",573,510,197,function() page(1) end)
        button(content,"weapon_save","SAVE FOR THIS DEMO",365,550,197,function() if not settings.weaponSwapLoading then request("weapon_swap_save") end end)
        button(content,"weapon_load","LOAD SAVED SWAPS",573,550,197,function() if not settings.weaponSwapLoading then request("weapon_swap_load") end end)
        text(content,"weapon_apply_heading","3. APPLY TO REPLAY",805,174,304,16,green)
        local hasIcon=picked and picked.icon and picked.icon~="" and not game:isdefaultmaterial(picked.icon)
        image(content,"weapon_preview",805,224,304,180,hasIcon and {r=1,g=1,b=1} or {r=.08,g=.12,b=.08},hasIcon and picked.icon or "white")
        if not hasIcon then text(content,"weapon_preview_hint","PREVIEW WITH APPLY",827,306,260,17,green) end
        fittedText(content,"weapon_preview_name",picked and localize(picked.kitLabel) or "Choose a weapon",805,426,304,21,13,green)
        fittedText(content,"weapon_preview_gun",picked and localize(picked.label) or "",805,457,304,14,10,grey)
        text(content,"weapon_applied",picked and settings.weaponSwapApplied==picked.id and "APPLIED" or "",805,486,304,12,green)
        button(content,"weapon_apply",busy and "LOADING..." or "APPLY WEAPON",805,510,304,function()
            if picked and not settings.weaponSwapLoading then request("weapon_swap_apply",source.."|"..picked.id) end
        end)
        text(content,"weapon_saved_count",tostring(settings.weaponSwapCount or 0).." ACTIVE SWAPS IN THIS DEMO",805,556,304,10,grey)
        local function hideUnused(parent)
            local child=parent:getFirstChild()
            while child do
                if child.weaponState then
                    if not used[child] then
                        child.weaponState.alpha=0
                        child:registerAnimationState("weapon_hidden",child.weaponState);child:animateToState("weapon_hidden",0)
                        if not child.weaponHidden then child.weaponHidden=true;child:processEvent({name="disable"}) end
                    else hideUnused(child) end
                end
                child=child:getNextSibling()
            end
        end
        hideUnused(content);used=nil;menu.weaponSignature=signature()
    end
    Editor.registerPage("weapons",build)
    Editor.updateWeapons=function(menu)
        if menu.category=="weapons" and signature()~=menu.weaponSignature then menu:selectCategory("weapons","WEAPON SWAPPER") end
    end
end
