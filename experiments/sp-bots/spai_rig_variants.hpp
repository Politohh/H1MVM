// Included in spai_probe, after the generation state and published machine.
// Preparation is explicit during server scripting or client tree setup; actor-tree lookups
// only read published pointers and never load assets or mutate clip arrays.
void* prepare_rig_variant(const std::string& body_name)
{
    if(generation_retired || !active_states || !private_tree.load())
        throw std::runtime_error("No active animation generation for rig preparation");
    if(const auto found=rig_trees.find(body_name);found!=rig_trees.end())return found->second;
    if(!std::any_of(cod4::native_characters.begin(),cod4::native_characters.end(),
        [&](const auto& item){return item.body==body_name;}) || !spai_character_assets::ready(body_name))
        throw std::runtime_error("Requested MP rig is not fully loaded");
    const auto* body=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,body_name.c_str(),0).model;
    if(body_name==(remote_generation?remote_base_body:"body_usmc_desert_assault_mp_camo"))
    {rig_trees.emplace(body_name,private_tree.load());return private_tree.load();}
    const auto* base=static_cast<const unsigned char*>(private_tree.load());
    const auto count=*reinterpret_cast<const unsigned*>(base);
    if(!count || count>8192)throw std::runtime_error("Invalid base tree for rig preparation");
    const auto allocation_start=spai_campaign::allocations.size();
    const auto string_start=spai_campaign::current.strings.size();
    const auto byte_start=spai_campaign::current.bytes;
    void* prepared=nullptr;
    std::string prepared_message;
    try
    {
        auto* tree=static_cast<unsigned char*>(spai_campaign::allocate(16+count*16));
        std::memcpy(tree,base,16+count*16);
        for(size_t i=0;i<active_states;++i)
        {
            const auto leaf=private_leaf_indices[i];
            if(leaf>=count || !private_clips[i] || !private_campaign_indices[i])
                throw std::runtime_error("Rig variant requires complete campaign leaves");
            if(*reinterpret_cast<game::XAnimParts* const*>(base+16+leaf*16)!=private_clips[i])
                throw std::runtime_error("Rig variant leaf does not belong to the active base");
            auto* clip=spai_campaign::load(private_campaign_indices[i]);
            if(clip->assetType!=2)throw std::runtime_error("Unsupported rig variant pose composition");
            if(animation_schema::states[i].single_cycle)clip->flags&=~1u;
            clip->flags=private_clips[i]->flags;
            if(!spai_campaign::adapt_mp_torso(clip,body))throw std::runtime_error("Rig clip was already adapted");
            *reinterpret_cast<game::XAnimParts**>(tree+16+leaf*16)=clip;
        }
        std::ostringstream message;
        message<<"[SPAI_RIG_VARIANT] prepared body="<<body_name<<" states="<<active_states
            <<" tree="<<reinterpret_cast<uintptr_t>(tree)<<" generation="<<generation_number;
        prepared_message=message.str();
        {
            std::lock_guard guard(lifetime_mutex);
            const auto inserted=known_private_anims.insert(tree).second;
            if(!inserted)throw std::runtime_error("Rig tree address already published");
            try {rig_trees.emplace(body_name,tree);}
            catch(...) {known_private_anims.erase(tree);throw;}
        }
        prepared=tree;
    }
    catch(...)
    {
        spai_campaign::release_strings(spai_campaign::current.strings,string_start);
        spai_campaign::allocations.resize(allocation_start);spai_campaign::current.bytes=byte_start;
        throw;
    }
    // Publication commits the arena. A diagnostic allocation failure must not
    // roll it back while native lookups can already see the new tree.
    try {diagnostic(prepared_message);} catch(...) {}
    return prepared;
}
