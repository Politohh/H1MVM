#pragma once
#include "runtime_assets.hpp"
#include <utils/flags.hpp>
#include "theater_sky_gpu.hpp"
#include "theater_sky_cache.hpp"
#include <utils/cryptography.hpp>
#include <fstream>
#include <future>

namespace theater_sky
{
    class runtime
    {
        struct texture
        {
            game::GfxImage image{};
            std::string name;
            ~texture()
            {
                if (image.texture.shaderViewAlternate) image.texture.shaderViewAlternate->Release();
                if (image.texture.shaderView) image.texture.shaderView->Release();
                if (image.texture.map) image.texture.map->Release();
            }
        };
        std::vector<std::filesystem::path> files;
        std::vector<nlohmann::json> entries;
        std::filesystem::path game_directory, cache_directory;
        std::vector<std::string> names{"Original"};
        std::vector<const char*> choices;
        std::unordered_map<int,std::unique_ptr<texture>> textures;
        std::set<int> failed;
        std::mutex mutex;
        std::future<std::string> pending;
        int pending_selection{}, message_selection{};
        std::string message;
        static std::string hash(const std::string& bytes)
        {
            auto result=utils::cryptography::sha256::compute(bytes,true);
            std::transform(result.begin(),result.end(),result.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return result;
        }
    public:
        std::string feedback(int selection)
        {
            std::lock_guard lock(mutex);
            return selection && message_selection==selection ? message : "";
        }
        const char** discover()
        {
            game_directory=std::filesystem::current_path();
            cache_directory=runtime_assets::root().parent_path().parent_path()/"sky-cache";
            std::map<std::string,std::pair<std::filesystem::path,nlohmann::json>> presets;
            std::set<std::string> seen;
            try
            {
                std::string bytes;
                if (!runtime_assets::read("catalogs/skies.json",&bytes)) throw std::runtime_error("Missing sky preset index");
                const auto catalog=nlohmann::json::parse(bytes);
                if(catalog.at("version")!=1 || catalog.at("assets").size()>255) throw std::runtime_error("Unsupported sky preset index");
                for(const auto& entry:catalog.at("assets"))
                {
                    source::validate(entry);
                    const auto name=entry.at("name").get<std::string>();
                    presets.emplace(name,std::pair{std::filesystem::path{},entry}); seen.insert(name);
                }
            }
            catch(const std::exception& e) { console::error("Theater sky catalog: %s\n",e.what()); }
            // User-added presets extend the fixed stock catalog. Old extracted
            // copies of stock skies never mask a missing source/archive bug.
            if(!utils::flags::has_flag("bundled-only"))
            for (const auto& directory : {std::filesystem::path("demos/theater/skies")})
            {
                if (!std::filesystem::is_directory(directory)) continue;
                for (const auto& file:std::filesystem::directory_iterator(directory))
                {
                    if (!file.is_regular_file() || file.path().extension()!=".h1sky") continue;
                    if (seen.contains(file.path().stem().string())) continue;
                    try
                    {
                        std::ifstream input(file.path(),std::ios::binary); header h{};
                        input.read(reinterpret_cast<char*>(&h),sizeof(h));
                        if (!input || std::filesystem::file_size(file.path())!=sizeof(h)+h.size) continue;
                        (void)layout(h);
                        presets.emplace(file.path().stem().string(),std::pair{file.path(),nlohmann::json{}});
                    }
                    catch (const std::exception& e) { console::warn("Theater sky %s: %s\n",file.path().string().c_str(),e.what()); }
                }
            }
            for(const auto& [name, preset]:presets)
            {
                if(files.size()==255) break;
                names.push_back(name);files.push_back(preset.first);entries.push_back(preset.second);
            }
            for (const auto& name:names) choices.push_back(name.c_str());
            choices.push_back(nullptr);
            console::info("Theater: %zu native sky cubemaps available\n",files.size());
            return choices.data();
        }
        const game::GfxImage* replace(const game::GfxImage* source, bool active, int selection)
        {
            if (!active || !source || selection<1 || selection>static_cast<int>(files.size())) return source;
            const auto* world=*game::s_world;
            if (!world || world->skyCount<1 || world->skyCount>256 || !world->skies) return source;
            bool sky=false;
            for(int i=0;i<world->skyCount;++i) if(world->skies[i].skyImage==source) {sky=true;break;}
            if (!sky) return source;
            std::lock_guard lock(mutex);
            if (failed.contains(selection)) return source;
            if (const auto it=textures.find(selection);it!=textures.end()) return &it->second->image;
            try
            {
                if(!pending.valid())
                {
                    const auto entry=entries[selection-1];const auto file=files[selection-1];
                    const auto game=game_directory, cache=cache_directory;
                    pending_selection=selection;message_selection=selection;message="Preparing sky: "+names[selection];
                    pending=std::async(std::launch::async,[entry,file,game,cache] {
                        if(!entry.is_null()) return source::load(entry,game,cache,hash);
                        std::ifstream input(file,std::ios::binary|std::ios::ate);
                        const auto size=input.tellg();
                        if(!input || size<static_cast<std::streamoff>(sizeof(header)) || size>256*1024*1024+sizeof(header))
                            throw std::runtime_error("Invalid custom sky file size");
                        std::string bytes(static_cast<std::size_t>(size),'\0');input.seekg(0);input.read(bytes.data(),size);
                        if(!input) throw std::runtime_error("Cannot read custom sky file");
                        return bytes;
                    });
                }
                if(pending.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return source;
                std::string bytes;
                try { bytes=pending.get(); }
                catch(const std::exception& e)
                {
                    failed.insert(pending_selection);message_selection=pending_selection;message=e.what();
                    console::error("Theater sky: %s\n",e.what());return source;
                }
                if(pending_selection!=selection) return source;
                header h{};
                if(bytes.size()<sizeof(h)) throw std::runtime_error("Truncated sky file");
                std::memcpy(&h,bytes.data(),sizeof(h));(void)layout(h);
                if(bytes.size()!=sizeof(h)+h.size) throw std::runtime_error("Truncated sky pixels");
                auto texture=std::make_unique<struct texture>();texture->name=names[selection];
                auto& image=texture->image;
                image.imageFormat=static_cast<DXGI_FORMAT>(h.format);image.mapType=game::MAPTYPE_CUBE;
                image.semantic=2;image.category=3;image.flags=static_cast<unsigned char>(h.flags);
                image.width=static_cast<unsigned short>(h.width);image.height=static_cast<unsigned short>(h.height);
                image.depth=image.numElements=1;image.levelCount=static_cast<unsigned char>(h.mips);
                image.dataLen1=image.dataLen2=h.size;image.name=texture->name.c_str();
                if (FAILED(create_texture(*game::d3d11_device,h,bytes.data()+sizeof(h),&image.texture.map,&image.texture.shaderView)))
                    throw std::runtime_error("Cannot create native sky cubemap or shader view");
                // HDR BC6 skies have no sRGB variant. Both native view slots must
                // remain valid; the selected sky is retained until client shutdown.
                image.texture.shaderViewAlternate=image.texture.shaderView;image.texture.shaderViewAlternate->AddRef();
                const auto result=&image;textures.emplace(selection,std::move(texture));
                message_selection=selection;message="Sky ready: "+names[selection];
                console::info("Theater sky loaded: %s\n",result->name);return result;
            }
            catch(const std::exception& e)
            {
                failed.insert(selection);message_selection=selection;message=e.what();
                console::error("Theater sky: %s\n",e.what());return source;
            }
        }
    };
}
