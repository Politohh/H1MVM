#pragma once
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace spai_scores
{
    // Version two adds assists; version one remains readable for older demos.
    // Only numbers cross serverinfo; names use the existing native agent slot.
    struct row { unsigned entity, team, alive, score, kills, deaths, assists; };
    constexpr size_t max_payload = 1024;

    inline bool decode(std::string_view text, std::vector<row>& result, unsigned version=2)
    {
        result.clear();
        if(text.size()>max_payload || (version!=1 && version!=2))return false;
        const size_t fields=version==1?6:7;
        std::array<bool,24> seen{};
        size_t cursor=0;
        std::vector<row> parsed;
        while(cursor<text.size())
        {
            if(parsed.size()==24)return false;
            std::array<unsigned,7> values{};
            constexpr std::array<unsigned,7> limits{41,2,1,2147483647u,1000000,1000000,1000000};
            for(size_t field=0;field<fields;++field)
            {
                const auto start=cursor;
                while(cursor<text.size() && text[cursor]>='0' && text[cursor]<='9')
                {
                    const auto digit=static_cast<unsigned>(text[cursor++]-'0');
                    if(values[field]>limits[field]/10 ||
                       (values[field]==limits[field]/10 && digit>limits[field]%10))return false;
                    values[field]=values[field]*10+digit;
                    if(values[field]>limits[field])return false;
                }
                if(cursor==start || cursor-start>10 || (cursor-start>1 && text[start]=='0') ||
                   cursor==text.size() || text[cursor++]!=(field+1==fields?';':','))return false;
            }
            if(values[0]<18 || seen[values[0]-18])return false;
            seen[values[0]-18]=true;
            parsed.push_back({values[0],values[1],values[2],values[3],values[4],values[5],values[6]});
        }
        result=std::move(parsed);
        return true;
    }

    inline bool decode_envelope(std::string_view payload, std::string_view epoch, std::vector<row>& result)
    {
        result.clear();
        if(epoch.empty() || epoch.size()>10 || (epoch.size()>1 && epoch[0]=='0'))return false;
        for(const auto ch:epoch)if(ch<'0' || ch>'9')return false;
        if(payload.size()<2 || (payload[0]!='1' && payload[0]!='2') || payload[1]!=':')return false;
        const auto version=static_cast<unsigned>(payload[0]-'0');
        const auto prefix=std::to_string(version)+":"+std::string(epoch)+":";
        if(payload.size()<prefix.size() || payload.substr(0,prefix.size())!=prefix)return false;
        return decode(payload.substr(prefix.size()),result,version);
    }
}
