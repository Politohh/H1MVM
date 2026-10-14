#pragma once
#include "component/demo/cod4_character_catalog.hpp"

namespace spai_character_assets
{
    inline std::mutex census_mutex;
    inline std::set<std::string> census_models;
    inline std::atomic<std::uint64_t> census_generation{1};

    inline size_t release_census()
    {
        std::lock_guard lock(census_mutex);
        ++census_generation; // Invalidate queued requests and pumps, including same-map restarts.
        const auto count=census_models.size();
        for(const auto& model:census_models)
            utils::hook::invoke<void>(game::base_address+0x76850,model.c_str());
        census_models.clear();
        return count;
    }

    inline bool allowed(const std::string& model)
    {
        return std::any_of(cod4::native_characters.begin(),cod4::native_characters.end(),
            [&](const auto& item){return model==item.body || model==item.head;});
    }

    // Select the stock default male body through the installed costume rules.
    // Paired heads come from the verified MP catalog, never filename guessing.
    inline const cod4::character_assets* select(const std::string& environment,
        const std::string& team, std::string role)
    {
        if(team!="allies" && team!="axis")return nullptr;
        if(role=="rifle")role="assault";
        if(role=="mg")role="lmg";
        if(environment.empty() || !game::DB_XAssetExists(game::ASSET_TYPE_STRINGTABLE,"mp/costumemodeltable.csv"))return nullptr;
        auto* table=game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,"mp/costumemodeltable.csv",0).stringTable;
        if(!table || table->columnCount<21 || table->rowCount<1 || table->rowCount>16384)return nullptr;
        const cod4::character_assets* selected=nullptr;
        for(int row=1;row<table->rowCount;++row)
        {
            const auto cell=[&](int column)->std::string_view
            {const auto* value=game::StringTable_GetColumnValueForRow(table,row,column);return value?value:"";};
            if(cell(1)!="shirt" || cell(2)!="1" || cell(10)!=environment ||
                cell(11)!=(team=="allies"?"ally":"!ally") || cell(12)!=role)continue;
            const auto model=cell(0);
            for(const auto& item:cod4::native_characters)
                if(item.body==model)
                {
                    if(selected && selected!=&item)throw std::runtime_error("Ambiguous native character selection");
                    selected=&item;
                }
        }
        return selected;
    }

    inline bool ready(const std::string& name)
    {
        if(!game::Sys_IsDatabaseReady2() || !allowed(name) || !game::DB_XAssetExists(game::ASSET_TYPE_XMODEL,name.c_str()) ||
            game::DB_IsXAssetDefault(game::ASSET_TYPE_XMODEL,name.c_str()))return false;
        const auto* model=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,name.c_str(),0).model;
        const bool head=std::any_of(cod4::native_characters.begin(),cod4::native_characters.end(),
            [&](const auto& item){return name==item.head;});
        if(!model || !model->boneNames || !model->baseMat || !model->parentList || model->numBones<(head?3:10))return false;
        // Precaching can leave a named two-bone placeholder. Require the real
        // attachment chains before inspecting compatibility or spawning an actor.
        const std::vector<const char*> required_bones=head
            ? std::vector<const char*>{"j_spine4","j_neck","j_head"}
            : std::vector<const char*>{"pelvis","torso_stabilizer","j_spinelower","j_wrist_ri","tag_weapon_right"};
        for(const auto* required:required_bones)
        {
            bool found=false;
            for(unsigned i=0;i<model->numBones;++i)
                if(!std::strcmp(game::SL_ConvertToString(model->boneNames[i]),required)){found=true;break;}
            if(!found)return false;
        }
        return true;
    }

    // Resolve installed ownership through the same native model registry used
    // by Theater. Never invent a filename from the user-facing model name.
    inline std::set<std::string> zones(const std::string& model)
    {
        if(!allowed(model))throw std::runtime_error("Character outside native MP catalog");
        std::array<std::uint16_t,8> entries{};
        const auto count=utils::hook::invoke<int>(game::base_address+0x75210,model.c_str(),7,entries.data());
        if(count<0 || count>static_cast<int>(entries.size()))throw std::runtime_error("Invalid character registry count");
        std::set<std::string> result;
        for(int i=0;i<count;++i)
        {
            if(entries[i]>=9216)throw std::runtime_error("Invalid character registry index");
            const auto zone=*reinterpret_cast<const std::uint16_t*>(game::base_address+0x152E0B0+entries[i]*12+6);
            if(zone>=7144)throw std::runtime_error("Invalid character zone index");
            const auto* record=reinterpret_cast<const unsigned char*>(game::base_address+0x14D6D40+zone*48);
            if(record[8]>=13)throw std::runtime_error("Invalid character zone category");
            // Initialized MP registry capture: hash at 0, indices at 4/6,
            // category at 8, inline NUL-terminated name at 9 (39 bytes).
            // It is not a pointer, unlike the public fastfile asset records.
            const auto* name=reinterpret_cast<const char*>(record+9);
            const std::string value(name,strnlen(name,39));
            if(value.empty() || value.size()>=39 || !value.starts_with("mp_") || !value.ends_with("_tr") ||
                !std::all_of(value.begin(),value.end(),[](unsigned char c){return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_';}))
                throw std::runtime_error("Invalid installed character zone name");
            result.insert(value);
        }
        return result;
    }

    inline void validate_budget(const std::set<std::string>& models)
    {
        std::map<unsigned,std::set<unsigned>> required;
        for(const auto& model:models)
        {
            (void)zones(model); // Validate each registry record before dereferencing it below.
            std::array<std::uint16_t,8> entries{};
            const auto count=utils::hook::invoke<int>(game::base_address+0x75210,model.c_str(),7,entries.data());
            for(int i=0;i<count;++i)
            {
                const auto zone=*reinterpret_cast<const std::uint16_t*>(game::base_address+0x152E0B0+entries[i]*12+6);
                const auto category=*reinterpret_cast<const std::uint8_t*>(game::base_address+0x14D6D40+zone*48+8);
                required[category].insert(zone);
            }
        }
        for(const auto& [category,entries]:required)
        {
            const auto* pool=reinterpret_cast<const std::uint8_t*>(game::base_address+0x14D4BF0+category*112);
            const auto capacity=*reinterpret_cast<const unsigned*>(pool+0x48);
            if(entries.size()>capacity)throw std::runtime_error("Character census exceeds native transient category capacity");
        }
    }
}
