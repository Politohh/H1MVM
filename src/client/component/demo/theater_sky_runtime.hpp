#pragma once
#include "runtime_assets.hpp"
#include <utils/flags.hpp>
#include "theater_sky_gpu.hpp"
#include <fstream>

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
        std::vector<std::string> names{"Original"};
        std::vector<const char*> choices;
        std::unordered_map<int,std::unique_ptr<texture>> textures;
        std::set<int> failed;
        std::mutex mutex;
    public:
        const char** discover()
        {
            std::set<std::string> seen;
            for (const auto& directory : {runtime_assets::path("demos/theater/skies"), std::filesystem::path("demos/theater/skies")})
            {
                if (directory.is_relative() && utils::flags::has_flag("bundled-only")) continue;
                if (!std::filesystem::is_directory(directory)) continue;
                for (const auto& file:std::filesystem::directory_iterator(directory))
                {
                    if (!file.is_regular_file() || file.path().extension()!=".h1sky") continue;
                    if (seen.contains(file.path().filename().string())) continue;
                    try
                    {
                        std::ifstream input(file.path(),std::ios::binary); header h{};
                        input.read(reinterpret_cast<char*>(&h),sizeof(h));
                        if (!input || std::filesystem::file_size(file.path())!=sizeof(h)+h.size) continue;
                        (void)layout(h);files.push_back(file.path());seen.insert(file.path().filename().string());
                    }
                    catch (const std::exception& e) { console::warn("Theater sky %s: %s\n",file.path().string().c_str(),e.what()); }
                }
            }
            std::sort(files.begin(),files.end(),[](const auto& a,const auto& b) { return a.filename()<b.filename(); });
            if (files.size()>255) files.resize(255);
            for (const auto& file:files) names.push_back(file.stem().string());
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
                std::ifstream input(files[selection-1],std::ios::binary);header h{};
                input.read(reinterpret_cast<char*>(&h),sizeof(h));(void)layout(h);
                if (!input || std::filesystem::file_size(files[selection-1])!=sizeof(h)+h.size) throw std::runtime_error("Truncated sky file");
                std::vector<char> pixels(h.size);input.read(pixels.data(),pixels.size());
                if (!input) throw std::runtime_error("Cannot read sky pixels");
                auto texture=std::make_unique<struct texture>();texture->name=names[selection];
                auto& image=texture->image;
                image.imageFormat=static_cast<DXGI_FORMAT>(h.format);image.mapType=game::MAPTYPE_CUBE;
                image.semantic=2;image.category=3;image.flags=static_cast<unsigned char>(h.flags);
                image.width=static_cast<unsigned short>(h.width);image.height=static_cast<unsigned short>(h.height);
                image.depth=image.numElements=1;image.levelCount=static_cast<unsigned char>(h.mips);
                image.dataLen1=image.dataLen2=h.size;image.name=texture->name.c_str();
                if (FAILED(create_texture(*game::d3d11_device,h,pixels.data(),&image.texture.map,&image.texture.shaderView)))
                    throw std::runtime_error("Cannot create native sky cubemap or shader view");
                // HDR BC6 skies have no sRGB variant. Both native view slots must
                // remain valid; the selected sky is retained until client shutdown.
                image.texture.shaderViewAlternate=image.texture.shaderView;image.texture.shaderViewAlternate->AddRef();
                const auto result=&image;textures.emplace(selection,std::move(texture));
                console::info("Theater sky loaded: %s\n",result->name);return result;
            }
            catch(const std::exception& e)
            {
                failed.insert(selection);console::error("Theater sky: %s\n",e.what());return source;
            }
        }
    };
}
