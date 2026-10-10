#pragma once
#include "demo_event.hpp"
#include "cod4_obituary_events.hpp"
#include "cod4_match.hpp"
#include "cod4_client_slots.hpp"
#include "demo_timeline.hpp"
#include <set>

namespace demo_events
{
    inline std::vector<event> collect(const cod4::camera_track& camera, const cod4::world_track& world)
    {
        std::vector<event> result;
        const auto weapons = camera.configstrings.find(2258);
        std::vector<std::string> weapon_names{""};
        if(weapons!=camera.configstrings.end())
        { std::istringstream input(weapons->second);for(std::string name;input>>name;)weapon_names.push_back(name); }
        for (const auto& kill : cod4::collect_obituary_events(world, weapons == camera.configstrings.end() ? "" : weapons->second))
        {
            const auto& frame = world.sample(kill.time);
            const auto name = [&](unsigned client)
            {
                if (client == 1022) return std::string("World");
                if (const auto it = frame.client_names.find(client); it != frame.client_names.end()) return clean(it->second);
                if (const auto it = frame.clients.find(client); it != frame.clients.end())
                {
                    const auto* text = reinterpret_cast<const char*>(it->second.words.data()+15);
                    const auto label = clean({text, static_cast<std::size_t>(std::find(text,text+16,'\0')-text)});
                    if (!label.empty()) return label;
                }
                return "Player " + std::to_string(client+1);
            };
            const auto attacker=name(kill.attacker),victim=name(kill.victim);
            result.push_back({kill.time,"kill",clean(attacker+" > "+victim),attacker,victim,
                kill.source_weapon<weapon_names.size()?weapon_names[kill.source_weapon]:"",
                kill.environmental?256u:kill.event_parameter>=128?kill.event_parameter:0u});
        }
        // In S&D, the recorded game timer switches on at round live and off at
        // round end. Positive-to-positive timer changes (e.g. bomb time) are
        // not new rounds. Never invent a start for a recording begun mid-round.
        if (cod4::recorded_gametype(camera.configstrings) == "sd")
        {
            bool running = false;
            if (const auto it = camera.configstrings.find(11); it != camera.configstrings.end())
            { int value{}; std::from_chars(it->second.data(),it->second.data()+it->second.size(),value); running=value>0; }
            std::set<int> seen;
            for (const auto& command : world.commands)
            {
                if (!command.text.starts_with("d 11 ") || !seen.insert(command.index).second) continue;
                auto value = std::string_view(command.text).substr(5);
                if (value.size()>1 && value.front()=='"' && value.back()=='"') value=value.substr(1,value.size()-2);
                int time{}; const auto parsed=std::from_chars(value.data(),value.data()+value.size(),time);
                if (parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || time<0) continue;
                const bool next=time>0;
                if (next!=running) result.push_back({command.time,next?"round_start":"round_end",next?"Round start (timer live)":"Round end (timer stopped)"});
                running=next;
            }
        }
        std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.time<b.time;});
        return result;
    }

    struct source_index
    {
        cod4::camera_track camera;
        std::vector<event> events;
        std::vector<demo_timeline::event> kills;
        int pov{};
    };
    inline source_index read_source(const std::filesystem::path& path)
    {
        std::ifstream file(path,std::ios::binary|std::ios::ate);const auto size=file.tellg();
        if(size<=0 || size>256*1024*1024) throw std::runtime_error("Invalid COD4 recording size");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(bytes.data()),size)) throw std::runtime_error("Cannot read COD4 recording");
        source_index result;result.camera=cod4::decode(bytes);
        auto world=cod4::decode_world(bytes);cod4::resolve_view_client(world,cod4::decode_players(bytes));
        const auto slots=cod4::make_client_slots(world);
        result.pov=static_cast<int>(slots.client(world.client_num));
        for(const auto& kill:cod4::collect_obituary_events(world))
        {
            const auto [attacker,victim]=slots.obituary(kill.attacker,kill.victim,kill.time);
            result.kills.push_back({kill.time,static_cast<int>(attacker),static_cast<int>(victim)});
        }
        result.events=collect(result.camera,world);return result;
    }
    inline bool matches(const source_index& source,const demo_timeline::index& native,const std::string& map,int first,int last)
    {
        if(source.camera.map!=map || source.pov!=native.pov || source.camera.frames.empty()
            || first<source.camera.frames.front().time || last>source.camera.frames.back().time) return false;
        std::vector<demo_timeline::event> kills;
        for(const auto& e:source.kills) if(e.time>=first && e.time<=last) kills.push_back(e);
        // Identity comparison includes the converter's dynamic slot mapping.
        // Do not borrow names merely because two recordings share a filename.
        return !kills.empty() && kills==native.events;
    }
}
