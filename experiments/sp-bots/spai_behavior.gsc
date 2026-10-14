// Original reusable SP-style behavior controller. Research integration in progress.
init_behavior_level()
{
    level notify("spai_population_reinit");
    level.spai_cover_claims=[];
    level.spai_population=[];
    level.spai_match_participants=[];
    level.spai_member_serial=0;level.spai_assist_victims=[];
    level.spai_population_active=true;
    level thread spai_publish_match_scores();
    level thread spai_watch_player_contributions();
    level thread stop_population_on_game_end();
    level thread watch_match_life_rules();
    if(!isDefined(level.spai_damage_observer_installed))
    {
        level.spai_native_modify_damage=level.modifyplayerdamage;
        level.modifyplayerdamage=::spai_modify_damage;
        level.spai_damage_observer_installed=true;
    }
    level.spai_blast_radius=spaiFragRadius();
    level.agent_funcs["spai_tactical"]=[];
    level.agent_funcs["spai_tactical"]["on_damaged"]=::damage_received;
    level.agent_funcs["spai_tactical"]["on_damaged_finished"]=::damage_finished;
    level.agent_funcs["spai_tactical"]["on_killed"]=::actor_killed;
    // Player victims reach the native gametype death callback. Keep its team
    // scoring intact while observing kills made by our registered agents.
    if(!isDefined(level.spai_native_normal_death) && isDefined(level.onnormaldeath))
    {
        level.spai_native_normal_death=level.onnormaldeath;
        level.onnormaldeath=::spai_normal_death;
    }
    if(!isDefined(level.spai_player_death_observer_installed))
    {
        level.spai_native_player_killed=level.onplayerkilled;
        level.onplayerkilled=::spai_player_killed;
        level.spai_player_death_observer_installed=true;
    }
}

spai_match_score_rows()
{
    rows="";
    // Iterate native slots for stable encoding independent of GSC array order.
    for(number=18;number<42;number++)
    {
        member=level.spai_match_participants[number];
        if(!isDefined(member))continue;
        team=0;
        if(member.team=="axis")team=1;
        else if(member.team=="allies")team=2;
        else if(member.team!="none")continue;
        rows+=number+","+team+","+int(member.alive)+","+int(member.score)+","+member.kills+","+member.deaths+","+member.assists+";";
    }
    return rows;
}

spai_publish_match_scores()
{
    level endon("spai_population_reinit");
    previous=undefined;
    while(1)
    {
        rows=spai_match_score_rows();
        if(!isDefined(previous) || rows!=previous)
        {
            if(spaiPublishScores(rows))previous=rows;
        }
        // Coalesce a frame's damage/respawn events; no unchanged network writes.
        wait 0.2;
    }
}

stop_population_on_game_end()
{
    level endon("spai_population_reinit");
    level waittill("game_ended");
    level.spai_population_active=false;
    foreach(record in level.spai_population)
    {
        if(!isDefined(record))continue;
        record.pending=false;
    }
    foreach(actor in level.agentarray)
    {
        if(isDefined(actor.agent_type) && actor.agent_type=="spai_tactical" && isAlive(actor))
        {
            actor cancel_action();
            actor notify("spai_combat_stop");
            actor.spai_reacting=0;
            actor setAnimState("idle",0);
            actor combat_state("match_ended");
        }
    }
    spaiLog("[SPAI] population stopped at game end");
}

respawn_permitted(record)
{
    if(!record.enabled || !level.spai_population_active)return false;
    lives=maps\mp\_utility::getgametypenumlives();
    if(isDefined(record.actor))
    {
        member=level.spai_match_participants[record.actor getEntityNumber()];
        if(isDefined(member))sync_match_life_rule(member);
    }
    return lives<=0 || record.spawns<lives;
}

watch_match_life_rules()
{
    level endon("spai_population_reinit");level endon("game_ended");
    while(1)
    {
        level waittill("update_numlives");
        foreach(member in level.spai_match_participants)
            if(isDefined(member))sync_match_life_rule(member);
    }
}

sync_match_life_rule(member)
{
    limit=int(max(0,maps\mp\_utility::getgametypenumlives()));
    if(limit==member.limit)return;
    remaining=0;if(limit>0)remaining=int(max(0,limit-member.spawns));
    level.livescount[member.team]=int(max(0,level.livescount[member.team]+remaining-member.remaining));
    member.limit=limit;member.remaining=remaining;
    if(isAlive(member.actor))member.actor.pers["lives"]=remaining;
    spaiLog("[SPAI] participant life rule actor="+member.actor getEntityNumber()+" limit="+limit+" remaining="+remaining);
}

init_behavior()
{
    spaiClearAim(self);
    self notify("spai_landing_reset");self.spai_airborne=false;
    self.spai_landing_anim_mode="anim deltas";
    self.spai_reloading=0;self.spai_reload_committed=0;
    self.spai_shots=0;self.spai_combat_state="idle";
    self.spai_busy=0;self.spai_reacting=0;self.spai_dead=0;
    self.spai_reactions=0;self.spai_recovered=0;self.spai_cooldown=0;
    self.spai_last_reaction=undefined;
    self.spai_cycles=0;self.spai_claimed=0;self.spai_avoiding=0;self.spai_evades=0;
    self.spai_held=0;self.spai_throwbacks=0;self.spai_throw_failed=0;self.spai_grenade_replans=0;
    self.spai_target=undefined;self.spai_last_seen=undefined;self.spai_last_seen_time=undefined;
    self.spai_action_target=undefined;self.spai_action_threat=undefined;
    self.spai_path_goal=undefined;self.spai_path_state="idle";self.spai_gait=undefined;
    self.spai_gait_time=0;
    self.spai_next_replan=0;self.spai_replans=0;
    self.spai_fire_events=0;self.spai_fire_missing_muzzle=0;self.spai_fire_eye_blocked=0;
    self.spai_fire_muzzle_blocked=0;self.spai_fire_target_blocked=0;
    self.agent_type="spai_tactical";
    self assign_match_name();
    // Standalone animation fixtures retain their original timing. Match-owned
    // actors get a persistent difficulty profile after each spawn.
    self.spai_aim_seconds=0.3;self.spai_scan_seconds=0.1;
    self.spai_sight_radius=1024;self.spai_memory_ms=15000;
    self.spai_roaming=false;self.spai_patrolling=false;self.spai_next_patrol=0;
    self.spai_patrols=0;self.spai_patrol_arrivals=0;
    self.spai_patrol_history=[];self.spai_patrol_heading=undefined;
    self.spai_failed_link=undefined;self.spai_route_recoveries=0;
    self.spai_heard=undefined;self.spai_sound_search=false;self.spai_sound_searches=0;
    self.spai_noise_sent=undefined;
    self.spai_next_open_fire=0;self.spai_burst_cooldown=650;
    self.spai_mantling=false;self.spai_vaulting=false;self.spai_pending_reaction=false;
}

assign_match_name()
{
    // The native agentname setter requires localized-string values. It owns
    // configstring registration and the agent snapshot name index. A normal
    // script-only self.name does not provide a name to receiving clients.
    number=self getEntityNumber()-18;
    if(number<0 || number>=24)return;
    names=[];
    names[0]=&"SP Bot 01";names[1]=&"SP Bot 02";
    names[2]=&"SP Bot 03";names[3]=&"SP Bot 04";
    names[4]=&"SP Bot 05";names[5]=&"SP Bot 06";
    names[6]=&"SP Bot 07";names[7]=&"SP Bot 08";
    names[8]=&"SP Bot 09";names[9]=&"SP Bot 10";
    names[10]=&"SP Bot 11";names[11]=&"SP Bot 12";
    names[12]=&"SP Bot 13";names[13]=&"SP Bot 14";
    names[14]=&"SP Bot 15";names[15]=&"SP Bot 16";
    names[16]=&"SP Bot 17";names[17]=&"SP Bot 18";
    names[18]=&"SP Bot 19";names[19]=&"SP Bot 20";
    names[20]=&"SP Bot 21";names[21]=&"SP Bot 22";
    names[22]=&"SP Bot 23";names[23]=&"SP Bot 24";
    self.agentname=names[number];
}

configure_difficulty(value)
{
    value=int(min(3,max(0,value)));
    self.spai_difficulty=value;
    aim=[];aim[0]=0.75;aim[1]=0.45;aim[2]=0.25;aim[3]=0.1;
    burst=[];burst[0]=900;burst[1]=650;burst[2]=450;burst[3]=300;
    self.spai_burst_cooldown=burst[value];
    scan=[];scan[0]=0.2;scan[1]=0.15;scan[2]=0.1;scan[3]=0.05;
    radius=[];radius[0]=1536;radius[1]=2048;radius[2]=3072;radius[3]=4096;
    memory=[];memory[0]=6000;memory[1]=10000;memory[2]=15000;memory[3]=20000;
    self.spai_aim_seconds=aim[value];self.spai_scan_seconds=scan[value];
    self.spai_sight_radius=radius[value];self.spai_memory_ms=memory[value];
    record=level.spai_population[self getEntityNumber()];
    if(isDefined(record))record.difficulty=value;
}

apply_match_spawn_health()
{
    // Private Match maxHealth is transferred to this native player tweakable.
    // Read it on every life, just as the normal MP player spawn does; do not
    // preserve a previous life's maximum or impose the prototype's 100 HP.
    health=maps\mp\gametypes\_tweakables::gettweakablevalue("player","maxhealth");
    self maps\mp\agents\_agent_common::set_agent_health(health);
}

register_match_participant()
{
    number=self getEntityNumber();
    if(isDefined(level.spai_match_participants[number]))return;
    // The engine clears entity script fields on death. Own membership and
    // remaining lives on level so respawn does not duplicate native counts.
    member=spawnStruct();member.actor=self;member.team=self.team;member.alive=false;
    member.kills=0;member.deaths=0;member.score=0;member.player_kill_lives=[];
    member.assists=0;member.assist_total=0;
    member.teamkills=0;member.total_teamkills=0;member.round_teamkills=0;
    member.teamkill_victims=[];member.teamkill_punish=false;member.teamkill_kicked=false;
    level.spai_member_serial++;member.serial=level.spai_member_serial;
    lives=maps\mp\_utility::getgametypenumlives();
    // max() returns a float; native life-counter decrement requires integers.
    member.limit=int(max(0,lives));member.remaining=member.limit;member.spawns=0;
    level.spai_match_participants[number]=member;
    self.pers["lives"]=member.remaining;
    self maps\mp\gametypes\_spawnlogic::addtocharactersarray();
    self maps\mp\gametypes\_spawnlogic::addtoparticipantsarray();
    self maps\mp\gametypes\_playerlogic::addtoteamcount();
    if(lives>0)self maps\mp\gametypes\_playerlogic::addtolivescount();
    self match_participant_spawned();
}

match_participant_spawned()
{
    member=level.spai_match_participants[self getEntityNumber()];
    if(!isDefined(member) || member.alive)return;
    sync_match_life_rule(member);
    self maps\mp\agents\_agent_utility::initplayerscriptvariables(1);
    self.agent_teamparticipant=1;
    self.sessionstate="playing";self.sessionteam=self.team;
    self.hasdied=0;self.lastspawntime=getTime();self.spawntime=getTime();
    member.alive=true;
    member.spawns++;
    level.spai_assist_victims[self getEntityNumber()]=undefined;
    if(member.remaining>0)
    {
        member.remaining--;
        self maps\mp\gametypes\_playerlogic::removefromlivescount();
    }
    self.pers["lives"]=member.remaining;
    self maps\mp\gametypes\_playerlogic::addtoalivecount();
    // Native player agents start this on each spawn. Our custom spawn path
    // bypasses spawn_agent_player, so it must start the same health rules here.
    // The alive guard above prevents duplicate startup for a registered life.
    self thread maps\mp\gametypes\_healthoverlay::playerhealthregen();
}

match_participant_killed(attacker,weapon,mod,inflictor)
{
    member=level.spai_match_participants[self getEntityNumber()];
    if(!isDefined(member) || !member.alive)return;
    sync_match_life_rule(member);
    member.alive=false;self.sessionstate="dead";self.hasdied=1;
    // Native agent death does not run the player kill-credit/obituary path.
    // The per-life membership guard above owns exactly-once dispatch; the full
    // player-death handler would also remove this actor from alive counts.
    if(level.spai_population_active)
    {
        member.deaths++;
        notice=obituary(self,attacker,weapon,mod);
        spaiLog("[SPAI_SCORE] obituary victim="+self getEntityNumber()+" weapon="+notice["weaponIndex"]+" custom="+notice["customIndex"]+" alternate="+notice["isAlternate"]);
        if(isDefined(attacker) && attacker!=self && isDefined(attacker.team) && attacker.team!="spectator" &&
           (!level.teambased || attacker.team!=self.team))
        {
            member.score=int(max(0,member.score-maps\mp\gametypes\_tweakables::gettweakablevalue("game","deathpointloss")));
            // Native persistence is player-only. Agent score records stay on
            // level across respawns, separate from native player arrays.
            if(isPlayer(attacker))
            {
                attacker maps\mp\_events::killedplayerevent(self,weapon,mod);
                if(level.killstreak_kills)
                    self maps\mp\gametypes\_damage::incrementkillstreak(attacker,inflictor,weapon,mod);
                attacker.pers["cur_death_streak"]=0;
                attacker notify("killed_enemy");
            }
            else spai_record_agent_kill(attacker);
            spai_award_agent_assists(self,attacker,member.spawns);
            // Native damage already tracks eligible hostile contributors.
            // Reuse its reward path; never count the killer as an assister.
            if(isDefined(self.attackers))
            {
                foreach(contributor in self.attackers)
                {
                    if(!isDefined(contributor) || contributor==attacker || !isPlayer(contributor))continue;
                    contributor thread maps\mp\_events::processassistevent(self);
                }
                self.attackers=[];
            }
            if(isDefined(level.onnormaldeath))[[level.onnormaldeath]](self,attacker,weapon);
        }
        else if(isDefined(attacker) && attacker!=self && isDefined(attacker.team) &&
                level.teambased && attacker.team==self.team)
        {
            // Let the same native rules own teamkill bookkeeping/penalties as
            // a player victim. This helper does not remove victim alive counts.
            if(isPlayer(attacker))self maps\mp\gametypes\_damage::handlefriendlyfiredeath(attacker);
            else spai_record_agent_teamkill(self,attacker,member.spawns,member.serial);
        }
        else if(isDefined(attacker) && attacker==self)
            member.score=int(max(0,member.score-maps\mp\gametypes\_tweakables::gettweakablevalue("game","suicidepointloss")));
    }
    // Award the killing score before elimination evaluation can end the round.
    self maps\mp\gametypes\_playerlogic::removefromalivecount();
}

spai_record_agent_kill(attacker)
{
    if(!isDefined(attacker) || isPlayer(attacker))return;
    member=level.spai_match_participants[attacker getEntityNumber()];
    if(!isDefined(member) || member.actor!=attacker)return;
    member.kills++;
    // Private Match currently exposes TDM; use its native configured kill
    // value and multiplier. Objective-event scoring requires separate routing.
    if(maps\mp\gametypes\_rank::allowplayerscore("kill"))
        member.score+=maps\mp\gametypes\_rank::getscoreinfovalue("kill")*level.objectivepointsmod;
    spaiLog("[SPAI_STATS] kill actor="+attacker getEntityNumber()+" kills="+member.kills+" deaths="+member.deaths+" score="+member.score);
}

spai_player_killed(inflictor,attacker,damage,mod,weapon,dir,hitloc,offset,deathanim,life)
{
    // Native handleworlddeath omits friendly non-player attackers. Observe the
    // later player callback without routing an agent through player persistence.
    if(isPlayer(self) && !isDefined(self.wasswitchingteamsforonplayerkilled) && !isDefined(self.nuked))
        spai_record_agent_teamkill(self,attacker,life,0);
    if(isDefined(level.spai_native_player_killed))
        [[level.spai_native_player_killed]](inflictor,attacker,damage,mod,weapon,dir,hitloc,offset,deathanim,life);
}

spai_teamkill_delay(member)
{
    if(member.teamkills<=level.maxallowedteamkills)return 0;
    return (member.teamkills-level.maxallowedteamkills)*maps\mp\gametypes\_tweakables::gettweakablevalue("team","teamkillspawndelay");
}

spai_record_agent_teamkill(victim,attacker,life,victimSerial)
{
    if(!level.spai_population_active || !level.teambased || !isDefined(victim) ||
       !isDefined(attacker) || attacker==victim || isPlayer(attacker) || !isDefined(life) ||
       !isDefined(attacker.team) || !isDefined(victim.team) || attacker.team=="spectator" || attacker.team!=victim.team)return;
    // A late callback must not count a living victim or a different membership
    // that has since reused the entity slot. Native player deaths carry lifeid;
    // our registered agents carry the level-owned spawn and membership serial.
    if(isAlive(victim))return;
    if(isPlayer(victim))
    {
        if(!isDefined(victim.lifeid) || victim.lifeid!=life)return;
    }
    else
    {
        victimMember=level.spai_match_participants[victim getEntityNumber()];
        if(!isDefined(victimMember) || victimMember.actor!=victim || victimMember.alive ||
           victimMember.spawns!=life || victimMember.serial!=victimSerial)return;
    }
    member=level.spai_match_participants[attacker getEntityNumber()];
    if(!isDefined(member) || member.actor!=attacker || member.teamkill_kicked)return;
    number=victim getEntityNumber();previous=member.teamkill_victims[number];
    if(isDefined(previous) && previous.life==life && previous.serial==victimSerial)return;
    previous=spawnStruct();previous.life=life;previous.serial=victimSerial;
    member.teamkill_victims[number]=previous;
    member.teamkills++;member.total_teamkills++;member.round_teamkills++;
    if(maps\mp\gametypes\_tweakables::gettweakablevalue("team","teamkillpointloss"))
        member.score=int(max(0,member.score-maps\mp\gametypes\_rank::getscoreinfovalue("kill")));
    delay=spai_teamkill_delay(member);
    if(delay>0)member.teamkill_punish=true;
    limit=maps\mp\gametypes\_tweakables::gettweakablevalue("team","teamkillkicklimit");
    kickActor=limit>0 && member.total_teamkills>=limit;
    if(kickActor)member.teamkill_kicked=true;
    spaiLog("[SPAI_TEAMKILL] actor="+attacker getEntityNumber()+" victim="+number+" life="+life+" total="+member.total_teamkills+" score="+member.score+" delay="+delay+" kick="+kickActor);
    if(kickActor || delay>0)level thread spai_apply_teamkill_penalty(attacker,member);
}

spai_apply_teamkill_penalty(actor,member)
{
    level endon("spai_population_reinit");level endon("game_ended");
    // Finish the original death dispatch before removing or damaging its killer.
    waittillframeend;
    if(!isDefined(actor) || level.spai_match_participants[actor getEntityNumber()]!=member)return;
    if(member.teamkill_kicked)
    {
        number=actor getEntityNumber();slot=undefined;
        if(isDefined(level.spai_match_slots))slot=level.spai_match_slots[number];
        if(isDefined(slot) && slot.actor==actor)spai_match_remove(slot);
        else
        {
            actor unregister_match_participant();
            actor.agentname=undefined;actor detachAll();actor despawnAgent();
            actor maps\mp\agents\_agent_utility::deactivateAgent();
            level.spai_population[number]=undefined;
        }
        // The configured population may fill the vacancy, as native MP bot
        // fill does. That is a new membership, never the kicked actor's respawn.
        spaiLog("[SPAI_TEAMKILL] removed actor="+number+" serial="+member.serial);
        return;
    }
    if(member.teamkill_punish && isAlive(actor))
        actor doDamage(actor.health+500000,actor.origin,actor,actor,"MOD_SUICIDE");
}

spai_normal_death(victim,attacker,life)
{
    if(isDefined(victim) && isPlayer(victim))spai_award_agent_assists(victim,attacker,life);
    spai_observe_player_kill(victim,attacker,life);
    // Always preserve the native callback and caller context, including stock
    // player kills and matches with SP Bots disabled.
    if(isDefined(level.spai_native_normal_death))
        [[level.spai_native_normal_death]](victim,attacker,life);
}

spai_watch_player_contributions()
{
    level endon("spai_population_reinit");level endon("game_ended");
    foreach(player in level.players)
    {player thread spai_watch_damage_contributions();player thread spai_watch_gunshots();}
    while(1)
    {
        level waittill("connected",player);
        player thread spai_watch_damage_contributions();
        player thread spai_watch_gunshots();
    }
}

spai_watch_gunshots()
{
    self notify("spai_gunshot_watch_restart");self endon("spai_gunshot_watch_restart");
    self endon("disconnect");level endon("spai_population_reinit");level endon("game_ended");
    self.spai_noise_sent=undefined;
    while(1)
    {
        self waittill("weapon_fired",weapon);
        if(isAlive(self) && self.sessionstate=="playing" && isDefined(weapon))
            self spai_emit_gunshot(weapon);
    }
}

spai_emit_gunshot(weapon)
{
    if(!level.spai_population_active || !isAlive(self) || !isDefined(self.team) || self.team=="spectator")return;
    if(isDefined(self.spai_noise_sent) && getTime()-self.spai_noise_sent<250)return;
    self.spai_noise_sent=getTime();
    radius=1600;if(isWeaponSilenced(weapon))radius=384;
    // Capture the shot location once. Listeners get no enemy entity reference
    // and cannot follow a hidden shooter's later movement.
    point=self.origin+(0,0,48);heard=0;
    foreach(listener in level.agentarray)
    {
        if(!isDefined(listener.agent_type) || listener.agent_type!="spai_tactical" ||
           !isAlive(listener) || !listener.spai_roaming || !listener valid_enemy(self))continue;
        d=distance(listener.origin,point);if(d>radius)continue;
        obstruction=bulletTrace(point,listener getTagOrigin("j_head"),false,self);
        audible=radius;if(obstruction["fraction"]<0.98)audible*=0.5;
        if(d>audible)continue;
        if(isDefined(listener.spai_last_seen_time) && getTime()-listener.spai_last_seen_time<listener.spai_memory_ms)continue;
        cue=spawnStruct();cue.point=point;cue.time=getTime();
        listener.spai_heard=cue;heard++;
        spaiLog("[SPAI_HEARING] listener="+listener getEntityNumber()+" source="+self getEntityNumber()+" point="+point+" distance="+d+" radius="+audible);
    }
    spaiLog("[SPAI_GUNSHOT] source="+self getEntityNumber()+" heard="+heard+" radius="+radius);
}

spai_watch_damage_contributions()
{
    self notify("spai_assist_watch_restart");self endon("spai_assist_watch_restart");
    self endon("disconnect");level endon("spai_population_reinit");level endon("game_ended");
    level.spai_assist_victims[self getEntityNumber()]=undefined;
    while(1)
    {
        // Native damage notification occurs after match-rule filtering. Never
        // turn an attempted shot or friendly/protected hit into an assist.
        self waittill("damage",damage,attacker);
        spai_track_agent_contribution(self,attacker,damage,self.lifeid);
    }
}

spai_modify_damage(victim,attacker,damage,mod,weapon,point,dir,hitloc)
{
    // The native hook runs with pre-hit health, after perk adjustment and
    // before damage completion. Preserve the existing modifier and its return.
    if(isDefined(level.spai_native_modify_damage))
        damage=[[level.spai_native_modify_damage]](victim,attacker,damage,mod,weapon,point,dir,hitloc);
    if(isDefined(victim) && isDefined(level.spai_assist_victims[victim getEntityNumber()]) &&
       (!isDefined(victim.laststand) || !victim.laststand) &&
       (victim.health==victim.maxhealth || !isDefined(victim.attackers)))
    {
        level.spai_assist_victims[victim getEntityNumber()]=undefined;
        spaiLog("[SPAI_ASSIST] native health reset victim="+victim getEntityNumber());
    }
    return damage;
}

spai_track_agent_contribution(victim,attacker,damage,life)
{
    if(!level.spai_population_active || !isDefined(life) || damage<=0 || !isAlive(victim) ||
       !isDefined(attacker) || isPlayer(attacker) || attacker==victim || !isDefined(attacker.team) ||
       attacker.team=="spectator" || (level.teambased && attacker.team==victim.team))return;
    member=level.spai_match_participants[attacker getEntityNumber()];
    if(!isDefined(member) || member.actor!=attacker)return;
    number=victim getEntityNumber();record=level.spai_assist_victims[number];
    if(!isDefined(record) || record.life!=life || record.victim!=victim)
    {
        record=spawnStruct();record.victim=victim;record.life=life;
        record.contributors=[];record.awarded=false;
        level.spai_assist_victims[number]=record;
    }
    contribution=spawnStruct();contribution.serial=member.serial;contribution.team=member.team;
    record.contributors[attacker getEntityNumber()]=contribution;
    spaiLog("[SPAI_ASSIST] damage victim="+number+" life="+life+" actor="+attacker getEntityNumber()+" applied="+damage);
}

spai_award_agent_assists(victim,killer,life)
{
    if(!level.spai_population_active || !isDefined(life) || !isDefined(killer) || killer==victim ||
       !isDefined(killer.team) || killer.team=="spectator" || (level.teambased && killer.team==victim.team))return;
    record=level.spai_assist_victims[victim getEntityNumber()];
    if(!isDefined(record) || record.victim!=victim || record.life!=life || record.awarded)return;
    record.awarded=true;
    if(isDefined(level.assists_disabled) && level.assists_disabled)return;
    for(number=18;number<42;number++)
    {
        contribution=record.contributors[number];member=level.spai_match_participants[number];
        if(!isDefined(contribution) || !isDefined(member) || member.serial!=contribution.serial ||
           member.team!=contribution.team || member.actor==killer || member.team=="spectator" ||
           (level.teambased && member.team==victim.team))continue;
        member.assist_total++;
        if(!isDefined(level.assists_count_disabled) || !level.assists_count_disabled)member.assists=member.assist_total;
        if(maps\mp\gametypes\_rank::allowplayerscore("assist"))
            member.score+=maps\mp\gametypes\_rank::getscoreinfovalue("assist")*level.objectivepointsmod;
        spaiLog("[SPAI_ASSIST] awarded actor="+number+" victim="+victim getEntityNumber()+" assists="+member.assists+" score="+member.score);
    }
}

spai_observe_player_kill(victim,attacker,life)
{
    if(level.spai_population_active && isDefined(victim) && isPlayer(victim) &&
       isDefined(attacker) && !isPlayer(attacker) && isDefined(attacker.team) &&
       attacker.team!="spectator" && (!level.teambased || attacker.team!=victim.team))
    {
        member=level.spai_match_participants[attacker getEntityNumber()];
        if(isDefined(member) && member.actor==attacker && isDefined(life))
        {
            // Native callback supplies the player's life ID, not its current
            // life after a later respawn. Slot reuse cannot duplicate credit.
            number=victim getEntityNumber();
            if(!isDefined(member.player_kill_lives[number]) || member.player_kill_lives[number]!=life)
            {
                member.player_kill_lives[number]=life;
                spai_record_agent_kill(attacker);
            }
        }
    }
}

unregister_match_participant()
{
    number=self getEntityNumber();member=level.spai_match_participants[number];
    if(!isDefined(member))return;
    self disable_respawn();
    if(isAlive(self)){self cancel_action();self notify("spai_combat_stop");}
    self maps\mp\agents\_agent_utility::set_agent_team(member.team);
    if(member.alive)
    {
        member.alive=false;
        self maps\mp\gametypes\_playerlogic::decrementalivecount(self.team);
    }
    self.pers["lives"]=member.remaining;
    self maps\mp\gametypes\_playerlogic::removeallfromlivescount();self.pers["lives"]=0;
    self maps\mp\gametypes\_playerlogic::removefromteamcount();
    self maps\mp\gametypes\_spawnlogic::removefromparticipantsarray();
    self maps\mp\gametypes\_spawnlogic::removefromcharactersarray();
    self.agent_gameparticipant=0;self.agent_teamparticipant=0;
    level.spai_match_participants[number]=undefined;self.sessionstate="dead";
    level.spai_assist_victims[number]=undefined;
}

cover_available(cover)
{
    foreach(claim in level.spai_cover_claims)
    {
        if(!isDefined(claim) || !isDefined(claim.owner) || claim.owner==self || !isAlive(claim.owner))continue;
        if(!isDefined(claim.owner.spai_claimed) || !claim.owner.spai_claimed)continue;
        if(distance2D(cover.origin,claim.origin)<64 || distance2D(cover.peek,claim.peek)<48 ||
           distance2D(cover.peek,claim.origin)<48 || distance2D(cover.origin,claim.peek)<48)return false;
    }
    return true;
}

claim_cover(cover)
{
    // No wait between the shared reservation check and insertion. A scan may
    // yield, so the caller must recheck after choosing its candidate.
    if(!self cover_available(cover))return false;
    claim=spawnStruct();claim.owner=self;claim.origin=cover.origin;claim.peek=cover.peek;
    level.spai_cover_claims[self getEntityNumber()]=claim;
    self.spai_action_cover=cover;
    // This population owns its cover reservations in GSC. Native claim-node
    // eviction can dereference a stale sentient owner after death/respawn in
    // MP (3FAAB8). Do not mix the two ownership systems.
    self.spai_claimed=1;
    spaiLog("[SPAI] cover claimed actor="+self getEntityNumber()+" origin="+cover.origin);
    return true;
}

release_cover()
{
    level.spai_cover_claims[self getEntityNumber()]=undefined;
    self.spai_claimed=0;
    self.spai_action_cover=undefined;
}

cover_protects_from(cover,threat)
{
    // Re-evaluate world geometry against the last observed enemy position.
    // A moving enemy is not itself a reason to throw away a good cover route.
    torso=bulletTrace(threat,cover.origin+(0,0,40),false,self);
    head=bulletTrace(threat,cover.origin+(0,0,60),false,self);
    if(torso["fraction"]>=0.98 || head["fraction"]>=0.98)return false;
    peek=bulletTrace(threat,cover.peek+(0,0,52),false,self);
    return peek["fraction"]>=0.99;
}

observe_target(target)
{
    if(!self valid_enemy(target))return false;
    point=target getTagOrigin("j_spineupper");
    sight=bulletTrace(self getTagOrigin("j_head"),point,true,self);
    if(!isDefined(sight["entity"]) || sight["entity"]!=target)return false;
    self.spai_target=target;self.spai_last_seen=point;self.spai_last_seen_time=getTime();
    return true;
}

// Select an authored gait for the task, not a random clip each frame. Retain
// old replay/fixture schemas and let native root motion set the actual speed.
forward_gait(remaining)
{
    if(!isDefined(level.spai_locomotion) || !level.spai_locomotion)return "move";
    if(remaining<24)return "move_cqb";
    if(self.spai_combat_state=="evade_grenade")
    {if(remaining>96)return "sprint";return "run";}
    if(self.spai_combat_state=="move_cover")
    {if(remaining>256)return "sprint";if(remaining>64)return "run";return "crouch_move";}
    if(self.spai_combat_state=="investigate" || self.spai_combat_state=="patrol" || self.spai_combat_state=="investigate_sound")
    {
        // Travel promptly between positions; reserve walking for the final
        // approach instead of spending most ordinary routes at combat-walk speed.
        // Keep authored root motion and collision checks; do not slide the rig.
        if(remaining>384)return "sprint";
        if(remaining>96)return "run";
    }
    return "move";
}

use_gait(gait)
{
    if(isDefined(self.spai_gait) && self.spai_gait==gait)return;
    self.spai_gait=gait;self.spai_gait_time=getTime();
    self setAnimState(gait,0);
    spaiLog("[SPAI_GAIT] actor="+self getEntityNumber()+" context="+self.spai_combat_state+" gait="+gait);
}

update_path_gait()
{
    if(self.spai_path_state!="move" || !isDefined(self.spai_path_goal))return;
    if(getTime()-self.spai_gait_time<350)return;
    self use_gait(self forward_gait(distance2D(self.origin,self.spai_path_goal)));
}

direct_gait(goal,facing,through_route)
{
    travel=goal-self.origin;
    heading=vectorToAngles((travel[0],travel[1],0))[1];
    remaining=length2D(travel);
    // An intermediate node is not the destination. Keep running between nodes,
    // slowing only for the last 32 units so root motion cannot skip the endpoint.
    cruise=remaining;
    if(isDefined(through_route) && through_route && remaining>32)cruise=max(cruise,256);
    gait=self forward_gait(cruise);
    if(isDefined(facing) && isDefined(level.spai_locomotion) && level.spai_locomotion)
    {
        target=facing-self.origin;target_yaw=vectorToAngles((target[0],target[1],0))[1];
        candidates=[];candidates[0]="move_cqb";candidates[1]="move_back";candidates[2]="move_left";candidates[3]="move_right";
        best=361;
        foreach(candidate in candidates)
        {
            delta=spaiAnimDelta(candidate,0,1);
            yaw=heading-vectorToAngles((delta[0],delta[1],0))[1];
            error=abs(angleClamp180(yaw-target_yaw));
            if(error<best){best=error;gait=candidate;}
        }
    }
    delta=spaiAnimDelta(gait,0,1);
    // Account for the clip's local travel direction, including back/strafe.
    yaw=heading-vectorToAngles((delta[0],delta[1],0))[1];
    self scrAgentSetOrientMode("face angle abs",(0,yaw,0));
    self use_gait(gait);
}

walk_to(goal,facing)
{
    self scrAgentSetGoalPos(goal);
    self scrAgentSetScripted(1);
    self scrAgentSetPhysicsMode("gravity");
    self set_motion_mode("anim deltas");
    self.spai_gait=undefined;
    self direct_gait(goal,facing);
    started=getTime();
    last=self.origin;
    blocked=0;
    while(distance2D(self.origin,goal)>6 && getTime()-started<8000)
    {
        self direct_gait(goal,facing);
        wait 0.05;
        if(distance2D(last,self.origin)<0.1) blocked++;
        else blocked=0;
        if(blocked>20) break;
        last=self.origin;
    }
    self setAnimState("idle",0);
    self.spai_gait=undefined;
    spaiLog("[SPAI] leg remaining="+distance2D(self.origin,goal)+" blocked="+blocked+" time="+(getTime()-started)+" health="+self.health);
    stop=self.origin;
    wait 0.1;
    spaiLog("[SPAI] idle drift="+distance2D(stop,self.origin));
    return distance2D(self.origin,goal)<12;
}

face_ground_point(point)
{
    // Agent orientation rotates the whole character, not just its aim bones.
    // Pitching from the feet to a nearby chest tipped the entire rifle pose
    // backwards. Vertical weapon aim needs its own upper-body animation layer.
    delta=point-self.origin;
    if(length2D(delta)>0.01)
        self scrAgentSetOrientMode("face angle abs",vectorToAngles((delta[0],delta[1],0)));
}

aim_at_observed_point(point)
{
    // Standing rifle clips have a neutral grip about 55 units above the feet.
    // Do not use the already-tilted head/hand as the origin: that feeds the
    // controller back into itself and oscillates at close range.
    delta=point-(self.origin+(0,0,55));
    if(length2D(delta)<1){spaiClearAim(self);return;}
    pitch=vectorToAngles(delta)[0];
    if(pitch>180)pitch-=360;
    spaiSetCombatAim(self,max(-30,min(30,pitch)));
}

fire_burst(target)
{
    self endon("spai_combat_stop");self endon("disconnect");self endon("death");
    if(!self valid_enemy(target))return false;
    if(self getWeaponAmmoClip(self getCurrentWeapon())<=0)return false;
    // A firing pose cannot manufacture sight by reaching around an edge.
    if(!self observe_target(target))return false;
    self scrAgentSetScripted(1);self set_motion_mode("anim deltas");
    self face_ground_point(self.spai_last_seen);self setAnimState("aim",0);
    began=getTime();aim_ms=int(self.spai_aim_seconds*1000);
    self aim_at_observed_point(self.spai_last_seen);
    while(getTime()-began<aim_ms)
    {
        wait min(0.05,(aim_ms-(getTime()-began))*0.001);
        if(!self observe_target(target))break;
        self face_ground_point(self.spai_last_seen);
        self aim_at_observed_point(self.spai_last_seen);
    }
    if(!self observe_target(target))
    {
        spaiClearAim(self);self setAnimState("idle",0);
        spaiLog("[SPAI_BURST] actor="+self getEntityNumber()+" result=lost_during_aim shots=0");
        return false;
    }
    shots_before=self.spai_shots;events_before=self.spai_fire_events;
    muzzle_before=self.spai_fire_muzzle_blocked;target_before=self.spai_fire_target_blocked;
    began=getTime();duration=int((spaiAnimDuration("fire")+0.15)*1000);
    self thread fire_events(target);self setAnimState("fire",0);
    visible=true;
    // Keep the authored fire notetracks, but stop promptly when the contact
    // disappears. This does not spend ammunition or fire through occlusion.
    while(getTime()-began<duration && visible)
    {
        wait 0.05;visible=self observe_target(target);
        if(visible){self face_ground_point(self.spai_last_seen);self aim_at_observed_point(self.spai_last_seen);}
    }
    spaiClearAim(self);self notify("spai_fire_stop");self setAnimState("idle",0);
    fired=self.spai_shots-shots_before;
    spaiLog("[SPAI_BURST] actor="+self getEntityNumber()+" shots="+fired+" events="+(self.spai_fire_events-events_before)+" muzzle_blocked="+(self.spai_fire_muzzle_blocked-muzzle_before)+" target_blocked="+(self.spai_fire_target_blocked-target_before)+" visible="+visible+" elapsed="+(getTime()-began));
    return fired>0;
}

fire_events(target)
{
    self endon("spai_fire_stop");
    self endon("spai_action_cancel");
    self endon("spai_combat_stop");
    self endon("disconnect");
    self endon("death");
    while(1)
    {
        self waittill("spai_anim",name);
        if(name!="fire" || !level.spai_population_active)continue;
        self.spai_fire_events++;
        if(!isDefined(target) || !isAlive(target) || target.team==self.team)continue;
        weapon=self getCurrentWeapon();
        ammo=self getWeaponAmmoClip(weapon);
        if(ammo<=0)continue;
        offset=spaiWeaponFlashOffset(getWeaponModel(weapon),getWeaponFlashTagName(weapon));
        if(!isDefined(offset)){self.spai_fire_missing_muzzle++;continue;}
        hand=self getTagOrigin("tag_weapon_right");
        angles=self getTagAngles("tag_weapon_right");
        muzzle=hand+anglesToForward(angles)*offset[0]-anglesToRight(angles)*offset[1]+anglesToUp(angles)*offset[2];
        point=target getTagOrigin("j_spineupper");
        sight=bulletTrace(self getTagOrigin("j_head"),point,true,self);
        if(!isDefined(sight["entity"]) || sight["entity"]!=target){self.spai_fire_eye_blocked++;continue;}
        clearance=bulletTrace(self.origin+(0,0,36),muzzle,false,self);
        if(clearance["fraction"]<0.99){self.spai_fire_muzzle_blocked++;continue;}
        trace=bulletTrace(muzzle,point,true,self);
        if(!isDefined(trace["entity"]) || trace["entity"]!=target){self.spai_fire_target_blocked++;continue;}
        self.spai_target=target;
        self.spai_last_seen=point;
        self.spai_last_seen_time=getTime();
        self setWeaponAmmoClip(weapon,ammo-1);
        spaiLog("[SPAI] muzzle="+muzzle+" actor="+self.origin+" target="+point);
        spaiBullet(self,weapon,muzzle,point);
        self spai_emit_gunshot(weapon);
        self.spai_shots++;
        spaiLog("[SPAI] animation actor="+self getEntityNumber()+" shot="+self.spai_shots+" ammo="+self getWeaponAmmoClip(weapon)+" time="+getTime());
    }
}


acquire_target(radius,min_dot)
{
    candidates=[];
    if(isDefined(level.players))
        for(i=0;i<level.players.size;i++)
            if(isDefined(level.players[i].sessionstate) && level.players[i].sessionstate=="playing")
                candidates[candidates.size]=level.players[i];
    if(isDefined(level.agentarray))
        for(i=0;i<level.agentarray.size;i++) candidates[candidates.size]=level.agentarray[i];
    best=undefined;best_distance=radius;
    forward=anglesToForward((0,self.angles[1],0));
    eye=self getTagOrigin("j_head");
    for(i=0;i<candidates.size;i++)
    {
        candidate=candidates[i];
        if(!isDefined(candidate) || candidate==self || !isAlive(candidate))continue;
        if(isDefined(candidate.isagent) && candidate.isagent && (!isDefined(candidate.isactive) || !candidate.isactive))continue;
        if(!isDefined(candidate.team) || candidate.team==self.team || candidate.team=="spectator")continue;
        delta=candidate.origin-self.origin;
        dist=length(delta);
        if(dist<1 || dist>best_distance)continue;
        facing=vectorDot(forward,vectorNormalize((delta[0],delta[1],0)));
        if(facing<min_dot)continue;
        point=candidate getTagOrigin("j_spineupper");
        sight=bulletTrace(eye,point,true,self);
        if(!isDefined(sight["entity"]) || sight["entity"]!=candidate)continue;
        best=candidate;best_distance=dist;
    }
    return best;
}

combat_state(value)
{
    if(self.spai_combat_state==value)return;
    spaiLog("[SPAI] combat actor="+self getEntityNumber()+" "+self.spai_combat_state+" -> "+value);
    self.spai_combat_state=value;
}

choose_cover(threat,radius)
{
    search_started=getTime();
    nodes=getAllNodes();
    best=undefined;
    bestscore=1000000;
    checked=0;occluded=0;peeks=0;blocked_lanes=0;
    for(i=0;i<nodes.size;i++)
    {
        if(!isDefined(nodes[i].origin))continue;
        p=nodes[i].origin;
        dist=distance2D(self.origin,p);
        if(dist<64 || dist>radius || abs(p[2]-self.origin[2])>32)continue;
        checked++;
        // Yield for actual nearby trace work, not every distant map node.
        // Waiting on the full catalog can let a moving contact cancel every
        // search before any usable cover is returned.
        if(checked%32==0)wait 0.01;
        torso=bulletTrace(threat,p+(0,0,40),false,self);
        head=bulletTrace(threat,p+(0,0,60),false,self);
        if(torso["fraction"]>=0.98 || head["fraction"]>=0.98)continue;
        occluded++;
        towards=vectorNormalize((threat[0]-p[0],threat[1]-p[1],0));
        side=(-towards[1],towards[0],0);
        for(sign=-1;sign<=1;sign+=2)
        {
            // A clear bullet ray is not a walkable body-width peek. Try short
            // grounded offsets, and require room to return behind the wall.
            for(offset=32;offset<=96;offset+=16)
            {
                peek=p+side*(sign*offset);
                lane=bulletTrace(p+(0,0,40),peek+(0,0,40),false,self);
                floor=bulletTrace(peek+(0,0,24),peek-(0,0,32),false,self);
                if(lane["fraction"]<0.99 || floor["fraction"]>=1)continue;
                if(abs(floor["position"][2]-p[2])>12)continue;
                peek=floor["position"];
                visible=bulletTrace(threat,peek+(0,0,52),false,self);
                if(visible["fraction"]<0.99)continue;
                score=dist+offset*0.25;
                if(score>=bestscore)continue;
                candidate=spawnStruct();candidate.node=nodes[i];candidate.origin=p;candidate.peek=peek;candidate.side=sign;
                if(!self cover_available(candidate))continue;
                if(!self walkable_link(p,peek) || !self walkable_link(peek,p))
                {blocked_lanes++;continue;}
                peeks++;
                best=candidate;bestscore=score;
                break;
            }
        }
    }
    spaiLog("[SPAI] cover search nodes="+nodes.size+" nearby="+checked+" occluded="+occluded+" peeks="+peeks+" blocked_lanes="+blocked_lanes+" elapsed="+(getTime()-search_started));
    return best;
}

state_changed(old,new)
{
    self endon("spai_motion_stop");
    self endon("spai_action_cancel");
    self endon("spai_combat_stop");
    self endon("disconnect");
    self.statechanges++;
    if(self.statechanges>30 || self.pathfailed) return;
    spaiLog("[SPAI] state "+old+" -> "+new);
    self.spai_path_state=new;self.spai_gait=undefined;
    self reset_traversal_motion();
    if(new=="idle")
    {
        self set_motion_mode("anim deltas");
        self setAnimState("idle",0);
    }
    if(new=="move")
    {
        self set_motion_mode("code_move");
        self scrAgentSetOrientMode("face motion");
        remaining=0;if(isDefined(self.spai_path_goal))remaining=distance2D(self.origin,self.spai_path_goal);
        self use_gait(self forward_gait(remaining));
    }
    if(new=="traverse")
    {
        from=self getNegotiationStartNode();
        to=self getNegotiationEndNode();
        if(!isDefined(from)||!isDefined(to))
        {
            spaiLog("[SPAI] missing traversal nodes");
            return;
        }
        spaiLog("[SPAI] nodes defined; anim="+isDefined(from.animscript)+" from="+isDefined(from.origin)+" to="+isDefined(to.origin));
        if(isDefined(from.animscript))spaiLog("[SPAI] link anim="+from.animscript);
        if(isDefined(from.origin))spaiLog("[SPAI] link start="+from.origin);
        if(isDefined(to.origin))spaiLog("[SPAI] link end="+to.origin);
        delta=to.origin-from.origin;
        hit=bulletTrace(from.origin+(0,0,32),to.origin+(0,0,32),false,self);
        spaiLog("[SPAI] link height="+abs(delta[2])+" clearance="+hit["fraction"]);
        // Only animation schemas with a dedicated mantle slot enable climbs.
        // The older ten-state replay schema retains its original vault state.
        if(isDefined(level.spai_mantle_animation) && isDefined(from.animscript) && from.animscript=="mantle_on_56")
        {
            plan=self mantle_plan(from.origin,to.origin,level.spai_mantle_animation);
            if(!isDefined(plan))
            {self reject_traversal(from.origin,to.origin,"mantle preflight");return;}
            self perform_mantle(plan);
            return;
        }
        if(isDefined(from.animscript) && from.animscript=="window_over_40")
        {
            plan=self window_vault_plan(from.origin,to.origin);
            if(!isDefined(plan))
            {self reject_traversal(from.origin,to.origin,"window vault preflight");return;}
            self perform_window_vault(plan);
            return;
        }
        // Validate the standing capsule and ground continuity for ordinary links.
        walkTarget=self walking_traversal_target(self.origin,to.origin);
        if(!isDefined(walkTarget) && self downward_link(self.origin,to.origin))walkTarget=to.origin;
        route=undefined;
        if(isDefined(walkTarget)){route=[];route[0]=walkTarget;}
        else route=self walking_detour(self.origin,to.origin);
        if(!isDefined(route))
        {
            self reject_traversal(from.origin,to.origin,"unsupported traversal");
            return;
        }
        self set_motion_mode("anim deltas");
        started=getTime();
        for(leg=0;leg<route.size;leg++)
        {
            walkTarget=route[leg];
            // Arrival has a tolerance, so the next leg must be clear from the
            // actual capsule position, not just from the planned waypoint.
            if(leg>0 && !self body_walkable_link(self.origin,walkTarget))
            {self.pathfailed=1;spaiLog("[SPAI] walking detour continuation blocked");break;}
            initial=walkTarget-self.origin;
            // A fixed six-unit radius can complete a short link without taking
            // a step. Native navigation then requests that same link forever.
            tolerance=clamp(length2D(initial)*0.25,0.5,6);
            heading=vectorNormalize((initial[0],initial[1],0));
            self.spai_gait=undefined;
            while(!self walking_link_arrived(walkTarget,heading,tolerance) && getTime()-started<4000)
            {
                // Keep authored gait speed on long links, then use the close
                // approach clip to avoid overshooting the endpoint.
                through=isDefined(self.spai_path_goal) && distance2D(self.origin,self.spai_path_goal)>128;
                self direct_gait(walkTarget,undefined,through);
                wait 0.05;
            }
            if(!self walking_link_arrived(walkTarget,heading,tolerance))
            {self.pathfailed=1;break;}
            if(length2D(initial)<12)spaiLog("[SPAI] short link progress="+length2D(initial)+" remaining="+distance2D(self.origin,walkTarget)+" origin="+self.origin);
            if(route.size>1)spaiLog("[SPAI] walking detour leg="+leg+" origin="+self.origin+" target="+walkTarget);
        }
        self setAnimState("idle",0);
        spaiLog("[SPAI] traversal remaining="+distance2D(self.origin,to.origin));
        if(distance(self.origin,to.origin)>24)self.pathfailed=1;
    }
}

walking_link_arrived(goal,heading,tolerance)
{
    remaining=goal-self.origin;
    if(abs(remaining[2])>8)return false;
    if(length2D(remaining)<=tolerance)return true;
    // A root-motion frame can cross a tiny endpoint. Accept that real step
    // only near the endpoint; never snap the actor or disable collision.
    return length2D(remaining)<=6 && vectorDot((remaining[0],remaining[1],0),heading)<=0;
}

window_vault_plan(from,to)
{
    // Only called for an authored window_over_40 navigation link. That clip
    // folds the legs over the sill but has no vertical world-root motion.
    // Native agent traversal therefore temporarily uses animation physics.
    travel=to-self.origin;delta=spaiAnimDelta("vault",0,1);
    duration=spaiAnimDuration("vault");
    if(length2D(delta)<1 || distance(self.origin,from)>24 || abs(travel[2])>16 || duration<=0 || duration>4)return undefined;
    scale=length2D(travel)/length2D(delta);
    if(scale<0.5 || scale>2.5)return undefined;
    first=self ground_trace(self.origin);last=self ground_trace(to);
    if(first["fraction"]>=1 || last["fraction"]>=1 ||
       abs(first["position"][2]-self.origin[2])>4 || abs(last["position"][2]-to[2])>4)return undefined;
    if(!self aiPhysicsTracePassed(self.origin+(0,0,64),to+(0,0,64),16,54,1) ||
       !self aiPhysicsTracePassed(to+(0,0,18),to+(0,0,19),16,54,1))return undefined;
    facing=vectorToAngles((travel[0],travel[1],0));source=vectorToAngles((delta[0],delta[1],0));
    plan=spawnStruct();plan.target=to;plan.scale=scale;plan.duration=duration;plan.yaw=facing[1]-source[1];
    return plan;
}

perform_window_vault(plan)
{
    self.spai_vaulting=true;
    self scrAgentSetOrientMode("face angle abs",(0,plan.yaw,0));
    self set_motion_mode("anim deltas");self scrAgentSetAnimScale(plan.scale,1);
    self scrAgentSetPhysicsMode("noclip");self setAnimState("vault",0);
    spaiLog("[SPAI_VAULT] started actor="+self getEntityNumber()+" origin="+self.origin+" target="+plan.target);
    wait plan.duration;
    self setAnimState("idle",0);
    remaining=distance(self.origin,plan.target);
    self reset_traversal_motion();
    if(remaining>24)self.pathfailed=1;
    spaiLog("[SPAI_VAULT] completed actor="+self getEntityNumber()+" origin="+self.origin+" remaining="+remaining+" physics="+self scrAgentGetPhysicsMode());
}

mantle_plan(from,to,animation)
{
    travel=to-self.origin;
    if(distance(self.origin,from)>24 || length2D(travel)<24 || length2D(travel)>100 || travel[2]<40 || travel[2]>66)return undefined;
    first=self ground_trace(self.origin);last=self ground_trace(to);
    if(first["fraction"]>=1 || last["fraction"]>=1 ||
       abs(first["position"][2]-self.origin[2])>4 || abs(last["position"][2]-to[2])>4)return undefined;
    // Check standing clearance above both ends and over the lip. The native
    // collision solver still owns motion throughout the animated climb.
    highStart=(self.origin[0],self.origin[1],last["position"][2]+8);
    highEnd=(to[0],to[1],highStart[2]);
    if(!self aiPhysicsTracePassed(self.origin+(0,0,18),highStart+(0,0,18),16,54,1) ||
       !self aiPhysicsTracePassed(highStart+(0,0,18),highEnd+(0,0,18),16,54,1) ||
       !self aiPhysicsTracePassed(highEnd+(0,0,18),last["position"]+(0,0,18),16,54,1))return undefined;
    delta=spaiAnimDelta(animation,0,1);
    if(length2D(delta)<1 || delta[2]<1)return undefined;
    plan=spawnStruct();plan.target=to;plan.animation=animation;
    plan.horizontal=length2D(travel)/length2D(delta);
    plan.vertical=(travel[2]+8)/delta[2];
    if(plan.horizontal<0.2 || plan.horizontal>1.0 || plan.vertical<0.8 || plan.vertical>1.5)return undefined;
    facing=vectorToAngles(travel);source=vectorToAngles(delta);
    plan.yaw=facing[1]-source[1];plan.duration=spaiAnimDuration(animation);
    if(plan.duration<=0 || plan.duration>4)return undefined;
    return plan;
}

perform_mantle(plan)
{
    self.spai_mantling=true;
    self scrAgentSetOrientMode("face angle abs",(0,plan.yaw,0));
    self set_motion_mode("anim deltas");
    self scrAgentSetAnimScale(plan.horizontal,plan.vertical);
    // Mode 1 removes gravity but preserves collision, unlike native noclip.
    self scrAgentSetPhysicsMode("no_gravity");
    spaiLog("[SPAI] mantle started origin="+self.origin+" target="+plan.target);
    self setAnimState(plan.animation,0);
    wait plan.duration;
    self reset_traversal_motion();self setAnimState("idle",0);
    wait 0.3;
    remaining=distance(self.origin,plan.target);
    if(remaining>24 || abs(self.origin[2]-plan.target[2])>8)self.pathfailed=1;
    spaiLog("[SPAI] mantle completed origin="+self.origin+" remaining="+remaining+" failed="+self.pathfailed);
}

reset_traversal_motion()
{
    wasClimbing=(isDefined(self.spai_mantling) && self.spai_mantling) || (isDefined(self.spai_vaulting) && self.spai_vaulting);
    self scrAgentSetPhysicsMode("gravity");
    self scrAgentSetAnimScale(1,1);
    self.spai_mantling=false;self.spai_vaulting=false;
    if(wasClimbing && isAlive(self))
    {
        floor=self ground_trace(self.origin);
        if(floor["fraction"]>=1 || abs(self.origin[2]-floor["position"][2])>4)
        {
            // Animated movement replaces vertical velocity every frame. After
            // an interrupted climb, native code movement must own the fall;
            // otherwise an idle clip leaves the actor sinking at 1 unit/sec.
            self.spai_airborne=true;self.spai_landing_anim_mode="anim deltas";
            self scrAgentSetAnimMode("code_move");
            self notify("spai_landing_reset");
            self thread finish_mantle_landing();
        }
    }
}

set_motion_mode(mode)
{
    if(isDefined(self.spai_airborne) && self.spai_airborne)
    {
        self.spai_landing_anim_mode=mode;
        self scrAgentSetAnimMode("code_move");
    }
    else self scrAgentSetAnimMode(mode);
}

finish_mantle_landing()
{
    self endon("spai_landing_reset");self endon("spai_dead");
    self endon("death");self endon("disconnect");
    previous=self.origin;settled=0;
    while(isAlive(self) && self.spai_airborne)
    {
        wait 0.05;
        floor=self ground_trace(self.origin);
        if(floor["fraction"]<1 && abs(self.origin[2]-floor["position"][2])<=3 && abs(self.origin[2]-previous[2])<0.2)settled++;
        else settled=0;
        previous=self.origin;
        if(settled<2)continue;
        self.spai_airborne=false;
        self scrAgentSetAnimMode(self.spai_landing_anim_mode);
        spaiLog("[SPAI] mantle gravity landing origin="+self.origin);
    }
}

walkable_link(from,to)
{
    delta=to-from;
    if(length2D(delta)>512 || abs(delta[2])>64)return 0;
    segments=int(ceil(length2D(delta)/20));
    if(segments<1)return 0;
    last=from;
    for(i=0;i<=segments;i++)
    {
        p=from+delta*(i/float(segments));
        floor=self ground_trace(p);
        if(floor["fraction"]>=1 || abs(floor["position"][2]-p[2])>18)return 0;
        current=floor["position"];
        if(abs(current[2]-last[2])>18)return 0;
        if(!self aiPhysicsTracePassed(last+(0,0,18),current+(0,0,18),16,54,1))return 0;
        last=current;
    }
    return 1;
}

walking_traversal_target(from,to)
{
    if(self walkable_link(from,to))return to;
    if(self body_walkable_link(from,to))return to;
    delta=to-from;
    if(length2D(delta)<32 || length2D(delta)>512 || abs(delta[2])>64)return undefined;
    direction=vectorNormalize((delta[0],delta[1],0));
    // Some authored nodes touch the standing capsule's wall margin. Stay on
    // the approach, within native traversal arrival tolerance, and validate
    // the entire shortened route with unchanged collision and floor checks.
    // Interior obstructions, gaps and unsupported height changes still fail.
    for(inset=4;inset<=12;inset+=4)
    {
        point=to-direction*inset;floor=self ground_trace(point);
        if(floor["fraction"]>=1)continue;
        point=floor["position"];
        if(abs(point[2]-to[2])>8 || distance(point,to)>16)continue;
        if(!self walkable_link(from,point) && !self body_walkable_link(from,point))continue;
        spaiLog("[SPAI] walking endpoint inset="+inset+" target="+point+" node="+to);
        return point;
    }
    return undefined;
}

walking_detour(from,to)
{
    delta=to-from;
    if(length2D(delta)<32 || length2D(delta)>256 || abs(delta[2])>18)return undefined;
    // A bounded local detour for an obstructed ordinary walking link. Keep
    // both legs grounded and capsule-clear; never bypass the native collider.
    direction=vectorNormalize((delta[0],delta[1],0));side=(-direction[1],direction[0],0);
    for(offset=16;offset<=64;offset+=16)
    {
        for(part=1;part<=3;part++)
        {
            for(sign=-1;sign<=1;sign+=2)
            {
                point=from+delta*(part/4.0)+side*(offset*sign);
                support=self traversal_support(point,32);
                if(!isDefined(support))continue;
                if(!self body_walkable_link(from,support) || !self body_walkable_link(support,to))continue;
                route=[];route[0]=support;route[1]=to;
                spaiLog("[SPAI] walking detour from="+from+" via="+support+" to="+to);
                return route;
            }
        }
    }
    return undefined;
}

traversal_support(point,depth)
{
    // Use the full standing body, as native scripted-agent ground placement
    // does. A centre ray can fall between stair treads while the body remains
    // supported higher up. Reject no-hit and start-solid trace results.
    upper=point+(0,0,24);lower=point-(0,0,depth);
    support=self aiPhysicsTrace(upper,lower,16,72,1);
    if(support[2]>=upper[2]-0.1 || support[2]<=lower[2]+0.1)return undefined;
    return support;
}

body_walkable_link(from,to)
{
    delta=to-from;
    if(length2D(delta)<1 || length2D(delta)>512 || abs(delta[2])>64)return false;
    previous=self traversal_support(from,32);
    if(!isDefined(previous) || abs(previous[2]-from[2])>4)return false;
    segments=int(ceil(length2D(delta)/8));
    for(i=1;i<=segments;i++)
    {
        p=from+delta*(i/float(segments));
        // Follow support locally rather than an imaginary diagonal between
        // endpoint heights. Real stairs can begin late along a level approach.
        current=self traversal_support((p[0],p[1],previous[2]),32);
        if(!isDefined(current) || abs(current[2]-previous[2])>18)return false;
        if(!self aiPhysicsTracePassed(previous+(0,0,18),current+(0,0,18),16,54,1))return false;
        previous=current;
    }
    if(abs(previous[2]-to[2])>8)return false;
    spaiLog("[SPAI] validated standing route from="+from+" to="+to);
    return true;
}

downward_link(from,to)
{
    // A walking capsule cannot sweep diagonally through the lip of a drop.
    // Validate the space above it and the landing separately; native gravity
    // and collision still determine the actual motion. This is only used for
    // authored traversal links, never as a shortcut for cover/grenade movement.
    delta=to-from;
    if(length2D(delta)<16 || length2D(delta)>256 || delta[2]>-18 || delta[2]<-48)return false;
    high=self traversal_support(from,32);low=self traversal_support(to,32);
    if(!isDefined(high) || !isDefined(low))return false;
    if(abs(high[2]-from[2])>4 || abs(low[2]-to[2])>4)return false;
    above=(low[0],low[1],high[2]);
    if(!self aiPhysicsTracePassed(high+(0,0,18),above+(0,0,18),16,54,1))return false;
    if(!self aiPhysicsTracePassed(above+(0,0,18),low+(0,0,18),16,54,1))return false;
    segments=int(ceil(length2D(delta)/8));previous=high[2];
    for(i=0;i<=segments;i++)
    {
        p=from+delta*(i/float(segments));
        support=self traversal_support((p[0],p[1],high[2]),64);
        if(!isDefined(support))return false;
        z=support[2];
        if(z>previous+2 || z<low[2]-4 || z>high[2]+2)return false;
        previous=z;
    }
    spaiLog("[SPAI] validated gravity drop from="+from+" to="+to);
    return true;
}


cancel_action()
{
    spaiClearAim(self);
    self.spai_pending_reaction=false;
    self.spai_path_goal=undefined;self.spai_path_state="idle";self.spai_gait=undefined;
    self cancel_throwback();
    self.spai_avoiding=0;
    self.spai_avoid_missile=undefined;
    self notify("spai_reload_cancel");self notify("spai_reload_events_stop");self.spai_reloading=0;
    self notify("spai_action_cancel");
    self notify("spai_fire_stop");
    self notify("spai_motion_stop");
    self.onenteranimstate=undefined;
    self scrAgentSetScripted(1);
    self scrAgentClearPath();
    self release_cover();
    self reset_traversal_motion();
    self.spai_busy=0;
    self.spai_patrolling=false;
    self.spai_sound_search=false;
}

valid_enemy(target)
{
    return isDefined(target) && isAlive(target) && isDefined(target.team) && target.team!=self.team && target.team!="spectator";
}

tactical_think()
{
    self endon("spai_combat_stop");
    self endon("spai_dead");
    self endon("disconnect");
    level endon("game_ended");
    // Registered private-match bots can spawn during the native countdown.
    // Use the same flag as player controls, rather than a second timer or the
    // host's individual frozen state (which can also belong to a menu).
    if(self.spai_roaming && maps\mp\_utility::gameflagexists("prematch_done") &&
       !maps\mp\_utility::gameflag("prematch_done"))
    {
        self cancel_action();self setAnimState("idle",0);
        self combat_state("countdown");
        maps\mp\_utility::gameflagwait("prematch_done");
        if(!isAlive(self) || !level.spai_population_active)return;
        self combat_state("idle");
    }
    while(isAlive(self) && level.spai_population_active)
    {
        // Finish an authored crossing before a nonterminal replan/reaction.
        // Health and lethal damage still run immediately in the damage callback.
        if(self.spai_vaulting){wait self.spai_scan_seconds;continue;}
        // Perception continues during movement and cover actions. Only visible
        // positions refresh memory; losing sight never tracks through walls.
        if(isDefined(self.spai_target))self observe_target(self.spai_target);
        // Patrol uses native navigation, but retains the same line-of-sight
        // perception. It must yield to a spotted enemy, damage or a grenade.
        if((self.spai_patrolling || self.spai_sound_search) && !self.spai_reacting && !self.spai_avoiding)
        {
            spotted=self acquire_target(self.spai_sight_radius,0.5);
            if(isDefined(spotted))
            {
                self cancel_action();self observe_target(spotted);
                spaiLog("[SPAI] patrol contact actor="+self getEntityNumber()+" target="+spotted getEntityNumber());
            }
        }
        if(isDefined(self.spai_heard) && getTime()-self.spai_heard.time>6000)self.spai_heard=undefined;
        if(self.spai_patrolling && isDefined(self.spai_heard) && !self.spai_reacting && !self.spai_avoiding)
            self cancel_action();
        if(self.spai_busy && !self.spai_reacting && !self.spai_avoiding && isDefined(self.spai_action_target))
        {
            expired=!self valid_enemy(self.spai_action_target) ||
                !isDefined(self.spai_last_seen_time) || getTime()-self.spai_last_seen_time>self.spai_memory_ms;
            moved=isDefined(self.spai_action_threat) && isDefined(self.spai_last_seen) &&
                distance2D(self.spai_action_threat,self.spai_last_seen)>96;
            if(!expired && moved && self.spai_combat_state=="move_cover" &&
                isDefined(self.spai_action_cover) && self cover_protects_from(self.spai_action_cover,self.spai_last_seen))
            {
                self.spai_action_threat=self.spai_last_seen;
                moved=false;
                spaiLog("[SPAI] cover route retained actor="+self getEntityNumber()+" threat="+self.spai_last_seen);
            }
            if(expired || (moved && self.spai_combat_state!="fire" && getTime()>=self.spai_next_replan))
            {
                self cancel_action();self.spai_replans++;self.spai_next_replan=getTime()+500;
                self.spai_action_target=undefined;self.spai_action_threat=undefined;
                if(expired){self.spai_target=undefined;self.spai_last_seen_time=undefined;}
                self combat_state("replan");
            }
            else if(self.spai_combat_state=="fire" && isDefined(self.spai_last_seen))
                self face_ground_point(self.spai_last_seen);
        }
        if(!self.spai_reacting)
        {
            grenade=self nearest_grenade(350);
            if(self grenade_requires_response(grenade))
            {
                if(self.spai_avoiding)
                {
                    self.spai_grenade_replans++;
                    spaiLog("[SPAI] grenade priority actor="+self getEntityNumber()+" remaining="+spaiGrenadeRemaining(grenade));
                }
                self cancel_action();
                self.spai_busy=1;self.spai_avoiding=1;
                self thread grenade_response(grenade);
            }
        }
        if(!self.spai_busy && !self.spai_reacting)
        {
            if(self.spai_roaming && isDefined(self.spai_failed_link))
            {
                self.spai_busy=1;self.spai_patrolling=true;
                self thread recover_failed_route();
                wait self.spai_scan_seconds;continue;
            }
            weapon=self getCurrentWeapon();
            if(self getWeaponAmmoClip(weapon)<=0 && self getWeaponAmmoStock(weapon)<=0)
            {self combat_state("empty");wait 0.2;continue;}
            target=self acquire_target(self.spai_sight_radius,0.5);
            if(isDefined(target))
            {
                self.spai_target=target;
                self.spai_last_seen=target getTagOrigin("j_spineupper");
                self.spai_last_seen_time=getTime();
            }
            else if(isDefined(self.spai_target) && self valid_enemy(self.spai_target) &&
                isDefined(self.spai_last_seen_time) && getTime()-self.spai_last_seen_time<self.spai_memory_ms)
            {
                // A grenade retreat can break line of sight. Investigate the
                // last observed position without updating it through walls.
                target=self.spai_target;
            }
            if(isDefined(target))
            {
                self.spai_busy=1;
                self.spai_action_target=target;self.spai_action_threat=self.spai_last_seen;
                self thread tactical_action(target,self.spai_last_seen);
            }
            else if(self.spai_roaming && isDefined(self.spai_heard))
            {
                cue=self.spai_heard;self.spai_heard=undefined;
                self.spai_busy=1;self.spai_sound_search=true;
                self.spai_action_target=undefined;self.spai_action_threat=undefined;
                self thread investigate_sound(cue);
            }
            else if(self.spai_roaming && getTime()>=self.spai_next_patrol)
            {
                self.spai_busy=1;self.spai_patrolling=true;
                self thread patrol_action();
            }
            else self combat_state("idle");
        }
        wait self.spai_scan_seconds;
    }
}

investigate_sound(cue)
{
    self endon("spai_action_cancel");self endon("spai_combat_stop");
    self endon("spai_dead");self endon("disconnect");
    goal=undefined;best=192;
    foreach(node in getAllNodes())
    {
        if(!isDefined(node.origin))continue;
        d=distance(node.origin,cue.point-(0,0,48));
        if(d<best){best=d;goal=node;}
    }
    if(isDefined(goal))
    {
        self.spai_sound_searches++;
        self.spai_path_goal=goal.origin;self.statechanges=0;self.pathfailed=0;
        self.onenteranimstate=::state_changed;
        self scrAgentSetGoalRadius(12);self scrAgentSetGoalNode(goal);
        self scrAgentSetScripted(0);self combat_state("investigate_sound");
        began=getTime();
        spaiLog("[SPAI_SOUND_SEARCH] actor="+self getEntityNumber()+" cue="+cue.point+" goal="+goal.origin);
        while(((distance2D(self.origin,goal.origin)>32 && getTime()-began<8000) || self.spai_vaulting) && !self.pathfailed)
        {self update_path_gait();wait 0.1;}
        self notify("spai_motion_stop");self.onenteranimstate=undefined;
        self scrAgentSetScripted(1);self scrAgentClearPath();self reset_traversal_motion();
        self set_motion_mode("anim deltas");self scrAgentSetAnimScale(1,1);
        self setAnimState("idle",0);self.spai_gait=undefined;
        self face_ground_point(cue.point);
        // Perception continues in tactical_think and can interrupt this pause.
        wait 1;
    }
    self.spai_sound_search=false;self.spai_busy=0;
    self.spai_path_goal=undefined;self.spai_path_state="idle";self.spai_gait=undefined;
}

reject_traversal(from,to,reason)
{
    self.pathfailed=1;self setAnimState("idle",0);
    link=spawnStruct();link.from=from;link.to=to;
    self.spai_failed_link=link;
    spaiLog("[SPAI_ROUTE_REJECT] actor="+self getEntityNumber()+" reason="+reason+" from="+from+" to="+to);
}

recover_failed_route()
{
    self endon("spai_action_cancel");self endon("spai_combat_stop");
    self endon("spai_dead");self endon("disconnect");
    link=self.spai_failed_link;self.spai_failed_link=undefined;
    self notify("spai_motion_stop");self.onenteranimstate=undefined;
    self scrAgentSetScripted(1);self scrAgentClearPath();self reset_traversal_motion();
    self remember_patrol_area(link.from);
    delta=link.to-link.from;direction=vectorNormalize((delta[0],delta[1],0));
    goal=undefined;best=1000000;
    // Native navigation can keep requesting the same rejected negotiation
    // link even for a different goal. Physically back out to verified standing
    // ground so it can choose a fresh route. Never complete a blocked link by
    // snapping position, disabling collision, or relaxing vault clearance.
    foreach(node in getAllNodes())
    {
        if(!isDefined(node.origin))continue;
        travel=node.origin-self.origin;d=length2D(travel);
        if(d<64 || d>256 || abs(travel[2])>32)continue;
        alignment=vectorDot(vectorNormalize((travel[0],travel[1],0)),direction);
        if(alignment>0.25)continue;
        cost=abs(d-128)+128*alignment;
        if(cost>=best)continue;
        support=self traversal_support(node.origin,32);
        if(!isDefined(support) || !self body_walkable_link(self.origin,support))continue;
        goal=support;best=cost;
    }
    self combat_state("route_recovery");start=self.origin;
    if(isDefined(goal))
    {
        self.spai_route_recoveries++;
        spaiLog("[SPAI_ROUTE_RECOVERY] actor="+self getEntityNumber()+" start="+start+" goal="+goal);
        arrived=self walk_to(goal,undefined);
        travel=self.origin-start;
        if(length2D(travel)>16)self.spai_patrol_heading=vectorNormalize((travel[0],travel[1],0));
        spaiLog("[SPAI_ROUTE_RECOVERY] actor="+self getEntityNumber()+" moved="+length2D(travel)+" arrived="+arrived+" physics="+self scrAgentGetPhysicsMode());
    }
    else
    {
        spaiLog("[SPAI_ROUTE_RECOVERY] actor="+self getEntityNumber()+" no verified retreat");
        wait 1;
    }
    self.spai_patrolling=false;self.spai_busy=0;self.spai_next_patrol=getTime()+250;
    self.spai_path_goal=undefined;self.spai_path_state="idle";self.spai_gait=undefined;
}

remember_patrol_area(point)
{
    // Bounded per-life spatial memory, not knowledge of unseen opponents.
    // Keep recently searched ground unattractive without excluding it forever.
    history=[];entry=spawnStruct();entry.point=point;entry.time=getTime();
    history[0]=entry;
    foreach(previous in self.spai_patrol_history)
    {
        if(history.size>=12)break;
        if(getTime()-previous.time<60000)history[history.size]=previous;
    }
    self.spai_patrol_history=history;
}

patrol_area_cost(point)
{
    cost=0;
    foreach(entry in self.spai_patrol_history)
    {
        age=getTime()-entry.time;
        if(age>=60000)continue;
        separation=distance2D(point,entry.point);
        if(separation>=384)continue;
        cost=max(cost,768*(1-separation/384)*(1-age/60000.0));
    }
    return cost;
}

patrol_action()
{
    self endon("spai_action_cancel");self endon("spai_combat_stop");
    self endon("spai_dead");self endon("disconnect");
    self remember_patrol_area(self.origin);
    outside_search_area=isDefined(level.spai_patrol_center) && distance2D(self.origin,level.spai_patrol_center)>768;
    nodes=getAllNodes();choices=[];
    foreach(node in nodes)
    {
        if(!isDefined(node.origin))continue;
        d=distance2D(self.origin,node.origin);
        if(d<192 || d>1024 || abs(node.origin[2]-self.origin[2])>128)continue;
        if(isDefined(self.spai_last_patrol) && distance2D(node.origin,self.spai_last_patrol)<128)continue;
        choices[choices.size]=node;
    }
    if(choices.size>0)
    {
        goal=undefined;best=1000000;
        // Search different nearby lanes without marching all the way through
        // the map and out of contact. Outside the main search area, favor
        // inward progress; within it, avoid repeatedly checking the same ground.
        // A small heading preference breaks ties without forcing straight runs.
        foreach(candidate in choices)
        {
            travel=candidate.origin-self.origin;
            heading=vectorNormalize((travel[0],travel[1],0));
            cost=self patrol_area_cost(candidate.origin)+abs(length2D(travel)-512)*0.25+randomFloat(160);
            if(outside_search_area)
                cost+=distance2D(candidate.origin,level.spai_patrol_center);
            else if(isDefined(self.spai_patrol_heading))
                cost-=64*vectorDot(heading,self.spai_patrol_heading);
            if(cost<best){best=cost;goal=candidate;}
        }
        travel=goal.origin-self.origin;
        self.spai_patrol_heading=vectorNormalize((travel[0],travel[1],0));
        spaiLog("[SPAI_PATROL_PLAN] actor="+self getEntityNumber()+" origin="+self.origin+" goal="+goal.origin+" egress="+outside_search_area+" history="+self.spai_patrol_history.size+" revisit="+self patrol_area_cost(goal.origin));
        self.spai_last_patrol=goal.origin;
        self.spai_path_goal=goal.origin;
        self.spai_patrols++;self.statechanges=0;self.pathfailed=0;
        self.onenteranimstate=::state_changed;
        self scrAgentSetGoalRadius(12);self scrAgentSetGoalNode(goal);
        self scrAgentSetScripted(0);self combat_state("patrol");
        began=getTime();
        while(((distance2D(self.origin,goal.origin)>24 && getTime()-began<12000) || self.spai_vaulting) && !self.pathfailed)
        {self update_path_gait();wait 0.1;}
        if(distance2D(self.origin,goal.origin)<=24)self.spai_patrol_arrivals++;
        // A rejected route should not be selected again immediately, either.
        if(self.pathfailed)self remember_patrol_area(goal.origin);
        spaiLog("[SPAI] patrol route actor="+self getEntityNumber()+" reached="+(distance2D(self.origin,goal.origin)<=24)+" remaining="+distance2D(self.origin,goal.origin));
    }
    self notify("spai_motion_stop");self.onenteranimstate=undefined;
    self scrAgentSetScripted(1);self scrAgentClearPath();
    self reset_traversal_motion();
    self set_motion_mode("anim deltas");self scrAgentSetAnimScale(1,1);
    self setAnimState("idle",0);
    self.spai_patrolling=false;self.spai_busy=0;self.spai_next_patrol=getTime()+250;
    self.spai_path_goal=undefined;self.spai_path_state="idle";self.spai_gait=undefined;
}

tactical_action(target,threat)
{
    self endon("spai_action_cancel");
    self endon("spai_combat_stop");
    self endon("spai_dead");
    self endon("disconnect");
    if(self.spai_roaming && !self observe_target(target))
    {
        self investigate_contact(target,threat);
        self.spai_busy=0;return;
    }
    // A match encounter must not abandon every clear shot to search for cover.
    // Return fire from a visible contact first, then use cover while the burst
    // cools down. Perception and muzzle clearance still gate every real bullet.
    if(self.spai_roaming && getTime()>=self.spai_next_open_fire && self observe_target(target))
    {
        self combat_state("fire");
        if(self getWeaponAmmoClip(self getCurrentWeapon())<=0)self reload_weapon();
        fired=self fire_burst(target);
        // A vanished contact before the first shot did not use a burst. On
        // reacquisition, return fire immediately rather than take a false
        // firing cooldown. Visible muzzle obstruction still calls for cover.
        self.spai_next_open_fire=getTime();
        if(fired || self observe_target(target))self.spai_next_open_fire+=self.spai_burst_cooldown;
        spaiLog("[SPAI_CONTACT] actor="+self getEntityNumber()+" fired="+fired+" cooldown="+(self.spai_next_open_fire-getTime()));
        self.spai_busy=0;return;
    }
    // At close range, keep facing the visible opponent during the short burst
    // recovery instead of abandoning the duel for a long cover search.
    if(self.spai_roaming && self observe_target(target) && distance2D(self.origin,self.spai_last_seen)<192 && getTime()<self.spai_next_open_fire)
    {
        self face_ground_point(self.spai_last_seen);self setAnimState("aim",0);
        self aim_at_observed_point(self.spai_last_seen);
        self combat_state("track_contact");wait 0.1;self.spai_busy=0;return;
    }
    self combat_state("seek_cover");
    cover=self choose_cover(threat,640);
    if(!isDefined(cover))
    {
        self combat_state("no_cover");
        if(self getWeaponAmmoClip(self getCurrentWeapon())<=0)self reload_weapon();
        self fire_burst(target);
        wait 1;
        self.spai_busy=0;return;
    }
    if(!self claim_cover(cover))
    {self combat_state("cover_taken");self.spai_busy=0;return;}
    self.spai_path_goal=cover.origin;
    self.statechanges=0;self.pathfailed=0;
    self.onenteranimstate=::state_changed;
    self scrAgentSetGoalRadius(4);
    self scrAgentSetGoalNode(cover.node);
    self scrAgentSetScripted(0);
    self combat_state("move_cover");
    began=getTime();
    while(((distance2D(self.origin,cover.origin)>8 && getTime()-began<15000) || self.spai_vaulting) && !self.pathfailed)
    {
        if(!self valid_enemy(target) && !self.spai_vaulting)break;
        self update_path_gait();
        wait 0.1;
    }
    self notify("spai_motion_stop");self.onenteranimstate=undefined;
    self scrAgentSetScripted(1);self scrAgentClearPath();
    self reset_traversal_motion();
    self set_motion_mode("anim deltas");
    self scrAgentSetAnimScale(1,1);
    self.spai_gait=undefined;
    reached=distance2D(self.origin,cover.origin)<=8 && !self.pathfailed && self valid_enemy(target);
    if(reached)
    {
        // Perception can update while navigating without invalidating cover.
        if(isDefined(self.spai_last_seen))threat=self.spai_last_seen;
        pose="cover_left";if(cover.side<0)pose="cover_right";
        self face_ground_point(threat);
        self setAnimState(pose,0);self combat_state("hidden");
        wait 0.5;
        hidden=bulletTrace(threat,self getTagOrigin("j_head"),false,self);
        if(hidden["fraction"]<0.98 && self getWeaponAmmoClip(self getCurrentWeapon())<=0)
        {self combat_state("reload");self reload_weapon();self setAnimState(pose,0);wait 0.2;}
        peek_walkable=self walkable_link(self.origin,cover.peek);
        spaiLog("[SPAI_COVER_ARRIVAL] actor="+self getEntityNumber()+" hidden="+hidden["fraction"]+" peek_walkable="+peek_walkable+" remaining="+distance2D(self.origin,cover.origin));
        if(hidden["fraction"]<0.98 && peek_walkable)
        {
            self combat_state("peek");
            if(self walk_to(cover.peek,threat) && self valid_enemy(target))
            {
                self face_ground_point(threat);
                wait 0.25;
                self combat_state("fire");
                self fire_burst(target);
                self combat_state("return_cover");
                if(self walk_to(cover.origin,threat))
                {
                    self face_ground_point(threat);
                    self setAnimState(pose,0);
                    wait 0.3;
                    hidden=bulletTrace(threat,self getTagOrigin("j_head"),false,self);
                    if(hidden["fraction"]<0.98)
                    {
                        if(self getWeaponAmmoClip(self getCurrentWeapon())<=0)
                        {self combat_state("reload");self reload_weapon();self setAnimState(pose,0);wait 0.2;}
                        self.spai_cycles++;
                    }
                }
            }
        }
    }
    self release_cover();
    self combat_state("hold_cover");
    wait 1;
    self.spai_busy=0;
}

investigate_contact(target,last_seen)
{
    // Only navigate to the remembered location. A live target's hidden origin
    // is never used as a navigation goal; reacquisition still needs eye LOS.
    goal=undefined;best=1000000;
    foreach(node in getAllNodes())
    {
        if(!isDefined(node.origin))continue;
        score=distance(node.origin,last_seen-(0,0,40));
        if(score<best){best=score;goal=node;}
    }
    if(!isDefined(goal) || best>192)
    {self.spai_target=undefined;self.spai_last_seen_time=undefined;return;}
    self.spai_path_goal=goal.origin;
    self.statechanges=0;self.pathfailed=0;
    self.onenteranimstate=::state_changed;
    self scrAgentSetGoalRadius(12);self scrAgentSetGoalNode(goal);
    self scrAgentSetScripted(0);self combat_state("investigate");
    began=getTime();visible=false;
    while(((self valid_enemy(target) && distance2D(self.origin,goal.origin)>24 &&
          getTime()-began<10000) || self.spai_vaulting) && !self.pathfailed)
    {
        if(!self.spai_vaulting && self observe_target(target)){visible=true;break;}
        self update_path_gait();
        wait 0.1;
    }
    self notify("spai_motion_stop");self.onenteranimstate=undefined;
    self scrAgentSetScripted(1);self scrAgentClearPath();
    self reset_traversal_motion();
    self set_motion_mode("anim deltas");self scrAgentSetAnimScale(1,1);
    self setAnimState("idle",0);
    if(!visible)visible=self observe_target(target);
    if(!visible && (self.pathfailed || distance2D(self.origin,goal.origin)<=24))
    {self.spai_target=undefined;self.spai_last_seen_time=undefined;}
    spaiLog("[SPAI] investigate actor="+self getEntityNumber()+" reacquired="+visible+" remaining="+distance2D(self.origin,goal.origin));
}

damage_received(inflictor,attacker,damage,flags,mod,weapon,point,dir,hitloc,offset)
{
    spaiLog("[SPAI] actor incoming damage="+damage+" mod="+mod+" hitloc="+hitloc+" point="+point);
    if(isDefined(attacker))spaiLog("[SPAI] actor attacker="+attacker getEntityNumber()+" self="+self getEntityNumber());
    // Registered match actors use the same filtering/perk/match-rule path as
    // native player agents. The completion callback owns actual health damage.
    if(isDefined(level.spai_match_participants[self getEntityNumber()]))
    {
        result=self maps\mp\gametypes\_damage::callback_playerdamage_internal(inflictor,attacker,self,damage,flags,mod,weapon,point,dir,hitloc,offset);
        spaiLog("[SPAI] match damage result="+result);
        return;
    }
    // Standalone animation fixtures have no native participant initialization.
    self damage_finished(inflictor,attacker,damage,flags,mod,weapon,point,dir,hitloc,offset);
}

damage_finished(inflictor,attacker,damage,flags,mod,weapon,point,dir,hitloc,offset)
{
    // Native match damage has already applied reflected FF to the player,
    // but still dispatches agent completion on the original victim. Do not
    // apply it a second time to that victim. Split FF must still complete.
    if(isDefined(level.spai_match_participants[self getEntityNumber()]) &&
       level.friendlyfire==2 && !(flags & level.idflags_no_protection) &&
       isDefined(attacker) && isPlayer(attacker) &&
       maps\mp\_utility::attackerishittingteam(self,attacker))
    {
        spaiLog("[SPAI] reflected player damage leaves agent unharmed");
        return;
    }
    before=self.health;
    self maps\mp\agents\_agents::agent_damage_finished(inflictor,attacker,damage,flags,mod,weapon,point,dir,hitloc,offset);
    if(!isAlive(self))return;
    applied=before-self.health;
    member=level.spai_match_participants[self getEntityNumber()];
    if(isDefined(member))spai_track_agent_contribution(self,attacker,applied,member.spawns);
    spaiLog("[SPAI] match damage applied="+applied+" health="+self.health);
    // Incoming fire is a perception cue even outside the patrol view cone.
    // Record an attacker only when the same native eye trace can see them.
    if(applied>0 && self valid_enemy(attacker))self observe_target(attacker);
    if(!level.spai_population_active || self.spai_dead || applied<20 || self.spai_reacting || getTime()<self.spai_cooldown)return;
    if(self.spai_vaulting)
    {
        if(!self.spai_pending_reaction){self.spai_pending_reaction=true;self thread react_after_vault();}
        return;
    }
    self begin_damage_reaction();
}

react_after_vault()
{
    self endon("spai_dead");self endon("disconnect");self endon("spai_combat_stop");
    while(self.spai_vaulting && self.spai_pending_reaction)wait 0.05;
    if(!self.spai_pending_reaction || !isAlive(self) || !level.spai_population_active)return;
    self.spai_pending_reaction=false;
    if(!self.spai_reacting && getTime()>=self.spai_cooldown)self begin_damage_reaction();
}

begin_damage_reaction()
{
    animation=self choose_damage_reaction();
    self.spai_reacting=1;self.spai_reactions++;self.spai_cooldown=getTime()+2500;
    self.spai_last_reaction=animation;
    self cancel_action();self thread react(animation);
}

choose_damage_reaction()
{
    if(!isDefined(level.spai_reaction_variety) || !level.spai_reaction_variety)return "reaction";
    if(isDefined(self.spai_action_cover) && self.spai_combat_state=="hidden")
    {
        if(self.spai_action_cover.side<0)return "react_cover_right";
        return "react_cover_left";
    }
    if(isDefined(self.spai_gait) && self.spai_gait=="crouch_move")return "react_crouch";
    candidates=[];
    if(isDefined(self.spai_gait) && (self.spai_gait=="run" || self.spai_gait=="sprint"))
    {candidates[0]="react_run";candidates[1]="react_stumble";}
    else {candidates[0]="react_stand";candidates[1]="react_stand_alt";}
    choice=randomInt(candidates.size);
    if(isDefined(self.spai_last_reaction) && candidates[choice]==self.spai_last_reaction)
        choice=(choice+1)%candidates.size;
    return candidates[choice];
}

react(animation)
{
    self endon("spai_dead");self endon("disconnect");self endon("spai_combat_stop");
    if(!isDefined(animation))animation="reaction";
    self combat_state("reaction");
    self set_motion_mode("anim deltas");self scrAgentSetAnimScale(1,1);
    duration=spaiAnimDuration(animation);began=getTime();start=self.origin;blocked=false;
    self setAnimState(animation,0);
    // Check the upcoming root segment, not the whole clip's end point. A wall
    // near the end must not erase the clear travel at the beginning. Native
    // gravity/collision still owns movement; never teleport or use noclip.
    while(getTime()-began<int(duration*1000))
    {
        t=min(1,(getTime()-began)/(duration*1000));
        next=min(1,t+0.1/duration);
        delta=spaiAnimDelta(animation,t,next);
        if(!blocked && length2D(delta)>0.01)
        {
            forward=anglesToForward((0,self.angles[1],0));side=(-forward[1],forward[0],0);
            to=self.origin+forward*delta[0]+side*delta[1]+(0,0,delta[2]);
            if(!self walkable_link(self.origin,to))
            {blocked=true;self scrAgentSetAnimScale(0,1);}
        }
        wait 0.05;
    }
    self setAnimState("idle",0);self scrAgentSetAnimScale(1,1);
    self.spai_reacting=0;self.spai_recovered++;
    spaiLog("[SPAI_REACTION] actor="+self getEntityNumber()+" animation="+animation+" moved="+distance2D(start,self.origin)+" blocked="+blocked);
    self combat_state("recover");
}

choose_death_animation(hitloc)
{
    if(!isDefined(level.spai_death_variety) || !level.spai_death_variety)return level.spai_death_animation;
    if(isDefined(hitloc) && (hitloc=="head" || hitloc=="helmet"))return "death_head";
    // The corner-cover poses stand upright. Only an actual crouched gait
    // selects a crouched death; a previous approach gait is cleared on arrival.
    if(isDefined(self.spai_gait) && self.spai_gait=="crouch_move")return "death_crouch";
    candidates=[];
    if(isDefined(self.spai_gait) && (self.spai_gait=="run" || self.spai_gait=="sprint"))
    {candidates[0]="death";candidates[1]="death_run_front";}
    else
    {
        candidates[0]="death_stand";candidates[1]="death_stand_alt";
        if(isDefined(level.spai_reaction_variety) && level.spai_reaction_variety)
        {candidates[2]="death_twist";candidates[3]="death_flop";candidates[4]="death_knees";}
    }
    member=level.spai_match_participants[self getEntityNumber()];
    choice=randomInt(candidates.size);
    if(isDefined(member) && isDefined(member.last_death_animation) &&
       candidates[choice]==member.last_death_animation)choice=(choice+1)%candidates.size;
    return candidates[choice];
}

actor_killed(inflictor,attacker,damage,mod,weapon,dir,hitloc,offset,deathanim)
{
    if(isDefined(self.spai_dead) && self.spai_dead)return;
    selected_death=self choose_death_animation(hitloc);
    wasVaulting=self.spai_vaulting;
    self.spai_dead=1;self.spai_reacting=0;
    self notify("spai_dead");
    self cancel_action();
    if(wasVaulting)spaiLog("[SPAI_VAULT] death cleanup actor="+self getEntityNumber()+" physics="+self scrAgentGetPhysicsMode()+" active="+self.spai_vaulting+" pending="+self.spai_pending_reaction);
    spaiLog("[SPAI] tactical actor killed");
    // Preserve the native agent corpse lifecycle without invoking its outer
    // killed callback, which would duplicate our scoring and respawn ownership.
    // Stock delayStartRagdoll queries getCorpseAnim through the public script
    // registry. Our per-rig tree is private; that handle is not valid there.
    self scrAgentSetAllowRagdoll(1);
    if(isDefined(level.weapondropfunction))self thread [[level.weapondropfunction]](attacker,mod);
    transition=0;
    if(isDefined(level.spai_death_animation))
    {
        self setAnimState(selected_death,0);
        transition=min(0.8,spaiAnimDuration(selected_death));
        foreach(event in spaiAnimEvents(selected_death))
            if(event["name"]=="start_ragdoll")transition=max(0.1,min(1.5,event["time"]));
    }
    spaiLog("[SPAI_DEATH_CHOICE] actor="+self getEntityNumber()+" state="+selected_death+" hitloc="+hitloc);
    self.body=self cloneAgent(int(max(1000,deathanim)));
    member=level.spai_match_participants[self getEntityNumber()];
    if(isDefined(member)){member.corpse=self.body;member.last_death_animation=selected_death;}
    if(isDefined(self.body))
    {
        if(!spaiBindCorpseRig(self,self.body))spaiLog("[SPAI] FAILED corpse rig assignment");
        self.body thread spai_finish_death(transition);
        spaiLog("[SPAI_DEATH] actor="+self getEntityNumber()+" corpse="+self.body getEntityNumber()+" transition="+transition);
    }
    self match_participant_killed(attacker,weapon,mod,inflictor);
    record=level.spai_population[self getEntityNumber()];
    if(isDefined(record) && respawn_permitted(record) && !record.pending)
    {
        record.pending=true;
        level thread respawn_owned_actor(self,record);
    }
}

spai_finish_death(transition)
{
    // Entity-owned thread ends when the native eight-slot corpse pool recycles
    // this body. It cannot ragdoll a later occupant or a respawned live agent.
    self endon("death");level endon("game_ended");
    if(transition>0)wait transition;
    self startRagdoll();
    spaiLog("[SPAI_DEATH] ragdoll corpse="+self getEntityNumber());
}

enable_respawn(body,head,weapon,team,delay)
{
    record=spawnStruct();record.actor=self;record.enabled=true;record.pending=false;
    record.body=body;record.head=head;record.weapon=weapon;record.team=team;
    record.delay=delay;record.spawns=1;record.failed=false;record.retries=0;
    level.spai_population[self getEntityNumber()]=record;
}

disable_respawn()
{
    record=level.spai_population[self getEntityNumber()];
    if(isDefined(record)){record.enabled=false;record.pending=false;}
}

respawn_owned_actor(actor,record)
{
    level endon("game_ended");
    wait record.delay;
    if(!respawn_permitted(record) || !isDefined(actor) || isAlive(actor)){record.pending=false;return;}
    number=actor getEntityNumber();
    if(level.spai_population[number]!=record)return;
    member=level.spai_match_participants[number];
    if(isDefined(member))
    {
        penalty=spai_teamkill_delay(member);
        if(member.teamkill_punish)
        {
            member.teamkill_punish=false;
            if(penalty>0)
            {
                spaiLog("[SPAI_TEAMKILL] respawn delay actor="+number+" seconds="+penalty);
                wait penalty;
            }
        }
        else if(penalty>0)member.teamkills=int(max(0,member.teamkills-1));
    }
    if(!isDefined(actor) || !respawn_permitted(record) || level.spai_population[number]!=record || isAlive(actor))
    {record.pending=false;return;}
    // Use the map's native spawn selection and reject occupied/invalid points.
    // Never move a human player or use an unchecked offset from their position.
    actor maps\mp\agents\_agent_utility::set_agent_team(record.team);
    selected=undefined;
    while(!isDefined(selected) && respawn_permitted(record) && level.spai_population[number]==record)
    {
        for(attempt=0;attempt<32 && respawn_permitted(record);attempt++)
        {
            point=actor select_match_spawn();
            if(isDefined(point)){selected=point;break;}
            wait 0.05;
        }
        if(!isDefined(selected) && respawn_permitted(record))
        {
            record.retries++;
            spaiLog("[SPAI] respawn waiting for native point actor="+number+" retry="+record.retries);
            wait 1;
        }
    }
    if(!respawn_permitted(record) || level.spai_population[number]!=record || isAlive(actor)){record.pending=false;return;}
    actor notify("spai_combat_stop");
    actor detachAll();actor despawnAgent();
    if(isDefined(record.rig_selected) && record.rig_selected && !spaiUseRig(actor,record.body))
    {record.pending=false;record.failed=true;spaiLog("[SPAI] FAILED respawn rig assignment");return;}
    actor spawnAgent(selected.origin,selected.angles,"default_anim");
    actor maps\mp\agents\_agent_utility::activateAgent();
    actor scrAgentSetScripted(1);
    actor setModel(record.body);actor attach(record.head,"",true);
    actor maps\mp\agents\_agent_utility::set_agent_team(record.team);
    actor apply_match_spawn_health();
    actor giveWeapon(record.weapon);actor switchToWeaponImmediate(record.weapon);
    actor setWeaponAmmoClip(record.weapon,weaponClipSize(record.weapon));
    actor setWeaponAmmoStock(record.weapon,weaponClipSize(record.weapon)*3);
    actor scrAgentSetPhysicsMode("gravity");actor scrAgentSetAnimMode("anim deltas");
    actor setAnimState("idle",0);actor init_behavior();
    if(isDefined(record.difficulty))actor configure_difficulty(record.difficulty);
    if(isDefined(record.roaming))actor.spai_roaming=record.roaming;
    actor match_participant_spawned();
    record.pending=false;record.spawns++;
    spaiLog("[SPAI] respawn completed actor="+number+" team="+record.team+" spawn="+record.spawns+" health="+actor.health+" time="+getTime());
    actor thread tactical_think();
}

match_spawn_clear(point)
{
    if(!isDefined(point) || !isDefined(point.origin))return false;
    if(!self aiPhysicsTracePassed(point.origin+(0,0,18),point.origin+(0,0,19),16,54,1))return false;
    floor=self ground_trace(point.origin);
    if(floor["fraction"]>=1 || abs(floor["position"][2]-point.origin[2])>24)return false;
    foreach(other in level.agentarray)
        if(other!=self && isAlive(other) && distance(other.origin,point.origin)<64)return false;
    if(isDefined(level.players))foreach(player in level.players)
        if(isAlive(player) && distance(player.origin,point.origin)<64)return false;
    return true;
}

select_match_spawn()
{
    preferred=self [[level.getspawnpoint]]();
    if(self match_spawn_clear(preferred))return preferred;
    // An unavailable mode selector remains unavailable. TDM's native scorer
    // does not account for every scripted agent; filter its own map-authored
    // candidate set before rescoring, rather than teleporting to an offset.
    if(!isDefined(preferred) || level.gametype!="war")return undefined;
    team=self.team;
    if(game["switchedsides"])team=maps\mp\_utility::getotherteam(team);
    if(level.usestartspawns && level.ingraceperiod)
        candidates=maps\mp\gametypes\_spawnlogic::getspawnpointarray("mp_tdm_spawn_"+team+"_start");
    else candidates=maps\mp\gametypes\_spawnlogic::getteamspawnpoints(team);
    valid=[];
    foreach(point in candidates)if(self match_spawn_clear(point))valid[valid.size]=point;
    if(valid.size==0)return undefined;
    selected=self maps\mp\gametypes\_spawnscoring::getspawnpoint_awayfromenemies(valid,team);
    if(!self match_spawn_clear(selected))return undefined;
    self maps\mp\gametypes\_spawnlogic::recon_set_spawnpoint(selected);
    spaiLog("[SPAI] match spawn alternate actor="+self getEntityNumber()+" candidates="+valid.size);
    return selected;
}

reload_weapon()
{
    self endon("spai_reload_cancel");self endon("spai_dead");self endon("disconnect");self endon("spai_combat_stop");
    weapon=self getCurrentWeapon();
    if(self getWeaponAmmoClip(weapon)>=weaponClipSize(weapon) || self getWeaponAmmoStock(weapon)<=0)return false;
    self.spai_reloading=1;self.spai_reload_committed=0;
    self notify("spai_fire_stop");
    self notify("spai_reload_events_stop");
    // Let the 0.15s native blend fully retire the previous reload before re-entry.
    self setAnimState("aim",0);
    wait 0.2;
    self thread reload_events(weapon);
    self setAnimState("reload",0);
    spaiLog("[SPAI] reload started clip="+self getWeaponAmmoClip(weapon)+" reserve="+self getWeaponAmmoStock(weapon)+" time="+getTime());
    wait(spaiAnimDuration("reload")+0.15);
    self notify("spai_reload_events_stop");
    self setAnimState("aim",0);self.spai_reloading=0;
    return self.spai_reload_committed;
}
reload_events(weapon)
{
    self endon("spai_reload_cancel");self endon("spai_reload_events_stop");self endon("spai_dead");self endon("disconnect");self endon("spai_combat_stop");
    while(1)
    {
        self waittill("spai_anim",name);
        if(name!="reload done" || self.spai_reload_committed || !isAlive(self) || self getCurrentWeapon()!=weapon)continue;
        clip=self getWeaponAmmoClip(weapon);stock=self getWeaponAmmoStock(weapon);
        amount=int(min(stock,max(0,weaponClipSize(weapon)-clip)));
        spaiLog("[SPAI] reload transfer clip="+clip+" stock="+stock+" capacity="+weaponClipSize(weapon)+" amount="+amount);
        if(amount<=0)continue;
        self setWeaponAmmoStock(weapon,stock-amount);
        self setWeaponAmmoClip(weapon,clip+amount);
        self.spai_reload_committed=1;
        spaiLog("[SPAI] reload committed clip="+self getWeaponAmmoClip(weapon)+" reserve="+self getWeaponAmmoStock(weapon)+" time="+getTime());
    }
}
cancel_reload()
{
    self notify("spai_reload_cancel");self notify("spai_reload_events_stop");
    self.spai_reloading=0;self setAnimState("idle",0);
}

grenade_requires_response(grenade)
{
    if(!isDefined(grenade) || !isValidMissile(grenade))return false;
    if(!self.spai_avoiding)return true;
    // Do not abandon a live grenade already in hand. Once it has been released,
    // an earlier fuse may interrupt the recovery wait just like an escape run.
    if(self.spai_held)return false;
    active=self.spai_avoid_missile;
    if(!isDefined(active) || !isValidMissile(active))return true;
    if(active==grenade)return false;
    // A small margin prevents noisy near-equal fuse readings from restarting
    // an escape every scan. Neither fuse is modified by changing priority.
    return spaiGrenadeRemaining(grenade)+250<spaiGrenadeRemaining(active);
}

nearest_grenade(radius)
{
    candidates=getEntArray("grenade","classname");
    best=undefined;best_time=60001;
    for(i=0;i<candidates.size;i++)
    {
        g=candidates[i];
        if(!isDefined(g) || !isValidMissile(g) || distance(self.origin,g.origin)>radius)continue;
        remaining=spaiGrenadeRemaining(g);
        if(remaining<0 || remaining>=best_time)continue;
        best=g;best_time=remaining;
    }
    return best;
}

escape_point(danger)
{
    best=undefined;score=distance2D(self.origin,danger);
    // A long straight escape is often impossible at a cover corner. Search
    // shorter collision-checked retreats too, preferring solid blast cover.
    for(step=48;step<=480;step+=48)
    {
        for(yaw=0;yaw<360;yaw+=22.5)
        {
            p=self.origin+anglesToForward((0,yaw,0))*step;
            floor=self ground_trace(p);
            if(floor["fraction"]>=1)continue;
            p=floor["position"]+(0,0,2);
            distance_score=distance2D(p,danger);
            if(distance_score<48 || !self walkable_link(self.origin,p))continue;
            hidden=1;
            for(height=16;height<=64;height+=24)
            {
                sight=bulletTrace(danger+(0,0,8),p+(0,0,height),false,self.spai_avoid_missile);
                if(sight["fraction"]>=0.98)hidden=0;
            }
            candidate=distance_score+hidden*400-step*0.1;
            if(candidate<=score)continue;
            best=p;score=candidate;
        }
    }
    return best;
}

ground_trace(p)
{
    // A live grenade can intercept the floor ray even when player hits are
    // disabled. It is a transient hazard, not a step or supporting surface.
    ignore=self;
    if(isDefined(self.spai_avoid_missile) && isValidMissile(self.spai_avoid_missile))
        ignore=self.spai_avoid_missile;
    return bulletTrace(p+(0,0,24),p-(0,0,32),false,ignore);
}

avoid_grenade(grenade)
{
    self endon("spai_action_cancel");self endon("spai_combat_stop");
    self endon("spai_dead");self endon("disconnect");
    self combat_state("evade_grenade");self.spai_evades++;
    if(isDefined(grenade) && isValidMissile(grenade))
    {
        self.spai_avoid_missile=grenade;
        goal=self escape_point(grenade.origin);
        if(isDefined(goal))
        {
            spaiLog("[SPAI] grenade escape goal="+goal+" danger="+grenade.origin);
            self walk_to(goal);
            if(isDefined(grenade) && isValidMissile(grenade))
                spaiLog("[SPAI] grenade escape separation="+distance(self.origin,grenade.origin)+" remaining="+spaiGrenadeRemaining(grenade));
        }
        else spaiLog("[SPAI] no direct grenade escape route");
        // Keep weapons interrupted until this hazard expires; no fuse modification.
        while(isDefined(grenade) && isValidMissile(grenade))wait 0.05;
    }
    self setAnimState("idle",0);
    if(isDefined(self.spai_last_seen))
        self face_ground_point(self.spai_last_seen);
    self.spai_avoiding=0;self.spai_busy=0;self.spai_avoid_missile=undefined;
    self combat_state("recover_grenade");
}

// Initial throwback controller. A first-impact arc is checked against world
// geometry and friendly positions; post-impact bounce prediction still needs
// broader validation before this can be promoted out of the prototype.
grenade_response(grenade)
{
    self endon("spai_action_cancel");self endon("spai_dead");
    self endon("spai_combat_stop");self endon("disconnect");
    self.spai_avoid_missile=grenade;
    plan=undefined;
    if(isDefined(grenade) && isValidMissile(grenade) && distance(self.origin,grenade.origin)<64 && spaiGrenadeRemaining(grenade)>2000)
        plan=self plan_return(grenade,self.origin+(0,0,48));
    if(isDefined(plan))
    {
        self.spai_grenade=grenade;self.spai_held=0;self.spai_released=0;self.spai_throw_failed=0;
        self.spai_throw_plan=plan;
        self combat_state("throwback");
        self scrAgentSetOrientMode("face angle abs",(0,plan.yaw,0));
        self setAnimState("idle",0);
        wait 0.15;
        self thread return_events();
        self setAnimState("throwback",0);
        began=getTime();
        while(getTime()-began<spaiAnimDuration("throwback")*1000 && !self.spai_throw_failed)
        {
            if(!isDefined(grenade) || !isValidMissile(grenade))break;
            if(getTime()-began>1000 && !self.spai_released)break;
            wait 0.05;
        }
        self cancel_throwback();
        self setAnimState("idle",0);
        if(self.spai_released)
        {
            while(isDefined(grenade) && isValidMissile(grenade))wait 0.05;
            self.spai_avoiding=0;self.spai_busy=0;self.spai_avoid_missile=undefined;
            self combat_state("recover_throwback");
            return;
        }
    }
    self avoid_grenade(grenade);
}

cancel_throwback()
{
    self notify("spai_return_events_stop");self notify("spai_return_hand_stop");
    if(isDefined(self.spai_held) && self.spai_held && isDefined(self.spai_grenade) && isValidMissile(self.spai_grenade))
        spaiGrenadePose(self.spai_grenade,self,self.spai_grenade.origin,0);
    self.spai_held=0;
    if(isDefined(self.spai_grenade) && isDefined(self.spai_grenade.spai_claimant) && self.spai_grenade.spai_claimant==self)
        self.spai_grenade.spai_claimant=undefined;
    self.spai_grenade=undefined;
}

return_events()
{
    self endon("spai_return_events_stop");self endon("disconnect");
    while(1)
    {
        self waittill("spai_anim",name);
        grenade=self.spai_grenade;
        if(!isDefined(grenade) || !isValidMissile(grenade))return;
        if(name=="grenade_left" && !self.spai_held && !self.spai_released)
        {
            if(isDefined(grenade.spai_claimant) && grenade.spai_claimant!=self){self.spai_throw_failed=1;return;}
            hand=self getTagOrigin("j_wrist_le");
            trace=bulletTrace(grenade.origin,hand,false,grenade);
            if(spaiGrenadeRemaining(grenade)<1000 || distance(grenade.origin,hand)>64 || trace["fraction"]<0.99)
            {self.spai_throw_failed=1;return;}
            grenade.spai_claimant=self;
            self.spai_held=1;
            self thread return_follow_hand();
        }
        if(name=="grenade_throw" && self.spai_held && !self.spai_released)
        {
            self notify("spai_return_hand_stop");
            hand=self getTagOrigin("j_wrist_le");
            plan=self.spai_throw_plan;
            velocity=anglesToForward((0,plan.yaw,0))*plan.speed+(0,0,plan.up);
            landing=self return_arc(grenade,hand,velocity);
            if(!isDefined(landing)){self.spai_throw_failed=1;return;}
            self.spai_released=spaiRethrow(grenade,self,hand,velocity);
            if(self.spai_released)
            {self.spai_held=0;self.spai_throwbacks++;spaiLog("[SPAI] tactical release velocity="+velocity+" predicted_impact="+landing);}
            else self.spai_throw_failed=1;
        }
    }
}

return_follow_hand()
{
    self endon("spai_return_hand_stop");self endon("disconnect");
    while(self.spai_held && isDefined(self.spai_grenade) && isValidMissile(self.spai_grenade))
    {
        grenade=self.spai_grenade;hand=self getTagOrigin("j_wrist_le");
        trace=bulletTrace(grenade.origin,hand,false,grenade);
        if(trace["fraction"]<0.99 || !spaiGrenadePose(grenade,self,hand,1))
        {self.spai_throw_failed=1;return;}
        wait 0.05;
    }
}

plan_return(grenade,start)
{
    if(level.spai_blast_radius<=0 || getDvarFloat("g_gravity")!=800)return undefined;
    preferred=self.angles[1];
    if(isDefined(self.spai_last_seen))preferred=vectorToAngles(self.spai_last_seen-self.origin)[1];
    for(up=180;up<=420;up+=120)
    {
        for(i=0;i<16;i++)
        {
            yaw=preferred+i*22.5;
            velocity=anglesToForward((0,yaw,0))*700+(0,0,up);
            landing=self return_arc(grenade,start,velocity);
            if(!isDefined(landing))continue;
            plan=spawnStruct();plan.yaw=yaw;plan.speed=700;plan.up=up;
            spaiLog("[SPAI] return plan yaw="+yaw+" up="+up+" landing="+landing);
            return plan;
        }
    }
    return undefined;
}

return_arc(grenade,start,velocity)
{
    // Gravity-trajectory prototype is restricted to the verified standard
    // gravity. Samples include a small swept envelope, not only a center ray.
    if(level.spai_blast_radius<=0 || getDvarFloat("g_gravity")!=800)return undefined;
    remaining=spaiGrenadeRemaining(grenade)*0.001-0.5;
    previous=start;
    for(t=0.05;t<2.5 && t<remaining;t+=0.05)
    {
        p=start+velocity*t-(0,0,400*t*t);
        if(!self return_segment_clear(previous,p))return undefined;
        hit=bulletTrace(previous,p,false,grenade);
        if(hit["fraction"]<1)
        {
            if(hit["normal"][2]<0.7 || distance(self.origin,hit["position"])<level.spai_blast_radius+160)return undefined;
            friends=level.players;
            for(j=0;j<friends.size;j++)if(!self landing_clear(friends[j],hit["position"]))return undefined;
            friends=level.agentarray;
            for(j=0;j<friends.size;j++)if(!self landing_clear(friends[j],hit["position"]))return undefined;
            return hit["position"];
        }
        offsets=[];offsets[0]=(3,0,0);offsets[1]=(-3,0,0);offsets[2]=(0,3,0);offsets[3]=(0,-3,0);offsets[4]=(0,0,3);
        for(j=0;j<offsets.size;j++)
        {
            edge=bulletTrace(previous+offsets[j],p+offsets[j],false,grenade);
            if(edge["fraction"]<1)return undefined;
        }
        previous=p;
    }
    return undefined;
}

landing_clear(other,point)
{
    if(!isDefined(other) || !isAlive(other) || !isDefined(other.team) || other.team!=self.team)return true;
    return distance(other.origin,point)>level.spai_blast_radius+160;
}

return_segment_clear(start,end)
{
    friends=level.players;
    for(k=0;k<friends.size;k++)if(!self return_misses_friend(friends[k],start,end))return false;
    friends=level.agentarray;
    for(k=0;k<friends.size;k++)if(!self return_misses_friend(friends[k],start,end))return false;
    return true;
}

return_misses_friend(other,start,end)
{
    if(!isDefined(other) || other==self || !isAlive(other) || !isDefined(other.team) || other.team!=self.team)return true;
    segment=end-start;center=other.origin+(0,0,32);offset=center-start;
    length2=segment[0]*segment[0]+segment[1]*segment[1]+segment[2]*segment[2];
    alpha=0;
    if(length2>0.001)alpha=(offset[0]*segment[0]+offset[1]*segment[1]+offset[2]*segment[2])/length2;
    if(alpha<0)alpha=0;
    if(alpha>1)alpha=1;
    return distance(start+segment*alpha,center)>48;
}
