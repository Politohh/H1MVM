// Included inside spai_probe after the shared storage and native helpers.
// The wire schema owns only class zero for an explicitly negotiated SP session.
// Never mutate the stock AnimationClass asset on a receiving client.
utils::hook::detour remote_class_hook;
utils::hook::detour remote_entity_class_hook;
game::AnimationClass remote_class{};
game::AnimationStateMachine remote_machine{};
std::array<game::AnimationState,max_states> remote_states{};
std::array<game::AnimationEntry,max_states> remote_entries{};
std::array<unsigned __int64,max_states> remote_indices{};
void* remote_source_tree=nullptr;
utils::hook::detour server_system_info_hook;
utils::hook::detour client_system_info_hook;
std::string remote_match_epoch;
std::string server_character_epoch,server_character_manifest;
std::set<std::string> remote_character_models,remote_character_requests;
std::string remote_character_manifest;
std::chrono::steady_clock::time_point remote_load_started{};
bool remote_character_ready=false;
game::AnimationClass* schema_class(game::AnimationClass* original);
utils::hook::detour character_registry_reset_hook;
std::atomic<std::uint64_t> remote_registry_generation{1};
std::uint64_t remote_owned_registry_generation=0;
void character_registry_reset()
{
    // Native DB/map reload destroys the name registry and rebuilds its zeroed
    // reference counts. Ownership in that registry cannot cross this boundary.
    ++remote_registry_generation;
    character_registry_reset_hook.invoke<void>();
}
void discard_stale_character_requests()
{
    const auto generation=remote_registry_generation.load();
    if(remote_character_requests.empty() || remote_owned_registry_generation==generation)return;
    diagnostic("[SPAI_CLIENT_RIG] invalidated models="+std::to_string(remote_character_requests.size())+
        " epoch="+remote_match_epoch+" registry="+std::to_string(remote_owned_registry_generation)+" next="+std::to_string(generation));
    remote_character_requests.clear();remote_character_ready=false;
}

bool decode_character_manifest(const std::string& text,std::set<std::string>& models)
{
    models.clear();
    if(text.empty() || text.size()>2048)return false;
    size_t begin=0;
    while(begin<text.size())
    {
        const auto end=text.find(',',begin);
        const auto name=text.substr(begin,end==std::string::npos?end:end-begin);
        if(!spai_character_assets::allowed(name) || !models.insert(name).second || models.size()>32)return false;
        if(end==std::string::npos)return true;
        begin=end+1;
    }
    return false; // Reject a trailing delimiter, not a partially valid list.
}

bool publish_character_manifest(const std::string& text)
{
    // Registered only as a server GSC builtin. Script main precaches assets
    // before SV_Loaded becomes true; the initial serverinfo is emitted later.
    if(game::VirtualLobby_Loaded())return false;
    std::set<std::string> models;
    if(!decode_character_manifest(text,models))return false;
    const auto epoch=std::to_string(*game::mp::sv_serverId_value);
    if(server_character_epoch==epoch && !server_character_manifest.empty() && server_character_manifest!=text)return false;
    server_character_epoch=epoch;server_character_manifest=text;
    const auto info=score_serverinfo();
    if(!info.empty())game::SV_SetConfigstring(0,info.c_str());
    diagnostic("[SPAI_CLIENT_RIG] manifest published epoch="+epoch+" models="+text);
    return true;
}

void release_remote_characters()
{
    discard_stale_character_requests();
    const auto count=remote_character_requests.size();
    for(const auto& model:remote_character_requests)
        utils::hook::invoke<void>(game::base_address+0x76850,model.c_str());
    remote_character_requests.clear();remote_character_models.clear();
    remote_character_manifest.clear();remote_character_ready=false;remote_load_started={};
    if(count)diagnostic("[SPAI_CLIENT_RIG] released models="+std::to_string(count)+" epoch="+remote_match_epoch);
}

// Called before native CG_DrawActiveFrame processes/evaluates snapshots.
// Never replace a named placeholder after an actor DObj has been rendered.
bool remote_models_ready()
{
    if(!utils::flags::has_flag("spai-probe") || (!remote_requested() && !replay_rigs_requested()))return true;
    // Map initialization can call the draw entry while replacing client state.
    // Wait until its native asset releases have completed before taking refs.
    if(!game::CL_IsCgameInitialized() || *game::mp::connstate!=game::CA_ACTIVE)return false;
    discard_stale_character_requests();
    if(remote_character_ready)return true;
    if(remote_load_started==std::chrono::steady_clock::time_point{})remote_load_started=std::chrono::steady_clock::now();
    if(std::chrono::steady_clock::now()-remote_load_started>std::chrono::seconds(20))
    {game::Com_Error(game::ERR_DROP,"SP Bots character preload timed out.");return false;}
    if(remote_character_models.empty())return false;
    try
    {
        if(remote_character_requests.empty())
        {
            if(!game::Sys_IsDatabaseReady2())return false;
            spai_character_assets::validate_budget(remote_character_models);
            remote_owned_registry_generation=remote_registry_generation.load();
            for(const auto& model:remote_character_models)
            {
                utils::hook::invoke<void>(game::base_address+0x75720,model.c_str());
                remote_character_requests.insert(model);
                diagnostic("[SPAI_CLIENT_RIG] acquired model="+model+" epoch="+remote_match_epoch);
            }
        }
        utils::hook::invoke<void>(game::base_address+0x757A0);
        // Pump even when the DB is busy: this completes a pending transient
        // load/unload. Gating the pump on DB-ready can deadlock same-map reloads.
        static std::chrono::steady_clock::time_point last_report{};
        const auto now=std::chrono::steady_clock::now();
        const bool report=now-last_report>std::chrono::seconds(1);
        if(report)last_report=now;
        bool ready=true;
        for(const auto& model:remote_character_models)
        {
            const bool registry=utils::hook::invoke<bool>(game::base_address+0x75470,model.c_str());
            const bool bones=spai_character_assets::ready(model);
            if(!registry || !bones)
            {
                ready=false;
                if(report)diagnostic("[SPAI_CLIENT_RIG] preload pending model="+model+" registry="+std::to_string(registry)+
                    " bones="+std::to_string(bones)+" db="+std::to_string(game::Sys_IsDatabaseReady2())+" epoch="+remote_match_epoch);
                if(report && !registry)
                {
                    std::array<std::uint16_t,8> entries{};
                    const int count=utils::hook::invoke<int>(game::base_address+0x75210,model.c_str(),7,entries.data());
                    for(int i=0;i<count && i<8;++i)
                    {
                        const auto* entry=reinterpret_cast<const unsigned char*>(game::base_address+0x152E0B0+entries[i]*12);
                        const auto zone=*reinterpret_cast<const std::uint16_t*>(entry+6);
                        const auto* record=reinterpret_cast<const unsigned char*>(game::base_address+0x14D6D40+zone*48);
                        diagnostic("[SPAI_CLIENT_RIG] preload registry model="+model+" refs="+std::to_string(entry[8])+" dirty="+std::to_string(entry[9])+
                            " zone="+std::string(reinterpret_cast<const char*>(record+9),strnlen(reinterpret_cast<const char*>(record+9),39))+
                            " state="+std::to_string(record[44])+" zone_refs="+std::to_string(record[45])+
                            " refresh="+std::to_string(*reinterpret_cast<const unsigned*>(game::base_address+0x1552688))+
                            " event="+std::to_string(utils::hook::invoke<int>(game::base_address+0x4F79D0))+
                            " postload="+std::to_string(utils::hook::invoke<int>(game::base_address+0x3968A0)));
                    }
                }
            }
        }
        if(!ready)return false;
        // Prepare every admitted body while the DB is settled, before native
        // snapshot setup starts unrelated weapon streaming. Setup may run only
        // once for a stationary actor, so deferring here cannot be repaired by
        // waiting for another DObj rebuild.
        if(remote_requested())
        {
            schema_class(utils::hook::invoke<game::AnimationClass*>(game::base_address+0x3950C0,
                game::ASSET_TYPE_ANIMCLASS,"default_anim",0));
            prepare_remote_tree();
        }
        else if(!private_tree.load() || active_states!=animation_schema::state_count(replay_schema))
            throw std::runtime_error("Recorded animation schema was not prepared before drawing");
        for(const auto& model:remote_character_models)
            if(std::any_of(cod4::native_characters.begin(),cod4::native_characters.end(),
                [&](const auto& item){return model==item.body;}))prepare_rig_variant(model);
        remote_character_ready=true;
        diagnostic("[SPAI_CLIENT_RIG] preload ready before draw epoch="+remote_match_epoch+" models="+std::to_string(remote_character_models.size()));
        return true;
    }
    catch(const std::exception& e)
    {diagnostic(std::string("[SPAI_CLIENT_RIG] FAILED remote character preload: ")+e.what());game::Com_Error(game::ERR_DROP,"SP Bots character preload failed.");return false;}
}
bool remote_class_initialized=false;
unsigned remote_observation_generation=0;

// The initial getInfo handshake cannot describe a later map or an SP-off
// match on the same connection. Publish the contract with native serverinfo
// (configstring 0, emitted on map load even when systeminfo 1 is empty).
void server_system_info(int index,const char* value)
{
    if(utils::flags::has_flag("spai-network-probe") && index<=3)
        diagnostic("[SPAI_NET] set config="+std::to_string(index)+" length="+std::to_string(value?std::strlen(value):0)+
            " lobby="+std::to_string(game::VirtualLobby_Loaded()));
    if(index!=0 || game::VirtualLobby_Loaded())
    {server_system_info_hook.invoke<void>(index,value);return;}
    utils::info_string info{std::string(value?value:"")};
    const auto* enabled=game::Dvar_FindVar("spai_enabled");
    const auto schema=enabled && enabled->current.integer ? animation_schema::current_wire : "0";
    const auto epoch=std::to_string(*game::mp::sv_serverId_value);
    // Ordinary matches deliberately admit older clients. Recheck existing
    // connections before a later map exposes SP actor snapshots to them.
    if(schema[0]!='0') enforce_peer_capabilities();
    info.set("spai_wire_schema",schema);
    info.set("spai_match_epoch",epoch);
    info.set("spai_characters",schema[0]!='0' && server_character_epoch==epoch ? server_character_manifest : "");
    info.set("spai_scores",schema[0]!='0' && score_epoch==epoch ? "2:"+epoch+":"+score_rows : "");
    const auto augmented=info.build();
    if(augmented.size()>=8192)
    {game::Com_Error(game::ERR_DROP,"SP Bots serverinfo exceeds the native size limit.");return;}
    diagnostic("[SPAI_NET] publish map contract epoch="+epoch+" schema="+schema);
    server_system_info_hook.invoke<void>(index,augmented.c_str());
}

void retire_remote_tree()
{
    release_remote_characters();
    if(!remote_generation)return;
    {
        std::lock_guard guard(lifetime_mutex);
        retire_current_storage(private_tree.exchange(nullptr));
    }
    remote_source_tree=nullptr;remote_generation=false;generation_retired=false;
    active_states=0;private_clips.fill(nullptr);private_campaign_indices.fill(0);
    diagnostic("[SPAI_NET] retired remote map generation="+std::to_string(generation_number));
}

void client_system_info()
{
    client_system_info_hook.invoke<void>();
    if(game::environment::is_dedi() || game::SV_Loaded() || demo_playback::playing())return;
    const auto* value=game::CL_GetConfigString(0);
    utils::info_string info{std::string(value?value:"")};
    const auto schema=info.get("spai_wire_schema");
    const auto epoch=info.get("spai_match_epoch");
    const auto supported=schema==animation_schema::current_wire;
    const auto selected=supported?animation_schema::current:0u;
    if(!schema.empty() && schema!="0" && !supported)
    {game::Com_Error(game::ERR_DROP,"Unsupported SP Bots map animation schema.");return;}
    if(supported && epoch.empty())
    {game::Com_Error(game::ERR_DROP,"SP Bots map animation epoch is missing.");return;}
    if(epoch!=remote_match_epoch || remote_schema.load()!=selected || generation_retired)
    {
        retire_remote_tree();
        remote_match_epoch=epoch;remote_schema=selected;
        remote_class_initialized=false;++remote_observation_generation;
        // Retained native trees own their old clips until their destructor has
        // returned. Never free them just because a new configstring arrived.
        collect_retired();
        diagnostic("[SPAI_NET] map contract epoch="+epoch+" schema="+std::to_string(selected)+
            " observation="+std::to_string(remote_observation_generation));
    }
    if(supported)
    {
        const auto manifest=info.get("spai_characters");
        if(!manifest.empty() && manifest!=remote_character_manifest)
        {
            if(!remote_character_manifest.empty() || !decode_character_manifest(manifest,remote_character_models))
            {game::Com_Error(game::ERR_DROP,"Invalid or changed SP Bots character manifest.");return;}
            remote_character_manifest=manifest;
            diagnostic("[SPAI_CLIENT_RIG] received manifest epoch="+epoch+" models="+manifest);
        }
    }
}
// Native clients prune the unused mp_stand_grenade_throw leaf that GSC keeps
// on the server. Use the observed h1_mp_hold_grenade_throw leaf in this private clone as
// the vault clip's carrier. Wire state numbers and campaign clips stay equal;
// native tree indices are deliberately resolved independently on each peer.

std::string network_schema_marker()
{
    if(!utils::flags::has_flag("spai-probe") || game::VirtualLobby_Loaded() || !game::SV_Loaded())return {};
    const auto* enabled=game::Dvar_FindVar("spai_enabled");
    return enabled && enabled->current.integer ? network_capability : "";
}
void clear_network_schema()
{
    remote_schema=0;
    if(remote_generation)generation_retired=true;
}
bool accept_network_schema(const std::string& marker)
{
    if(marker.empty()){clear_network_schema();return true;}
    if(marker!=network_capability || !utils::flags::has_flag("spai-probe"))return false;
    remote_schema=animation_schema::current;
    diagnostic("[SPAI_NET] accepted animation schema="+std::to_string(animation_schema::current)+" states="+std::to_string(max_states));
    return true;
}
bool remote_requested()
{
    return remote_schema.load()==animation_schema::current && !game::environment::is_dedi()
        && !game::VirtualLobby_Loaded() && !game::SV_Loaded() && !demo_playback::playing();
}
game::AnimationClass* schema_class(game::AnimationClass* original)
{
    if(!original || !original->name || std::strcmp(original->name,"default_anim"))
    {
        game::Com_Error(game::ERR_DROP,"SP Bots class-zero mapping differs from the negotiated schema.");
        return original;
    }
    if(!remote_class_initialized)
    {
        remote_class=*original;
        remote_machine={};remote_states={};remote_entries={};remote_indices={};
        remote_machine.stateCount=max_states;
        remote_machine.states=remote_states.data();
        for(size_t i=0;i<max_states;++i)
        {
            remote_states[i].entryCount=1;
            remote_states[i].animEntries=&remote_entries[i];
            remote_states[i].animIndices=&remote_indices[i];
            remote_states[i].blendTime=0.15f;
        }
        remote_class.stateMachine=&remote_machine;
        remote_class.animCtrl=game::ANIMCTRL_PLAYER;
        remote_class_initialized=true;
        diagnostic("[SPAI_NET] client class schema published before animation decoding");
    }
    return &remote_class;
}
game::AnimationClass* remote_class_lookup(int index)
{
    auto* original=remote_class_hook.invoke<game::AnimationClass*>(index);
    return index==0 && remote_requested()?schema_class(original):original;
}
game::AnimationClass* remote_entity_class_lookup(const unsigned char* entity)
{
    auto* original=remote_entity_class_hook.invoke<game::AnimationClass*>(entity);
    const auto packed=*reinterpret_cast<const unsigned*>(entity+0x54);
    return entity[10]==18 && (packed&0x1E001)==1 && remote_requested()?schema_class(original):original;
}

// These native animation paths inline the asset lookup rather than using
// either class getter above. Route only their default_anim lookups, preserving
// the canonical database asset and every ordinary session's return value.
game::AnimationClass* remote_inline_class_lookup(int type,const char* name,int error)
{
    auto* original=utils::hook::invoke<game::AnimationClass*>(game::base_address+0x3950C0,type,name,error);
    return type==game::ASSET_TYPE_ANIMCLASS && name && !std::strcmp(name,"default_anim") && remote_requested()
        ?schema_class(original):original;
}

void remote_evaluate(int local_client,game::AnimationClass* animation_class,const unsigned char* entity,unsigned char* character,int flags)
{
    utils::hook::invoke<void>(game::base_address+0x60DF0,local_client,animation_class,entity,character,flags);
    if(!utils::flags::has_flag("spai-network-probe") || !remote_requested() || entity[10]!=18 || !private_tree.load())return;
    const auto packed=*reinterpret_cast<const unsigned*>(entity+0x54);
    const auto id=*reinterpret_cast<const unsigned short*>(entity);
    const auto selected=*reinterpret_cast<unsigned short*>(character+0x5C6);
    static std::unordered_set<uint64_t> observed;
    static unsigned observed_generation=0;
    if(observed_generation!=remote_observation_generation)
    {observed.clear();observed_generation=remote_observation_generation;}
    // Native interpolation can defer the first evaluation until its snapshot
    // timestamp. Record both that pending observation and later convergence.
    const auto key=(static_cast<uint64_t>(id)<<48)|(static_cast<uint64_t>(packed)<<16)|selected;
    if(observed.size()>=1024 || !observed.insert(key).second)return;
    auto* tree=*reinterpret_cast<void**>(character+0x4B0);
    auto* anims=tree?utils::hook::invoke<void*>(game::base_address+0x5CBB20,tree):nullptr;
    diagnostic("[SPAI_NET] evaluate actor="+std::to_string(id)+" state="+std::to_string((packed>>1)&127)+
        " selected="+std::to_string(selected)+
        " private_tree="+std::to_string(anims==(actor_rig_trees[id].load()?actor_rig_trees[id].load():private_tree.load()))+
        " epoch="+remote_match_epoch);
}

void prepare_remote_tree()
{
    const auto* cg=reinterpret_cast<const unsigned char*>(game::CG_GetLocalClientGlobals());
    if(!cg)throw std::runtime_error("Client globals are not initialized");
    auto* source=*reinterpret_cast<void* const*>(cg+0x161B88);
    if(!source)throw std::runtime_error("Client multiplayer animation tree is not loaded");
    if(remote_generation && source==remote_source_tree && !generation_retired)return;
    const auto count=*static_cast<const unsigned*>(source);
    if(count==0 || count>8192)throw std::runtime_error("Client animation tree size is invalid");
    if(utils::flags::has_flag("spai-network-probe"))
    {
        auto catalog=nlohmann::json::array();
        for(unsigned leaf=0;leaf<count;++leaf)
        {
            auto* p=*reinterpret_cast<game::XAnimParts* const*>(static_cast<const unsigned char*>(source)+16+leaf*16);
            const char* name=nullptr;char text[128]{};SIZE_T got=0;
            if(!ReadProcessMemory(GetCurrentProcess(),p,&name,sizeof(name),&got) || got!=sizeof(name))continue;
            if(!ReadProcessMemory(GetCurrentProcess(),name,text,sizeof(text)-1,&got) || got==0)continue;
            const auto length=strnlen(text,sizeof(text));
            if(!length || length>=sizeof(text)-1 || !std::all_of(text,text+length,[](unsigned char c){return std::isalnum(c) || c=='_' || c=='/' || c=='.' || c=='-';}))continue;
            catalog.push_back({{"leaf",leaf},{"name",text},{"parts",reinterpret_cast<uintptr_t>(p)}});
        }
        std::ofstream out("spai-diagnostics/remote-tree-catalog-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(remote_observation_generation)+".json");
        out<<catalog.dump(2);
    }
    // Adapt the base to a real, owned body from this map's manifest. A default
    // USMC asset on a SAS map may only be a named streaming placeholder.
    const auto reference=std::find_if(remote_character_models.begin(),remote_character_models.end(),
        [](const auto& model){return std::any_of(cod4::native_characters.begin(),cod4::native_characters.end(),
            [&](const auto& item){return model==item.body;});});
    if(reference==remote_character_models.end() || !spai_character_assets::ready(*reference))
        throw std::runtime_error("Required owned MP adaptation body is not loaded");
    remote_base_body=*reference;
    const auto* body=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,reference->c_str(),0).model;
    if(!body || !body->boneNames || !body->baseMat)throw std::runtime_error("MP adaptation body is incomplete");
    std::array<unsigned,max_states> leaves{};
    for(size_t i=0;i<max_states;++i)
    {
        const auto* slot=animation_schema::states[i].client_slot;
        if(!game::DB_XAssetExists(game::ASSET_TYPE_XANIMPARTS,slot))throw std::runtime_error("Client animation slot is unavailable");
        const auto* parts=game::DB_FindXAssetHeader(game::ASSET_TYPE_XANIMPARTS,slot,0).parts;
        unsigned matches=0;
        std::ostringstream match_log;match_log<<"[SPAI_NET] slot="<<slot<<" tree_entries="<<count<<" parts="<<parts<<" leaves=";
        for(unsigned leaf=0;leaf<count;++leaf)
        {
            const auto* p=*reinterpret_cast<game::XAnimParts* const*>(static_cast<const unsigned char*>(source)+16+leaf*16);
            if(p==parts){leaves[i]=leaf;++matches;match_log<<leaf<<",";}
        }
        diagnostic(match_log.str());
        if(matches!=1)
        {
            std::ofstream dump("spai-diagnostics/remote-player-tree.bin",std::ios::binary);
            dump.write(static_cast<const char*>(source),16+count*16);
            throw std::runtime_error(std::string("Client animation slot missing or ambiguous: ")+slot);
        }
    }
    if(private_tree.load())
    {
        std::lock_guard guard(lifetime_mutex);
        retire_current_storage(private_tree.load());
    }
    auto* isolated=static_cast<unsigned char*>(spai_campaign::allocate(16+count*16));
    std::memcpy(isolated,source,16+count*16);
    for(size_t i=0;i<max_states;++i)
    {
        const auto& definition=animation_schema::states[i];
        auto* clip=spai_campaign::load(definition.campaign_index);
        if(clip->assetType!=2)throw std::runtime_error("Remote campaign clip requires unsupported composition");
        if(definition.single_cycle)clip->flags&=~1u;
        spai_campaign::adapt_mp_torso(clip,body);
        *reinterpret_cast<game::XAnimParts**>(isolated+16+leaves[i]*16)=clip;
        private_clips[i]=clip;
        private_campaign_indices[i]=static_cast<unsigned>(definition.campaign_index);
        remote_entries[i]={spai_campaign::intern(definition.name),spai_campaign::intern(definition.client_slot)};
        remote_states[i].name=remote_entries[i].alias;
        remote_states[i].notify=spai_campaign::intern("spai_anim");
        remote_indices[i]=leaves[i];
        private_leaf_indices[i]=leaves[i];
    }
    remote_machine.name=spai_campaign::intern("spai_probe");
    remote_class.animTree=spai_campaign::intern("multiplayer");
    {
        std::lock_guard guard(lifetime_mutex);
        known_private_anims.insert(isolated);
    }
    ++generation_number;active_states=max_states;generation_retired=false;
    remote_source_tree=source;remote_generation=true;private_tree.store(isolated);
    collect_retired();
    diagnostic("[SPAI_NET] client campaign tree ready: states="+std::to_string(max_states)+" stock assets unchanged");
    diagnostic("[SPAI_NET] ready map epoch="+remote_match_epoch+" generation="+std::to_string(generation_number));
}

// Character-info composition is already supplied by the snapshot/model path.
// Resolve client leaves independently of the server tree, and let the native
// setup function rebuild the DObj/tree when the selected anims pointer changes.
void prepare_client_rig(const unsigned char* entity,unsigned char* character)
{
    const auto id=*reinterpret_cast<const unsigned short*>(entity);
    if(id>=actor_rig_trees.size())throw std::runtime_error("Invalid SP character slot");
    const auto* name=reinterpret_cast<const char*>(character+36);
    const std::string body(name,strnlen(name,64));
    const auto observe_pending=[&](const char* reason)
    {
        const auto line="[SPAI_CLIENT_RIG] pending actor="+std::to_string(id)+" body="+body+
            " reason="+reason+" info="+std::to_string(reinterpret_cast<uintptr_t>(character))+" epoch="+remote_match_epoch;
        static std::unordered_set<std::string> observed;
        if(observed.size()<256 && observed.insert(line).second)diagnostic(line);
    };
    if(body.empty() || body.size()>=64 || !spai_character_assets::allowed(body))
    {
        actor_rig_trees[id].store(nullptr);
        observe_pending("composition");
        return; // Native streaming has not supplied the supported body yet.
    }
    if(!remote_character_models.contains(body))
        throw std::runtime_error("SP actor body is outside the negotiated character manifest");
    const auto found=rig_trees.find(body);
    if(found==rig_trees.end() && !spai_character_assets::ready(body))
    {observe_pending("residency");return;}
    auto* selected=prepare_rig_variant(body);
    if(actor_rig_trees[id].exchange(selected)!=selected)
        diagnostic("[SPAI_CLIENT_RIG] assigned actor="+std::to_string(id)+" body="+body+
            " tree="+std::to_string(reinterpret_cast<uintptr_t>(selected))+" info="+std::to_string(reinterpret_cast<uintptr_t>(character))+" epoch="+remote_match_epoch);
}
