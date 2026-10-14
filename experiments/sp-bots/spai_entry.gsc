#using_animtree("multiplayer");
main()
{
    // Menu settings are the only opt-in. No test spawns, host movement,
    // forced team/class selection, or automatic match-ending commands.
    replay=spaiReplayRequested();
    spaiLog("[SPAI_ENTRY] map="+getDvar("mapname")+" replay="+replay+" selected="+getDvarInt("spai_bots")+" enabled="+getDvarInt("spai_enabled")+" from_lobby="+getDvarInt("spai_from_lobby")+" capacity="+getMaxAgents());
    if(!replay && (!getDvarInt("spai_enabled") || !getDvarInt("spai_from_lobby")))return;
    precacheModel("body_usmc_desert_assault_mp_camo");
    precacheModel("head_usmc_desert_assault_mp");
    precacheItem("h1_m16_mp");precacheItem("h1_fraggrenade_mp");
    precacheModel(getWeaponModel("h1_m16_mp"));
    if(replay)
    {
        if(spai_prepare_animation_set())spaiLog("[SPAI] replay animation schema ready; live population disabled");
        else spaiLog("[SPAI] FAILED replay animation preparation");
        return;
    }
    if(!spai_precache_match_characters())return;
    level thread spai_match_start();
}
