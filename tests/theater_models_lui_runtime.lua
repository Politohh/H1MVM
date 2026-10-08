-- Run only with -luicheck in a paused replay: lui_testfile <absolute path>.
-- Stops after one failure; never applies/saves appearances or changes the demo.
local root=Engine.GetLuiRoot()
local function find(element,id)
    if element.id==id then return element end
    local child=element:getFirstChild()
    while child do
        local found=find(child,id)
        if found then return found end
        child=child:getNextSibling()
    end
end
local count,stage,hold=0,0,0
local menu
local baselineWidgets
local function widgetCount(element)
    local total=1
    local child=element:getFirstChild()
    while child do total=total+widgetCount(child);child=child:getNextSibling() end
    return total
end
local ids={'model_faction_next','model_faction_previous','model_faction','model_class','model_collection','model_clear_filters','model_heads','model_bodies','model_next','model_previous','model_players_next','model_players_previous'}
local function fail(err)
    stage=-1
    print('[LUI CHECK] FAILED ONCE: '..tostring(err))
    if menu then menu.pendingCategory=nil end
end
local function step()
    if stage<0 then return end
    if stage==0 then
        menu=find(root,'h1_theater_editor')
        if menu then
            local original=menu.selectCategory
            menu.selectCategory=function(self,id,label)
                local ok,err=pcall(original,self,id,label)
                if not ok then fail('builder: '..tostring(err));self.category='failed' end
            end
            menu:requestCategory('models','MODEL SWAPPER')
            stage=1;print('[LUI CHECK] OPEN MODEL SWAPPER')
        end
        return
    end
    assert(find(root,'h1_theater_editor')==menu,'editor disappeared')
    if menu.pendingCategory then return end
    assert(menu.category=='models','model page missing')
    if not baselineWidgets then baselineWidgets=widgetCount(menu) end
    if count<400 then
        local id=ids[count%#ids+1]
        local b=assert(find(menu,id),'missing '..id)
        local oldFaction=menu.modelFaction
        local oldRole=menu.modelRole
        b:processEvent({name='button_action',controller=0})
        assert(menu.pendingCategory,'callback did not request refresh: '..id)
        if id=='model_faction_next' or id=='model_faction_previous' or id=='model_faction' then
            assert(menu.modelFaction~=oldFaction,'faction callback did not change selection')
        elseif id=='model_class' then assert(menu.modelRole~=oldRole,'class callback did not change selection') end
        count=count+1
        if count%20==0 then print('[LUI CHECK] CLICKS '..count..' '..id) end
    else
        hold=hold+1
        if hold==1 then
            assert(widgetCount(menu)<=baselineWidgets+16,'native widget tree grew during refresh')
            collectgarbage('collect')
            print('[LUI CHECK] PASS 400 verified production button actions; bounded widget tree')
        end
        if hold==300 then
            stage=-1
            print('[LUI CHECK] PASS post-click GC hold')
            menu:requestCategory('main','MAIN')
        end
    end
end
root:addEventHandler('h1_theater_update',function()
    if stage<0 then return end
    local ok,err=pcall(step)
    if not ok then fail(err) end
end)
game:theateraction('editor',0)
print('[LUI CHECK] START')
