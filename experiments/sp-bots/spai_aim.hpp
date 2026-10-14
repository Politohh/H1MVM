// Schema 7: server-authored upper-body pitch in the native agent snapshot.
// Included inside spai_probe. Ground alignment fields in older recordings are
// deliberately NOT interpreted as weapon aim.
inline bool uses_snapshot_aim()
{
    if(replay_requested())return replay_schema>=7;
    if(remote_requested())return remote_schema.load()>=7;
    return active_states && !remote_generation;
}

inline utils::hook::detour combat_aim_controller_hook;
inline utils::hook::detour combat_aim_producer_hook;
struct combat_aim_request
{
    float pitch{};
    int time{};
    unsigned generation{};
    const void* agent{};
    bool enabled{};
};
inline std::array<combat_aim_request,2048> combat_aim_requests{};
inline std::mutex combat_aim_mutex;
inline std::array<std::atomic<unsigned long long>,2048> combat_aim_observed_at{};

void clear_server_aim(unsigned id)
{
    std::lock_guard guard(combat_aim_mutex);
    if(id<combat_aim_requests.size())combat_aim_requests[id]={};
}
inline void clear_all_server_aim()
{
    std::lock_guard guard(combat_aim_mutex);
    combat_aim_requests.fill({});
}
inline bool owned_aim_entity(const unsigned char* entity)
{
    if(!entity || !active_states || game::VirtualLobby_Loaded())return false;
    const auto id=*reinterpret_cast<const unsigned short*>(entity);
    return id>=18 && id<42 && entity[10]==18 &&
        (*reinterpret_cast<const unsigned*>(entity+0x54)&0x1E001)==1;
}
inline int combat_aim_controller(const unsigned char* entity,void* character,float* output)
{
    const bool corpse=entity && entity[10]==19 &&
        *reinterpret_cast<const unsigned short*>(entity)>=50 &&
        *reinterpret_cast<const unsigned short*>(entity)<58 &&
        (*reinterpret_cast<const unsigned*>(entity+0x54)&0x1E001)==1;
    if(!uses_snapshot_aim() || (!owned_aim_entity(entity) && !corpse))
        return combat_aim_controller_hook.invoke<int>(entity,character,output);
    // Native payload is six angle triplets. Only back_low/back_mid tilt;
    // pelvis, root and auxiliary controllers remain neutral. Keep native IK.
    std::fill_n(output,18,0.0f);
    const auto state=(*reinterpret_cast<const unsigned*>(entity+0x54)>>1)&127;
    if(!corpse && (state==6 || state==7))
    {
        float pitch=*reinterpret_cast<const float*>(entity+0xb8);
        if(!std::isfinite(pitch) || std::abs(pitch)>30)pitch=0;
        output[0]=pitch*0.5f;output[3]=pitch*0.5f;
    }
    // Read-only, explicitly enabled acceptance telemetry. Character address
    // lets a verifier distinguish receiving-client controllers from server ones.
    static const bool observe=utils::flags::has_flag("spai-aim-observe");
    if(observe)
    {
        const auto id=*reinterpret_cast<const unsigned short*>(entity);
        const auto now=GetTickCount64();
        auto previous=combat_aim_observed_at[id].load();
        if(now-previous>=100 && combat_aim_observed_at[id].compare_exchange_strong(previous,now))
        {
            diagnostic("[SPAI_AIM_CONTROLLER] "+nlohmann::json({{"actor",id},
                {"character",reinterpret_cast<uintptr_t>(character)},{"state",state},
                {"replay",replay_requested()},{"pitch",*reinterpret_cast<const float*>(entity+0xb8)},
                {"back_low",output[0]},{"back_mid",output[3]}}).dump());
        }
    }
    return 1;
}
inline void combat_aim_producer(unsigned char* entity)
{
    combat_aim_producer_hook.invoke<void>(entity);
    if(!uses_snapshot_aim() || !owned_aim_entity(entity))return;
    const auto id=*reinterpret_cast<const unsigned short*>(entity);
    const auto state=(*reinterpret_cast<const unsigned*>(entity+0x54)>>1)&127;
    combat_aim_request request;
    {std::lock_guard guard(combat_aim_mutex);request=combat_aim_requests[id];}
    const auto elapsed=static_cast<long long>(*game::level_time)-request.time;
    const bool fresh=request.enabled && request.generation==generation_number &&
        request.agent==game::mp::g_entities[id].agent && elapsed>=0 && elapsed<=250 &&
        (state==6 || state==7);
    // Also clear native ground-slope pitch while there is no aim request.
    *reinterpret_cast<float*>(entity+0xb8)=fresh?request.pitch:0.0f;
}
inline bool set_server_aim(unsigned id,float pitch)
{
    if(id<18 || id>=42 || !game::SV_Loaded() || game::VirtualLobby_Loaded() ||
        !std::isfinite(pitch) || std::abs(pitch)>30)return false;
    const auto* entity=reinterpret_cast<const unsigned char*>(&game::mp::g_entities[id]);
    if(!uses_snapshot_aim() || !owned_aim_entity(entity) || !game::mp::g_entities[id].agent)return false;
    std::lock_guard guard(combat_aim_mutex);
    combat_aim_requests[id]={pitch,*game::level_time,generation_number,game::mp::g_entities[id].agent,true};
    return true;
}
