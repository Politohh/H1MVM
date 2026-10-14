// Original match population controller. Opt-in research integration; the menu
// and native pre-map slot reservation will feed these settings in the product.
// Called during script main, while native model precaching is still legal.
spai_precache_match_characters()
{
    level.spai_match_looks=[];
    teams=[];teams[0]="allies";teams[1]="axis";
    environment=getMapCustom("environment");
    foreach(team in teams)
    {
        // Use the native loadout builder: the bare name omits the furniture kit.
        weapon=maps\mp\gametypes\_class::buildweaponname("h1_m16","none","base",0,0,0);
        precacheItem(weapon);
        look=spaiCharacterFor(environment,team,weaponClass(weapon));
        if(!isDefined(look))
        {spaiLog("[SPAI] FAILED character selection environment="+environment+" team="+team+" weapon="+weapon);return false;}
        precacheModel(look["body"]);precacheModel(look["head"]);
        look["weapon"]=weapon;level.spai_match_looks[team]=look;
    }
    manifest="";
    foreach(team in teams)
    {
        look=level.spai_match_looks[team];
        if(manifest!="")manifest+=",";
        manifest+=look["body"]+","+look["head"];
    }
    if(!spaiPublishCharacters(manifest))
    {spaiLog("[SPAI] FAILED character manifest publication");return false;}
    return true;
}

spai_prepare_match_characters()
{
    if(!isDefined(level.spai_match_looks))
    {spaiLog("[SPAI] FAILED match characters were not precached");return false;}
    foreach(look in level.spai_match_looks)
    {
        models=[];models[0]=look["body"];models[1]=look["head"];
        foreach(model in models)
        {
            if(!spaiTestCharacterZone(model))
            {spaiLog("[SPAI] FAILED match character ownership request "+model);return false;}
            began=getTime();
            while(!spaiCharacterReady(model) && getTime()-began<15000)wait 0.05;
            if(!spaiCharacterReady(model))
            {spaiLog("[SPAI] FAILED match character readiness "+model);return false;}
        }
        if(!spaiPrepareRig(look["body"]))return false;
    }
    return true;
}

spai_match_start()
{
    level endon("game_ended");
    level.spai_match_ready=false;
    level.spai_match_slots=[];
    // Custom script main can run before the stock gametype initializes level.
    wait 2;
    spaiLog("[SPAI] match initialization agents="+isDefined(level.agentarray)+" spawn="+isDefined(level.getspawnpoint)+" enabled="+getDvarInt("spai_enabled"));
    while(!isDefined(level.agentarray) || !isDefined(level.getspawnpoint))wait 0.05;
    while(!getDvarInt("spai_enabled"))wait 0.25;
    // A client can already have the complete world model from native weapon
    // streaming. Do not reload that fastfile while its model is being used.
    // Renderer-free servers still request the installed zone when needed.
    if(!isDefined(spaiWeaponFlashOffset(getWeaponModel("h1_m16_mp"),getWeaponFlashTagName("h1_m16_mp"))))
    {
        if(!spaiTestWeaponZone("mp_wm_m16_base_tr"))return;
        spaiLog("[SPAI] match weapon metadata requested");
    }
    else spaiLog("[SPAI] match weapon metadata already resident");
    began=getTime();
    while(!isDefined(spaiWeaponFlashOffset(getWeaponModel("h1_m16_mp"),getWeaponFlashTagName("h1_m16_mp"))) && getTime()-began<20000)wait 0.1;
    if(!isDefined(spaiWeaponFlashOffset(getWeaponModel("h1_m16_mp"),getWeaponFlashTagName("h1_m16_mp"))))
    {spaiLog("[SPAI] match unavailable: weapon metadata missing");return;}
    if(!spai_prepare_animation_set() || !spai_prepare_match_characters())return;
    init_behavior_level();
    // Map-authored navigation provides a search area even before an opponent
    // has been seen. This does not read hidden enemy positions.
    center=(0,0,0);node_count=0;
    foreach(node in getAllNodes())
        if(isDefined(node.origin)){center+=node.origin;node_count++;}
    if(node_count>0)level.spai_patrol_center=center/float(node_count);
    level.spai_match_ready=true;
    while(level.spai_population_active)
    {
        spai_match_reconcile();
        wait 0.25;
    }
}

// Shared immutable schema used by live matches and fresh-process replay.
// Replay prepares it synchronously before snapshots; it does not start AI.
spai_prepare_animation_set()
{
    schema=spaiAnimationSchema();
    if(schema!=1 && schema!=2 && schema!=3 && schema!=4 && schema!=5 && schema!=6 && schema!=7 && schema!=8){spaiLog("[SPAI] FAILED unsupported animation schema");return false;}
    level.spai_mantle_animation=undefined;
    level.spai_death_animation=undefined;
    level.spai_locomotion=false;
    level.spai_death_variety=false;
    level.spai_reaction_variety=false;
    for(i=1;i<=6;i++)if(!spaiCampaign(i,i-1))return false;
    if(!spaiCampaign(9,6) || !spaiCampaign(10,7) || !spaiCampaign(11,8) || !spaiCampaign(12,9))return false;
    if(schema==1)
        return spaiPrepare(%mp_stand_idle,%pb_stand_shoot_walk_forward,%mp_crouch_idle,%mp_stand_reload,%mp_crouch_reload,%mp_stand_grenade_throw,%mp_prone_idle_iw6,%mp_stand_ads_aim_5,%mp_pistol_stand_reload,%mp_stand_grenade_pull_throwback);
    if(!spaiCampaign(13,10))return false;
    if(schema>=4)
    {
        if(!spaiCampaign(16,11))return false;
        if(schema>=5)
        {
            for(i=17;i<=23;i++)if(!spaiCampaign(i,i-5))return false;
            if(schema>=6)
            {
                for(i=24;i<=28;i++)if(!spaiCampaign(i,i-5))return false;
                if(schema>=8)
                {
                    for(i=29;i<=38;i++)if(!spaiCampaign(i,i-5))return false;
            ready=spaiPrepare(%mp_stand_idle,%pb_stand_shoot_walk_forward,%mp_crouch_idle,%mp_stand_reload,%mp_crouch_reload,%mp_stand_grenade_throw,%mp_prone_idle_iw6,%mp_stand_ads_aim_5,%mp_pistol_stand_reload,%mp_stand_grenade_pull_throwback,%mp_hold_standjump_land,%mp_prone_reload_iw6,%mp_hold_run,%pb_sprint_assault,%mp_crouch_f,%pb_stand_shoot_walk_back,%pb_stand_shoot_walk_left,%pb_stand_shoot_walk_right,%mp_pistol_stand_walk_f,%mp_pistol_stand_idle,%mp_pistol_crouch_idle,%mp_pistol_prone_idle,%mp_pistol_crouch_reload,%mp_pistol_prone_reload,%mp_pistol_stand_walk_b,%mp_pistol_stand_walk_l,%mp_pistol_crouch_walk_f,%mp_pistol_stand_run_f,%mp_pistol_stand_run_b,%mp_pistol_crouch_walk_l,%mp_pistol_crouch_walk_r,%mp_pistol_stand_run_l,%mp_pistol_stand_run_r,%mp_pistol_stand_walk_r);
                    level.spai_reaction_variety=ready;
                }
                else
            ready=spaiPrepare(%mp_stand_idle,%pb_stand_shoot_walk_forward,%mp_crouch_idle,%mp_stand_reload,%mp_crouch_reload,%mp_stand_grenade_throw,%mp_prone_idle_iw6,%mp_stand_ads_aim_5,%mp_pistol_stand_reload,%mp_stand_grenade_pull_throwback,%mp_hold_standjump_land,%mp_prone_reload_iw6,%mp_hold_run,%pb_sprint_assault,%mp_crouch_f,%pb_stand_shoot_walk_back,%pb_stand_shoot_walk_left,%pb_stand_shoot_walk_right,%mp_pistol_stand_walk_f,%mp_pistol_stand_idle,%mp_pistol_crouch_idle,%mp_pistol_prone_idle,%mp_pistol_crouch_reload,%mp_pistol_prone_reload);
                level.spai_death_variety=ready;
            }
            else
            ready=spaiPrepare(%mp_stand_idle,%pb_stand_shoot_walk_forward,%mp_crouch_idle,%mp_stand_reload,%mp_crouch_reload,%mp_stand_grenade_throw,%mp_prone_idle_iw6,%mp_stand_ads_aim_5,%mp_pistol_stand_reload,%mp_stand_grenade_pull_throwback,%mp_hold_standjump_land,%mp_prone_reload_iw6,%mp_hold_run,%pb_sprint_assault,%mp_crouch_f,%pb_stand_shoot_walk_back,%pb_stand_shoot_walk_left,%pb_stand_shoot_walk_right,%mp_pistol_stand_walk_f);
            level.spai_locomotion=ready;
        }
        else ready=spaiPrepare(%mp_stand_idle,%pb_stand_shoot_walk_forward,%mp_crouch_idle,%mp_stand_reload,%mp_crouch_reload,%mp_stand_grenade_throw,%mp_prone_idle_iw6,%mp_stand_ads_aim_5,%mp_pistol_stand_reload,%mp_stand_grenade_pull_throwback,%mp_hold_standjump_land,%mp_prone_reload_iw6);
        if(ready)level.spai_death_animation="death";
    }
    else ready=spaiPrepare(%mp_stand_idle,%pb_stand_shoot_walk_forward,%mp_crouch_idle,%mp_stand_reload,%mp_crouch_reload,%mp_stand_grenade_throw,%mp_prone_idle_iw6,%mp_stand_ads_aim_5,%mp_pistol_stand_reload,%mp_stand_grenade_pull_throwback,%mp_hold_standjump_land);
    if(ready)level.spai_mantle_animation="mantle";
    return ready;
}

spai_match_settings()
{
    config=spawnStruct();config.enabled=getDvarInt("spai_enabled")!=0;
    config.allies=int(min(17,max(0,getDvarInt("spai_allies"))));
    config.axis=int(min(17,max(0,getDvarInt("spai_axis"))));
    config.difficulty=int(min(3,max(0,getDvarInt("spai_difficulty"))));
    config.allies_difficulty=config.difficulty;config.axis_difficulty=config.difficulty;
    if(getDvarInt("spai_from_lobby"))
    {
        // Friendly/enemy are relative to the actual listen-server host, never
        // to the first array entry or a guessed fixed faction. Wait for their
        // team selection; a spectator/disconnected host owns no bot teams.
        hostteam=undefined;
        if(isDefined(level.players))foreach(player in level.players)
            if(player isHost() && isDefined(player.pers["team"]) &&
               (player.pers["team"]=="allies" || player.pers["team"]=="axis"))
                hostteam=player.pers["team"];
        config.allies=0;config.axis=0;
        if(isDefined(hostteam))
        {
            friendly=int(min(17,max(0,getDvarInt("spai_friendly"))));
            enemy=int(min(17,max(0,getDvarInt("spai_enemy"))));
            fd=int(min(4,max(0,getDvarInt("spai_friendly_difficulty"))));
            ed=int(min(4,max(0,getDvarInt("spai_enemy_difficulty"))));
            if(hostteam=="allies")
            {config.allies=friendly;config.axis=enemy;config.allies_difficulty=fd;config.axis_difficulty=ed;}
            else
            {config.axis=friendly;config.allies=enemy;config.axis_difficulty=fd;config.allies_difficulty=ed;}
        }
    }
    if(!config.enabled){config.allies=0;config.axis=0;}
    // Distribute the allocated pool fairly, including deliberately uneven teams.
    // Never resize the native pool after map load or consume another system's AI.
    capacity=int(max(0,getMaxAgents()));
    allies=0;axis=0;
    while(allies+axis<capacity && (allies<config.allies || axis<config.axis))
    {
        if(allies<config.allies && (allies<=axis || axis>=config.axis))allies++;
        else axis++;
    }
    config.allies=allies;config.axis=axis;
    return config;
}

spai_match_count(team)
{
    count=0;
    foreach(slot in level.spai_match_slots)
        if(isDefined(slot) && slot.team==team)count++;
    return count;
}

spai_match_remove(slot)
{
    actor=slot.actor;number=actor getEntityNumber();
    // Cancel level-owned pending respawns before releasing the engine slot.
    spaiClearAim(actor);
    actor disable_respawn();
    actor notify("spai_combat_stop");
    actor unregister_match_participant();
    actor.agentname=undefined;
    actor detachAll();actor despawnAgent();
    actor maps\mp\agents\_agent_utility::deactivateAgent();
    spaiClearRig(actor);
    level.spai_population[number]=undefined;
    level.spai_match_slots[number]=undefined;
    spaiLog("[SPAI] match removed actor="+number+" team="+slot.team);
}

spai_match_reconcile()
{
    config=spai_match_settings();
    allies=0;axis=0;
    foreach(slot in level.spai_match_slots)
    {
        if(!isDefined(slot))continue;
        if(slot.team=="allies")allies++;else axis++;
        remove=(slot.team=="allies" && allies>config.allies) || (slot.team=="axis" && axis>config.axis);
        if(remove){spai_match_remove(slot);continue;}
        record=level.spai_population[slot.actor getEntityNumber()];
        requested=config.allies_difficulty;if(slot.team=="axis")requested=config.axis_difficulty;
        // Mixed chooses one profile per owned actor, not a new random value
        // every reconciliation tick. It also survives death/respawn.
        if(!isDefined(slot.requested_difficulty) || slot.requested_difficulty!=requested)
        {
            slot.requested_difficulty=requested;slot.difficulty=requested;
            if(requested==4)slot.difficulty=randomInt(4);
        }
        if(isDefined(record))record.difficulty=slot.difficulty;
        if(isAlive(slot.actor))slot.actor configure_difficulty(slot.difficulty);
    }
    // One spawn per pass bounds work and lets native deactivation finish first.
    allies=spai_match_count("allies");axis=spai_match_count("axis");
    team=undefined;
    if(allies<config.allies && (allies<=axis || axis>=config.axis))team="allies";
    else if(axis<config.axis)team="axis";
    if(isDefined(team))
    {
        if(level.agentarray.size<getMaxAgents())addAgent();
        actor=maps\mp\agents\_agent_utility::getFreeAgent("spai_tactical");
        requested=config.allies_difficulty;if(team=="axis")requested=config.axis_difficulty;
        if(isDefined(actor))spai_match_spawn(actor,team,requested);
    }
    level.spai_match_missing=config.allies+config.axis-spai_match_count("allies")-spai_match_count("axis");
}

spai_match_spawn(actor,team,requested_difficulty)
{
    difficulty=requested_difficulty;if(difficulty==4)difficulty=randomInt(4);
    actor maps\mp\agents\_agent_utility::set_agent_team(team);
    // The same native map spawn and capsule/occupancy checks used on respawn.
    selected=actor select_match_spawn();
    if(!isDefined(selected))return false;
    look=level.spai_match_looks[team];
    if(!isDefined(look) || !spaiCharacterReady(look["body"]) || !spaiCharacterReady(look["head"]) || !spaiUseRig(actor,look["body"]))return false;
    weapon=look["weapon"];
    actor spawnAgent(selected.origin,selected.angles,"default_anim");
    actor maps\mp\agents\_agent_utility::activateAgent();
    actor scrAgentSetScripted(1);
    actor setModel(look["body"]);
    actor attach(look["head"],"",true);
    actor maps\mp\agents\_agent_utility::set_agent_team(team);
    actor apply_match_spawn_health();
    actor giveWeapon(weapon);actor switchToWeaponImmediate(weapon);
    actor setWeaponAmmoClip(weapon,weaponClipSize(weapon));actor setWeaponAmmoStock(weapon,weaponClipSize(weapon)*3);
    actor scrAgentSetPhysicsMode("gravity");actor scrAgentSetAnimMode("anim deltas");
    actor setAnimState("idle",0);actor init_behavior();
    actor register_match_participant();
    actor enable_respawn(look["body"],look["head"],weapon,team,3);
    level.spai_population[actor getEntityNumber()].rig_selected=true;
    actor configure_difficulty(difficulty);
    actor.spai_roaming=true;
    level.spai_population[actor getEntityNumber()].roaming=true;
    slot=spawnStruct();slot.actor=actor;slot.team=team;
    slot.requested_difficulty=requested_difficulty;slot.difficulty=difficulty;
    level.spai_match_slots[actor getEntityNumber()]=slot;
    actor thread tactical_think();
    spaiLog("[SPAI] match spawned actor="+actor getEntityNumber()+" team="+team+" difficulty="+difficulty+" body="+look["body"]+" head="+look["head"]+" weapon="+weapon);
    return true;
}
