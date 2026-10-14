#pragma once
#include "component/demo/theater_sky_source.hpp"
#include "campaign_index.hpp"
#include <utils/cryptography.hpp>

namespace spai_campaign
{
    struct storage
    {
        std::vector<std::unique_ptr<unsigned char[]>> allocations;
        std::vector<game::scr_string_t> strings;
        size_t bytes=0;
    };
    inline storage current;
    inline auto& allocations=current.allocations;
    inline game::scr_string_t intern(const char* name)
    {
        const auto id=game::SL_GetString(name,0);
        current.strings.push_back(id);
        return id;
    }
    inline void release_strings(std::vector<game::scr_string_t>& strings,size_t first=0)
    {
        while(strings.size()>first)
        {
            game::VariableUnion value{};value.stringValue=strings.back();
            game::RemoveRefToValue(game::VAR_STRING,value);
            strings.pop_back();
        }
    }
    inline void release(storage& retired)
    {
        release_strings(retired.strings);
        retired.allocations.clear();retired.bytes=0;
    }
    inline void* allocate(size_t size)
    {
        if (!size || size > 1024*1024) throw std::runtime_error("Campaign allocation bounds");
        auto p=std::make_unique<unsigned char[]>(size); auto* result=p.get();
        allocations.push_back(std::move(p));current.bytes+=size;return result;
    }
    inline std::vector<unsigned char> read_range(const indexed_clip& clip)
    {
        std::ifstream f("common.ff",std::ios::binary|std::ios::ate);
        if(!f || f.tellg()!=static_cast<std::streamoff>(campaign_file_size))
            throw std::runtime_error("Unsupported installed campaign common.ff");
        std::vector<unsigned char> result(clip.length);size_t copied=0;
        for(const auto& block:campaign_blocks)
        {
            const auto first=std::max(clip.offset,block.decoded_offset);
            const auto last=std::min(clip.offset+clip.length,block.decoded_offset+block.decoded_size);
            if(last<=first)continue;
            std::vector<unsigned char> encoded(static_cast<size_t>(block.size1)+block.size2);
            f.seekg(block.offset1);f.read(reinterpret_cast<char*>(encoded.data()),block.size1);
            if(block.size2){f.seekg(block.offset2);f.read(reinterpret_cast<char*>(encoded.data()+block.size1),block.size2);}
            if(!f)throw std::runtime_error("Campaign source read failed");
            if(utils::cryptography::sha256::compute(encoded.data(),encoded.size(),true)!=block.sha256)
                throw std::runtime_error("Campaign compressed block fingerprint mismatch");
            const auto decoded=theater_sky::source::decode(encoded,block.decoded_size);
            std::memcpy(result.data()+first-clip.offset,decoded.data()+first-block.decoded_offset,last-first);
            copied+=last-first;
        }
        if(copied!=clip.length)throw std::runtime_error("Incomplete campaign index");
        return result;
    }
    struct reader
    {
        std::vector<unsigned char> data;
        size_t position=0;
        explicit reader(std::vector<unsigned char> bytes):data(std::move(bytes)){}
        void take(void* output,size_t size)
        {
            if(position>data.size() || size>data.size()-position)throw std::runtime_error("Truncated campaign animation");
            std::memcpy(output,data.data()+position,size);position+=size;
        }
        template<typename T> T value(){T v{};take(&v,sizeof(v));return v;}
        template<typename T> T* array(size_t count)
        {
            if(count>1024*1024/sizeof(T))throw std::runtime_error("Campaign array overflow");
            auto* p=static_cast<T*>(allocate(std::max(size_t(1),count*sizeof(T))));take(p,count*sizeof(T));return p;
        }
        std::string string()
        {
            std::string s;
            for(size_t i=0;i<512;++i){const auto c=value<char>();if(!c)return s;s+=c;}
            throw std::runtime_error("Campaign string too long");
        }
    };
    inline void inline_pointer(const void* ptr)
    {
        if(reinterpret_cast<uintptr_t>(ptr)!=0xFDFDFDFFFFFFFFFFULL)
            throw std::runtime_error("Unsupported campaign pointer relocation");
    }
    inline std::vector<std::string> strings()
    {
        reader r(read_range(campaign_clips[0]));r.position=72;
        const auto count=r.value<uint32_t>();if(count>8192)throw std::runtime_error("Campaign string count");
        r.position=112;auto* pointers=r.array<uint64_t>(count);
        std::vector<std::string> out;
        for(unsigned i=0;i<count;++i)
        {
            if(!pointers[i])out.emplace_back();
            else {inline_pointer(reinterpret_cast<void*>(pointers[i]));out.push_back(r.string());}
        }
        return out;
    }
    inline game::XAnimParts* load(size_t index)
    {
        if(index==0 || index>=std::size(campaign_clips))throw std::runtime_error("Campaign clip index");
        const auto table=strings(); reader r(read_range(campaign_clips[index]));
        auto* a=r.array<game::XAnimParts>(1);inline_pointer(a->name);
        const auto name=r.string();if(name!=campaign_clips[index].name)throw std::runtime_error("Campaign header identity mismatch");
        auto* stored=static_cast<char*>(allocate(name.size()+1));std::memcpy(stored,name.c_str(),name.size()+1);a->name=stored;
        if(!a->numframes || !std::isfinite(a->framerate) || a->framerate<1 || a->framerate>240)
            throw std::runtime_error("Campaign animation timing invalid");
        const auto intern=[&](game::scr_string_t id){if(id>=table.size())throw std::runtime_error("Campaign string index");return spai_campaign::intern(table[id].c_str());};
        if(a->names){inline_pointer(a->names);a->names=r.array<game::scr_string_t>(a->boneCount[9]);for(unsigned i=0;i<a->boneCount[9];++i)a->names[i]=intern(a->names[i]);}
        if(a->notify)
        {
            inline_pointer(a->notify);a->notify=r.array<game::XAnimNotifyInfo>(a->notifyCount);
            for(unsigned i=0;i<a->notifyCount;++i){auto& n=a->notify[i];n.name=intern(n.name);if(!std::isfinite(n.time)||n.time<0||n.time>1)throw std::runtime_error("Campaign notify timing");}
        }
        if(a->deltaPart)
        {
            inline_pointer(a->deltaPart);auto* d=r.array<game::XAnimDeltaPart>(1);a->deltaPart=d;
            if(d->trans)
            {
                inline_pointer(d->trans);const auto header=r.value<uint64_t>();const auto count=static_cast<uint16_t>(header);
                if(count>a->numframes)throw std::runtime_error("Campaign delta count");
                const size_t index_bytes=(count+1)*(a->numframes>=256?2:1);
                auto* t=static_cast<game::XAnimPartTrans*>(allocate(sizeof(game::XAnimPartTrans)+index_bytes));std::memcpy(t,&header,8);d->trans=t;
                if(count)
                {
                    r.take(&t->u,32);r.take(&t->u.frames.indices,index_bytes);
                    if(t->u.frames.frames._1)
                    {
                        inline_pointer(t->u.frames.frames._1);
                        if(t->smallTrans)t->u.frames.frames._1=reinterpret_cast<unsigned char(*)[3]>(r.array<unsigned char>((count+1)*3));
                        else t->u.frames.frames._2=reinterpret_cast<unsigned short(*)[3]>(r.array<unsigned short>((count+1)*3));
                    }
                }
                else r.take(t->u.frame0,12);
            }
            const auto quaternion=[&]<typename T>(T*& p,unsigned components)
            {
                if(!p)return;inline_pointer(p);const auto h=r.value<uint64_t>();const auto count=static_cast<uint16_t>(h);
                if(count>a->numframes)throw std::runtime_error("Campaign quaternion count");
                const size_t bytes=(count+1)*(a->numframes>=256?2:1);
                p=static_cast<T*>(allocate(sizeof(T)+bytes));std::memcpy(p,&h,8);
                if(count)
                {
                    r.take(&p->u,8);r.take(&p->u.frames.indices,bytes);
                    if(p->u.frames.frames){inline_pointer(p->u.frames.frames);p->u.frames.frames=reinterpret_cast<decltype(p->u.frames.frames)>(r.array<short>((count+1)*components));}
                }
                else r.take(p->u.frame0,components*2);
            };
            quaternion(d->quat2,2);quaternion(d->quat,4);
        }
        const auto array=[&]<typename T>(T*& ptr,size_t count){if(ptr){inline_pointer(ptr);ptr=r.array<T>(count);}else if(count)throw std::runtime_error("Missing campaign array");};
        array(a->dataByte,a->dataByteCount);array(a->dataShort,a->dataShortCount);array(a->dataInt,a->dataIntCount);
        array(a->randomDataShort,a->randomDataShortCount);array(a->randomDataByte,a->randomDataByteCount);array(a->randomDataInt,a->randomDataIntCount);
        if(a->indices.data){inline_pointer(a->indices.data);a->indices.data=r.array<unsigned char>(a->indexCount*(a->numframes>=256?2:1));}
        if(a->secondaryName)
        {
            // H1 serializes the alternate's asset name after the indexed pose
            // arrays (see the H1 XAnimParts writer). Our schema selects this
            // concrete full-pose clip, not the database's random alternative
            // chain. Consume/validate the reference, then pin that selection.
            inline_pointer(a->secondaryName);
            const auto secondary=r.string();
            if(secondary.empty() || secondary.size()>128 || a->assetType!=2 || a->alternativeAnim ||
                !std::all_of(secondary.begin(),secondary.end(),[](unsigned char c){return std::isalnum(c) || c=='_';}))
                throw std::runtime_error("Unsupported campaign alternate reference");
            console::info("[SPAI] pinned campaign variant=%s secondary=%s alternatives=%u\n",a->name,secondary.c_str(),a->numAlternatives);
            a->secondaryName=nullptr;a->alternateAnimWeight=0;a->numAlternatives=0;
        }
        if(a->blendShapeCount || a->blendShapeWeightNames || a->blendShapeCoefficientMagnitudes || a->numberOfBlendShapeKeys || a->blendShapeKeys || a->compressedBlendShapeCoefficients || a->blendShapeWeights || a->alternativeAnim || a->svAmimData)
            throw std::runtime_error("Unsupported campaign animation extensions");
        console::info("[SPAI] campaign clip=%s frames=%u bones=%u notifies=%u bytes=%zu from installed common.ff\n",a->name,a->numframes,a->boneCount[9],a->notifyCount,r.position);
        return a;
    }
}
