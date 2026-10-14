#pragma once

namespace spai_campaign
{
    // The tested MP body inserts pelvis -> torso_stabilizer -> j_spinelower.
    // The SP clips do not animate torso_stabilizer. Leaving its inverse-root
    // bind rotation active changes the basis of the entire SP torso chain.
    // Give this extra MP bone a neutral local rotation and compensate the
    // spine's bind translation for its new parent basis. Original bone indices,
    // root motion and notifies remain unchanged. Only the private clip is edited.
    inline bool adapt_mp_torso(game::XAnimParts* clip, const game::XModel* body)
    {
        const auto stabilizer=intern("torso_stabilizer");
        for(unsigned i=0;i<clip->boneCount[9];++i) if(clip->names[i]==stabilizer) return false;
        auto index=[&](const char* name)->int
        {
            for(unsigned i=0;i<body->numBones;++i)
                if(std::string_view(game::SL_ConvertToString(body->boneNames[i]))==name) return static_cast<int>(i);
            throw std::runtime_error("MP adaptation requires the verified torso hierarchy");
        };
        auto parent=[&](int i)->int
        { return i<body->numRootBones?-1:i-body->parentList[i-body->numRootBones]; };
        if(parent(index("j_spinelower"))!=index("torso_stabilizer") || parent(index("torso_stabilizer"))!=index("pelvis"))
            throw std::runtime_error("Unverified MP torso hierarchy");
        if(clip->numframes>=256 || clip->boneCount[9]>=254 || clip->boneCount[4]>=254 || clip->boneCount[8]>=254
            || !clip->dataShort || !clip->dataByte || !clip->dataInt || !body->baseMat
            || clip->dataShortCount>65531 || clip->dataByteCount>=65535 || clip->dataIntCount>65532)
            throw std::runtime_error("Clip outside bounded MP torso adapter");
        unsigned rotations=0,translations=0;
        for(unsigned i=0;i<5;++i) rotations+=clip->boneCount[i];
        for(unsigned i=5;i<9;++i) translations+=clip->boneCount[i];
        if(rotations!=clip->boneCount[9] || translations!=rotations)
            throw std::runtime_error("Invalid animation partitions");
        // For these sub-256-frame clips, animated frame indices are dataBytes;
        // each animated rotation consumes one dataShort key-count value.
        const unsigned insertion=clip->boneCount[1]+clip->boneCount[2]+2*clip->boneCount[3]+4*clip->boneCount[4];
        if(insertion>clip->dataShortCount) throw std::runtime_error("Truncated rotation stream");
        unsigned byte_at=0,short_at=0,int_at=0,random_short_at=0,random_byte_at=0;
        const auto* source_bytes=reinterpret_cast<const unsigned char*>(clip->dataByte);
        auto key_count=[&]()->unsigned {
            if(short_at>=clip->dataShortCount) throw std::runtime_error("Truncated animation key count");
            const unsigned n=static_cast<unsigned short>(clip->dataShort[short_at++])+1;
            if(n>clip->numframes+1u || byte_at+n>clip->dataByteCount) throw std::runtime_error("Invalid animation key count");
            for(unsigned j=0;j<n;++j)
                if(source_bytes[byte_at+j]>clip->numframes || (j && source_bytes[byte_at+j]<=source_bytes[byte_at+j-1]))
                    throw std::runtime_error("Invalid animation key frames");
            byte_at+=n;return n;
        };
        for(unsigned group=1;group<=2;++group)
            for(unsigned j=0;j<clip->boneCount[group];++j) random_short_at+=key_count()*(group==1?2:4);
        short_at=insertion;
        const auto spine_name=intern("j_spinelower");
        int spine=-1;
        for(unsigned j=0;j<rotations;++j) if(clip->names[j]==spine_name) spine=static_cast<int>(j);
        if(spine<0) throw std::runtime_error("Campaign clip has no lower spine");
        std::vector<bool> translated(rotations,false);
        auto bone_index=[&]()->unsigned {
            if(byte_at>=clip->dataByteCount) throw std::runtime_error("Truncated translation index");
            const unsigned v=source_bytes[byte_at++];
            if(v>=rotations || translated[v]) throw std::runtime_error("Invalid or duplicate translation index");
            translated[v]=true;return v;
        };
        int spine_int=-1,spine_zero=-1;
        for(unsigned group=5;group<=6;++group)
            for(unsigned j=0;j<clip->boneCount[group];++j) {
                const auto bone=bone_index();const auto n=key_count();
                if(bone==static_cast<unsigned>(spine)) spine_int=static_cast<int>(int_at);
                int_at+=6;
                if(group==5) random_byte_at+=n*3;else random_short_at+=n*3;
            }
        for(unsigned j=0;j<clip->boneCount[7];++j) {
            if(bone_index()==static_cast<unsigned>(spine)) spine_int=static_cast<int>(int_at);
            int_at+=3;
        }
        const unsigned zero_begin=byte_at;
        for(unsigned j=0;j<clip->boneCount[8];++j) {
            const auto at=byte_at;
            if(bone_index()==static_cast<unsigned>(spine)) spine_zero=static_cast<int>(at);
        }
        if(byte_at!=clip->dataByteCount || short_at!=clip->dataShortCount || int_at!=clip->dataIntCount
            || random_short_at!=clip->randomDataShortCount || random_byte_at!=clip->randomDataByteCount)
            throw std::runtime_error("Animation stream counts disagree");
        if(spine_int<0 && spine_zero<0) throw std::runtime_error("Missing lower-spine translation");
        // baseMat is model-space bind data. Express the same spine displacement
        // in pelvis space now that torso_stabilizer has an identity rotation.
        const auto& lower=body->baseMat[index("j_spinelower")];
        const auto& stabilizer_bind=body->baseMat[index("torso_stabilizer")];
        const auto& pelvis_bind=body->baseMat[index("pelvis")];
        auto local=[&](const auto& parent_bind) {
            std::array<float,3> v{},u{},cross{};
            float length=0;for(unsigned j=0;j<4;++j) length+=parent_bind.quat[j]*parent_bind.quat[j];
            if(!std::isfinite(length) || length<0.99f || length>1.01f) throw std::runtime_error("Invalid bind quaternion");
            const float scale=1.0f/std::sqrt(length),w=parent_bind.quat[3]*scale;
            for(unsigned j=0;j<3;++j) {v[j]=lower.trans[j]-parent_bind.trans[j];u[j]=-parent_bind.quat[j]*scale;}
            cross={u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
            const std::array<float,3> second={u[1]*cross[2]-u[2]*cross[1],u[2]*cross[0]-u[0]*cross[2],u[0]*cross[1]-u[1]*cross[0]};
            for(unsigned j=0;j<3;++j) v[j]+=2*(w*cross[j]+second[j]);
            return v;
        };
        auto corrected=local(pelvis_bind);const auto previous=local(stabilizer_bind);
        for(unsigned j=0;j<3;++j) {
            if(std::abs(pelvis_bind.trans[j]-stabilizer_bind.trans[j])>0.001f) throw std::runtime_error("Unexpected stabilizer displacement");
            corrected[j]-=previous[j];
            if(!std::isfinite(corrected[j]) || std::abs(corrected[j])>16) throw std::runtime_error("Invalid spine correction");
        }
        const unsigned added_ints=spine_zero>=0?3:0;
        auto* ints=static_cast<int*>(allocate((clip->dataIntCount+added_ints)*sizeof(int)));
        std::memcpy(ints,clip->dataInt,clip->dataIntCount*sizeof(int));
        if(spine_zero>=0) {spine_int=clip->dataIntCount;std::memset(ints+spine_int,0,12);}
        for(unsigned j=0;j<3;++j) {
            float value;std::memcpy(&value,ints+spine_int+j,4);value+=corrected[j];
            if(!std::isfinite(value)) throw std::runtime_error("Invalid spine translation");
            std::memcpy(ints+spine_int+j,&value,4);
        }
        auto* bytes=static_cast<unsigned char*>(allocate(clip->dataByteCount+1));
        if(spine_zero>=0) {
            // Promote the zero track to the end of the static partition.
            std::memcpy(bytes,clip->dataByte,zero_begin);bytes[zero_begin]=static_cast<unsigned char>(spine);
            unsigned out=zero_begin+1;
            for(unsigned j=zero_begin;j<clip->dataByteCount;++j) if(j!=static_cast<unsigned>(spine_zero)) bytes[out++]=clip->dataByte[j];
        } else std::memcpy(bytes,clip->dataByte,clip->dataByteCount);
        // Zero translation tracks have explicit indices too; do not leave the
        // native decoder reading beyond the array for our added stabilizer.
        bytes[clip->dataByteCount]=static_cast<unsigned char>(rotations);
        auto* names=static_cast<game::scr_string_t*>(allocate((rotations+1)*sizeof(game::scr_string_t)));
        std::memcpy(names,clip->names,rotations*sizeof(game::scr_string_t)); names[rotations]=stabilizer;
        auto* shorts=static_cast<short*>(allocate((clip->dataShortCount+4)*sizeof(short)));
        std::memcpy(shorts,clip->dataShort,insertion*sizeof(short));
        shorts[insertion]=0;shorts[insertion+1]=0;shorts[insertion+2]=0;shorts[insertion+3]=32767;
        std::memcpy(shorts+insertion+4,clip->dataShort+insertion,(clip->dataShortCount-insertion)*sizeof(short));
        clip->names=names;clip->dataShort=shorts;clip->dataShortCount+=4;
        clip->dataByte=reinterpret_cast<decltype(clip->dataByte)>(bytes);++clip->dataByteCount;clip->dataInt=ints;clip->dataIntCount+=static_cast<unsigned short>(added_ints);
        if(spine_zero>=0){++clip->boneCount[7];--clip->boneCount[8];}
        ++clip->boneCount[4];++clip->boneCount[8];++clip->boneCount[9];
        return true;
    }
}
