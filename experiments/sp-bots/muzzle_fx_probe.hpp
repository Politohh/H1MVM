// Read-only observer of the native bolted effect creator, research builds only.
inline utils::hook::detour muzzle_fx_probe_hook;
inline std::atomic<unsigned> muzzle_fx_stack_seen{0};
inline utils::hook::detour muzzle_consumer_probe_hook;
inline utils::hook::detour muzzle_tag_probe_hook;
inline utils::hook::detour muzzle_composition_probe_hook;
inline int observe_muzzle_composition(int client,void* obj,const unsigned char* entity,unsigned char* character,
    int mask,unsigned primary,unsigned stowed,unsigned extra,unsigned offhand,unsigned execution)
{
    const auto result=muzzle_composition_probe_hook.invoke<int>(client,obj,entity,character,mask,primary,stowed,extra,offhand,execution);
    if(entity && entity[10]==18 && active_states)
    {
        const auto id=*reinterpret_cast<const unsigned short*>(entity);
        auto* actual=utils::hook::invoke<void*>(game::base_address+0x4E98E0,id,client);
        auto models=nlohmann::json::array();
        if(actual)
        {
            const auto count=utils::hook::invoke<unsigned>(game::base_address+0x5C7780,actual);
            for(unsigned i=0;i<count && i<32;++i)
                if(const auto* model=utils::hook::invoke<game::XModel*>(game::base_address+0x5C7730,actual,i))models.push_back(model->name);
        }
        nlohmann::json sample={{"actor",id},{"primary",primary},{"stowed",stowed},{"offhand",offhand},
            {"mask",mask},{"flags",*reinterpret_cast<const unsigned*>(entity+0x5c)},
            {"recorded",*reinterpret_cast<const unsigned*>(entity+0x4c)},{"models",models}};
        static std::unordered_set<std::string> observed;
        const auto text=sample.dump();
        if(observed.size()<256 && observed.insert(text).second)diagnostic("[SPAI_FX_COMPOSITION] "+text);
    }
    return result;
}
inline void observe_muzzle_consumer(int client, unsigned char* entity, unsigned weapon,
    unsigned alternate, int event, int viewmodel, int allowed)
{
    const auto number=*reinterpret_cast<const unsigned short*>(entity+0x13c);
    const auto before=*reinterpret_cast<const unsigned*>(entity+0x23c);
    muzzle_consumer_probe_hook.invoke<void>(client,entity,weapon,alternate,event,viewmodel,allowed);
    if(number>=18 && number<42 && (before&12))
        diagnostic("[SPAI_FX_CONSUMER] actor="+std::to_string(number)+" weapon="+std::to_string(weapon)+
            " alternate="+std::to_string(alternate)+" event="+std::to_string(event)+
            " viewmodel="+std::to_string(viewmodel)+" allowed="+std::to_string(allowed)+
            " before="+std::to_string(before)+" after="+std::to_string(*reinterpret_cast<const unsigned*>(entity+0x23c)));
}
inline void observe_muzzle_tag(int client,const void* definition,unsigned entity,unsigned tag)
{
    if(entity>=18 && entity<42)
    {
        auto* obj=utils::hook::invoke<void*>(game::base_address+0x4E98E0,entity,client);
        unsigned char bone=254;
        const auto found=obj?utils::hook::invoke<int>(game::base_address+0x5C7530,obj,tag,&bone):0;
        diagnostic("[SPAI_FX_TAG] actor="+std::to_string(entity)+" dobj="+std::to_string(obj!=nullptr)+
            " tag="+std::string(game::SL_ConvertToString(tag))+" found="+std::to_string(found)+
            " bone="+std::to_string(bone)+" effect="+std::string(definition?*reinterpret_cast<const char* const*>(definition):"<null>"));
    }
    muzzle_tag_probe_hook.invoke<void>(client,definition,entity,tag);
}
inline void* observe_bolted_effect(int client, const void* definition, int time,
    unsigned entity, unsigned bone, unsigned model, unsigned viewmodel,
    unsigned flags, unsigned mark_entity)
{
    auto* result=muzzle_fx_probe_hook.invoke<void*>(client,definition,time,entity,bone,model,viewmodel,flags,mark_entity);
    if(definition && entity>=18 && entity<42)
    {
        const auto* name=*reinterpret_cast<const char* const*>(definition);
        if(name && std::string_view(name).find("muzzleflash")!=std::string_view::npos &&
            !(muzzle_fx_stack_seen.fetch_or(1u<<(entity-18))&(1u<<(entity-18))))
        {
            void* stack[12]{};
            const auto count=CaptureStackBackTrace(0,12,stack,nullptr);
            std::ostringstream trace;trace<<"[SPAI_FX_STACK] actor="<<entity;
            for(unsigned i=0;i<count;++i)trace<<" "<<stack[i];
            diagnostic(trace.str());
        }
        diagnostic("[SPAI_BOLTED_FX] actor="+std::to_string(entity)+" bone="+std::to_string(bone)+
            " time="+std::to_string(time)+" effect="+std::string(name?name:"<null>")+
            " created="+std::to_string(result!=nullptr));
    }
    return result;
}
