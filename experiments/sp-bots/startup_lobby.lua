-- Startup is one loading phase, ending at the multiplayer lobby. Native
-- sign-in/data callbacks still own initialization, errors and cancellation.
if not Engine.InFrontend() then
    game:finishstartuplobby()
    return
end

local covers = {}
local finish
local function lobbyReady(name)
    return name == "menu_xboxlive" or name == "menu_xboxlive_privatelobby"
        or name == "menu_xboxlive_lobby" or name == "h1_theater_library"
end
local function leaveStartup()
    return not Engine.InFrontend() or not game:startuplobbyloading()
        or lobbyReady(LUI.FlowManager.GetTopOpenAndVisibleMenuName())
end
local function loadingView(menu)
    if menu.startupLoadingView then return end
    -- Construct before the first render. Overscan covers the safe-area margins
    -- and the narrow native connection-dialog container at every aspect ratio.
    local cover = LUI.UIElement.new({leftAnchor=false,rightAnchor=false,topAnchor=false,bottomAnchor=false,
        left=-640,right=640,top=-360,bottom=360})
    cover.id="startup_loading_view"
    cover:addElement(LUI.UIImage.new({leftAnchor=false,rightAnchor=false,topAnchor=false,bottomAnchor=false,
        left=-10000,right=10000,top=-10000,bottom=10000,material=RegisterMaterial("white"),
        color={r=.015,g=.02,b=.015},alpha=1}))
    cover:addElement(LUI.UIImage.new({leftAnchor=false,rightAnchor=false,topAnchor=false,bottomAnchor=false,
        left=-250,right=250,top=-115,bottom=35,material=RegisterMaterial("h1_logo_mw_medium")}))
    local label=LUI.UIText.new({leftAnchor=false,rightAnchor=false,topAnchor=false,bottomAnchor=false,
        left=-350,right=350,top=75,bottom=95,font=CoD.TextSettings.BodyFont.Font,alignment=LUI.Alignment.Center,
        color={r=.65,g=.85,b=.65}})
    label:setText("LOADING MULTIPLAYER...")
    cover:addElement(label)
    menu:addElement(cover)
    menu.startupLoadingView=cover
    covers[#covers+1]=cover
    -- Connection popups can outlive the entry menu that originally owned the
    -- startup timer. Each cover must be able to retire itself during navigation.
    local ticks=0
    cover:registerEventHandler("h1_startup_cover",function()
        ticks=ticks+1
        if leaveStartup() or ticks>2400 then finish() end
    end)
    cover:addElement(LUI.UITimer.new(50,"h1_startup_cover"))
end

finish = function()
    game:finishstartuplobby()
    for _,cover in ipairs(covers) do cover:close() end
    covers={}
end
LUI.onmenuopen("menu_xboxlive", function()
    print("[theater] Startup: multiplayer lobby ready")
    finish()
end)
LUI.onmenuopen("h1_theater_library", finish)
LUI.onmenuopen("menu_xboxlive_privatelobby", finish)
LUI.onmenuopen("menu_xboxlive_lobby", finish)

-- Native frontend rebuilds may replace a registered menu builder. Observe the
-- successful lobby navigation as well, instead of relying on that builder hook.
local requestAddMenu=LUI.FlowManager.RequestAddMenu
LUI.FlowManager.RequestAddMenu=function(element,name,...)
    local result=requestAddMenu(element,name,...)
    if lobbyReady(name) then finish() end
    return result
end

-- Only reskin progress dialogs during this startup. Native timers, data-fetch
-- events and cancellation handlers continue running. Errors/EULA are untouched.
LUI.overrideMenuBuilder("waiting_popup",function(native,config,...)
    local menu=native(config,...)
    if game:startuplobbyloading() and not game:startuplobbypending() and config and
        (config.isLive or config.isLiveWithCancel) then loadingView(menu) end
    return menu
end)

local function findPlayButton(element)
    if element.id == "mp_main_menu_play_online" then return element end
    local child = element:getFirstChild()
    while child do
        local button = findPlayButton(child)
        if button then return button end
        child = child:getNextSibling()
    end
end

LUI.onmenuopen("mp_main_menu", function(menu)
    if not game:startuplobbyloading() then return end
    loadingView(menu)
    local ticks, sawWaiting, returnedTicks = 0, false, 0
    local timer = LUI.UITimer.new(50, "h1_startup_lobby")
    menu:registerEventHandler("h1_startup_lobby", function()
        ticks=ticks+1
        if leaveStartup() or ticks>2400 then
            finish();timer:close();return
        end
        local top=LUI.FlowManager.GetTopOpenAndVisibleMenuName()
        if not game:startuplobbypending() then
            if top~="mp_main_menu" then sawWaiting=true;returnedTicks=0
            elseif sawWaiting then
                returnedTicks=returnedTicks+1
                -- Native cancel/failure returned to the entry menu. End the
                -- loading view, allowing recovery instead of hiding an error.
                if returnedTicks>=10 then finish();timer:close() end
            end
            return
        end
        -- Notices and shader preparation must finish before native sign-in.
        if top ~= "mp_main_menu" then return end
        local button = findPlayButton(menu)
        if not button or button.disabled then return end
        local controller = menu.exclusiveController or Engine.GetFirstActiveController()
        if Engine.IsPC() and (controller == nil or controller < 0) then controller = 0 end
        if controller == nil or controller < 0 then return end
        game:beginstartuplobby()
        print("[theater] Startup: initializing multiplayer behind loading view")
        button:processEvent({name="button_action",controller=controller,noRefocus=true})
    end)
    menu:addElement(timer)
end)
