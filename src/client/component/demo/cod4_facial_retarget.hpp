#pragma once
#include "cod4_facial_animation.hpp"

namespace cod4::face
{
    struct bone { std::string name; int parent{-1}; pose rest; };
    using skeleton = std::vector<bone>;

    inline vec3 rotate(const quat& rotation, const vec3& value)
    {
        const auto q = normalized(rotation);
        const auto result = multiply(multiply(q, {value[0],value[1],value[2],0}),conjugate(q));
        return {result[0],result[1],result[2]};
    }
    inline pose compose(const pose& parent, const pose& child)
    {
        for(float v:parent.translation) if(!std::isfinite(v)) throw std::runtime_error("Invalid facial rest position");
        for(float v:child.translation) if(!std::isfinite(v)) throw std::runtime_error("Invalid facial rest position");
        auto position = rotate(parent.rotation,child.translation);
        for(unsigned i=0;i<3;++i)
        {
            position[i] += parent.translation[i];
            if(!std::isfinite(position[i])) throw std::runtime_error("Overflowed facial rest position");
        }
        return {normalized(multiply(parent.rotation,child.rotation)),position};
    }
    inline pose relative(const pose& parent, const pose& child)
    {
        vec3 position{};
        for(unsigned i=0;i<3;++i) position[i] = child.translation[i]-parent.translation[i];
        const auto inverse = conjugate(normalized(parent.rotation));
        return {normalized(multiply(inverse,child.rotation)),rotate(inverse,position)};
    }
    inline std::vector<pose> model_space(const skeleton& bones, const std::vector<pose>& local)
    {
        if(bones.empty() || bones.size()>254 || local.size()!=bones.size())
            throw std::runtime_error("Invalid facial rest skeleton bounds");
        std::vector<pose> result;
        for(unsigned i=0;i<bones.size();++i)
        {
            if(bones[i].parent < -1 || bones[i].parent >= static_cast<int>(i))
                throw std::runtime_error("Invalid facial rest hierarchy");
            for(float v:local[i].translation) if(!std::isfinite(v)) throw std::runtime_error("Invalid facial rest position");
            normalized(local[i].rotation);
            result.push_back(bones[i].parent < 0 ? local[i] : compose(result[bones[i].parent],local[i]));
        }
        return result;
    }
    inline std::vector<pose> rest_model_space(const skeleton& bones)
    {
        std::vector<pose> local;
        for(const auto& b:bones) local.push_back(b.rest);
        return model_space(bones,local);
    }

    // Authenticated against the five v25 stock head parts used by the supplied
    // recordings: absolute parent byte, float3 position, packed XYZ quaternion
    // per non-root bone, followed by every bone name and a bone-count-plus-one
    // trailer. The trailer is not needed for rest transforms and is not given
    // guessed classification/flag semantics here.
    inline skeleton decode_rest(std::span<const std::uint8_t> bytes)
    {
        if(bytes.size()>1024*1024) throw std::runtime_error("Oversized CoD4 head rest asset");
        reader r(bytes);
        if(r.read<std::uint16_t>()!=25) throw std::runtime_error("Unsupported CoD4 head rest version");
        const unsigned children=r.read<std::uint16_t>(), roots=r.read<std::uint16_t>();
        if(!roots || roots+children>254) throw std::runtime_error("Invalid CoD4 head rest bounds");
        skeleton result(roots+children);
        for(unsigned i=roots;i<result.size();++i)
        {
            result[i].parent = r.read<std::uint8_t>();
            if(result[i].parent>=static_cast<int>(i)) throw std::runtime_error("Invalid CoD4 head rest parent");
            result[i].rest.translation={r.real(),r.real(),r.real()};
            std::array<int,3> packed{r.read<std::int16_t>(),r.read<std::int16_t>(),r.read<std::int16_t>()};
            const auto squared=std::int64_t{32767}*32767-(std::int64_t{packed[0]}*packed[0]
                +std::int64_t{packed[1]}*packed[1]+std::int64_t{packed[2]}*packed[2]);
            result[i].rest.rotation=normalized({static_cast<float>(packed[0]),static_cast<float>(packed[1]),
                static_cast<float>(packed[2]),std::floor(std::sqrt(static_cast<float>(std::max(std::int64_t{0},squared)))+0.5f)});
        }
        std::vector<std::string> names;
        for(auto& b:result)
        {
            b.name=r.text();
            if(b.name.empty() || std::find(names.begin(),names.end(),b.name)!=names.end())
                throw std::runtime_error("Duplicate/empty CoD4 head rest name");
            names.push_back(b.name);
        }
        r.take(result.size()+1);
        if(!r.finished()) throw std::runtime_error("Trailing CoD4 head rest data");
        return result;
    }

    enum class region : unsigned { generic, jaw, eyes, eyelids, brows, cheeks, lips, count };
    struct expression_strength
    {
        // Initial artistic calibration, not measured CoD4/H1 expression parity.
        std::array<float, static_cast<unsigned>(region::count)> values{1, 0.25f, 1, 0.55f, 0.35f, 0.25f, 0.2f};
        float blend_seconds{0.2f};
        bool operator==(const expression_strength&) const = default;
    };
    struct correspondence
    {
        unsigned source{}, destination{};
        int source_reference{-1}, destination_reference{-1}, secondary_source{-1};
        region area{region::generic};
        float translation_factor{1}, rotation_factor{1};
    };

    // Compiled XAnim translations are offsets from XModelParts tag positions,
    // while rotations replace the local rest rotation. A zero translation key
    // preserves the bind position; it must not move a facial joint to its parent.
    inline pose sample_local(const clip& animation, const track& channel, double seconds, const pose& rest)
    {
        auto result = sample(animation, channel, seconds, rest);
        if (!channel.translations.empty())
            for (unsigned i = 0; i < 3; ++i) result.translation[i] += rest.translation[i];
        return result;
    }

    // Transfer expression changes in head space, preserving the target's own
    // neutral pose and facial proportions. Explicit correspondences are supplied
    // only after the destination rig has been inspected. No name/index guessing.
    // Lower lips/chin transfer their OWN motion relative to the animated jaw.
    // The destination jaw then supplies inherited motion about its own pivot.
    // This preserves H1's jaw-to-lip lever arms instead of copying CoD4's.
    inline std::vector<pose> retarget(const skeleton& source, const clip& animation, double seconds,
        unsigned source_head, const skeleton& destination, unsigned destination_head,
        std::span<const correspondence> mapping, float translation_scale, const expression_strength& strength = {})
    {
        if(source_head>=source.size() || destination_head>=destination.size()
            || source[source_head].name!="j_head" || destination[destination_head].name!="j_head"
            || !std::isfinite(translation_scale) || translation_scale<=0 || translation_scale>4)
            throw std::runtime_error("Invalid facial rest calibration");
        const auto original=rest_model_space(source), target=rest_model_space(destination);
        std::vector<pose> animated_local;
        for(const auto& b:source)
        {
            auto p=b.rest;
            const auto found=std::find_if(animation.tracks.begin(),animation.tracks.end(),
                [&](const track& t){return t.bone==b.name && facial_bone(t.bone);});
            if(found!=animation.tracks.end()) p=sample_local(animation,*found,seconds,b.rest);
            animated_local.push_back(p);
        }
        const auto animated=model_space(source,animated_local);
        auto changed=target;
        std::vector<const correspondence*> destinations(destination.size());
        const auto below_head=[](const skeleton& bones,unsigned index,unsigned head)
        {
            while(index!=head && bones[index].parent>=0) index=static_cast<unsigned>(bones[index].parent);
            return index==head;
        };
        for(const auto& pair:mapping)
        {
            if(pair.source>=source.size() || pair.destination>=destination.size()
                || !facial_bone(source[pair.source].name) || !destination[pair.destination].name.starts_with("jnt_")
                || !below_head(source,pair.source,source_head) || !below_head(destination,pair.destination,destination_head)
                || destinations[pair.destination])
                throw std::runtime_error("Invalid facial control correspondence");
            const unsigned sr = pair.source_reference < 0 ? source_head : static_cast<unsigned>(pair.source_reference);
            const unsigned dr = pair.destination_reference < 0 ? destination_head : static_cast<unsigned>(pair.destination_reference);
            const auto area = static_cast<unsigned>(pair.area);
            if (sr >= source.size() || dr >= pair.destination || !below_head(source, pair.source, sr)
                || !below_head(source, sr, source_head) || !below_head(destination, pair.destination, dr)
                || !below_head(destination, dr, destination_head) || area >= strength.values.size()
                || !std::isfinite(strength.values[area]) || strength.values[area] < 0 || strength.values[area] > 1
                || !std::isfinite(pair.translation_factor) || pair.translation_factor < 0 || pair.translation_factor > 4
                || !std::isfinite(pair.rotation_factor) || pair.rotation_factor < 0 || pair.rotation_factor > 1)
                throw std::runtime_error("Invalid facial region calibration");
            if (pair.secondary_source >= 0 && (static_cast<unsigned>(pair.secondary_source) >= source.size()
                || !facial_bone(source[pair.secondary_source].name) || !below_head(source, pair.secondary_source, sr)))
                throw std::runtime_error("Invalid facial centre control");
            destinations[pair.destination] = &pair;
        }
        std::vector<pose> result;
        for(unsigned i=0;i<destination.size();++i)
        {
            // Keep unmapped controls in their own local rest poses so they
            // follow a mapped parent naturally (teeth and jaw attachments).
            if(!destinations[i])
                changed[i]=destination[i].parent<0 ? destination[i].rest : compose(changed[destination[i].parent],destination[i].rest);
            else
            {
                const auto& pair = *destinations[i];
                const unsigned sr = pair.source_reference < 0 ? source_head : static_cast<unsigned>(pair.source_reference);
                const unsigned dr = pair.destination_reference < 0 ? destination_head : static_cast<unsigned>(pair.destination_reference);
                const auto source_frame = relative(original[source_head], original[sr]);
                const auto target_frame = relative(target[destination_head], target[dr]);
                const auto delta = [&](unsigned bone)
                {
                    const auto before = relative(original[sr], original[bone]);
                    const auto after = relative(animated[sr], animated[bone]);
                    vec3 translation{};
                    for (unsigned c = 0; c < 3; ++c) translation[c] = after.translation[c] - before.translation[c];
                    return pose{normalized(multiply(multiply(source_frame.rotation,
                        multiply(after.rotation, conjugate(before.rotation))), conjugate(source_frame.rotation))),
                        rotate(source_frame.rotation, translation)};
                };
                auto motion = delta(pair.source);
                if (pair.secondary_source >= 0)
                {
                    const auto second = delta(static_cast<unsigned>(pair.secondary_source));
                    motion.rotation = interpolate(motion.rotation, second.rotation, 0.5f);
                    for (unsigned c = 0; c < 3; ++c) motion.translation[c] = (motion.translation[c] + second.translation[c]) * 0.5f;
                }
                const float gain = strength.values[static_cast<unsigned>(pair.area)];
                motion.rotation = interpolate({0, 0, 0, 1}, motion.rotation, gain * pair.rotation_factor);
                for (auto& component : motion.translation) component *= translation_scale * gain * pair.translation_factor;
                const auto inverse = conjugate(target_frame.rotation);
                const auto rotation = multiply(multiply(inverse, motion.rotation), target_frame.rotation);
                const auto translation = rotate(inverse, motion.translation);
                auto output = relative(target[dr], target[i]);
                output.rotation = normalized(multiply(rotation, output.rotation));
                for (unsigned c = 0; c < 3; ++c) output.translation[c] += translation[c];
                changed[i] = compose(changed[dr], output);
            }
            result.push_back(destination[i].parent<0 ? changed[i] : relative(changed[destination[i].parent],changed[i]));
        }
        return result;
    }
}
