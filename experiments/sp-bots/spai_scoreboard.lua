-- Original adapter for the stock scoreboard. Never supply agent IDs to a
-- native player lookup. All bot state comes from received map configstrings.
local board_api = LUI and LUI.mp_hud and LUI.mp_hud.Scoreboard
if not board_api or not board_api.UpdateTeam or not board_api.AllocateRow
    or not board_api.UpdatePlayerStatRow or not game.getspaiscoreboard then return end
-- Retail HKS seals module tables against adding fields through __newindex.
-- This private sentinel must not go through the native module declaration path.
if rawget(board_api, "spai_rows_installed") then return end
rawset(board_api, "spai_rows_installed", true)

local update_team = board_api.UpdateTeam
local update_row = board_api.UpdatePlayerStatRow
local count_players = Game.GetNumPlayersOnTeam
local player_at_rank = GameX.GetPlayerScoreInfoAtRankForGameMode
local contexts = {}
local unpack_values = unpack or table.unpack

local function fit_population(board)
    local scale=math.min(1,18/math.max(18,board.maxNumActivePlayers))
    if scale<1 or board.spai_fit_active then
        board:registerAnimationState("spai_fit",{scale=scale})
        board:animateToState("spai_fit")
    end
    board.spai_fit_active=scale<1
end

local function merged_rows(team, mode)
    if Engine.InFrontend() or Engine.GetDvarBool("demo_cod4PlaybackHud") then return nil end
    local data = game:getspaiscoreboard()
    if type(data) ~= "table" or data.valid ~= true or type(data.rows) ~= "table" then return nil end
    local agents = {}
    local teams = {[0]=Teams.free, [1]=Teams.axis, [2]=Teams.allies}
    for _, agent in ipairs(data.rows) do
        if teams[agent.team] == team then agents[#agents+1] = agent end
    end
    if #agents == 0 then return nil end
    local rows = {}
    local count = count_players(team)
    if type(count) ~= "number" or count < 0 or count > 18 then return nil end
    for rank=1,count do
        local row = player_at_rank(team, rank, mode)
        if row then rows[#rows+1] = row end
    end
    for _, agent in ipairs(agents) do
        rows[#rows+1] = {
            name=string.format("SP Bot %02d",agent.entity-17),
            client=-100-agent.entity, spai=true, rank=0,
            rankIcon=RegisterMaterial("white"), isDead=not agent.alive,
            score=agent.score, kills=agent.kills, deaths=agent.deaths,
            assists=agent.assists or 0, extrascore0=0, extrascore1=0
        }
    end
    table.sort(rows,function(a,b)
        if a.score ~= b.score then return a.score > b.score end
        if a.kills ~= b.kills then return a.kills > b.kills end
        if a.deaths ~= b.deaths then return a.deaths < b.deaths end
        return a.client < b.client
    end)
    return rows
end

local function scoped_count(team,...)
    local context=contexts[#contexts]
    if context and context.rows and context.team==team then return #context.rows end
    return count_players(team,...)
end

local function scoped_rank(team,rank,mode,...)
    local context=contexts[#contexts]
    if context and context.rows and context.team==team and context.mode==mode then
        return context.rows[rank]
    end
    return player_at_rank(team,rank,mode,...)
end

board_api.UpdatePlayerStatRow=function(board,rank,row,...)
    local index=board.unusedRowIndex
    update_row(board,rank,row,...)
    if row.spai then
        -- Agents have no player progression rank. Do not display a fake rank.
        local widget=board.rowBank[index]
        widget.rankNum:setText("")
        widget.rankIcon:setAlpha(0)
    end
end

board_api.UpdateTeam=function(board,team,...)
    local mode=GameX.GetGameMode()
    local rows=merged_rows(team,mode)
    if not rows and #contexts==0 then
        local result={update_team(board,team,...)}
        if board.spai_fit_active then fit_population(board) end
        return unpack_values(result)
    end
    if rows then
        local needed=board.unusedRowIndex-1+#rows
        if needed>42 then error("SP Bots scoreboard exceeds native participant capacity") end
        for index=#board.rowBank+1,needed do
            local widget=board_api.AllocateRow(index)
            board:addElement(widget)
            board.rowBank[index]=widget
        end
    end
    local saved_count,saved_rank=Game.GetNumPlayersOnTeam,GameX.GetPlayerScoreInfoAtRankForGameMode
    contexts[#contexts+1]={team=team,mode=mode,rows=rows}
    Game.GetNumPlayersOnTeam=scoped_count
    GameX.GetPlayerScoreInfoAtRankForGameMode=scoped_rank
    local result={pcall(update_team,board,team,...)}
    Game.GetNumPlayersOnTeam=saved_count
    GameX.GetPlayerScoreInfoAtRankForGameMode=saved_rank
    contexts[#contexts]=nil
    if not result[1] then error(result[2]) end
    -- Use the native layout, fitting oversized mixed populations on screen.
    fit_population(board)
    return unpack_values(result,2)
end
