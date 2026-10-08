#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
#include "component/demo/theater_sky_cache.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>

using namespace theater_sky;
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
std::string hash(const std::string& bytes)
{
    std::array<unsigned char,32> digest{};
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
        static_cast<ULONG>(bytes.size()),digest.data(),static_cast<ULONG>(digest.size()))<0) throw std::runtime_error("SHA256 failed");
    std::ostringstream result;result<<std::hex<<std::setfill('0');
    for(const auto c:digest) result<<std::setw(2)<<static_cast<unsigned>(c);
    return result.str();
}
template<typename F> void rejects(F action)
{
    try { action(); } catch(const std::exception&) { return; }
    throw std::runtime_error("Invalid sky input was accepted");
}
int main(int argc,char** argv)
{
    try
    {
        require(argc==4,"Usage: theater_sky_source <game> <index.json> <disposable-cache>");
        const std::filesystem::path game=argv[1],cache=argv[3];
        std::ifstream input(argv[2]);const auto catalog=nlohmann::json::parse(input);
        require(catalog.at("version")==1 && catalog.at("assets").size()==45,"Incomplete preset index");
        const std::array<std::uint8_t,10> literals{0x90,'a','b','c','d','e','f','g','h','i'};
        require(source::decode(literals,9)==std::vector<std::uint8_t>({'a','b','c','d','e','f','g','h','i'}),"Literal decode failed");
        const std::array<std::uint8_t,11> overlap{0x14,'a',1,0,0x60,'1','2','3','4','5','6'};
        const auto decoded=source::decode(overlap,15);
        require(std::string(decoded.begin(),decoded.end())=="aaaaaaaaa123456","Overlapping LZ4 match failed");
        const std::array<std::uint8_t,3> invalid{0,0,0};
        rejects([&]{ source::decode(invalid,8); });
        rejects([&]{ source::decode(std::span(literals).first(4),9); });
        rejects([&]{ source::decode(literals,8); });
        unsigned count=0;std::size_t bytes=0;
        for(const auto& entry:catalog.at("assets"))
        {
            const auto extracted=source::load(entry,game,cache,hash);
            require(hash(extracted)==entry.at("sha256").get<std::string>(),"Extracted sky hash mismatch");
            // A warm cache must work without reading ANY original game archive.
            require(source::load(entry,cache/"no-game-files",cache,hash)==extracted,"Cache hit changed pixels");
            ++count;bytes+=extracted.size();
        }
        auto first=catalog.at("assets").at(0);
        const auto cached=cache/first.at("sha256").get<std::string>()/(first.at("name").get<std::string>()+".h1sky");
        const auto original=source::load(first,game,cache,hash);
        { std::ofstream output(cached,std::ios::binary|std::ios::trunc);auto bad=original;bad.back()^=1;output.write(bad.data(),bad.size()); }
        rejects([&]{ source::load(first,cache/"no-game-files",cache,hash); });
        require(source::load(first,game,cache,hash)==original,"Corrupted cache did not repair");
        auto bad=first;bad["zone"]="../escape";rejects([&]{source::validate(bad);});
        bad=first;bad["blocks"][0]["segments"][0][0]=first["source_size"];rejects([&]{source::validate(bad);});
        bad=first;bad["blocks"][0]["take"]=0;rejects([&]{source::validate(bad);});
        bad=first;bad["blocks"][0]["skip"]=65537;rejects([&]{source::validate(bad);});
        bad=first;bad["sha256"]=std::string(64,'0');rejects([&]{source::load(bad,game,cache,hash);});
        bad=first;bad["source_size"]=first["source_size"].get<std::uint64_t>()+1;rejects([&]{source::extract(bad,game);});
        std::cout<<"PASS "<<count<<" stock skies, "<<bytes<<" identical bytes, independent warm cache, corrupted cache repair, missing/wrong archives, hash rejection and bounded LZ4/index validation\n";
        return 0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
