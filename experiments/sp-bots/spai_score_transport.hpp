// Included in spai_probe. Server-owned state never substitutes for received
// client configstrings; a new map cannot inherit an old scoreboard.
std::string score_rows;
std::string score_epoch;

void clear_score_rows()
{
    score_rows.clear();score_epoch.clear();
}

std::string score_serverinfo()
{
    char buffer[8192]{};
    reinterpret_cast<void(*)(int,char*,int)>(game::base_address+0x552E90)(0,buffer,sizeof(buffer));
    return buffer;
}

bool publish_score_rows(const std::string& rows)
{
    if(!game::SV_Loaded() || game::VirtualLobby_Loaded())return false;
    const auto* enabled=game::Dvar_FindVar("spai_enabled");
    if(!enabled || !enabled->current.integer)return false;
    std::vector<spai_scores::row> decoded;
    if(!spai_scores::decode(rows,decoded))return false;
    const auto epoch=std::to_string(*game::mp::sv_serverId_value);
    if(score_epoch==epoch && score_rows==rows)return true;
    const auto info=score_serverinfo();
    if(info.empty())return false;
    score_rows=rows;score_epoch=epoch;
    game::SV_SetConfigstring(0,info.c_str());
    diagnostic("[SPAI_STATS_WIRE] epoch="+epoch+" rows="+rows);
    return true;
}
