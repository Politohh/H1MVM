// Included inside spai_probe after model ownership and manifest validation.
std::string recording_marker()
{
    if(!utils::flags::has_flag("spai-probe") || game::VirtualLobby_Loaded() || demo_playback::playing())return {};
    if(remote_requested())return animation_schema::current_recording_marker;
    const auto* enabled=game::Dvar_FindVar("spai_enabled");
    const auto* lobby=game::Dvar_FindVar("spai_from_lobby");
    return enabled && lobby && enabled->current.integer && lobby->current.integer
        ? animation_schema::current_recording_marker : "";
}

std::string recording_characters()
{
    if(recording_marker().empty())return {};
    const auto manifest=remote_requested()?remote_character_manifest:
        server_character_epoch==std::to_string(*game::mp::sv_serverId_value)?server_character_manifest:"";
    std::set<std::string> models;
    if(!decode_character_manifest(manifest,models))
        throw std::runtime_error("SP Bots recording requires its current character manifest");
    return "H1SPAICHAR:"+manifest;
}

void clear_replay_marker()
{
    if(replay_schema>=3)release_remote_characters();
    replay_schema=0;
}

void read_replay_marker(const std::vector<std::string>& fields)
{
    clear_replay_marker();
    const auto schema=animation_schema::read_marker(fields,utils::flags::has_flag("spai-probe"));
    std::string manifest;
    unsigned count=0;
    for(const auto& field:fields)
        if(field.starts_with("H1SPAICHAR:")){++count;manifest=field.substr(11);}
    std::set<std::string> models;
    if(schema>=3)
    {
        if(count!=1 || !decode_character_manifest(manifest,models))
            throw std::runtime_error("Invalid or missing SP Bots recording character manifest");
        release_remote_characters();
        remote_match_epoch="replay";
        remote_character_manifest=manifest;
        remote_character_models=std::move(models);
        diagnostic("[SPAI_REPLAY_RIG] accepted models="+manifest);
    }
    else if(count)throw std::runtime_error("Unexpected character manifest in legacy or ordinary recording");
    replay_schema=schema;
}
