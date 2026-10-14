// Isolated fixed-angle experiment. No production aiming or replay contract.
    inline utils::hook::detour aim_controller_probe_hook;
    inline std::atomic<float> aim_probe_pitch{0};
    inline std::atomic<unsigned> aim_probe_calls{0};
    inline bool aim_probe_enabled=false;
    inline std::atomic<bool> entity_aim_probe{false};

    inline int fixed_aim_controller(const unsigned char* entity, void* character, float* output)
    {
        if(!active_states || !entity || entity[10]!=18 ||
            (*reinterpret_cast<const unsigned*>(entity+0x54)&0x1E001)!=1)
            return aim_controller_probe_hook.invoke<int>(entity,character,output);
        // Native payload: four bone angle triplets, then two auxiliary triplets.
        // Preserve pelvis and auxiliary transforms; only back_low/back_mid tilt.
        std::fill_n(output,18,0.0f);
        const auto state=(*reinterpret_cast<const unsigned*>(entity+0x54)>>1)&0x7f;
        if(state==6 || state==7)
        {
            float pitch=entity_aim_probe.load()?*reinterpret_cast<const float*>(entity+0xb8):aim_probe_pitch.load();
            if(!std::isfinite(pitch) || std::abs(pitch)>30)pitch=0;
            output[0]=pitch*0.5f;
            output[3]=pitch*0.5f;
            ++aim_probe_calls;
        }
        return 1;
    }

    inline utils::hook::detour hand_ik_probe_hook;
    inline std::atomic<bool> hand_ik_probe_skip{false};
    inline std::atomic<unsigned> hand_ik_probe_skips{0};
    inline void probe_hand_ik(unsigned char* obj,void* data,void* bits)
    {
        bool owned=false;
        if(obj && hand_ik_probe_skip.load())
        {
            std::lock_guard guard(lifetime_mutex);
            owned=live_private_trees.contains(*reinterpret_cast<void**>(obj));
        }
        if(owned){++hand_ik_probe_skips;return;}
        hand_ik_probe_hook.invoke<void>(obj,data,bits);
    }

    inline void capture_fixed_aim_pose(unsigned entity, float expected_pitch)
    {
        if(!game::SV_Loaded() || game::VirtualLobby_Loaded() || expected_pitch!=aim_probe_pitch.load())return;
        auto* centity=utils::hook::invoke<unsigned char*>(game::base_address+0x6BFF0,0,static_cast<short>(entity));
        if(!centity || !(*reinterpret_cast<const unsigned*>(centity+0x23C)&1))return;
        auto* obj=utils::hook::invoke<void*>(game::base_address+0x4E98E0,
            *reinterpret_cast<const unsigned short*>(centity+0x13C),0);
        if(!obj)return;
        nlohmann::json result={{"entity_yaw",*reinterpret_cast<const float*>(centity+0x13c+0xb4)},{"entity_pitch",*reinterpret_cast<const float*>(centity+0x13c+0xb8)},{"entity",entity},{"pitch",expected_pitch},{"ik_skipped",hand_ik_probe_skip.load()},{"ik_skip_calls",hand_ik_probe_skips.load()}};
        for(const auto* name:{"pelvis","j_ankle_le","j_spineupper","j_head","j_wrist_ri","j_wrist_le","tag_ik_target","tag_weapon_right","tag_ik_loc_le"})
        {
            float axis[3][3]{},position[3]{};
            const auto tag=game::SL_GetString(name,0);
            const bool valid=utils::hook::invoke<bool>(game::base_address+0xF3D20,centity,obj,tag,axis,position);
            game::VariableUnion value{};value.stringValue=tag;game::RemoveRefToValue(game::VAR_STRING,value);
            if(!valid){if(std::string_view(name).starts_with("tag_")){result[name]=nullptr;continue;}diagnostic("[SPAI] FAILED client aim pose tag="+std::string(name));return;}
            result[name]={position[0],position[1],position[2]};
        }
        diagnostic("[SPAI_AIM_CLIENT] "+result.dump());
    }

    // Opt-in producer experiment only. Write before snapshot capture, never
    // rewrite delta packets or rely on a client-only target cache.
    inline utils::hook::detour snapshot_aim_probe_hook;
    struct snapshot_aim_request {float pitch{};int time{};unsigned generation{};bool enabled{};};
    inline std::array<snapshot_aim_request,2048> snapshot_aim_requests{};
    inline std::mutex snapshot_aim_mutex;
    inline void snapshot_aim_producer(unsigned char* entity)
    {
        snapshot_aim_probe_hook.invoke<void>(entity);
        if(!entity || !active_states || game::VirtualLobby_Loaded() || entity[10]!=18)return;
        const auto packed=*reinterpret_cast<const unsigned*>(entity+0x54);
        const auto id=*reinterpret_cast<const unsigned short*>(entity);
        if(id<18 || id>=42 || (packed&0x1E001)!=1)return;
        snapshot_aim_request request;
        {std::lock_guard guard(snapshot_aim_mutex);request=snapshot_aim_requests[id];}
        if(!request.enabled || request.generation!=generation_number)return;
        const auto elapsed=static_cast<long long>(*game::level_time)-request.time;
        const auto state=(packed>>1)&127;
        const bool fresh=elapsed>=0 && elapsed<=250 && (state==6 || state==7);
        *reinterpret_cast<float*>(entity+0xb8)=fresh?request.pitch:0.0f;
    }
