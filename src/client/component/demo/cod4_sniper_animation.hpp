#pragma once
#include "cod4_sniper_style.hpp"
#include <map>

namespace cod4::sniper
{
    struct action
    {
        face::clip animation;
        int timer_ms{};
        bool loop{}, scale_to_timer{};
        double clip_seconds(double elapsed) const
        {
            // COD4's StartWeaponAnim uses XAnim length / weapon action time.
            // Fire and rechamber retain their natural authored playback rate.
            return scale_to_timer && timer_ms > 0
                ? elapsed * animation.frames * 1000.0 / (animation.fps * timer_ms) : elapsed;
        }
    };
    struct animation_set
    {
        face::clip up, down;
        face::skeleton hands, gun, rig;
        std::map<unsigned, action> actions;
        std::array<float, 28> recoil{};
    };
    inline void stabilize_draw_cartridge(animation_set& source)
    {
        const auto idle = source.actions.find(0);
        if (idle == source.actions.end()) return;
        const auto& tracks = idle->second.animation.tracks;
        const auto cartridge = std::find_if(tracks.begin(), tracks.end(),
            [](const auto& t) { return t.bone == "bullet_joint"; });
        if (cartridge == tracks.end()) return;
        // COD4's M40/R700 pull-out moves the round above its chambered bind.
        // That small offset exposes it through the remastered receiver, then
        // snaps back at idle. Use the chambered idle control for every draw
        // variant; reload/rechamber keep their authored cartridge movement.
        const auto rest = face::sample(idle->second.animation, *cartridge, 0, {});
        const face::track chambered{"bullet_joint", {{0, rest.rotation}}, {{0, rest.translation}}};
        for (const unsigned logical : {18u, 19u, 26u, 28u, 30u})
        {
            const auto found = source.actions.find(logical);
            if (found == source.actions.end()) continue;
            auto& draw = found->second.animation.tracks;
            const auto control = std::find_if(draw.begin(), draw.end(),
                [](const auto& t) { return t.bone == "bullet_joint"; });
            if (control != draw.end()) *control = chambered;
            else draw.push_back(chambered);
        }
    }
    inline unsigned bone_index(const face::skeleton& rig, std::string_view name)
    {
        for (unsigned i = 0; i < rig.size(); ++i) if (rig[i].name == name) return i;
        throw std::runtime_error("Missing COD4 sniper control: " + std::string(name));
    }
    inline face::skeleton combine_rigs(face::skeleton hands, const face::skeleton& gun)
    {
        const auto attachment = bone_index(hands, "tag_weapon");
        const auto offset = static_cast<int>(hands.size());
        for (auto b : gun)
        {
            for (const auto& existing : hands) if (existing.name == b.name)
                throw std::runtime_error("Duplicate COD4 sniper rig bone");
            b.parent = b.parent < 0 ? static_cast<int>(attachment) : b.parent + offset;
            hands.push_back(std::move(b));
        }
        (void)face::rest_model_space(hands);
        return hands;
    }
    // Viewhands strip tagPositions to zero and carry their complete local
    // positions in the animation. Gun parts retain their tagPositions and use
    // offset channels. Reconstruct the native geometric bind from baseMat;
    // tag_view's model placement is not part of an animation.
    inline face::skeleton geometry_rest(face::skeleton rig, std::span<const face::pose> bind_world)
    {
        if (rig.size() != bind_world.size()) throw std::runtime_error("Invalid sniper bind bounds");
        for (unsigned i = 0; i < rig.size(); ++i)
        {
            rig[i].rest = rig[i].parent < 0 ? bind_world[i]
                : face::relative(bind_world[rig[i].parent], bind_world[i]);
            if (rig[i].name == "tag_view") rig[i].rest = {};
        }
        return rig;
    }
    inline face::pose gun_alignment(const face::skeleton& source, const face::skeleton& target)
    {
        const auto source_world = face::rest_model_space(source);
        const auto target_world = face::rest_model_space(target);
        // The Barrett's scope mount moved relative to its receiver in MWR.
        // Using it to place the whole gun breaks the magazine/hand contact.
        // Both versions share tag_clip; locate this receiver by its magazine
        // seat and calibrate its optical endpoint separately.
        for (unsigned i = 0; i < source.size(); ++i)
        {
            if (source[i].name != "tag_clip") continue;
            for (unsigned j = 0; j < target.size(); ++j) if (target[j].name == "tag_clip")
                return face::compose(source_world[i], face::relative(target_world[j], face::pose{}));
        }
        // Bolt-action rifles retain the shared scope mounting reference.
        for (unsigned i = 0; i < source.size(); ++i)
        {
            if (!source[i].name.starts_with("tag_") || !source[i].name.ends_with("_scope")) continue;
            for (unsigned j = 0; j < target.size(); ++j) if (target[j].name == source[i].name)
                return face::compose(source_world[i], face::relative(target_world[j], face::pose{}));
        }
        return {};
    }
    inline bool gun_bone(const face::skeleton& rig, unsigned index)
    {
        for (int i = static_cast<int>(index); i >= 0; i = rig.at(i).parent)
            if (rig[i].name == "j_gun") return true;
        return false;
    }
    inline face::pose sample_action_bone(const face::skeleton& rig, unsigned index,
        const face::clip& clip, const face::track& track, double seconds)
    {
        // Unlike stripped viewhands, gun channels are relative to XModelParts
        // tag positions. Treating a zero reload key as an absolute position
        // moves the magazine/bolt/bullet to j_gun instead of its own pivot.
        return gun_bone(rig, index) ? face::sample_local(clip, track, seconds, rig[index].rest)
            : face::sample(clip, track, seconds, rig[index].rest);
    }
    inline std::string_view source_gun_bone(std::string_view target)
    {
        // MWR splits the M40/R700 bolt cap into a sibling of j_bolt, while
        // COD4 animates the complete bolt with j_bolt. Keep its native bind.
        return target == "j_bolt_cap" ? "j_bolt" : target;
    }
    // Viewhands transfer model-space motion across native helper bones. Gun
    // mechanics transfer parent-relative movement around native bind pivots.
    // Unmapped helper bones retain their native local rest.
    inline std::vector<face::pose> retarget_action(const face::skeleton& source,
        const face::clip& clip, double seconds, const face::skeleton& target)
    {
        auto source_local = std::vector<face::pose>{};
        for (const auto& b : source) source_local.push_back(b.rest);
        for (const auto& track : clip.tracks)
        {
            const auto found = std::find_if(source.begin(), source.end(), [&](const auto& b) { return b.name == track.bone; });
            if (found != source.end())
            {
                const auto index = static_cast<unsigned>(found - source.begin());
                source_local[index] = sample_action_bone(source, index, clip, track, seconds);
            }
        }
        const auto source_rest = face::rest_model_space(source);
        const auto source_world = face::model_space(source, source_local);
        const auto target_rest = face::rest_model_space(target);
        std::vector<face::pose> world, local;
        for (unsigned i = 0; i < target.size(); ++i)
        {
            const auto& b = target[i];
            const auto name = source_gun_bone(b.name);
            const auto found = std::find_if(source.begin(), source.end(), [&](const auto& s) { return s.name == name; });
            auto value = b.parent < 0 ? b.rest : face::compose(world.at(b.parent), b.rest);
            if (found != source.end())
            {
                const auto index = static_cast<unsigned>(found - source.begin());
                int attachment = -1, source_attachment = -1;
                if (gun_bone(source, index) && gun_bone(target, i) && b.name != "j_gun")
                {
                    // Mechanical motion rotates about each part's native pivot.
                    // A whole-model skinning delta also rotates the difference
                    // between COD4/MWR pivots, sweeping the bolt/magazine off
                    // the receiver even when its translation channel is zero.
                    // Find a shared ancestor; inserted MWR helpers are skipped.
                    for (int p = b.parent; p >= 0 && attachment < 0; p = target[p].parent)
                    {
                        const auto parent_name = source_gun_bone(target[p].name);
                        for (int s = source[index].parent; s >= 0; s = source[s].parent)
                            if (source[s].name == parent_name)
                            { attachment = p; source_attachment = s; break; }
                    }
                }
                if (attachment >= 0)
                {
                    const auto bind = face::relative(source_rest[source_attachment], source_rest[index]);
                    const auto motion = face::relative(source_world[source_attachment], source_world[index]);
                    auto native = face::relative(target_rest[attachment], target_rest[i]);
                    const auto basis = face::normalized(face::multiply(face::conjugate(target_rest[attachment].rotation),
                        source_rest[source_attachment].rotation));
                    face::vec3 movement{};
                    for (unsigned a = 0; a < 3; ++a) movement[a] = motion.translation[a] - bind.translation[a];
                    movement = face::rotate(basis, movement);
                    for (unsigned a = 0; a < 3; ++a) native.translation[a] += movement[a];
                    const auto rotation = face::multiply(motion.rotation, face::conjugate(bind.rotation));
                    native.rotation = face::normalized(face::multiply(face::multiply(face::multiply(basis, rotation),
                        face::conjugate(basis)), native.rotation));
                    value = face::compose(world[attachment], native);
                }
                else
                {
                    // Viewhands retain model-space transfer across helper bones;
                    // j_gun transfers the complete authored weapon movement.
                    value = face::compose(source_world[index], face::relative(source_rest[index], target_rest[i]));
                }
            }
            local.push_back(b.parent < 0 ? value : face::relative(world.at(b.parent), value));
            world.push_back(value);
        }
        return local;
    }
    inline std::vector<face::pose> retarget_gun(const face::skeleton& source,
        const face::clip& clip, double seconds, const face::skeleton& target)
    {
        auto local = retarget_action(source, clip, seconds, target);
        const auto alignment = gun_alignment(source, target);
        const auto root = bone_index(target, "j_gun");
        local[root] = face::compose(local[root], alignment);
        // Detached magazines follow the authored hand contact, including the
        // lever arm during rotation. Receiver-fixed parts (bolts/cartridges)
        // still use native pivots in retarget_action.
        const auto s = std::find_if(source.begin(), source.end(), [](const auto& b) { return b.name == "tag_clip"; });
        const auto t = std::find_if(target.begin(), target.end(), [](const auto& b) { return b.name == "tag_clip"; });
        if (s != source.end() && t != target.end())
        {
            const auto si = static_cast<unsigned>(s - source.begin()), ti = static_cast<unsigned>(t - target.begin());
            const auto sw = face::model_space(source, retarget_action(source, clip, seconds, source));
            const auto sr = face::rest_model_space(source), tr = face::rest_model_space(target);
            const auto tw = face::model_space(target, local);
            const auto desired = face::compose(sw[si], face::relative(sr[si], face::compose(alignment, tr[ti])));
            local[ti] = t->parent < 0 ? desired : face::relative(tw[t->parent], desired);
        }
        return local;
    }
    inline face::vec3 optical_ads_correction(const animation_set& source,
        const face::skeleton& hands, const face::skeleton& gun, const face::skeleton& scope)
    {
        // Calibrate the authored full-ADS endpoint against the actual native
        // optical anchor. Scope mounts alone do not locate the MWR eyepiece.
        const auto& idle = source.actions.at(0).animation;
        auto h = retarget_action(source.hands, idle, 0, hands);
        auto g = retarget_gun(source.gun, idle, 0, gun);
        const auto ads = bone_index(hands, "tag_ads");
        h[ads] = face::sample(source.up, source.up.tracks.at(0),
            clip_seconds(1, false, source.up.frames, source.up.fps), {});
        const auto hw = face::model_space(hands, h), gw = face::model_space(gun, g);
        const auto sw = face::rest_model_space(scope);
        const auto mount = face::compose(hw[bone_index(hands, "tag_weapon")],
            gw[bone_index(gun, scope.at(0).name)]);
        const auto eye = face::compose(mount, sw[bone_index(scope, "tag_reticle_attach")]);
        // Keep COD4's fore/aft motion and the remastered model's scale. Only
        // remove transverse error, expressed in tag_ads's parent frame.
        const face::vec3 transverse{0, -eye.translation[1], -eye.translation[2]};
        return hands[ads].parent < 0 ? transverse
            : face::rotate(face::conjugate(hw[hands[ads].parent].rotation), transverse);
    }
    struct action_clock
    {
        int token{-1}, begin{}, last{-1};
        unsigned session{}, weapon{};
        double sample(unsigned new_session, unsigned new_weapon, int new_token, int time,
            int remaining, int timer_ms, bool loop, int sprint_begin, int sprint_in_ms = 0)
        {
            const bool reset = new_session != session || new_weapon != weapon || last < 0
                || time < last || time - last > 250;
            if (reset || new_token != token)
            {
                begin = time;
                if (!loop && timer_ms > 0 && remaining >= 0 && remaining <= timer_ms)
                    begin -= timer_ms - remaining;
            }
            session = new_session; weapon = new_weapon; token = new_token; last = time;
            // StartWeaponAnim restarts the loop after sprint-in. The recorded
            // lastSprintStart anchors entry, not the looping animation itself.
            if (loop && sprint_begin > 0 && sprint_begin <= time)
                return std::max(0, time - sprint_begin - sprint_in_ms) / 1000.0;
            return std::max(0, time - begin) / 1000.0;
        }
    };
}
