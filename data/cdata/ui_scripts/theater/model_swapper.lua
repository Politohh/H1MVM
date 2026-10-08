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
    local function text(parent,id,value,x,y,w,h,color)
        local cache=widgets(parent)
        local t=cache[id]
        if not t then
            t=LUI.UIText.new({leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
                left=x,top=y,width=w,height=h,font=CoD.TextSettings.FontBold110.Font,
                alignment=LUI.Alignment.Left,color=color or grey})
            t.id=id;parent:addElement(t);cache[id]=t
        end
        t.modelState={leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
            left=x,top=y,width=w,height=h,font=CoD.TextSettings.FontBold110.Font,
            alignment=LUI.Alignment.Left,color=color or grey}
        t:setText(value);return show(t)
    end
    local function label(c)
        local value=c.label or c.model
        if value:sub(1,5)=="LOOT_" and Engine.Localize then value=Engine.Localize(value) end
        return value
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
            tostring(settings.modelLoading)..tostring(d.head)..tostring(d.body)
    end
    local function build(content,menu)
        used={}
        menu.modelBrowser=true
        menu.modelKind=menu.modelKind or "head"
        menu.modelFaction=menu.modelFaction or 1
        menu.modelRole=menu.modelRole or 1
        menu.modelCollection=menu.modelCollection or "Default"
        menu.modelPage=menu.modelPage or 1
        menu.playerPage=menu.playerPage or 1
        local function redraw() menu:requestCategory("models","MODEL SWAPPER") end
        local function filterChanged() menu.modelPage=1;menu.modelPick=nil;redraw() end
        local players=settings.modelPlayers or {}
        local detail=settings.modelDetail or {}
        local selected=settings.modelSelected or ""
        local busy=settings.modelLoading
        place(menu.help,75,145,1044,12)
        place(menu.feedback,75,595,1044,10)
        menu.help:setText("CHOOSE A PLAYER, PICK AN APPEARANCE, THEN APPLY. YOUR ORIGINAL DEMO STAYS UNCHANGED.")
        button(content,"model_back","< BACK TO EDITOR",75,174,250,function() menu:requestCategory("main","MAIN") end)
        text(content,"model_players_heading","1. SELECT PLAYER",75,219,250,16,green)
        text(content,"model_catalog_heading","2. CHOOSE APPEARANCE",365,174,405,16,green)
        text(content,"model_preview_heading","3. APPLY TO PLAYER",805,174,304,16,green)
        local playerPages=math.max(1,math.ceil(#players/7))
        menu.playerPage=math.max(1,math.min(playerPages,menu.playerPage))
        for i=(menu.playerPage-1)*7+1,math.min(menu.playerPage*7,#players) do
            local a=players[i]
            local y=242+((i-1)%7)*38
            button(content,"model_player_"..((i-1)%7+1),(a.key==selected and "> " or "")..(a.name~="" and a.name or "Player "..a.slot),
                75,y,250,function() request("model_select",a.key) end)
            text(content,"model_player_info_"..((i-1)%7+1),a.visible and "IN THIS SCENE" or "OUT OF VIEW / NOT SPAWNED",82,y+29,236,9)
        end
        if #players==0 then text(content,"model_no_players","Waiting for replay players...",75,251,250,12) end
        button(content,"model_players_previous","<",75,520,35,function() menu.playerPage=(menu.playerPage-2)%playerPages+1;redraw() end)
        button(content,"model_players_next","PLAYERS  "..menu.playerPage.." / "..playerPages.."   >",116,520,209,
            function() menu.playerPage=menu.playerPage%playerPages+1;redraw() end)

        button(content,"model_heads",menu.modelKind=="head" and "> HEADS" or "HEADS",365,204,197,function()
            menu.modelKind="head";filterChanged()
        end)
        button(content,"model_bodies",menu.modelKind=="body" and "> BODIES" or "BODIES",573,204,197,function()
            menu.modelKind="body";filterChanged()
        end)
        button(content,"model_faction_previous","<",365,244,30,function()
            menu.modelFaction=(menu.modelFaction-2)%#factions+1;filterChanged()
        end)
        button(content,"model_faction","FACTION: "..factions[menu.modelFaction],401,244,333,function()
            menu.modelFaction=menu.modelFaction%#factions+1;filterChanged()
        end)
        button(content,"model_faction_next",">",740,244,30,function()
            menu.modelFaction=menu.modelFaction%#factions+1;filterChanged()
        end)
        button(content,"model_class","CLASS: "..string.upper(roles[menu.modelRole]),365,279,197,function()
            menu.modelRole=menu.modelRole%#roles+1;filterChanged()
        end)
        local catalog=game:gettheatermodelcatalog(menu.modelKind,
            menu.modelFaction==1 and "" or factions[menu.modelFaction],
            menu.modelRole==1 and "" or roles[menu.modelRole],menu.modelCollection,menu.modelPage,
            detail.head or "",detail.body or "")
        local collections=catalog.collections or {"All collections"}
        button(content,"model_collection","SET: "..menu.modelCollection,573,279,197,function()
            local current=1;for i,v in ipairs(collections) do if v==menu.modelCollection then current=i end end
            menu.modelCollection=collections[current%#collections+1];filterChanged()
        end)
        local matches=catalog.items or {}
        local pages=catalog.pages or 1
        menu.modelPage=catalog.page or 1
        local picked
        for _,c in ipairs(matches) do if c.id==menu.modelPick then picked=c end end
        if not picked then picked=matches[1];menu.modelPick=picked and picked.id end
        for i,c in ipairs(matches) do
            local y=309+(i-1)*47
            local row=button(content,"model_choice_"..i,(menu.modelPick==c.id and "> " or "")..label(c),365,y,405,
                function() menu.modelPick=c.id;redraw() end,{height=42,maxLabelWidth=330})
            row.modelChoice=c.id
            place(row.textLabel,61,5,330,15)
            local portrait=c.icon and c.icon~=""
            image(row,"model_thumbnail",6,3,36,36,portrait and {r=1,g=1,b=1} or {r=.08,g=.12,b=.09},portrait and c.icon or "white")
            text(row,"model_placeholder_text",portrait and "" or (menu.modelKind=="head" and "HEAD" or "BODY"),9,18,37,8,green)
            text(row,"model_variant",c.faction.." / "..string.upper(c.role),61,28,330,10)
        end
        if #matches==0 then text(content,"model_no_matches","No appearances match. Try CLEAR FILTERS.",365,333,405,13) end
        text(content,"model_count",(catalog.total or 0).." APPEARANCES   /   PAGE "..menu.modelPage.." OF "..pages,365,501,405,10,green)
        button(content,"model_previous","< PREVIOUS",365,520,130,function()
            menu.modelPage=(menu.modelPage-2)%pages+1;menu.modelPick=nil;redraw()
        end)
        button(content,"model_next","NEXT >",503,520,130,function()
            menu.modelPage=menu.modelPage%pages+1;menu.modelPick=nil;redraw()
        end)
        button(content,"model_clear_filters","CLEAR FILTERS",641,520,129,function()
            menu.modelFaction=1;menu.modelRole=1;menu.modelCollection="All collections";filterChanged()
        end)

        text(content,"model_target_name",detail.name or "SELECT A PLAYER",805,210,304,20,green)
        if picked then
            if picked.icon and picked.icon~="" then image(content,"model_portrait",879,243,152,152,{r=1,g=1,b=1},picked.icon)
            else
                image(content,"model_default_card",805,243,304,152,{r=.08,g=.12,b=.09})
                text(content,"model_default_name",picked.faction,826,271,263,20,green)
                text(content,"model_default_class",string.upper(picked.role),826,304,263,16)
                text(content,"model_default_hint","DEFAULT "..string.upper(menu.modelKind).." / NO PORTRAIT",826,369,263,11)
            end
            text(content,"model_candidate_name",label(picked),805,410,304,17,green)
            text(content,"model_candidate_meta",picked.faction.." / "..string.upper(picked.role).." / "..picked.collection,805,434,304,11)
        end
        text(content,"model_equipped_label","CURRENT "..string.upper(menu.modelKind),805,464,304,11,green)
        text(content,"model_equipped",label({label=catalog[menu.modelKind.."Label"] or "Recorded appearance"}),805,485,304,14)
        button(content,"model_apply",busy and "LOADING APPEARANCE..." or "APPLY "..string.upper(menu.modelKind),
            805,509,304,function()
                if picked and selected~="" and not settings.modelLoading then return request("model_"..menu.modelKind,tostring(picked.id)) end
            end)
        button(content,"model_focus","VIEW PLAYER IN SCENE",805,544,304,function() return request("model_focus") end)
        button(content,"model_restore","RESTORE THIS PLAYER",75,560,250,function()
            if not settings.modelLoading then return request("model_restore") end
        end)
        button(content,"model_save","SAVE FOR THIS DEMO",365,560,197,function()
            if not settings.modelLoading then return request("model_save") end
        end)
        button(content,"model_load","LOAD SAVED SWAPS",573,560,197,function()
            if not settings.modelLoading then return request("model_load") end
        end)
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
