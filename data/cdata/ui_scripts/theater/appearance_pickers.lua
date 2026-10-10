-- Fixed rows are refreshed in place: paging/selection never destroys the
-- native button currently dispatching its mouse or controller event.
return function(Editor, button, image, settings, request)
    local function localized(s)
        if s and (s:match("^[A-Z]+_") or s:sub(1,1)=="@") then return Engine.Localize(s) end
        return s or ""
    end
    local function caption(parent,id,x,y,w,h)
        local t=LUI.UIText.new({leftAnchor=true,rightAnchor=false,topAnchor=true,bottomAnchor=false,
            left=x,top=y,width=w,height=h,font=CoD.TextSettings.FontBold110.Font,alignment=LUI.Alignment.Left,
            color={r=.82,g=.86,b=.82}})
        t.id=id;parent:addElement(t);return t
    end
    local function build(kind,content,menu)
        local camo=kind=="camos"
        local p={page=1,selected=nil,rows={},source=settings.weaponSwapSource or ""}
        menu.appearancePicker=p
        menu.help:setText(camo and "APPLY CAMO TO THE POV AND WORLD GUN. KEEP THE WEAPON, KIT AND ATTACHMENTS." or
            "LOAD A MAP'S VISION OR A CUSTOM .VISION FILE. BAKED MAP LIGHTING IS UNCHANGED.")
        p.heading=caption(content,"appearance_heading",475,175,634,18)
        p.detail=caption(content,"appearance_detail",850,350,259,14)
        p.preview=image(content,"appearance_preview",866,215,210,120,{r=1,g=1,b=1},"white")
        p.counter=caption(content,"appearance_counter",475,512,350,12)
        function p.refresh()
            if camo and p.source~=(settings.weaponSwapSource or "") then
                p.source=settings.weaponSwapSource or "";p.selected=nil
            end
            p.items=camo and game:gettheatercamos() or (settings.visionChoices or {})
            local pages=math.max(1,math.ceil(#p.items/7));p.page=math.max(1,math.min(p.page,pages))
            if not p.selected then p.selected=p.items[(p.page-1)*7+1] and p.items[(p.page-1)*7+1].id end
            p.heading:setText(camo and localized(settings.weaponSwapLabel) or "MAP ATMOSPHERE / COLOR GRADING")
            local selected
            for _,c in ipairs(p.items) do if c.id==p.selected then selected=c end end
            if not selected then p.selected=nil end
            for i,row in ipairs(p.rows) do
                row.choice=p.items[(p.page-1)*7+i]
                local label=row.choice and localized(row.choice.label) or ""
                if #label>32 then label=label:sub(1,29).."..." end
                row.textLabel:setText((row.choice and row.choice.id==p.selected and "> " or "")..label)
                row:processEvent({name=row.choice and "enable" or "disable"})
            end
            local material=selected and selected.icon
            if not material or material=="" or game:isdefaultmaterial(material) then material="white" end
            p.preview:registerAnimationState("preview",{material=RegisterMaterial(material),alpha=camo and material~="white" and 1 or 0})
            p.preview:animateToState("preview",0)
            p.detail:setText(selected and localized(selected.label) or "No installed choices available")
            p.counter:setText(#p.items.." CHOICES / PAGE "..p.page.." OF "..pages)
        end
        for i=1,7 do
            local row=button(content,"appearance_row_"..i,"",475,212+(i-1)*41,350,function()
                local c=p.rows[i].choice
                if c then p.selected=c.id;p.refresh() end
            end,{height=34})
            p.rows[i]=row
        end
        local function page(direction)
            local pages=math.max(1,math.ceil(#p.items/7));p.page=(p.page-1+direction)%pages+1;p.selected=nil;p.refresh()
        end
        button(content,"appearance_previous","< PREVIOUS",475,545,168,function() page(-1) end)
        button(content,"appearance_next","NEXT >",657,545,168,function() page(1) end)
        button(content,"appearance_apply",camo and "APPLY CAMO" or "APPLY VISION",850,402,259,function()
            if p.selected~=nil then request(camo and "camo_apply" or "vision_apply",camo and p.source.."|"..p.selected or p.selected) end
        end)
        button(content,"appearance_restore",camo and "RESTORE THIS CAMO" or "RESTORE MAP VISION",850,450,259,function()
            request(camo and "camo_restore" or "vision_restore")
        end)
        button(content,"appearance_refresh",camo and "RESTORE ALL CAMOS" or "REFRESH VISION FILES",850,498,259,function()
            request(camo and "camo_reset" or "vision_refresh")
        end)
        p.refresh()
    end
    Editor.registerPage("camos",function(content,menu) build("camos",content,menu) end)
    Editor.registerPage("vision",function(content,menu) build("vision",content,menu) end)
    Editor.updateAppearancePickers=function(menu)
        if (menu.category=="camos" or menu.category=="vision") and menu.appearancePicker then menu.appearancePicker.refresh() end
    end
end
