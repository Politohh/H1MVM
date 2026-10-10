#pragma once
#include <charconv>
#include <optional>
#include <string>
#include <string_view>

namespace demo_events
{
    struct event { int time{}; std::string kind, label, attacker, victim, weapon; unsigned cause{}; };
    inline std::string hex(std::string_view text)
    {
        constexpr char digits[]="0123456789abcdef"; std::string result;
        for(unsigned char c:text) { result+=digits[c>>4];result+=digits[c&15]; }
        return result;
    }
    inline std::optional<std::string> unhex(std::string_view text)
    {
        if(text.size()>200 || text.size()%2) return {};
        std::string result;
        for(std::size_t i=0;i<text.size();i+=2)
        {
            unsigned value{};const auto parsed=std::from_chars(text.data()+i,text.data()+i+2,value,16);
            if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+i+2 || value<32 || value==127) return {};
            result+=static_cast<char>(value);
        }
        return result;
    }
    inline std::string clean(std::string_view text)
    {
        std::string result;
        for (std::size_t i = 0; i < text.size() && result.size() < 100; ++i)
        {
            if (text[i] == '^' && i + 1 < text.size() && text[i+1] >= '0' && text[i+1] <= '9') { ++i; continue; }
            if (static_cast<unsigned char>(text[i]) >= 32 && text[i] != 127) result += text[i];
        }
        return result;
    }
    inline std::optional<event> parse(std::string_view record)
    {
        if(record.starts_with("@event2:"))
        {
            std::string_view fields[5];auto text=record.substr(8);
            for(unsigned i=0;i<4;++i)
            { const auto end=text.find(':');if(end==text.npos)return {};fields[i]=text.substr(0,end);text.remove_prefix(end+1); }
            fields[4]=text;
            int time{};unsigned cause{};
            const auto t=std::from_chars(fields[0].data(),fields[0].data()+fields[0].size(),time);
            const auto c=std::from_chars(fields[4].data(),fields[4].data()+fields[4].size(),cause);
            const auto attacker=unhex(fields[1]),victim=unhex(fields[2]);
            if(t.ec!=std::errc{} || t.ptr!=fields[0].data()+fields[0].size() || time<0 || c.ec!=std::errc{}
                || c.ptr!=fields[4].data()+fields[4].size() || cause>256 || !attacker || !victim
                || attacker->empty() || victim->empty() || fields[3].size()>96
                || fields[3].find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=fields[3].npos) return {};
            return event{time,"kill",clean(*attacker+" > "+*victim),*attacker,*victim,std::string(fields[3]),cause};
        }
        if (!record.starts_with("@event:")) return {};
        const auto a = record.find(':', 7), b = a == record.npos ? a : record.find(':', a+1);
        if (a == record.npos || b == record.npos) return {};
        int time{};
        const auto value = std::from_chars(record.data()+7, record.data()+a, time);
        const auto kind = record.substr(a+1,b-a-1), label = record.substr(b+1);
        if (value.ec != std::errc{} || value.ptr != record.data()+a || time < 0 || label.empty() || label.size() > 100
            || (kind != "kill" && kind != "round_start" && kind != "round_end")) return {};
        return event{time,std::string(kind),clean(label)};
    }
    inline std::string encode(const event& e)
    {
        if(e.kind=="kill" && !e.attacker.empty() && !e.victim.empty())
            return "@event2:"+std::to_string(e.time)+":"+hex(clean(e.attacker))+":"+hex(clean(e.victim))+":"+e.weapon+":"+std::to_string(e.cause);
        return "@event:" + std::to_string(e.time) + ":" + e.kind + ":" + clean(e.label);
    }
}
