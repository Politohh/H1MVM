-- Execute the shipped startup coordinator with native menu/VM lifecycle seams.
local pending, inflight, frontend, top, controller, calls, hooks, timer, pc, overrides
local function element(state)
    return {state=state,children={},handlers={},addElement=function(self,e) self.children[#self.children+1]=e end,
        registerEventHandler=function(self,name,callback) self.handlers[name]=callback end,
        setText=function(self,text) self.text=text end,close=function(self) self.closed=true end}
end
game = {
    startuplobbypending=function() return pending end,
    startuplobbyloading=function() return pending or inflight end,
    beginstartuplobby=function() pending=false;inflight=true end,
    finishstartuplobby=function() pending=false;inflight=false end
}
Engine={InFrontend=function() return frontend end,IsPC=function() return pc end,
    GetFirstActiveController=function() return controller end}
CoD={TextSettings={BodyFont={Font='font'}}}
RegisterMaterial=function(name) return name end
local function nativeRequestAddMenu(element,name,controller) return 'native-result',name,controller end
LUI={
    Alignment={Center=1},UIElement={new=element},UIImage={new=element},UIText={new=element},
    onmenuopen=function(name,callback) hooks[name]=callback end,
    overrideMenuBuilder=function(name,callback) overrides[name]=callback end,
    FlowManager={GetTopOpenAndVisibleMenuName=function() return top end,
        RequestAddMenu=nativeRequestAddMenu},
    UITimer={new=function() timer=element();return timer end}
}
local function load()
    hooks={};overrides={}
    LUI.FlowManager.RequestAddMenu=nativeRequestAddMenu
    dofile('data/cdata/ui_scripts/patches/startup_lobby.lua')
end
local function menu()
    local button={id='mp_main_menu_play_online',processEvent=function(self,event)
        assert(not pending and inflight,'consume request before native navigation, retain loading')
        assert(event.name=='button_action' and event.controller==0)
        calls=calls+1
    end}
    local m=element()
    m.getFirstChild=function() return {getFirstChild=function() return button end} end
    m.registerEventHandler=function(self,name,callback) self.tick=callback end
    hooks.mp_main_menu(m)
    return m,button
end
local function reset()
    pending,inflight,frontend,top,controller,calls,pc=true,false,true,'mp_main_menu',0,0,false
    load()
end
reset()
local m,b=menu()
local entryTimer=timer
assert(m.startupLoadingView and calls==0,'loading view present before first frame; navigation deferred')
top='ShaderCacheDialog';m.tick();assert(calls==0)
top='mp_main_menu';b.disabled=true;m.tick();assert(calls==0)
b.disabled=false;controller=-1;m.tick();assert(calls==0)
controller=0;m.tick();assert(calls==1 and not m.startupLoadingView.closed)
local popup=overrides.waiting_popup(function() return element() end,{isLiveWithCancel=true})
assert(popup.startupLoadingView,'connection popup is part of startup loading')
local errorPopup=overrides.waiting_popup(function() return element() end,{})
assert(not errorPopup.startupLoadingView,'unrelated dialogs remain visible')
top='popup_connecting';m.tick();m.tick();assert(calls==1)
hooks.menu_xboxlive();assert(m.startupLoadingView.closed and popup.startupLoadingView.closed)
m.tick();assert(entryTimer.closed and not game:startuplobbyloading())
load();assert(menu().tick==nil,'no loading or auto-entry after lobby Back or VM restart')
assert(not overrides.waiting_popup(function() return element() end,{isLive=true}).startupLoadingView)
reset();m=menu();hooks.h1_theater_library();m.tick();assert(calls==0)
reset();frontend=false;load();frontend=true;load();assert(menu().tick==nil)
reset();m=menu();top='notice';for i=1,2401 do m.tick() end
assert(timer.closed and not pending and m.startupLoadingView.closed,'bounded timeout restores recovery UI')
reset();pc=true;controller=-1;m=menu();m.tick();assert(calls==1)
top='popup_connecting';m.tick();top='mp_main_menu'
for i=1,10 do m.tick() end
assert(not inflight and m.startupLoadingView.closed,'cancel restores recovery UI without retry')
reset();m=menu();m.tick();load();m=menu()
assert(m.startupLoadingView,'retain loading across frontend VM restart')
m.tick();assert(calls==1,'never duplicate initialization')
hooks.menu_xboxlive()

-- Private-lobby entry and leaving it can use different builders, or reuse a
-- native lobby menu without invoking the original onmenuopen hook again.
for _,name in ipairs({'menu_xboxlive_privatelobby','menu_xboxlive_lobby'}) do
    reset();m=menu();m.tick()
    assert(hooks[name], 'every successful lobby must complete startup')
    hooks[name]()
    top='mp_main_menu';load()
    assert(not menu().startupLoadingView and calls==1,'leaving private lobby must not restart loading')
end
reset();m=menu();m.tick()
local result,name,index=LUI.FlowManager.RequestAddMenu(m,'menu_xboxlive_privatelobby',0)
assert(result=='native-result' and name=='menu_xboxlive_privatelobby' and index==0,
    'navigation observer must preserve all native return values')
assert(not game:startuplobbyloading() and m.startupLoadingView.closed,
    'lobby navigation completes startup even when its builder hook was replaced')
top='mp_main_menu';load();assert(not menu().startupLoadingView)
reset();m=menu();m.tick();top='menu_xboxlive_privatelobby';load()
assert(not game:startuplobbyloading(),'VM restart in an existing lobby completes startup')
reset();m=menu();m.tick()
LUI.FlowManager.RequestAddMenu(m,'popup_connecting',0)
assert(game:startuplobbyloading(),'unrelated navigation must not finish startup early')

-- A connection popup can outlive the entry menu and its timer. Its own
-- loading view must clear on a visible lobby, gameplay, or a bounded timeout.
for _,destination in ipairs({'menu_xboxlive','menu_xboxlive_privatelobby','menu_xboxlive_lobby','h1_theater_library','gameplay','timeout'}) do
    reset();m=menu();m.tick()
    popup=overrides.waiting_popup(function() return element() end,{isLive=true})
    local cover=popup.startupLoadingView
    local tick=cover.handlers.h1_startup_cover
    assert(tick,'loading covers must not depend on the entry menu timer')
    top=destination
    if destination=='gameplay' then frontend=false end
    for i=1,(destination=='timeout' and 2401 or 1) do tick() end
    assert(cover.closed and not game:startuplobbyloading(),'orphaned loading view must retire')
end
print('Startup loading lifecycle tests passed')
