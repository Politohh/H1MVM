#include "component/demo/cod4_model_metadata.hpp"
#include <filesystem>
#include <iostream>
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int wmain(int argc,wchar_t** argv)
{
    try
    {
        check(cod4::recorded_gametype({{0,"\\g_gametype\\sd\\mapname\\mp_crash"}})=="sd","Source mode lost");
        check(cod4::recorded_gametype({})=="war","Missing-mode fallback changed");
        cod4::world_track synthetic;
        synthetic.commands={{100,1,1,"b 0 3 14 24"},{110,2,1,"b 0 3 14 24"},
            {120,3,2,"b 1 3 15 24 7"},{130,4,3,"b 0 3 15 24"},{150,5,4,"b 0 -1 15 24"}};
        const auto scores=cod4::collect_team_scores(synthetic);
        check(scores.size()==3,"Malformed/duplicate scoreboard handling changed");
        check(!cod4::team_scores_at(scores,99),"Invented score before first record");
        check(cod4::team_scores_at(scores,100)->allies==14 && cod4::team_scores_at(scores,149)->allies==15,
            "Score endpoints changed");
        check(cod4::team_scores_at(scores,150)->axis==-1 && cod4::team_scores_at(scores,110)->axis==3,
            "Negative score or rewind changed");
        const auto metadata=cod4::parse_model_metadata({"@teamscore:100:3:14:24","@teamscore:130:3:15:24"});
        check(metadata.team_scores.size()==2 && cod4::team_scores_at(metadata.team_scores,120)->allies==14,
            "Archived score metadata lost");
        bool rejected=false;
        try {cod4::parse_model_metadata({"@teamscore:100:3:14:24","@teamscore:90:3:15:24"});}
        catch(const std::runtime_error&){rejected=true;}
        check(rejected,"Unsorted metadata accepted");
        synthetic.commands={{100,1,1,"b 0 3 14 24"},{100,2,2,"b 0 3 15 24"}};
        const auto simultaneous=cod4::collect_team_scores(synthetic);
        check(simultaneous.size()==1 && simultaneous[0].allies==15,"Same-time score updates not coalesced");
        if(argc==2)
        {
            std::ifstream file(std::filesystem::path(argv[1]),std::ios::binary);
            check(bool(file),"Missing real S&D fixture");
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
            const auto camera=cod4::decode(bytes);
            const auto world=cod4::decode_world(bytes);
            const auto real=cod4::collect_team_scores(world);
            check(cod4::recorded_gametype(camera.configstrings)=="sd","Real S&D mode lost");
            check(real.size()==2 && real[0].axis==3 && real[0].allies==14 && real[0].limit==24
                && real[1].axis==3 && real[1].allies==15,"Real S&D scoreboard changed");
            std::cout<<"PASS real S&D frames="<<world.frames.size()<<" scores=3:14 -> 3:15 limit=24\n";
        }
        std::cout<<"PASS recorded mode, score changes, negative scores, malformed commands, archive, pause/rewind\n";
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
