// Experimental, opt-in native harness. Never included by the normal project.
#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "component/console.hpp"
#include "component/command.hpp"
#include "component/party.hpp"
#include "component/scheduler.hpp"
#include "component/scripting.hpp"
#include "component/gsc/script_extension.hpp"
#include "component/demo/demo_playback.hpp"
#include "component/demo/demo_game.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"
#include "game/scripting/array.hpp"
#include <utils/flags.hpp>
#include <utils/hook.hpp>
#include <utils/info_string.hpp>
#include "campaign_reader.hpp"
#include "mp_rig_adapter.hpp"
#include "spai_replay.hpp"
#include "spai_scores.hpp"
#include "spai_character_assets.hpp"

namespace spai_probe
{
    std::atomic<unsigned> remote_schema{};
    bool remote_generation=false;
    std::string remote_base_body;
    bool remote_requested();
    void prepare_remote_tree();
    void prepare_client_rig(const unsigned char* entity,unsigned char* character);
    unsigned replay_schema=0;
    bool replay_requested(){return animation_schema::state_count(replay_schema)!=0 && demo_playback::playing();}
    bool replay_rigs_requested(){return replay_schema>=3 && replay_requested();}
    void diagnostic(const std::string& text)
    {
        const auto path=utils::flags::has_flag("spai-network-probe")
            ? "spai-diagnostics/network-"+std::to_string(GetCurrentProcessId())+".log"
            : "spai-diagnostics/native-probe.log";
        std::ofstream file(path,std::ios::app);
        file << text << std::endl;
    }
    LONG CALLBACK startup_exception_trace(EXCEPTION_POINTERS* exception)
    {
        if(exception && (exception->ExceptionRecord->ExceptionCode==EXCEPTION_PRIV_INSTRUCTION ||
            exception->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION))
        {
            std::ostringstream message;
            message<<"[SPAI] exception code="<<std::hex<<exception->ExceptionRecord->ExceptionCode
                <<" rip="<<exception->ContextRecord->Rip<<" rsp="<<exception->ContextRecord->Rsp
                <<" rcx="<<exception->ContextRecord->Rcx<<" rdx="<<exception->ContextRecord->Rdx;
            void* stack[24]{};const auto count=CaptureStackBackTrace(0,24,stack,nullptr);
            for(unsigned i=0;i<count;++i) message<<" "<<stack[i];
            diagnostic(message.str());
            if(exception->ContextRecord->Rip>=game::base_address && exception->ContextRecord->Rip<game::base_address+0x10000000)
            {
                const auto* code=reinterpret_cast<const unsigned char*>(exception->ContextRecord->Rip-8);
                std::ostringstream bytes;bytes<<"[SPAI] fault bytes"<<std::hex;
                for(unsigned i=0;i<32;++i)bytes<<" "<<static_cast<unsigned>(code[i]);
                diagnostic(bytes.str());
            }
            const auto* stackWords=reinterpret_cast<const uintptr_t*>(exception->ContextRecord->Rsp);
            std::ostringstream words;words<<"[SPAI] fault stack"<<std::hex;
            for(unsigned i=0;i<24;++i)words<<" "<<stackWords[i];
            diagnostic(words.str());
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    utils::hook::detour connection_hook;
    utils::hook::detour bit_hook;
    utils::hook::detour print_hook;
    utils::hook::detour write_entity_hook;
    utils::hook::detour read_field_hook;
    utils::hook::detour actor_tree_hook;
    utils::hook::detour client_tree_hook;
    utils::hook::detour create_small_tree_hook;
    utils::hook::detour animation_count_hook;
    bool animation_count_hook_installed=false;
    void animation_count_init()
    {
        animation_count_hook.invoke<void>();
        // Diagnostic only: inspect the same static TLS context used by the
        // native actor-tree getter. Remote clients have a different BG object.
        const auto read=[](uintptr_t address,void* out,size_t size)
        {SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,size,&got) && got==size;};
        unsigned index=0;uintptr_t slot=0,bg=0,tree=0;
        read(game::base_address+0x1126FCE0,&index,sizeof(index));
        const auto slots=__readgsqword(0x58);
        if(index<4096 && read(slots+index*8,&slot,8) && read(slot+0x10,&bg,8))read(bg+0x72810,&tree,8);
        std::ostringstream line;
        line<<"[SPAI_NET_INIT] tls="<<index<<" bg="<<std::hex<<bg<<" player_tree="<<tree;
        void* stack[16]{};const auto count=CaptureStackBackTrace(0,16,stack,nullptr);
        for(unsigned i=0;i<count;++i)line<<" "<<stack[i];
        diagnostic(line.str());
        unsigned entries=0;
        if(read(tree,&entries,4) && entries>0 && entries<8192)
        {
            std::vector<unsigned char> data(16+entries*16);
            if(read(tree,data.data(),data.size()))
            {
                const auto name="spai-diagnostics/network-tree-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(bg)+".bin";
                std::ofstream out(name,std::ios::binary);out.write(reinterpret_cast<char*>(data.data()),data.size());
            }
        }
    }
    std::atomic<void*> private_tree{};
    std::map<std::string,void*> rig_trees;
    std::array<std::atomic<void*>,2048> actor_rig_trees{};
    std::mutex lifetime_mutex;
    std::unordered_map<void*,void*> live_private_trees;
    std::unordered_set<void*> known_private_anims;
    unsigned generation_number=0;
    game::AnimationClass* patched_class=nullptr;
    game::AnimationClass original_class{};
    void* original_tree_slot=nullptr;
    bool generation_retired=false;
    struct retired_storage
    {
        std::vector<void*> anims;
        spai_campaign::storage data;
    };
    std::vector<retired_storage> retired_generations;
    // Caller holds lifetime_mutex. All variants share the same allocation
    // arena, so one surviving engine tree retains the entire generation.
    void retire_current_storage(void* base)
    {
        std::vector<void*> trees;
        if(base)trees.push_back(base);
        for(const auto& [name,tree]:rig_trees)
            if(std::find(trees.begin(),trees.end(),tree)==trees.end())trees.push_back(tree);
        retired_generations.push_back({std::move(trees),std::move(spai_campaign::current)});
        spai_campaign::current={};rig_trees.clear();
        for(auto& tree:actor_rig_trees)tree.store(nullptr);
    }
    void collect_retired()
    {
        // Called only from the next map's script initialization. Shutdown has
        // completed and no new trees can acquire the unpublished generation.
        // Client trees may outlive server shutdown: retain their generation
        // until the native agent-tree destructor has actually returned.
        std::lock_guard guard(lifetime_mutex);
        for(auto it=retired_generations.begin();it!=retired_generations.end();)
        {
            const auto in_use=std::any_of(live_private_trees.begin(),live_private_trees.end(),
                [&](const auto& entry){return std::find(it->anims.begin(),it->anims.end(),entry.second)!=it->anims.end();});
            if(in_use || std::find(it->anims.begin(),it->anims.end(),private_tree.load())!=it->anims.end()){++it;continue;}
            diagnostic("[SPAI] reclaim retired allocations="+std::to_string(it->data.allocations.size())+
                " bytes="+std::to_string(it->data.bytes)+" strings="+std::to_string(it->data.strings.size()));
            spai_campaign::release(it->data);
            for(auto* tree:it->anims)known_private_anims.erase(tree);
            it=retired_generations.erase(it);
        }
    }
    void* create_small_tree(void* anims,void* allocator)
    {
        auto* tree=create_small_tree_hook.invoke<void*>(anims,allocator);
        std::lock_guard guard(lifetime_mutex);
        if(tree && known_private_anims.contains(anims))
        {
            live_private_trees[tree]=anims;
            diagnostic("[SPAI] private small tree created; live="+std::to_string(live_private_trees.size())+
                " anims="+std::to_string(reinterpret_cast<uintptr_t>(anims))+
                " instance="+std::to_string(reinterpret_cast<uintptr_t>(tree)));
        }
        return tree;
    }
    void free_agent_tree(void* tree,void* deallocator)
    {
        // Preserve the product's existing XAnimFreeSmallTree hook, including
        // replay-agent allocator handling. Only the native agent wrapper is
        // redirected here; this is not a second MinHook on that function.
        utils::hook::invoke<void>(game::base_address+0x5CB830,tree,deallocator);
        std::lock_guard guard(lifetime_mutex);
        if(live_private_trees.erase(tree))
            diagnostic("[SPAI] private small tree freed; live="+std::to_string(live_private_trees.size()));
    }
    constexpr size_t max_states=animation_schema::states.size();
    size_t active_states=0;
    std::array<game::XAnimParts*,max_states> private_clips{};
    std::array<unsigned,max_states> private_campaign_indices{};
    std::array<unsigned,max_states> private_leaf_indices{};
    void clear_server_aim(unsigned id);
    void* spawn_rig_tree(void* original,const unsigned char* entity)
    {
        // SpawnAgent selects its class tree directly, before entity state has
        // the scripted-class bits. Unlike later BG lookups it bypasses actor_tree.
        const auto id=*reinterpret_cast<const unsigned short*>(entity);
        clear_server_aim(id); // Native spawn owns slot reuse, including respawns.
        auto* selected=original;
        if(!game::VirtualLobby_Loaded() && original==private_tree.load() && id<actor_rig_trees.size())
            if(auto* variant=actor_rig_trees[id].load())selected=variant;
        diagnostic("[SPAI_RIG_VARIANT] spawn actor="+std::to_string(id)+" tree="+
            std::to_string(reinterpret_cast<uintptr_t>(selected)));
        return utils::hook::invoke<void*>(game::base_address+0x17F560,selected);
    }
    void client_tree(int local_client,const unsigned char* entity,unsigned char* character,int flag)
    {
        if((remote_requested() || replay_rigs_requested()) && (entity[10]==18 || entity[10]==19) && (*reinterpret_cast<const unsigned*>(entity+0x54)&0x1E001)==1)
        {
            try {if(remote_requested())prepare_remote_tree();prepare_client_rig(entity,character);}
            catch(const std::exception& e)
            {
                diagnostic(std::string("[SPAI_NET] remote preparation failed: ")+e.what());
                game::Com_Error(game::ERR_DROP,"SP Bots animation preparation failed: %s",e.what());
                return;
            }
        }
        client_tree_hook.invoke<void>(local_client,entity,character,flag);
        if(game::SV_Loaded() && !game::VirtualLobby_Loaded() && entity[10]==18)
        {
            const auto id=*reinterpret_cast<const unsigned short*>(entity);
            if(id<actor_rig_trees.size())if(auto* assigned=actor_rig_trees[id].load())
            {
                auto* instance=*reinterpret_cast<void**>(character+0x4b0);
                auto* actual=instance?utils::hook::invoke<void*>(game::base_address+0x5CBB20,instance):nullptr;
                const auto* body=reinterpret_cast<const char*>(character+36);
                std::ostringstream line;
                line<<"[SPAI_LISTEN_RIG] actor="<<id<<" body="<<std::string(body,strnlen(body,64))
                    <<" assigned="<<reinterpret_cast<uintptr_t>(assigned)<<" actual="<<reinterpret_cast<uintptr_t>(actual)
                    <<" instance="<<reinterpret_cast<uintptr_t>(instance)<<" generation="<<generation_number;
                for(unsigned i=0;i<9;++i)
                {
                    const auto* attachment=reinterpret_cast<const char*>(character+100+i*64);
                    if(*attachment)line<<" attachment="<<std::string(attachment,strnlen(attachment,64));
                }
                static std::unordered_set<std::string> observed;
                if(observed.size()<512 && observed.insert(line.str()).second)diagnostic(line.str());
            }
        }
        if(remote_requested() && entity[10]==18 && private_tree.load())
        {
            static std::unordered_set<uint64_t> observed;
            const auto id=*reinterpret_cast<const unsigned short*>(entity);
            const auto packed=*reinterpret_cast<const unsigned*>(entity+0x54);
            if(observed.size()<256 && observed.insert((static_cast<uint64_t>(id)<<32)|packed).second)
            {
                auto* tree=*reinterpret_cast<void**>(character+0x4b0);
                auto* anims=tree?utils::hook::invoke<void*>(game::base_address+0x5CBB20,tree):nullptr;
                diagnostic("[SPAI_NET] setup actor="+std::to_string(id)+" state="+std::to_string((packed>>1)&127)+
                    " selected="+std::to_string(*reinterpret_cast<unsigned short*>(character+0x5c6))+
                    " private_tree="+std::to_string(anims==(actor_rig_trees[id].load()?actor_rig_trees[id].load():private_tree.load())));
            }
        }
        if(private_tree.load() && *reinterpret_cast<const unsigned short*>(entity)==18)
        {
            static std::unordered_set<unsigned> seen;
            const auto animation=*reinterpret_cast<const unsigned*>(entity+0x54);
            if(seen.size()<64 && seen.insert(animation).second)
            {
                auto* tree=*reinterpret_cast<void**>(character+0x4b0);
                auto* anims=tree?utils::hook::invoke<void*>(game::base_address+0x5CBB20,tree):nullptr;
                std::ostringstream s;s<<"[SPAI] client character state="<<std::hex<<animation
                    <<" info="<<static_cast<void*>(character)<<" tree="<<tree<<" anims="<<anims
                    <<" actor="<<*reinterpret_cast<unsigned*>(character+0x5bc)
                    <<" selected_state="<<*reinterpret_cast<unsigned short*>(character+0x5c6);
                diagnostic(s.str());
            }
        }
    }
    void* actor_tree(const unsigned char* entity)
    {
        const auto animation=*reinterpret_cast<const unsigned*>(entity+0x54);
        auto* original=actor_tree_hook.invoke<void*>(entity);
        auto* result=original;
        // The virtual lobby also creates scripted agents with class zero.
        // Keep their native presentation tree, including after an SP match.
        if(!game::VirtualLobby_Loaded() && (!remote_generation || remote_requested()) && (entity[10]==18 || entity[10]==19) && (animation&0x1E001)==1)
            if(auto* tree=private_tree.load())
            {
                const auto id=*reinterpret_cast<const unsigned short*>(entity);
                auto* variant=id<actor_rig_trees.size()?actor_rig_trees[id].load():nullptr;
                result=variant?variant:tree;
            }
        if(private_tree.load() && *reinterpret_cast<const unsigned short*>(entity)==18)
        {
            static std::mutex lock;static std::unordered_set<uint64_t> seen;
            const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
            const auto key=(static_cast<uint64_t>(caller-game::base_address)<<32)|animation;
            std::lock_guard guard(lock);
            if(seen.size()<64 && seen.insert(key).second)
            {
                std::ostringstream s;s<<"[SPAI] actor tree caller="<<std::hex<<caller
                    <<" state="<<animation<<" type="<<static_cast<unsigned>(entity[10])
                    <<" original="<<original<<" selected="<<result;
                diagnostic(s.str());
            }
        }
        return result;
    }
    bool inspect_actor_packets = false;
    unsigned field_count = 0;
    bool write_entity(void* context,game::msg_t* msg,int time,int type,const unsigned char* from,const unsigned char* to,int force)
    {
        if (inspect_actor_packets && to && *reinterpret_cast<const unsigned short*>(to)==18)
        {
            if(utils::flags::has_flag("spai-network-probe"))
            {
                static std::unordered_set<unsigned> sent;
                const auto packed=*reinterpret_cast<const unsigned*>(to+0x54);
                if(sent.size()<128 && sent.insert(packed).second)
                    diagnostic("[SPAI_NET] sent actor=18 packed="+std::to_string(packed)+" state="+std::to_string((packed>>1)&127));
            }
            diagnostic("[WIRE] write entity18 type="+std::to_string(type)+" actual="+std::to_string(to[10])
                +" bit="+std::to_string(msg->bit)+" bytes="+std::to_string(msg->cursize));
            std::ofstream state("spai-diagnostics/wire-actor.bin",std::ios::binary);
            state.write(reinterpret_cast<const char*>(to),256);
        }
        return write_entity_hook.invoke<bool>(context,msg,time,type,from,to,force);
    }
    void read_field(game::msg_t* msg,int time,const unsigned char* from,unsigned char* to,const short* field,int xor_value,bool zero)
    {
        if (inspect_actor_packets && field_count++<150)
            diagnostic("[WIRE] read entity="+std::to_string(*reinterpret_cast<const unsigned short*>(to))
                +" type="+std::to_string(to[10])+" off="+std::to_string(field[0])+" codec="+std::to_string(field[2])
                +" bit="+std::to_string(msg->bit)+" bytes="+std::to_string(msg->cursize)+" zero="+std::to_string(zero));
        read_field_hook.invoke<void>(msg,time,from,to,field,xor_value,zero);
        if(remote_requested() && to && to[10]==18 && field[0]==0x54)
        {
            static std::unordered_set<uint64_t> received;
            const auto id=*reinterpret_cast<const unsigned short*>(to);
            const auto packed=*reinterpret_cast<const unsigned*>(to+0x54);
            if(received.size()<256 && received.insert((static_cast<uint64_t>(id)<<32)|packed).second)
                diagnostic("[SPAI_NET] decoded actor="+std::to_string(id)+" packed="+std::to_string(packed)+" state="+std::to_string((packed>>1)&127));
        }
    }
    void trace_print(int type, const char* format, ...)
    {
        char buffer[8192]{};
        va_list args;
        va_start(args,format);
        vsnprintf_s(buffer,sizeof(buffer),_TRUNCATE,format,args);
        va_end(args);
        diagnostic(std::string("[CON] ")+buffer);
        print_hook.invoke<void>(type,"%s",buffer);
    }
    int error_format(char* dest, size_t size, const char* format, va_list args)
    {
        const int result=utils::hook::invoke<int>(game::base_address+0x5AF170,dest,size,format,args);
        diagnostic(std::string("[SPAI] native error: ")+dest);
        void* frames[24]{};
        const auto count=CaptureStackBackTrace(0,24,frames,nullptr);
        for(unsigned i=0;i<count;++i)
            diagnostic(std::format("[SPAI_ERROR_STACK] {} {:x}",i,reinterpret_cast<uintptr_t>(frames[i])));
        if(std::strcmp(dest,"PLATFORM_DISCONNECTED_FROM_SERVER")==0)
        {
            std::ofstream code("spai-diagnostics/replay-return-code.bin",std::ios::binary);
            code.write(reinterpret_cast<const char*>(game::base_address+0x120000),0x20000);
        }
        return result;
    }
    void connection_limits(unsigned flags)
    {
        diagnostic("[SPAI] connection flags=" + std::to_string(flags));
        // The validated demo header arrives before connection allocation. A
        // cold replay has no private-match capacity setting, but its recorded
        // agent snapshots still require the native 24-slot agent rings.
        if (replay_schema || utils::flags::has_flag("spai-agent-rings")) flags |= 2u;
        connection_hook.invoke<void>(flags);
        const auto* limits = reinterpret_cast<const int*>(game::base_address + 0x356378C);
        diagnostic("[SPAI] allocated players=" + std::to_string(limits[0]) + " agents=" + std::to_string(limits[1]));
    }
    int read_bit(game::msg_t* msg)
    {
        static bool reported = false;
        if (!reported && msg->bit >= (msg->cursize + msg->splitSize) * 8)
        {
            reported = true;
            diagnostic("[SPAI] bit overflow bit=" + std::to_string(msg->bit) + " size=" + std::to_string(msg->cursize)
                + " readcount=" + std::to_string(msg->readcount));
            std::ofstream packet("spai-diagnostics/overflow-packet.bin",std::ios::binary);
            if (msg->data && msg->cursize > 0 && msg->cursize <= 131072)
                packet.write(reinterpret_cast<const char*>(msg->data),msg->cursize);
        }
        return bit_hook.invoke<int>(msg);
    }
    size_t behavior_slot(const gsc::function_args& args)
    {
        const auto name=args[0].as<std::string>();
        for(size_t i=0;i<active_states;++i)if(name==animation_schema::states[i].name && private_clips[i])return i;
        throw std::runtime_error("Private behavior clip is not loaded");
    }
    // Verified against MP 4617F0/3F3EB0. The public legacy weaponParms
    // declaration lacks the second weapon word and does not match this build.
    struct bullet_parms
    {
        float forward[3], right[3], up[3], muzzle[3], gun_forward[3];
        unsigned weapon, weapon_extra;
        bool alternate;
        unsigned char padding[3];
        void* definition;
    };
    static_assert(sizeof(bullet_parms)==0x50 && offsetof(bullet_parms,alternate)==0x44);

    void check_code(size_t offset, std::initializer_list<unsigned char> expected)
    {
        if(std::memcmp(reinterpret_cast<const void*>(game::base_address+offset),expected.begin(),expected.size()))
            throw std::runtime_error("Research bullet function fingerprint mismatch");
    }
    game::AnimationStateMachine* published_machine=nullptr;
    #include "spai_rig_variants.hpp"
    void audit_rig(const game::XAnimParts* original,const game::XAnimParts* campaign)
    {
        nlohmann::json report;
        for(const auto* model_name:{"body_usmc_desert_assault_mp_camo","head_usmc_desert_assault_mp"})
        {
            const auto* model=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,model_name,0).model;
            if(!model || !model->boneNames || !model->baseMat) throw std::runtime_error("Missing rig audit model");
            auto bones=nlohmann::json::array();
            for(unsigned i=0;i<model->numBones;++i)
            {
                const auto& mat=model->baseMat[i];
                bones.push_back({{"name",game::SL_ConvertToString(model->boneNames[i])},
                    {"parent",i<model->numRootBones?-1:static_cast<int>(i-model->parentList[i-model->numRootBones])},
                    {"q",{mat.quat[0],mat.quat[1],mat.quat[2],mat.quat[3]}},
                    {"p",{mat.trans[0],mat.trans[1],mat.trans[2]}}});
            }
            report[model_name]=bones;
        }
        for(const auto& pair:{std::pair{"original",original},std::pair{"campaign",campaign}})
        {
            auto names=nlohmann::json::array();
            for(unsigned i=0;i<pair.second->boneCount[9];++i) names.push_back(game::SL_ConvertToString(pair.second->names[i]));
            report[pair.first]={{"name",pair.second->name},{"flags",pair.second->flags},{"bones",names},
                {"counts",std::vector<unsigned char>(pair.second->boneCount,pair.second->boneCount+12)}};
        }
        std::ofstream out(std::string("spai-diagnostics/rig-")+campaign->name+".json");
        out<<report.dump(2);
        diagnostic(std::string("[SPAI] rig audit saved for ")+campaign->name);
    }

    #include "spai_score_transport.hpp"
    #include "spai_remote.hpp"
    #include "spai_recording.hpp"
    #include "spai_aim.hpp"
    #include "aim_controller_probe.hpp"
    #include "muzzle_fx_probe.hpp"

    class component final : public component_interface
    {
        void post_unpack() override
        {
            if (game::environment::is_sp() || !utils::flags::has_flag("spai-probe")) return;
            if(!game::environment::is_dedi())
            {
                check_code(0x3D6050,{0x40,0x53,0x56,0x41,0x54,0x41,0x56,0x41,0x57});
                muzzle_fx_probe_hook.create(game::base_address+0x3D6050,observe_bolted_effect);
                check_code(0xEA6A0,{0x44,0x89,0x44,0x24,0x18,0x55,0x41,0x54});
                check_code(0x113E40,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18});
                muzzle_consumer_probe_hook.create(game::base_address+0xEA6A0,observe_muzzle_consumer);
                muzzle_tag_probe_hook.create(game::base_address+0x113E40,observe_muzzle_tag);
                check_code(0x2BB870,{0x40,0x55,0x56,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57});
                muzzle_composition_probe_hook.create(game::base_address+0x2BB870,observe_muzzle_composition);
            }
            diagnostic("[SPAI_AIM_INIT] post-unpack reached");
            aim_probe_enabled=utils::flags::has_flag("spai-aim-probe");
            if(aim_probe_enabled)
            {
                check_code(0x61ED0,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18});
                aim_controller_probe_hook.create(game::base_address+0x61ED0,fixed_aim_controller);
                check_code(0xF3D20,{0x40,0x53,0x48,0x81,0xEC,0x90,0x00,0x00,0x00});
                check_code(0x5D4460,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20});
                hand_ik_probe_hook.create(game::base_address+0x5D4460,probe_hand_ik);
                gsc::function::add("spaitesthandik",[](const gsc::function_args& args)->scripting::script_value
                {
                    hand_ik_probe_skip.store(args[0].as<int>()!=0);
                    return static_cast<int>(hand_ik_probe_skips.load());
                });
                diagnostic("[SPAI_AIM_INIT] validating producer");
                check_code(0x557680,{0x40,0x53,0x48,0x81,0xEC,0xD0,0x00,0x00,0x00});
                diagnostic("[SPAI_AIM_INIT] creating producer hook");
                snapshot_aim_probe_hook.create(game::base_address+0x557680,snapshot_aim_producer);
                diagnostic("[SPAI_AIM_INIT] producer hook ready");
                gsc::function::add("spaitestsnapshotaim",[](const gsc::function_args& args)->scripting::script_value
                {
                    const auto ref=args[0].as<scripting::entity>().get_entity_reference();
                    if(ref.classnum || ref.entnum<18 || ref.entnum>=42)return false;
                    auto* entity=reinterpret_cast<unsigned char*>(&game::mp::g_entities[ref.entnum]);
                    if(!game::mp::g_entities[ref.entnum].agent || entity[10]!=18 ||
                        (*reinterpret_cast<const unsigned*>(entity+0x54)&0x1E001)!=1)return false;
                    const float pitch=args[1].as<float>();
                    if(!std::isfinite(pitch) || std::abs(pitch)>30)throw std::runtime_error("Snapshot aim outside bounds");
                    std::lock_guard guard(snapshot_aim_mutex);
                    snapshot_aim_requests[ref.entnum]={pitch,*game::level_time,generation_number,true};
                    return true;
                });
                gsc::function::add("spaitestuseentityaim",[](const gsc::function_args& args)->scripting::script_value
                {
                    entity_aim_probe.store(args[0].as<int>()!=0);
                    return true;
                });
                gsc::function::add("spaitestentitypitch",[](const gsc::function_args& args)->scripting::script_value
                {
                    const auto ref=args[0].as<scripting::entity>().get_entity_reference();
                    if(ref.classnum || ref.entnum<18 || ref.entnum>=42)return false;
                    auto* entity=reinterpret_cast<unsigned char*>(&game::mp::g_entities[ref.entnum]);
                    if(!game::mp::g_entities[ref.entnum].agent || entity[10]!=18 ||
                        (*reinterpret_cast<const unsigned*>(entity+0x54)&0x1E001)!=1)return false;
                    if(args.size()>1)
                    {
                        const float pitch=args[1].as<float>();
                        if(!std::isfinite(pitch) || std::abs(pitch)>30)throw std::runtime_error("Entity aim pitch outside bounds");
                        entity_aim_probe.store(true);
                        *reinterpret_cast<float*>(entity+0xb8)=pitch;
                    }
                    return *reinterpret_cast<const float*>(entity+0xb8);
                });
                gsc::function::add("spaitestaimsample",[](const gsc::function_args& args)->scripting::script_value
                {
                    const auto ref=args[0].as<scripting::entity>().get_entity_reference();
                    if(ref.classnum || ref.entnum<18 || ref.entnum>=42)return false;
                    const auto pitch=aim_probe_pitch.load();
                    scheduler::once([id=ref.entnum,pitch]{capture_fixed_aim_pose(id,pitch);},scheduler::main);
                    return true;
                });
                gsc::function::add("spaitestaimpitch",[](const gsc::function_args& args)->scripting::script_value
                {
                    const float pitch=args[0].as<float>();
                    if(!std::isfinite(pitch) || std::abs(pitch)>30)throw std::runtime_error("Aim probe pitch outside bounds");
                    aim_probe_pitch.store(pitch);
                    return static_cast<int>(aim_probe_calls.load());
                });
            }
            if(!aim_probe_enabled)
            {
                check_code(0x61ED0,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18});
                check_code(0x557680,{0x40,0x53,0x48,0x81,0xEC,0xD0,0x00,0x00,0x00});
                combat_aim_controller_hook.create(game::base_address+0x61ED0,combat_aim_controller);
                combat_aim_producer_hook.create(game::base_address+0x557680,combat_aim_producer);
            }
            if(!aim_probe_enabled && utils::flags::has_flag("spai-aim-observe"))
            {
                check_code(0xF3D20,{0x40,0x53,0x48,0x81,0xEC,0x90,0x00,0x00,0x00});
                gsc::function::add("spaisampleaimpose",[](const gsc::function_args& args)->scripting::script_value
                {
                    const auto ref=args[0].as<scripting::entity>().get_entity_reference();
                    if(ref.classnum || ref.entnum<18 || ref.entnum>=42)return false;
                    scheduler::once([id=ref.entnum]{capture_fixed_aim_pose(id,0);},scheduler::main);
                    return true;
                });
                gsc::function::add("spaiinspectaim",[](const gsc::function_args& args)->scripting::script_value
                {
                    const auto ref=args[0].as<scripting::entity>().get_entity_reference();
                    if(ref.classnum || ref.entnum<18 || ref.entnum>=42)return {};
                    const auto* entity=reinterpret_cast<const unsigned char*>(&game::mp::g_entities[ref.entnum]);
                    if(!game::mp::g_entities[ref.entnum].agent || entity[10]!=18)return {};
                    return *reinterpret_cast<const float*>(entity+0xb8);
                });
            }
            gsc::function::add("spaisetcombataim",[](const gsc::function_args& args)->scripting::script_value
            {
                const auto ref=args[0].as<scripting::entity>().get_entity_reference();
                return !ref.classnum && set_server_aim(ref.entnum,args[1].as<float>());
            });
            gsc::function::add("spaiclearaim",[](const gsc::function_args& args)->scripting::script_value
            {
                const auto ref=args[0].as<scripting::entity>().get_entity_reference();
                if(ref.classnum || ref.entnum<18 || ref.entnum>=42)return false;
                clear_server_aim(ref.entnum);return true;
            });
            scheduler::once([]
            {
            // Native LUI's typed setters require registered variables. GSC's
            // setDvar-created strings are not a substitute for this contract.
            // Research settings are deliberately unsaved outside owned tests.
            dvars::register_bool("spai_bots",false,0,"Use SP-style bots in a private match");
            dvars::register_int("spai_enabled",0,0,1,0,"Active scripted population");
            dvars::register_int("spai_from_lobby",0,0,1,0,"Host-relative bot settings");
            dvars::register_int("spai_friendly",0,0,17,0,"Friendly scripted bots");
            dvars::register_int("spai_enemy",0,0,17,0,"Enemy scripted bots");
            dvars::register_int("spai_friendly_difficulty",1,0,4,0,"Friendly difficulty");
            dvars::register_int("spai_enemy_difficulty",1,0,4,0,"Enemy difficulty");
            dvars::register_string("spai_reserved_dvar","",0,"Temporary agent pool setting");
            dvars::register_string("spai_reserved_text","",0,"Previous agent pool setting including native default");
            // MP 0x519620 reads scr_<gametype>_maxagents as a string. Empty
            // follows exactly the same fallback as absent; do not invent a
            // numeric default or override an existing command-line setting.
            if(!game::Dvar_FindVar("scr_war_maxagents"))
                dvars::register_string("scr_war_maxagents","",0,"Optional TDM agent capacity");
            diagnostic("[SPAI] native lobby settings registered");
            },scheduler::main);
            if(utils::flags::has_flag("spai-network-probe") && !game::environment::is_dedi())
            {
                scheduler::on_game_initialized([]
                {
                    diagnostic("[SPAI_NET] connecting to owned loopback server 127.0.0.1:27109");
                    command::execute("connect 127.0.0.1:27109",false);
                },scheduler::main,15s);
                scheduler::loop([]
                {
                    const auto* counts=reinterpret_cast<const unsigned char*>(game::base_address+0xB7F9CC0);
                    const auto* cl=game::mp::cl.get();
                    std::ostringstream s;
                    s<<"[SPAI_NET] client state="<<*game::mp::connstate
                     <<" cgame="<<game::CL_IsCgameInitialized()<<" lobby="<<game::VirtualLobby_Loaded()
                     <<" server="<<game::SV_Loaded()<<" states="<<active_states<<" codec="<<static_cast<unsigned>(counts[0])
                     <<" player_tree="<<*reinterpret_cast<void**>(game::base_address+0x73D9FF0);
                    if(cl)s<<" time="<<cl->serverTime<<" snap="<<cl->snap.serverTime<<" valid="<<cl->snap.valid;
                    diagnostic(s.str());
                },scheduler::main,1s);
            }
            if(utils::flags::has_flag("spai-network-probe"))
            {
                check_code(0x1CB760,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10});
                animation_count_hook.create(game::base_address+0x1CB760,animation_count_init);
                animation_count_hook_installed=true;
            }
            if(!game::environment::is_dedi()) scheduler::loop([]
            {
                if(!demo_playback::playing())return;
                const auto* cl=game::mp::cl.get();
                if(!cl)return;
                const auto* counts=reinterpret_cast<const unsigned char*>(game::base_address+0xB7F9CC0);
                diagnostic("[SPAI_REPLAY_NATIVE] time="+std::to_string(cl->serverTime)+
                    " snap="+std::to_string(cl->snap.serverTime)+" valid="+std::to_string(cl->snap.valid)+
                    " message="+std::to_string(cl->snap.messageNum)+" delta="+std::to_string(cl->snap.deltaNum)+
                    " state="+std::to_string(*game::mp::connstate)+" armed="+std::to_string(demo_playback::is_replay_armed())+
                    " states="+std::to_string(active_states)+" codec="+std::to_string(counts[0]));
            },scheduler::main,1s);
            AddVectoredExceptionHandler(1,startup_exception_trace);
            gsc::function::add("spaireplayrequested",[](const gsc::function_args&)->scripting::script_value
            {return replay_requested();});
            gsc::function::add("spaianimationschema",[](const gsc::function_args&)->scripting::script_value
            {return static_cast<int>(replay_requested()?(replay_schema==3?2:replay_schema):animation_schema::current);});
            check_code(0x552E90,{0x48,0x89,0x5C,0x24,0x08});
            if(!game::environment::is_dedi())
            {
                check_code(0x75150,{0x48,0x89,0x7C,0x24,0x08});
                character_registry_reset_hook.create(game::base_address+0x75150,character_registry_reset);
            }
            gsc::function::add("spaipublishscores",[](const gsc::function_args& args)->scripting::script_value
            {return publish_score_rows(args[0].as<std::string>());});
            gsc::function::add("spaitestreadscores",[](const gsc::function_args&)->scripting::script_value
            {
                if(!game::SV_Loaded() || game::VirtualLobby_Loaded())return std::string{};
                const auto text=score_serverinfo();
                return text.empty()?std::string{}:utils::info_string(text).get("spai_scores");
            });
            connection_hook.create(game::base_address + 0x12BCD0, connection_limits);
            bit_hook.create(game::base_address + 0x73F90, read_bit);
            print_hook.create(console::print,trace_print);
            write_entity_hook.create(game::base_address+0x4F6B30,write_entity);
            read_field_hook.create(game::base_address+0x4ED0E0,read_field);
            actor_tree_hook.create(game::base_address+0x61570,actor_tree);
            client_tree_hook.create(game::base_address+0x32FEB0,client_tree);
            check_code(0x61610,{0x48,0x83,0xec,0x28,0x8b,0xd1});
            check_code(0x615D0,{0x48,0x83,0xec,0x28,0x8b,0x51,0x54});
            remote_class_hook.create(game::base_address+0x61610,remote_class_lookup);
            remote_entity_class_hook.create(game::base_address+0x615D0,remote_entity_class_lookup);
            check_code(0x553E60,{0x40,0x53,0x56,0x48,0x81,0xEC,0x88,0x08,0x00,0x00});
            server_system_info_hook.create(game::base_address+0x553E60,server_system_info);
            check_code(0x342ED0,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x40});
            client_system_info_hook.create(game::base_address+0x342ED0,client_system_info);
            check_code(0x618B6,{0xE8,0x05,0x38,0x33,0x00});
            check_code(0x61931,{0xE8,0x8A,0x37,0x33,0x00});
            utils::hook::call(game::base_address+0x618B6,remote_inline_class_lookup);
            utils::hook::call(game::base_address+0x61931,remote_inline_class_lookup);
            // Keep 0x61188/0x612B8 on the database class: they construct the
            // stock native trees before the remote campaign tree exists.
            check_code(0x61A3A,{0xE8,0x81,0x36,0x33,0x00});
            check_code(0x61F1E,{0xE8,0x9D,0x31,0x33,0x00});
            for(const auto offset:{0x61A3A,0x61F1E})
                utils::hook::call(game::base_address+offset,remote_inline_class_lookup);
            if(utils::flags::has_flag("spai-network-probe"))
            {
                check_code(0x618CE,{0xE8,0x1D,0xF5,0xFF,0xFF});
                check_code(0x61AB2,{0xE8,0x39,0xF3,0xFF,0xFF});
                utils::hook::call(game::base_address+0x618CE,remote_evaluate);
                utils::hook::call(game::base_address+0x61AB2,remote_evaluate);
            }
            check_code(0x5CA9A0,{0x48,0x89,0x5C,0x24,0x08,0x57});
            check_code(0x17F577,{0xE9,0xB4,0xC2,0x44,0x00});
            create_small_tree_hook.create(game::base_address+0x5CA9A0,create_small_tree);
            check_code(0x5188FB,{0x8B,0x4C,0x24,0x20,0x48,0x8D,0x05,0xAA,0x17,0xEC,0x06,0x48,0x8B,0x0C,0xC8});
            check_code(0x51890A,{0xE8,0x51,0x6C,0xC6,0xFF,0x49,0x89,0x87,0xB0,0x04,0x00,0x00});
            // At this exact call site RSI owns the spawning gentity, RCX the
            // selected class tree; the native wrapper supplies the allocator.
            utils::hook::call(game::base_address+0x51890A,utils::hook::assemble([](utils::hook::assembler& a)
            {
                a.mov(rdx,rsi);
                a.jmp(reinterpret_cast<void*>(spawn_rig_tree));
            }));
            utils::hook::jump(game::base_address+0x17F577,free_agent_tree);
            scripting::on_shutdown([](bool free_scripts,bool post_shutdown)
            {
                if(!post_shutdown)
                {
                    // Native release takes its own transient lock and updates
                    // reference counts before G_ShutdownGame resets the registry.
                    const auto count=spai_character_assets::release_census();
                    diagnostic("[SPAI_RIG_CENSUS] shutdown released="+std::to_string(count)+
                        " generation="+std::to_string(spai_character_assets::census_generation.load()));
                }
                if(post_shutdown){clear_score_rows();clear_all_server_aim();}
                if(!post_shutdown || !patched_class)return;
                // The client can still evaluate the old agent snapshot here.
                // Keep its published immutable class and tree valid until the
                // next generation is ready; an empty class crashes that path.
                generation_retired=true;inspect_actor_packets=false;
                std::lock_guard guard(lifetime_mutex);
                diagnostic("[SPAI] lifecycle retired="+std::to_string(generation_number)+
                    " free_scripts="+std::to_string(free_scripts)+" live_private_trees="+std::to_string(live_private_trees.size())+
                    " retained_allocations="+std::to_string(spai_campaign::allocations.size()));
            });
            utils::hook::call(game::base_address + 0x1598BA,error_format);
            if (!game::environment::is_dedi() && utils::flags::has_flag("spai-autodevmap"))
            {
                scheduler::on_game_initialized([]
                {
                    diagnostic("[SPAI] native devmap mp_backlot");
                    command::execute("devmap mp_backlot",false);
                },scheduler::main,15s);
                scheduler::loop([]
                {
                    const auto* error = reinterpret_cast<const char*>(game::base_address + 0x35624B0);
                    if (*error) diagnostic(std::string("[SPAI] last error: ") + std::string(error,strnlen(error,4096)));
                    diagnostic("[SPAI] status server=" + std::to_string(game::SV_Loaded())
                        + " lobby=" + std::to_string(game::VirtualLobby_Loaded())
                        + " cgame=" + std::to_string(game::CL_IsCgameInitialized()));
                },scheduler::main,5s);
            }
            // Research-only lifecycle fixture: queue after this script frame.
            // MP's changelevel builtin is absent, so do not invoke its null slot.
            gsc::function::add("spaitestmap", [](const gsc::function_args& args) -> scripting::script_value
            {
                const auto map=args[0].as<std::string>();
                if(map!="mp_backlot" && map!="mp_crash")return false;
                if(!game::SV_Loaded())return false;
                diagnostic("[SPAI] lifecycle accepted map="+map);
                scheduler::once([map]
                {
                    diagnostic("[SPAI] lifecycle queued map="+map);
                    command::execute("map "+map,false);
                },game::environment::is_dedi()?scheduler::server:scheduler::main,250ms);
                return true;
            });
            // Bounded research request: test installed world-weapon metadata on
            // a renderer-free server. Readiness must be checked by the caller.
            gsc::function::add("spaitestweaponzone", [](const gsc::function_args& args) -> scripting::script_value
            {
                const auto zone=args[0].as<std::string>();
                if(zone!="mp_wm_m16_base_tr" && zone!="mp_wm_m16_base_p_tr")return false;
                if(!game::SV_Loaded())return false;
                // Clients own these models through native transient streaming
                // triggered by precacheItem/precacheModel. A common-zone load
                // can overlap it even if the model is absent right now; never
                // introduce that competing lifetime. The caller waits for its
                // requested muzzle metadata and reports a bounded timeout.
                if(!game::environment::is_dedi())
                {
                    diagnostic("[SPAI] weapon metadata delegated to native client streaming");
                    return true;
                }
                scheduler::once([zone]
                {
                    if(!game::SV_Loaded())return;
                    diagnostic("[SPAI] weapon queued zone="+zone);
                    command::execute("spai_load_weapon_zone "+zone,false);
                },scheduler::main,250ms);
                return true;
            });
            // Match character requests use native reference-counted transient
            // ownership on the main thread, including locally hosted matches.
            gsc::function::add("spaipublishcharacters",[](const gsc::function_args& args)->scripting::script_value
            {return publish_character_manifest(args[0].as<std::string>());});
            gsc::function::add("spaicharacterfor", [](const gsc::function_args& args) -> scripting::script_value
            {
                const auto* choice=spai_character_assets::select(args[0].as<std::string>(),args[1].as<std::string>(),args[2].as<std::string>());
                if(!choice)return {};
                scripting::array result;
                result.set(std::string("body"),std::string(choice->body));
                result.set(std::string("head"),std::string(choice->head));
                return result;
            });
            gsc::function::add("spaiclearrig", [](const gsc::function_args& args) -> scripting::script_value
            {
                const auto ref=args[0].as<scripting::entity>().get_entity_reference();
                if(ref.classnum || ref.entnum>=2046 || !game::mp::g_entities[ref.entnum].agent)return false;
                clear_server_aim(ref.entnum);
                actor_rig_trees[ref.entnum].store(nullptr);
                return true;
            });
            gsc::function::add("spaibindcorpserig", [](const gsc::function_args& args) -> scripting::script_value
            {
                if(args.size()!=2 || !game::SV_Loaded() || game::VirtualLobby_Loaded())return false;
                const auto actor=args[0].as<scripting::entity>().get_entity_reference();
                const auto corpse=args[1].as<scripting::entity>().get_entity_reference();
                if(actor.classnum || corpse.classnum || actor.entnum>=42 || corpse.entnum<50 || corpse.entnum>=58)return false;
                const auto* source=reinterpret_cast<const unsigned char*>(&game::mp::g_entities[actor.entnum]);
                const auto* target=reinterpret_cast<const unsigned char*>(&game::mp::g_entities[corpse.entnum]);
                auto* rig=actor_rig_trees[actor.entnum].load();
                if(source[10]!=18 || target[10]!=19 || !rig || (*reinterpret_cast<const unsigned*>(target+0x54)&0x1E001)!=1)return false;
                actor_rig_trees[corpse.entnum].store(rig);
                diagnostic("[SPAI_CORPSE_RIG] actor="+std::to_string(actor.entnum)+" corpse="+std::to_string(corpse.entnum));
                return true;
            });
            gsc::function::add("spaicharacterready", [](const gsc::function_args& args) -> scripting::script_value
            {
                return spai_character_assets::ready(args[0].as<std::string>());
            });
            gsc::function::add("spaitestcharacterzone", [](const gsc::function_args& args) -> scripting::script_value
            {
                const auto model=args[0].as<std::string>();
                if(!game::SV_Loaded() || game::VirtualLobby_Loaded() || !spai_character_assets::allowed(model))return false;
                const auto* current_map=game::Dvar_FindVar("mapname");
                if(!current_map || !current_map->current.string)return false;
                const std::string map=current_map->current.string;
                const auto generation=spai_character_assets::census_generation.load();
                scheduler::once([model,map,generation]
                {
                    const auto* current=game::Dvar_FindVar("mapname");
                    if(generation!=spai_character_assets::census_generation.load() || !game::SV_Loaded() || !current || !current->current.string || map!=current->current.string)return;
                    command::execute("spai_load_character_zone "+model,false);
                },scheduler::main,250ms);
                return true;
            });
            gsc::function::add("spaitestreleasecharacterzones", [](const gsc::function_args&) -> scripting::script_value
            {
                if(!game::SV_Loaded() || game::VirtualLobby_Loaded())return false;
                const auto* current_map=game::Dvar_FindVar("mapname");
                if(!current_map || !current_map->current.string)return false;
                const std::string map=current_map->current.string;
                const auto generation=spai_character_assets::census_generation.load();
                scheduler::once([map,generation]
                {
                    const auto* current=game::Dvar_FindVar("mapname");
                    if(generation!=spai_character_assets::census_generation.load() || !game::SV_Loaded() || !current || !current->current.string || map!=current->current.string)return;
                    command::execute("spai_release_character_zones",false);
                },scheduler::main,50ms);
                return true;
            });
            gsc::function::add("spaitestcharacterowned", [](const gsc::function_args&) -> scripting::script_value
            {
                std::lock_guard lock(spai_character_assets::census_mutex);
                return static_cast<int>(spai_character_assets::census_models.size());
            });
            gsc::function::add("spaicampaign", [](const gsc::function_args& args) -> scripting::script_value
            {
                if(generation_retired)
                {
                    std::lock_guard guard(lifetime_mutex);
                    retire_current_storage(private_tree.load());
                    private_clips.fill(nullptr);private_campaign_indices.fill(0);active_states=0;generation_retired=false;
                }
                collect_retired();
                const auto allocation_start=spai_campaign::allocations.size();
                const auto string_start=spai_campaign::current.strings.size();
                const auto byte_start=spai_campaign::current.bytes;
                try
                {
                if(active_states)throw std::runtime_error("Cannot replace clips in a published animation generation");
                const auto index=args[0].as<int>();
                if(index<1 || index>=std::size(campaign_clips))throw std::runtime_error("Campaign test index outside catalog");
                const auto destination=args.size()>1?args[1].as<int>():(index==1?0:1);
                if(destination<0 || destination>=max_states)throw std::runtime_error("Campaign state slot outside catalog");
                if(private_clips[destination])throw std::runtime_error("Campaign state slot already staged");
                auto* replacement=spai_campaign::load(static_cast<size_t>(index));
                // This direct-state prototype only supports complete poses. The
                // type-6 campaign recoil assets require their authored base/layers;
                // using them alone produces an invalid pose despite valid notifies.
                if(replacement->assetType!=2)
                    throw std::runtime_error("Campaign clip requires an unsupported pose composition");
                // A combat burst owns exactly one cycle. Keep native markers and
                // timing, but do not let the automatic-fire source loop forever.
                if(animation_schema::states[destination].single_cycle) replacement->flags&=~1u;
                const auto* slot=animation_schema::states[destination].server_slot;
                auto* target=game::DB_FindXAssetHeader(game::ASSET_TYPE_XANIMPARTS,slot,0).parts;
                if(!target)throw std::runtime_error("Missing test animation slot");
                audit_rig(target,replacement);
                const auto* body=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,"body_usmc_desert_assault_mp_camo",0).model;
                if(!body) throw std::runtime_error("MP adapter body is missing");
                if(spai_campaign::adapt_mp_torso(replacement,body))
                    diagnostic(std::string("[SPAI] adapted MP torso stabilizer: ")+replacement->name);
                // Keep stock assets intact: campaign parts belong only to the
                // private actor tree assembled by spaiPrepare.
                private_clips[destination]=replacement;
                private_campaign_indices[destination]=static_cast<unsigned>(index);
                return true;
                }
                catch(const std::exception& e)
                {
                    spai_campaign::release_strings(spai_campaign::current.strings,string_start);
                    spai_campaign::allocations.resize(allocation_start);spai_campaign::current.bytes=byte_start;
                    console::error("[SPAI] campaign loader rejected input: %s\n",e.what());
                    return false;
                }
            });
            gsc::function::add("spaipreparerig", [](const gsc::function_args& args) -> scripting::script_value
            {
                if(!game::SV_Loaded() || game::VirtualLobby_Loaded() || args.size()!=1)return false;
                try {return prepare_rig_variant(args[0].as<std::string>())!=nullptr;}
                catch(const std::exception& error)
                {console::error("[SPAI] FAILED rig preparation: %s\n",error.what());return false;}
            });
            gsc::function::add("spaiuserig", [](const gsc::function_args& args) -> scripting::script_value
            {
                if(!game::SV_Loaded() || game::VirtualLobby_Loaded() || args.size()!=2)return false;
                const auto ref=args[0].as<scripting::entity>().get_entity_reference();
                const auto body=args[1].as<std::string>();
                if(ref.classnum || ref.entnum>=2046 || !game::mp::g_entities[ref.entnum].agent)return false;
                const auto found=rig_trees.find(body);
                if(found==rig_trees.end())return false;
                actor_rig_trees[ref.entnum].store(found->second);
                diagnostic("[SPAI_RIG_VARIANT] assigned actor="+std::to_string(ref.entnum)+" body="+body);
                return true;
            });
            // Private clones are deliberately absent from the global script-tree registry.
            // Query their native data directly instead of exposing an invalid public anim handle.
            gsc::function::add("spaianimduration",[](const gsc::function_args& args)->scripting::script_value
            {
                const auto* clip=private_clips[behavior_slot(args)];
                return static_cast<float>(clip->numframes)/clip->framerate;
            });
            gsc::function::add("spaianimevents",[](const gsc::function_args& args)->scripting::script_value
            {
                const auto* clip=private_clips[behavior_slot(args)];
                scripting::array events;
                for(unsigned i=0;i<clip->notifyCount;++i)
                {
                    const auto& marker=clip->notify[i];
                    if(!std::isfinite(marker.time)||marker.time<0||marker.time>1)
                        throw std::runtime_error("Invalid private animation event time");
                    scripting::array event;
                    event.set(std::string("name"),std::string(game::SL_ConvertToString(marker.name)));
                    event.set(std::string("time"),marker.time*static_cast<float>(clip->numframes)/clip->framerate);
                    events.push(event);
                }
                return events;
            });
            gsc::function::add("spaianimdelta",[](const gsc::function_args& args)->scripting::script_value
            {
                const auto slot=behavior_slot(args);
                const float start=args.size()>1?args[1].as<float>():0.0f;
                const float end=args.size()>2?args[2].as<float>():1.0f;
                if(!std::isfinite(start)||!std::isfinite(end)||start<0||end>1||end<start)
                    throw std::runtime_error("Invalid private animation time range");
                auto* tree=private_tree.load();if(!tree)throw std::runtime_error("Private tree is missing");
                const unsigned char expected[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10};
                if(std::memcmp(reinterpret_cast<void*>(game::base_address+0x5CC3C0),expected,sizeof(expected)))
                    throw std::runtime_error("Native animation delta fingerprint mismatch");
                float rotation[2]{},translation[3]{};
                utils::hook::invoke<void>(game::base_address+0x5CC3C0,tree,static_cast<unsigned>(published_machine->states[slot].animIndices[0]&0xffff),rotation,translation,start,end);
                for(const float value:translation)if(!std::isfinite(value)||std::abs(value)>8192)
                    throw std::runtime_error("Invalid private animation delta");
                return scripting::vector(translation[0],translation[1],translation[2]);
            });
            gsc::function::add("spailog", [](const gsc::function_args& args) -> scripting::script_value
            {
                const auto text=args[0].as<std::string>();
                std::ofstream file("spai-diagnostics/native-probe.log",std::ios::app);
                file << text << std::endl;
                console::info("%s\n",text.c_str());
                return {};
            });
            gsc::function::add("spaigrenaderemaining", [](const gsc::function_args& args) -> scripting::script_value
            {
                const auto missile=args[0].as<scripting::entity>();
                const auto ref=missile.get_entity_reference();
                if(ref.classnum || ref.entnum>=2046)return -1;
                const auto* bytes=reinterpret_cast<const unsigned char*>(&game::mp::g_entities[ref.entnum]);
                if(bytes[0xA]!=4)return -1;
                // G_InitGrenadeTimer stores an absolute server deadline here.
                // Reading it preserves cooking time, bounces and ownership.
                check_code(0x42925C,{0x8B,0x0D,0x3A,0x8D,0xF3,0x06,0x03,0xCA,0x89,0x8B,0xCC,0x01,0x00,0x00});
                const auto deadline=*reinterpret_cast<const int*>(bytes+0x1CC);
                const auto remaining=static_cast<int64_t>(deadline)-*game::mp::gameTime;
                if(remaining<0 || remaining>60000)return -1;
                return static_cast<int>(remaining);
            });
            gsc::function::add("spaifragradius", [](const gsc::function_args&) -> scripting::script_value
            {
                constexpr auto name="h1_fraggrenade_mp";
                if(!game::DB_XAssetExists(game::ASSET_TYPE_WEAPON,name) ||
                    game::DB_IsXAssetDefault(game::ASSET_TYPE_WEAPON,name))return -1;
                const auto* weapon=game::DB_FindXAssetHeader(game::ASSET_TYPE_WEAPON,name,0).weapon;
                if(!weapon || !weapon->szInternalName || std::strcmp(weapon->szInternalName,name) ||
                    weapon->explosionRadius<1 || weapon->explosionRadius>4096)return -1;
                return weapon->explosionRadius;
            });
            gsc::function::add("spaigrenadepose", [](const gsc::function_args& args) -> scripting::script_value
            {
                if(args.size()!=4 || !private_tree.load())return false;
                const auto missile=args[0].as<scripting::entity>(),owner=args[1].as<scripting::entity>();
                const auto mr=missile.get_entity_reference(),ar=owner.get_entity_reference();
                if(mr.classnum || ar.classnum || mr.entnum>=2046 || ar.entnum>=2046 || mr.entnum==ar.entnum)return false;
                auto* grenade=&game::mp::g_entities[mr.entnum];auto* actor=&game::mp::g_entities[ar.entnum];
                auto* b=reinterpret_cast<unsigned char*>(grenade);auto* a=reinterpret_cast<unsigned char*>(actor);
                const auto hold=args[3].as<int>()!=0;
                if(!grenade->isInUse || !actor->isInUse || b[10]!=4 || a[10]!=18 || (hold && owner.get<int>("health")<=0))return false;
                const auto weapon=*reinterpret_cast<unsigned*>(b+0x4C);
                if(weapon!=utils::hook::invoke<unsigned>(game::base_address+0x461110,"h1_fraggrenade_mp") || b[6])return false;
                const auto deadline=*reinterpret_cast<int*>(b+0x1CC);
                const auto remaining=static_cast<int64_t>(deadline)-*game::mp::gameTime;
                if(remaining<0 || remaining>60000)return false;
                const auto point=args[2].as<scripting::vector>();
                float source_distance=0,hand_distance=0,p[3]{};
                for(unsigned i=0;i<3;++i)
                {
                    if(!std::isfinite(point[i]))return false;
                    const auto d=grenade->origin[i]-actor->origin[i],h=point[i]-actor->origin[i];
                    source_distance+=d*d;hand_distance+=h*h;p[i]=point[i];
                }
                if(source_distance>96*96 || hand_distance>96*96)return false;
                check_code(0x45F5C0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10});
                check_code(0x45F5DE,{0x33,0xD2,0x48,0x8D,0x4E,0x60});
                check_code(0x426CE0,{0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18});
                if(hold)game::G_SetOrigin(grenade,p);
                else
                {
                    const float velocity[3]{};
                    utils::hook::invoke<void>(game::base_address+0x426CE0,grenade,p,velocity,weapon,0);
                }
                game::SV_LinkEntity(grenade);
                return *reinterpret_cast<int*>(b+0x1CC)==deadline;
            });
            gsc::function::add("spairethrow", [](const gsc::function_args& args) -> scripting::script_value
            {
                if(args.size()!=4 || !private_tree.load())return false;
                const auto missile=args[0].as<scripting::entity>(),owner=args[1].as<scripting::entity>();
                const auto mr=missile.get_entity_reference(),ar=owner.get_entity_reference();
                if(mr.classnum || ar.classnum || mr.entnum>=2046 || ar.entnum>=2046 || mr.entnum==ar.entnum)return false;
                auto* grenade=&game::mp::g_entities[mr.entnum];
                auto* actor=&game::mp::g_entities[ar.entnum];
                auto* b=reinterpret_cast<unsigned char*>(grenade);
                auto* a=reinterpret_cast<unsigned char*>(actor);
                if(!grenade->isInUse || !actor->isInUse || b[10]!=4 || a[10]!=18 || owner.get<int>("health")<=0)return false;
                const auto weapon=*reinterpret_cast<unsigned*>(b+0x4C);
                const auto frag=utils::hook::invoke<unsigned>(game::base_address+0x461110,"h1_fraggrenade_mp");
                if(!frag || weapon!=frag || b[6])return false;
                const auto deadline=*reinterpret_cast<int*>(b+0x1CC);
                const auto remaining=static_cast<int64_t>(deadline)-*game::mp::gameTime;
                if(remaining<250 || remaining>60000)return false;
                const auto start=args[2].as<scripting::vector>(),velocity=args[3].as<scripting::vector>();
                float pickup_distance=0,release_distance=0,speed=0;
                float p[3]{},v[3]{};
                for(unsigned i=0;i<3;++i)
                {
                    if(!std::isfinite(start[i]) || !std::isfinite(velocity[i]))return false;
                    const auto separation=grenade->origin[i]-actor->origin[i];
                    const auto release=start[i]-actor->origin[i];
                    pickup_distance+=separation*separation;release_distance+=release*release;
                    speed+=velocity[i]*velocity[i];p[i]=start[i];v[i]=velocity[i];
                }
                if(pickup_distance>96*96 || release_distance>96*96 || speed<1 || speed>1600*1600)return false;
                check_code(0x3F5D40,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10});
                check_code(0x426CE0,{0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18});
                check_code(0x426CF4,{0xC7,0x41,0x60,0x05,0x00,0x00,0x00});
                check_code(0x426BCC,{0x48,0x8D,0x8F,0xC0,0x01,0x00,0x00,0x48,0x8B,0xD6});
                const auto previous=*reinterpret_cast<unsigned short*>(b+0x150);
                // Use EntHandle::setEnt so owner reference lists remain valid.
                // Do not call grenade creation/setup/timer initialization here.
                utils::hook::invoke<void>(game::base_address+0x3F5D40,b+0x150,actor);
                utils::hook::invoke<void>(game::base_address+0x3F5D40,b+0x1C0,actor);
                utils::hook::invoke<void>(game::base_address+0x3F5D40,b+0x1C8,nullptr);
                utils::hook::invoke<void>(game::base_address+0x3F5D40,b+0x238,nullptr);
                *reinterpret_cast<unsigned short*>(b+2)=0x7FF; // stock scripted-agent source
                utils::hook::invoke<void>(game::base_address+0x426CE0,grenade,p,v,weapon,0);
                game::SV_LinkEntity(grenade);
                const auto preserved=*reinterpret_cast<int*>(b+0x1CC)==deadline;
                diagnostic("[SPAI] rethrow entity="+std::to_string(mr.entnum)+" previous_handle="+std::to_string(previous)+
                    " owner="+std::to_string(ar.entnum)+" deadline="+std::to_string(deadline)+" remaining="+std::to_string(remaining)+
                    " preserved="+std::to_string(preserved));
                return preserved;
            });
            gsc::function::add("spaibullet", [](const gsc::function_args& args) -> scripting::script_value
            {
                if(args.size()!=4 || !private_tree.load())throw std::runtime_error("Expected private actor, weapon, start and end");
                const auto ref=args[0].as<scripting::entity>().get_entity_reference();
                if(ref.classnum || ref.entnum>=2046)throw std::runtime_error("Expected a live agent entity");
                auto* actor=&game::mp::g_entities[ref.entnum];
                const auto* bytes=reinterpret_cast<unsigned char*>(actor);
                if(*reinterpret_cast<const unsigned short*>(bytes)!=ref.entnum || bytes[10]!=18)
                    throw std::runtime_error("Expected a private scripted agent");
                const auto name=args[1].as<std::string>();
                const auto start=args[2].as<scripting::vector>(),end=args[3].as<scripting::vector>();
                float distance_squared=0;
                for(unsigned i=0;i<3;++i)
                {
                    if(!std::isfinite(start[i]) || !std::isfinite(end[i]))throw std::runtime_error("Invalid bullet endpoints");
                    distance_squared+=(end[i]-start[i])*(end[i]-start[i]);
                }
                if(distance_squared<1 || distance_squared>8192.0f*8192.0f)throw std::runtime_error("Bullet range outside prototype limits");
                check_code(0x4617F0,{0x48,0x89,0x5C,0x24,0x08});
                check_code(0x3F3130,{0x48,0x83,0xEC,0x48,0x8B,0x44,0x24,0x78});
                const auto weapon=utils::hook::invoke<unsigned>(game::base_address+0x461110,name.c_str());
                if(!(weapon&255) || utils::hook::invoke<int>(game::base_address+0x2E7D20,weapon,false)!=1)
                    throw std::runtime_error("Prototype supports bullet weapons only");
                bullet_parms parms{};
                utils::hook::invoke<void>(game::base_address+0x4617F0,&parms,weapon,false,0u);
                const auto distance=std::sqrt(distance_squared);
                for(unsigned i=0;i<3;++i){parms.forward[i]=(end[i]-start[i])/distance;parms.muzzle[i]=start[i];}
                const auto time=*game::mp::gameTime;
                // R9 is the ignore entity. Stock magicBullet passes nullptr;
                // passing the shooter supplies its entity and linked-owner exclusions.
                utils::hook::invoke<void>(game::base_address+0x3F3130,actor,0.0f,&parms,actor,0,time,distance+1.0f);
                // Publish EV_FIRE_WEAPON on the actual agent. An anonymous temp
                // entity has no weapon DObj, so the client cannot attach its flash.
                // This carries only presentation; bullet damage was applied once above.
                check_code(0x45C040,{0x40,0x53,0x48,0x83,0xEC,0x20});
                const auto before=*reinterpret_cast<const unsigned*>(bytes+0x28);
                utils::hook::invoke<void>(game::base_address+0x45C040,actor,39,0);
                diagnostic("[SPAI_FIRE_SERVER] actor="+std::to_string(ref.entnum)+
                    " weapon="+std::to_string(weapon)+" entity_weapon="+std::to_string(*reinterpret_cast<const unsigned*>(bytes+0x4c))+
                    " sequence_before="+std::to_string(before)+" sequence_after="+std::to_string(*reinterpret_cast<const unsigned*>(bytes+0x28)));
                return true;
            });
            gsc::function::add("spaiauditclientmodels", [](const gsc::function_args& args) -> scripting::script_value
            {
                if(game::environment::is_dedi())return false;
                const auto ref=args[0].as<scripting::entity>().get_entity_reference();
                if(ref.classnum || ref.entnum>=64)throw std::runtime_error("Expected a character slot");
                auto* obj=utils::hook::invoke<void*>(game::base_address+0x4E98E0,ref.entnum,0);
                if(!obj)return false;
                const auto count=utils::hook::invoke<unsigned>(game::base_address+0x5C7780,obj);
                if(count>32)throw std::runtime_error("Invalid client character model count");
                auto models=nlohmann::json::array();
                for(unsigned i=0;i<count;++i)
                    if(const auto* model=utils::hook::invoke<game::XModel*>(game::base_address+0x5C7730,obj,i))
                        models.push_back(model->name);
                diagnostic("[SPAI_CLIENT_MODELS] actor="+std::to_string(ref.entnum)+" models="+models.dump());
                return true;
            });
            const auto client_anim_time = [](const gsc::function_args& args) -> scripting::script_value
            {
                if(game::environment::is_dedi() || active_states<12)return -1.0f;
                const auto ref=args[0].as<scripting::entity>().get_entity_reference();
                size_t slot=11;
                if(args.size()==1 && ref.classnum==0 && ref.entnum>=50 && ref.entnum<58)
                {
                    const auto* centity=utils::hook::invoke<const unsigned char*>(game::base_address+0x6BFF0,0,ref.entnum);
                    if(!centity)return -1.0f;
                    slot=(*reinterpret_cast<const unsigned*>(centity+0x13c+0x54)>>1)&0x7f;
                    if(slot>=active_states || !std::string_view(animation_schema::states[slot].name).starts_with("death"))return -1.0f;
                }
                if(args.size()>1)
                {
                    const auto name=args[1].as<std::string>();slot=active_states;
                    for(size_t i=0;i<active_states;++i)if(name==animation_schema::states[i].name){slot=i;break;}
                    if(slot==active_states)return -1.0f;
                }
                if(ref.classnum || (args.size()==1?(ref.entnum<50 || ref.entnum>=58):(ref.entnum<18 || ref.entnum>=42)))return -1.0f;
                auto* obj=utils::hook::invoke<void*>(game::base_address+0x4E98E0,ref.entnum,0);
                if(!obj)return -1.0f;
                auto* tree=utils::hook::invoke<unsigned char*>(game::base_address+0x5C3390,obj);
                if(!tree || *reinterpret_cast<void**>(tree)!=actor_rig_trees[ref.entnum].load())return -1.0f;
                // Read-only inspection of the native XAnimInfo pool, whose
                // stride and offsets are verified by XAnimGetFrequency.
                check_code(0x5CBB3C,{0x48,0x8D,0x35,0x4D,0x18,0x41,0x0C});
                auto* pool=reinterpret_cast<const unsigned char*>(game::base_address+0xC9DD390);
                std::vector<unsigned short> pending{*reinterpret_cast<unsigned short*>(tree+8)};
                std::unordered_set<unsigned short> seen;
                while(!pending.empty() && seen.size()<128)
                {
                    const auto index=pending.back();pending.pop_back();
                    if(!index || !seen.insert(index).second)continue;
                    if(index>=16384)return -1.0f;
                    const auto* node=pool+index*72;
                    pending.push_back(*reinterpret_cast<const unsigned short*>(node+12));
                    pending.push_back(*reinterpret_cast<const unsigned short*>(node+14));
                    if(*reinterpret_cast<const unsigned short*>(node+18)!=private_leaf_indices[slot] ||
                       !*reinterpret_cast<const unsigned short*>(node+20))continue;
                    const auto time=*reinterpret_cast<const float*>(node+40);
                    const auto weight=*reinterpret_cast<const float*>(node+56);
                    if(!std::isfinite(time) || !std::isfinite(weight) || time<0 || time>1 || weight<=0)return -1.0f;
                    if(args.size()==1)diagnostic("[SPAI_CLIENT_DEATH] corpse="+std::to_string(ref.entnum)+" time="+std::to_string(time)+" weight="+std::to_string(weight));
                    else diagnostic("[SPAI_CLIENT_ANIM] actor="+std::to_string(ref.entnum)+" state="+animation_schema::states[slot].name+" time="+std::to_string(time)+" weight="+std::to_string(weight));
                    return time;
                }
                return -1.0f;
            };
            gsc::function::add("spaiclientdeathtime",client_anim_time);
            gsc::function::add("spaiclientanimtime",client_anim_time);
            gsc::function::add("spaiweaponbones", [](const gsc::function_args& args) -> scripting::script_value
            {
                const auto name=args[0].as<std::string>();
                const auto* model=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,name.c_str(),0).model;
                if(!model || !model->baseMat || !model->boneNames)throw std::runtime_error("Missing weapon model");
                nlohmann::json report;
                std::unordered_set<const game::XModel*> seen;
                std::function<void(const game::XModel*)> inspect=[&](const game::XModel* part)
                {
                    if(!part || !seen.insert(part).second || seen.size()>64)return;
                    auto bones=nlohmann::json::array();
                    for(unsigned i=0;i<part->numBones;++i)
                    {
                        const auto& m=part->baseMat[i];
                        bones.push_back({{"name",game::SL_ConvertToString(part->boneNames[i])},
                            {"p",{m.trans[0],m.trans[1],m.trans[2]}},{"q",{m.quat[0],m.quat[1],m.quat[2],m.quat[3]}}});
                    }
                    report[part->name]=bones;
                    for(unsigned i=0;i<part->numCompositeModels;++i)inspect(part->compositeModels[i]);
                };
                inspect(model);
                std::ofstream("spai-diagnostics/weapon-bones.json")<<report.dump(2);
                return true;
            });
            gsc::function::add("spaiweaponflashoffset", [](const gsc::function_args& args) -> scripting::script_value
            {
                const auto name=args[0].as<std::string>(),tag=args[1].as<std::string>();
                const auto* model=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,name.c_str(),0).model;
                if(!model || !model->boneNames || !model->baseMat || !model->numBones)return {};
                // Streamed placeholders must not yield a guessed muzzle. This
                // bounded path supports the tested rigid weapon root convention.
                const auto& root=model->baseMat[0];
                if(std::strcmp(game::SL_ConvertToString(model->boneNames[0]),"tag_weapon"))return {};
                for(unsigned i=0;i<3;++i)if(std::abs(root.trans[i])>0.0001f || std::abs(root.quat[i])>0.0001f)return {};
                if(std::abs(std::abs(root.quat[3])-1)>0.0001f)return {};
                for(unsigned i=0;i<model->numBones;++i)
                    if(tag==game::SL_ConvertToString(model->boneNames[i]))
                    {
                        const auto& p=model->baseMat[i].trans;
                        for(const auto v:p)if(!std::isfinite(v) || std::abs(v)>128)return {};
                        return scripting::vector(p[0],p[1],p[2]);
                    }
                return {};
            });
            gsc::function::add("spaiprepare", [](const gsc::function_args& args) -> scripting::script_value
            {
                if(active_states)throw std::runtime_error("Private animation generation is already published");
                if(args.size()!=2 && args.size()!=6 && args.size()!=7 && args.size()!=9 && args.size()!=10 && args.size()!=11 && args.size()!=12 && args.size()!=19 && args.size()!=24 && args.size()!=max_states)throw std::runtime_error("Unsupported private animation state count");
                const auto requested_states=args.size();
                if(replay_requested() && requested_states!=animation_schema::state_count(replay_schema))
                    throw std::runtime_error("Replay animation state count differs from its recorded schema");
                auto* asset = game::DB_FindXAssetHeader(game::ASSET_TYPE_ANIMCLASS, "default_anim", 0).animClass;
                if (!asset || (asset->stateMachine && asset->stateMachine!=published_machine))
                    throw std::runtime_error("Expected an empty or owned default animation class");
                // These globals were traced from the tested executable's SpawnAgent routine.
                auto** trees = reinterpret_cast<void**>(game::base_address + 0x73DA0B0);
                auto* player_tree = *reinterpret_cast<void**>(game::base_address + 0x73D9FF0);
                if (!player_tree) throw std::runtime_error("Multiplayer animation tree is not loaded");
                {
                    std::ofstream dump("spai-diagnostics/player-tree-header.bin",std::ios::binary);
                    const auto count=*static_cast<unsigned*>(player_tree);
                    if(count==0 || count>8192) throw std::runtime_error("Unexpected player animation tree size");
                    dump.write(static_cast<const char*>(player_tree),16+count*16);
                }
                auto* machine=static_cast<game::AnimationStateMachine*>(spai_campaign::allocate(sizeof(game::AnimationStateMachine)));
                auto* states=static_cast<game::AnimationState*>(spai_campaign::allocate(sizeof(game::AnimationState)*requested_states));
                auto* entries=static_cast<game::AnimationEntry*>(spai_campaign::allocate(sizeof(game::AnimationEntry)*requested_states));
                auto* indices=static_cast<unsigned __int64*>(spai_campaign::allocate(sizeof(unsigned __int64)*requested_states));
                for (size_t i=0;i<requested_states;++i)
                {
                    const auto raw = args.get_raw()[i].get_raw();
                    if (raw.type != game::VAR_ANIMATION) throw std::runtime_error("Expected animation type");
                    static_assert(sizeof(raw.u)==sizeof(indices[i]));
                    std::memcpy(&indices[i], &raw.u, sizeof(raw.u));
                    const auto& definition=animation_schema::states[i];
                    entries[i] = {spai_campaign::intern(definition.name), spai_campaign::intern(definition.server_slot)};
                    states[i] = {};
                    states[i].name = entries[i].alias;
                    states[i].notify = spai_campaign::intern("spai_anim");
                    states[i].blendTime = 0.15f;
                    states[i].entryCount = 1;
                    states[i].animEntries = &entries[i];
                    states[i].animIndices = &indices[i];
                    console::info("[SPAI] state %s animation=0x%llX\n",definition.name,indices[i]);
                }
                *machine={};
                machine->name=spai_campaign::intern("spai_probe");
                machine->stateCount=static_cast<unsigned short>(requested_states);
                machine->states=states;
                const auto tree_count=*static_cast<unsigned*>(player_tree);
                const auto tree_bytes=16+tree_count*16;
                auto* isolated=static_cast<unsigned char*>(spai_campaign::allocate(tree_bytes));
                std::memcpy(isolated,player_tree,tree_bytes);
                for(size_t i=0;i<requested_states;++i)
                {
                    const auto index=static_cast<unsigned>(indices[i]&0xFFFF);
                    if(index>=tree_count) throw std::runtime_error("Private animation leaf outside tree");
                    auto** leaf=reinterpret_cast<game::XAnimParts**>(isolated+16+index*16);
                    if(!*leaf || std::string((*leaf)->name)!=animation_schema::states[i].server_slot)
                        throw std::runtime_error("Private animation leaf layout mismatch");
                    if(private_clips[i]) *leaf=private_clips[i];
                    private_leaf_indices[i]=index;
                }
                // Publish the class/tree only after every leaf was validated.
                if(asset->stateMachine!=published_machine){original_class=*asset;original_tree_slot=trees[0];}
                patched_class=asset;
                asset->stateMachine=machine;
                asset->animCtrl=(aim_probe_enabled || !replay_requested() || replay_schema>=7)?game::ANIMCTRL_PLAYER:game::ANIMCTRL_NONE;
                asset->animTree=spai_campaign::intern("multiplayer");
                {
                    std::lock_guard guard(lifetime_mutex);
                    known_private_anims.insert(isolated);
                }
                ++generation_number;
                published_machine=machine;active_states=requested_states;remote_generation=false;
                private_tree.store(isolated);
                trees[0]=isolated;
                // The preceding generation is no longer published. Reclaim it
                // now if all native trees have released it; otherwise the next
                // initialization will retry after observing native destruction.
                collect_retired();
                diagnostic("[SPAI] isolated actor animation tree; stock MP assets unchanged");
                // Native server initialization caches the state/entry counts used by
                // snapshot encoding. This runtime class was created after that pass.
                // Rebuild the source table; the normal snapshot setup copies it to
                // its worker cache before writing the next network update.
                const unsigned char expected[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10};
                if (!animation_count_hook_installed && std::memcmp(reinterpret_cast<void*>(game::base_address+0x1CB760),expected,sizeof(expected)))
                    throw std::runtime_error("Animation count initializer fingerprint mismatch");
                utils::hook::invoke<void>(game::base_address+0x1CB760);
                const auto* counts=reinterpret_cast<const unsigned char*>(game::base_address+0x2DCF588);
                if(counts[0]!=active_states || !std::all_of(counts+16,counts+16+active_states,[](auto n){return n==1;}))
                    throw std::runtime_error("Server animation counts were not initialized");
                diagnostic("[SPAI] server animation counts rebuilt: states="+std::to_string(active_states)+" entries=1 each");
                inspect_actor_packets=true;
                console::info("[SPAI] prepared explicit states; tree=%p class=%p\n",player_tree,asset);
                return true;
            });
            gsc::function::add("spaiwiretest", [](const gsc::function_args&) -> scripting::script_value
            {
                const auto* counts=reinterpret_cast<const unsigned char*>(game::base_address+0xB7F9CC0);
                if(counts[0]!=active_states || !std::all_of(counts+16,counts+16+active_states,[](auto n){return n==1;}))
                    throw std::runtime_error("Snapshot animation counts have not caught up");
                std::vector<std::array<unsigned,3>> transitions;
                for(unsigned from=0;from<=active_states;++from)
                    for(unsigned to=0;to<active_states;++to)
                        transitions.push_back({from==0?0:((from-1)*2+1),to*2+1,0});
                for(const auto& transition:transitions)
                {
                    char buffer[32]{};
                    game::msg_t msg{};
                    game::MSG_Init(&msg,buffer,sizeof(buffer));
                    utils::hook::invoke<void>(game::base_address+0x4F3170,&msg,&transition[0],&transition[1]);
                    const int bits=msg.bit;
                    if(msg.overflowed || bits<1 || bits>32)
                        throw std::runtime_error("Unexpected animation packet length");
                    msg.bit=0;msg.readcount=0;msg.readOnly=1;
                    const auto decoded=utils::hook::invoke<unsigned>(game::base_address+0x4EC610,&msg,&transition[0]);
                    if(msg.overflowed || msg.bit!=bits || decoded!=transition[1])
                        throw std::runtime_error("Animation codec roundtrip mismatch");
                    diagnostic("[SPAI] codec roundtrip "+std::to_string(transition[0])+" -> "+std::to_string(decoded)
                        +" bits="+std::to_string(bits)+" PASS");
                }
                return true;
            });
        }
    };
}
REGISTER_COMPONENT(spai_probe::component)
