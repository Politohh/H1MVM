-- Full-width native character browser. Selections affect this replay only.
return function(Editor, createButton, createImage, settings, request)
    local green={r=.35,g=.8,b=.4}
    local grey={r=.82,g=.86,b=.82}
    local factions={"All factions","USMC","SAS / Urban","SAS / Woodland","OpFor","Spetsnaz / Urban","Spetsnaz / Woodland","Other"}
    local roles={"All classes","assault","smg","lmg","shotgun","sniper"}
    local used
    local function show(element)
        used[element]=true
        element.modelState.alpha=1
        element:registerAnimationState("model_visible",element.modelState)
        element:animateToState("model_visible",0)
        if element.modelHidden then element.modelHidden=nil;element:processEvent({name="enable"}) end
        return element
    end
    -- Optional native getChildById lookups assert on absent children. Keep our
    -- own cache, and update callbacks through a dispatcher stock buttons retain.
    local function widgets(parent)
        parent.modelWidgets=parent.modelWidgets or {}
        return parent.modelWidgets
    end
    local function button(parent,id,value,x,y,w,action,options)
        local cache=widgets(parent)
        local e=cache[id]
        if not e then
            e=createButton(parent,id,value,x,y,w,function(element,event)
                return cache[id].modelAction(element,event)
            end,options)
            cache[id]=e
        else e.textLabel:setText(value) end
        e.modelAction=action
        e.modelState={leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
            left=x,top=y,width=w,height=options and options.height or 28}
        return show(e)
    end
    local function image(parent,id,x,y,w,h,color,material)
        local cache=widgets(parent)
        local e=cache[id]
        if not e then e=createImage(parent,id,x,y,w,h,color,material);cache[id]=e end
        e.editorImageLayout={leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
            left=x,top=y,width=w,height=h,color=color,material=RegisterMaterial(material or "white")}
        e.modelState=e.editorImageLayout
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
        t.modelState={leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
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
        area.modelState={leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
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
    local function label(c)
        local value=c.label or c.model
        if value:sub(1,5)=="LOOT_" and Engine.Localize then value=Engine.Localize(value) end
        return value
    end
    local function hasPortrait(c)
        -- Catalog strings can name frontend-only or absent materials. Registering
        -- those during playback returns the checkerboard, not a usable portrait.
        -- Check on each refresh so a reused card can recover when assets change.
        return c.icon and c.icon~="" and not game:isdefaultmaterial(c.icon)
    end
    local function place(element,x,y,w,h)
        element:registerAnimationState("model_layout",{leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
            left=x,top=y,width=w,height=h})
        element:animateToState("model_layout",0)
    end
    local function signature()
        local keys={}
        for _,a in ipairs(settings.modelPlayers or {}) do keys[#keys+1]=a.key..":"..a.name..":"..tostring(a.visible) end
        local d=settings.modelDetail or {}
        return table.concat(keys,"|")..(settings.modelSelected or "")..tostring(settings.modelCatalogRevision)..
            tostring(settings.modelLoading)..tostring(settings.modelPreview)..tostring(d.head)..tostring(d.body)
    end
    local function build(content,menu)
        used={}
        menu.modelBrowser=true
        menu.wardrobeKind=menu.wardrobeKind or "look"
        menu.wardrobeCategory=menu.wardrobeCategory or "All looks"
        menu.wardrobeGroup=menu.wardrobeGroup or ""
        menu.wardrobeQuery=menu.wardrobeQuery or ""
        menu.wardrobeTeam=menu.wardrobeTeam or ""
        menu.modelPage=menu.modelPage or 1
        local function redraw()
            menu.modelDismissedMessage=settings.modelMessage
            menu:requestCategory("models","MODEL SWAPPER")
        end
        local function action(name,value)
            menu.modelDismissedMessage=nil
            return request(name,value)
        end
        local function browse(kind)
            menu.wardrobeKind=kind or menu.wardrobeKind
            menu.wardrobeGroup="";menu.wardrobeTeam="";menu.wardrobeLookName=nil;menu.wardrobePane=nil;menu.modelPage=1;redraw()
        end
        local function pane(name)
            menu.wardrobePane=menu.wardrobePane~=name and name or nil
            menu.modelPage=1;redraw()
        end
        local busy=settings.modelLoading
        local selected=settings.modelSelected or ""
        local players=settings.modelPlayers or {}
        local detail=settings.modelDetail or {}
        local function safeButton(id,value,x,y,w,fn,enabled)
            local b=button(content,id,value,x,y,w,function(...)
                if enabled then return fn(...) end
            end)
            b:processEvent({name=enabled and "enable" or "disable"})
            return b
        end
        place(menu.help,75,145,1034,12)
        place(menu.feedback,720,480,389,10)
        menu.help:setText("CHOOSE A HEAD, THEN A TEAM AND CLASS TO PREVIEW. KEEP CHANGES WHEN YOU LIKE IT.")
        button(content,"model_player_selector","PLAYER: "..(detail.name or "Choose player").."  >",75,177,440,function() pane("players") end)
        button(content,"model_manage","MANAGE SWAPS",949,177,160,function() pane("manage") end)
        button(content,"model_back","< EDITOR",720,177,150,function() menu:requestCategory("main","MAIN") end)
        button(content,"model_looks",menu.wardrobeKind=="look" and "> LOOKS" or "LOOKS",75,215,278,function() browse("look") end)
        button(content,"model_customize",menu.wardrobeKind~="look" and "> CUSTOMIZE" or "CUSTOMIZE",367,215,278,function() pane("customize") end)
        local catalog=game:gettheaterwardrobe(menu.wardrobeKind,menu.wardrobeCategory,menu.wardrobeGroup,menu.modelPage,menu.wardrobeQuery,menu.wardrobeTeam)
        button(content,"model_category",menu.wardrobeCategory=="All looks" and "ALL COLLECTIONS  >" or menu.wardrobeCategory.."  >",75,253,230,function() pane("categories") end)
        button(content,"model_search",menu.wardrobeQuery=="" and "SEARCH..." or "SEARCH: "..menu.wardrobeQuery,317,253,258,function(_,event)
            menu.wardrobeSearching=true
            Engine.OpenScreenKeyboard(event and event.controller or 0,"Search looks",menu.wardrobeQuery,40,false,false)
        end)
        button(content,"model_clear_search","X",585,253,60,function() menu.wardrobeQuery="";browse() end)
        if not menu.wardrobeSearchHandler then
            menu.wardrobeSearchHandler=true
            menu:registerEventHandler("text_input_complete",function(_,event)
                if not menu.wardrobeSearching then return end
                menu.wardrobeSearching=nil
                if event.text then menu.wardrobeQuery=event.text;browse() end
            end)
        end
        local choices={}
        local heading="CHOOSE A LOOK"
        local pages=1
        local function option(title,subtitle,fn,icon)
            choices[#choices+1]={label=title,subtitle=subtitle,choose=fn,icon=icon}
        end
        if menu.wardrobePane=="players" then
            heading="CHOOSE PLAYER"
            for _,a in ipairs(players) do
                option(a.name~="" and a.name or "Player "..a.slot,a.visible and "In scene" or "Out of view at this time",function()
                    action("model_select",a.key);menu.wardrobePane=nil;redraw()
                end)
            end
        elseif menu.wardrobePane=="categories" then
            heading="CHOOSE COLLECTION"
            for _,category in ipairs(catalog.categories or {}) do
                local title=category:gsub("LootDrop","Collection ")
                option(title,"Browse looks",function() menu.wardrobeCategory=category;browse() end)
            end
        elseif menu.wardrobePane=="customize" then
            heading="CUSTOMIZE INDIVIDUAL PARTS"
            option("Head only","Keep the current uniform",function() browse("head") end)
            option("Body only","Keep the current head",function() browse("body") end)
        elseif menu.wardrobePane=="manage" then
            heading="MANAGE SWAPS"
            option("Save swaps","Save kept changes for this demo",function() if not busy and not settings.modelPreview then action("model_save") end end)
            option("Load swaps","Load this demo's saved changes",function() if not busy and not settings.modelPreview then action("model_load") end end)
            option("Restore player","Restore the recorded head and uniform",function() if not busy then action("model_restore") end end)
        else
            local stage=catalog.stage or "looks"
            heading=stage=="teams" and "CHOOSE TEAM" or stage=="classes" and "CHOOSE CLASS" or stage=="variants" and "CHOOSE A VARIANT" or "CHOOSE A LOOK"
            if stage=="teams" then heading=(menu.wardrobeLookName or "Head").."  /  TEAM"
            elseif stage=="classes" then heading=(menu.wardrobeLookName or "Head").."  /  "..menu.wardrobeTeam.."  /  CLASS" end
            pages=catalog.pages or 1
            menu.modelPage=catalog.page or 1
            for _,c in ipairs(catalog.items or {}) do
                local title=label(c)
                local sub="Choose team and class"
                if stage=="teams" then title=c.team;sub="Choose class"
                elseif stage=="classes" then title=string.upper(c.role);sub=menu.wardrobeTeam
                elseif stage=="variants" then sub=c.faction.." / "..string.upper(c.role)
                elseif menu.wardrobeKind=="body" then sub=tostring(c.variants or 1).." variants" end
                option(title,sub,function()
                    if stage=="looks" then
                        menu.wardrobeLookPage=menu.modelPage
                        menu.wardrobeLookName=label(c);menu.wardrobeGroup=c.group;menu.wardrobeTeam="";menu.modelPage=1
                    elseif stage=="teams" then
                        menu.wardrobeTeamPage=menu.modelPage
                        menu.wardrobeTeam=c.team;menu.modelPage=1
                    else
                        if settings.modelLoading or selected=="" then return end
                        menu.wardrobePreviewName=(menu.wardrobeLookName or label(c)).." / "..c.faction.." / "..string.upper(c.role)
                        action("model_preview_"..menu.wardrobeKind,tostring(c.id))
                    end
                    redraw()
                end,c.icon)
            end
        end
        if menu.wardrobePane then pages=math.max(1,math.ceil(#choices/8));menu.modelPage=math.max(1,math.min(menu.modelPage,pages)) end
        fittedText(content,"model_heading",heading,75,293,425,12,10,green)
        menu.wardrobeBack=function()
            if menu.wardrobePane then menu.wardrobePane=nil;menu.modelPage=1
            elseif menu.wardrobeTeam~="" then menu.wardrobeTeam="";menu.modelPage=menu.wardrobeTeamPage or 1
            else menu.wardrobeGroup="";menu.wardrobeLookName=nil;menu.modelPage=menu.wardrobeLookPage or 1 end
            redraw()
        end
        if menu.wardrobeGroup~="" or menu.wardrobePane then
            local backLabel=menu.wardrobePane and "< BACK" or menu.wardrobeTeam~="" and "< TEAMS" or "< LOOKS"
            button(content,"model_browse_back",backLabel,529,287,116,menu.wardrobeBack)
        end
        local first=menu.wardrobePane and (menu.modelPage-1)*8+1 or 1
        for i=first,math.min(first+7,#choices) do
            local c=choices[i]
            local slot=i-first
            local card=button(content,"model_card_"..(slot+1),"",75+(slot%2)*292,322+math.floor(slot/2)*55,278,c.choose,{height=49})
            local portrait=hasPortrait(c)
            if portrait then image(card,"model_thumbnail",5,5,39,39,{r=1,g=1,b=1},c.icon) end
            local inset=portrait and 51 or 10
            local copy=textArea(card,"model_copy",inset,3,267-inset,43)
            fittedText(copy,"model_name",c.label,1,2,264-inset,16,12,grey,CoD.TextSettings.TitleFontTiny.Font)
            fittedText(copy,"model_variant",c.subtitle,1,27,264-inset,10,9)
        end
        if #choices==0 then text(content,"model_empty","No looks found. Try another search or collection.",85,370,550,12) end
        text(content,"model_count",tostring(menu.wardrobePane and #choices or catalog.total or 0)..(menu.wardrobePane and " OPTIONS" or catalog.stage=="teams" and " TEAMS" or catalog.stage=="classes" and " CLASSES" or menu.wardrobeGroup=="" and " LOOKS" or " VARIANTS").."   /   "..menu.modelPage.." OF "..pages,75,562,330,10,grey)
        button(content,"model_previous","<",457,550,88,function() menu.modelPage=(menu.modelPage-2)%pages+1;redraw() end)
        button(content,"model_next",">",557,550,88,function() menu.modelPage=menu.modelPage%pages+1;redraw() end)
        text(content,"model_preview_heading","LIVE PREVIEW",720,230,389,15,green)
        fittedText(content,"model_preview_player",detail.name or "Choose a player",720,255,389,17,12,grey)
        fittedText(content,"model_preview_name",settings.modelPreview and (menu.wardrobePreviewName or "Custom look") or "Your current look",720,436,389,17,12,green)
        text(content,"model_preview_note",menu.wardrobeKind=="look" and "Headgear + matching faction / class uniform" or "Customize changes only the selected part",720,461,389,10,grey)
        safeButton("model_focus","FRAME PLAYER",720,513,389,function() action("model_focus") end,selected~="")
        safeButton("model_keep",busy and "LOADING..." or "KEEP CHANGES",720,556,189,function() action("model_keep") end,settings.modelPreview and not busy)
        safeButton("model_cancel","CANCEL",920,556,189,function() action("model_cancel") end,settings.modelPreview or busy)
        local function hideUnused(parent)
            local child=parent:getFirstChild()
            while child do
                if child.modelState then
                    if not used[child] then
                        child.modelState.alpha=0
                        child:registerAnimationState("model_hidden",child.modelState);child:animateToState("model_hidden",0)
                        if not child.modelHidden then child.modelHidden=true;child:processEvent({name="disable"}) end
                    else hideUnused(child) end
                end
                child=child:getNextSibling()
            end
        end
        hideUnused(content)
        used=nil -- Do not retain a closed browser tree through this module.
        menu.modelSignature=signature()
    end
    Editor.registerPage("models",build)
    Editor.updateModels=function(menu)
        if menu.category=="models" and signature()~=menu.modelSignature then menu:selectCategory("models","MODEL SWAPPER") end
    end
end
