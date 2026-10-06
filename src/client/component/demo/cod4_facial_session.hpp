#pragma once
#include "cod4_facial_native.hpp"
#include "cod4_facial_catalog.hpp"
#include "cod4_actor_translation.hpp"
#include "cod4_client_slots.hpp"
#include <functional>
#include <map>

namespace cod4::face
{
    struct source_rig { std::string name; skeleton bones; };
    struct actor_interval
    {
        int begin{}, end{}, started{};
        unsigned rig{}, animation{}, token{}, client{};
    };
    struct session
    {
        std::vector<source_rig> rigs;
        std::vector<std::pair<std::string, clip>> clips;
        std::map<unsigned, std::vector<actor_interval>> actors;
        const actor_interval* at(unsigned entity, int time) const
        {
            const auto found = actors.find(entity);
            if (found == actors.end()) return nullptr;
            const auto& entries = found->second;
            const auto next = std::upper_bound(entries.begin(), entries.end(), time,
                [](int t, const actor_interval& value) { return t < value.begin; });
            if (next == entries.begin()) return nullptr;
            const auto& value = *(next - 1);
            return time < value.end ? &value : nullptr;
        }
    };
    inline session build_session(const world_track& world, const std::vector<player_frame>& players,
        const std::map<int, std::string>& configstrings, const client_slot_map& slots,
        const std::function<std::vector<std::uint8_t>(std::string_view, std::string_view)>& asset)
    {
        session result;
        std::map<std::string, unsigned> rigs, clips;
        const auto load_clip = [&](std::string_view name) -> unsigned
        {
            const std::string key(name.empty() ? "__neutral__" : name);
            if (const auto found = clips.find(key); found != clips.end()) return found->second;
            clip decoded{1, 30, false, {}};
            if (std::find(std::begin(source_face_clips), std::end(source_face_clips), name) != std::end(source_face_clips))
            {
                const auto bytes = asset("xanim", name);
                if (bytes.empty()) throw std::runtime_error("Missing original facial animation input: " + key);
                decoded = decode(bytes);
            }
            const auto index = static_cast<unsigned>(result.clips.size());
            const bool neutral = decoded.tracks.empty();
            if (neutral && clips.contains("__neutral__"))
            {
                clips.emplace(key, clips.at("__neutral__"));
                return clips.at(key);
            }
            clips.emplace(key, index);
            result.clips.emplace_back(neutral ? "__neutral__" : key, decoded);
            if (neutral) clips.emplace("__neutral__", index);
            return index;
        };
        std::map<unsigned, actor_interval> prior;
        for (std::size_t f = 0; f < world.frames.size(); ++f)
        {
            const auto& frame = world.frames[f];
            const auto current_slots = slots.at(frame.time);
            const auto identities = clients_with_corpse_identities(world, frame.time);
            const int end = f + 1 < world.frames.size() ? world.frames[f + 1].time : frame.time + 1;
            std::map<unsigned, actor_interval> current;
            auto actors = frame.entities;
            // CoD4 omits the recorder from packet entities; its body channels
            // live in playerState. Include that native character slot too.
            if (!players.empty() && !actors.contains(static_cast<unsigned>(world.client_num)))
            {
                entity_state local;
                local.set(4, 1); local.set(140, static_cast<unsigned>(world.client_num));
                actors.emplace(static_cast<unsigned>(world.client_num), local);
            }
            for (const auto& [entity, actor] : actors)
            {
                if (actor.get(4) != 1 && actor.get(4) != 2) continue;
                const auto client = actor.get(140);
                const auto identity = identities.find(client);
                if (identity == identities.end()) continue;
                const auto head = configstrings.find(830 + static_cast<int>(identity->second.get(12)));
                const auto body = configstrings.find(830 + static_cast<int>(identity->second.get(8)));
                // Some original characters embed their head in the body XModel.
                // Prefer the recorded separate head, otherwise use that exact body.
                const std::string rig_name = head != configstrings.end() && !head->second.empty() ? head->second
                    : body != configstrings.end() ? body->second : std::string{};
                const auto parts = parts_for(rig_name);
                if (parts.empty()) continue;
                if (!rigs.contains(rig_name))
                {
                    const auto bytes = asset("xmodelparts", parts);
                    if (bytes.empty()) throw std::runtime_error("Missing source facial rest asset: " + rig_name);
                    auto bones = decode_rest(bytes);
                    if (std::none_of(bones.begin(), bones.end(), [](const bone& b) { return facial_bone(b.name); })) continue;
                    rigs.emplace(rig_name, static_cast<unsigned>(result.rigs.size()));
                    result.rigs.push_back({rig_name, std::move(bones)});
                }
                unsigned legs = actor.get(204), torso = actor.get(208);
                if (actor.get(4) == 1 && static_cast<int>(client) == world.client_num && !players.empty())
                {
                    const auto next = std::upper_bound(players.begin(), players.end(), frame.time,
                        [](int time, const player_frame& value) { return time < value.time; });
                    const auto& player = next == players.begin() ? players.front() : *(next - 1);
                    legs = player.integer("legsAnim"); torso = player.integer("torsoAnim");
                }
                const auto name = [](unsigned value) -> std::string_view
                { return (value & 511u) < source_body_animations.size() ? source_body_animations[value & 511u] : std::string_view{}; };
                const bool death = name(legs).find("death") != std::string_view::npos;
                auto token = death ? legs : torso;
                auto animation = load_clip(name(token));
                // pt_ clips supply the upper-body expression. Without facial
                // curves there, retain the recorded pb_ full-body/idle face.
                if (!death && result.clips[animation].second.tracks.empty())
                {
                    const auto full_body = load_clip(name(legs));
                    if (!result.clips[full_body].second.tracks.empty()) { token = legs; animation = full_body; }
                }
                const unsigned native = actor.get(4) == 2 ? actor_entity_number(entity) : current_slots.client(client);
                actor_interval value{frame.time, end, frame.time, rigs.at(rig_name), animation, token, client};
                const auto old = prior.find(native);
                if (old != prior.end() && old->second.end == frame.time && old->second.rig == value.rig
                    && old->second.animation == value.animation && old->second.token == token && old->second.client == client)
                    value.started = old->second.started;
                else if (death && actor.get(4) == 2)
                {
                    const auto player = std::find_if(prior.begin(), prior.end(), [&](const auto& entry)
                    { return entry.first < 42 && entry.second.client == client; });
                    if (player != prior.end() && player->second.end == frame.time && player->second.rig == value.rig
                        && player->second.animation == value.animation && player->second.token == token)
                        value.started = player->second.started;
                }
                current.emplace(native, value);
                auto& entries = result.actors[native];
                if (!entries.empty() && entries.back().end == value.begin && entries.back().started == value.started
                    && entries.back().rig == value.rig && entries.back().animation == value.animation
                    && entries.back().token == value.token && entries.back().client == value.client)
                    entries.back().end = end;
                else entries.push_back(value);
            }
            prior = std::move(current);
        }
        return result;
    }
    constexpr unsigned max_session_bytes = 16 * 1024 * 1024;
    inline std::vector<std::uint8_t> encode_session(const session& input)
    {
        std::vector<std::uint8_t> output;
        const auto put = [&]<typename T>(const T& value)
        { const auto* p = reinterpret_cast<const std::uint8_t*>(&value); output.insert(output.end(), p, p + sizeof(T)); };
        const auto text = [&](const std::string& value)
        { output.insert(output.end(), value.begin(), value.end()); output.push_back(0); };
        const auto size = [&](std::size_t value) { put(static_cast<unsigned>(value)); };
        put(std::uint32_t{0x31434648}); // HFC1: facial curves only, no body or weapon tracks.
        size(input.rigs.size());
        for (const auto& rig : input.rigs)
        {
            text(rig.name); size(rig.bones.size());
            for (const auto& b : rig.bones) { text(b.name); put(b.parent); put(b.rest.rotation); put(b.rest.translation); }
        }
        size(input.clips.size());
        for (const auto& [name, animation] : input.clips)
        {
            text(name); put(animation.frames); put(animation.fps); put(std::uint8_t(animation.loop)); size(animation.tracks.size());
            for (const auto& t : animation.tracks)
            {
                text(t.bone); size(t.rotations.size());
                for (const auto& key : t.rotations) { put(key.frame); put(key.value); }
                size(t.translations.size());
                for (const auto& key : t.translations) { put(key.frame); put(key.value); }
            }
        }
        size(input.actors.size());
        for (const auto& [entity, intervals] : input.actors)
        {
            put(entity); size(intervals.size());
            for (const auto& value : intervals)
            { put(value.begin); put(value.end); put(value.started); put(value.rig); put(value.animation); put(value.token); put(value.client); }
        }
        if (output.size() > max_session_bytes) throw std::runtime_error("Facial demo data exceeds bounded session size");
        return output;
    }
    inline session decode_session(std::span<const std::uint8_t> bytes)
    {
        if (bytes.size() > max_session_bytes) throw std::runtime_error("Oversized facial demo data");
        reader r(bytes); session result;
        if (r.read<unsigned>() != 0x31434648) throw std::runtime_error("Unknown facial demo format");
        const auto count = [&](unsigned limit)
        { const auto n = r.read<unsigned>(); if (n > limit) throw std::runtime_error("Invalid facial demo count"); return n; };
        const auto name = [&]() { auto n = r.text(); if (n.empty()) throw std::runtime_error("Empty facial demo name"); return n; };
        for (unsigned n = count(64); n; --n)
        {
            source_rig rig; rig.name = name();
            for (unsigned b = count(254); b; --b)
            {
                bone value; value.name = name(); value.parent = r.read<int>();
                value.rest.rotation = normalized(r.read<quat>()); value.rest.translation = {r.real(), r.real(), r.real()};
                if (std::any_of(rig.bones.begin(), rig.bones.end(), [&](const bone& old) { return old.name == value.name; }))
                    throw std::runtime_error("Duplicate facial rest bone");
                rig.bones.push_back(value);
            }
            (void)rest_model_space(rig.bones); result.rigs.push_back(std::move(rig));
        }
        for (unsigned n = count(512); n; --n)
        {
            auto clip_name = name(); clip value;
            value.frames = count(4096); value.fps = count(1000); const auto loop = r.read<std::uint8_t>();
            if (!value.fps || loop > 1) throw std::runtime_error("Invalid facial clip bounds");
            value.loop = loop != 0;
            for (unsigned t = count(20); t; --t)
            {
                track channel; channel.bone = name();
                if (!facial_bone(channel.bone) || std::any_of(value.tracks.begin(), value.tracks.end(),
                    [&](const track& old) { return old.bone == channel.bone; })) throw std::runtime_error("Invalid facial channel");
                for (unsigned k = count(4097); k; --k)
                {
                    const auto frame = count(value.frames);
                    if (!channel.rotations.empty() && frame <= channel.rotations.back().frame) throw std::runtime_error("Unordered facial keys");
                    channel.rotations.push_back({frame, normalized(r.read<quat>())});
                }
                for (unsigned k = count(4097); k; --k)
                {
                    const auto frame = count(value.frames);
                    if (!channel.translations.empty() && frame <= channel.translations.back().frame) throw std::runtime_error("Unordered facial keys");
                    channel.translations.push_back({frame, {r.real(), r.real(), r.real()}});
                }
                value.tracks.push_back(std::move(channel));
            }
            result.clips.emplace_back(std::move(clip_name), std::move(value));
        }
        for (unsigned n = count(50); n; --n)
        {
            const auto entity = count(49);
            if (result.actors.contains(entity)) throw std::runtime_error("Duplicate facial actor");
            auto& entries = result.actors[entity];
            for (unsigned k = count(200000); k; --k)
            {
                actor_interval value; value.begin = r.read<int>(); value.end = r.read<int>(); value.started = r.read<int>();
                value.rig = r.read<unsigned>(); value.animation = r.read<unsigned>(); value.token = r.read<unsigned>(); value.client = count(63);
                if (value.begin < 0 || value.end <= value.begin || value.started < 0 || value.started > value.begin
                    || value.rig >= result.rigs.size() || value.animation >= result.clips.size() || value.token >= 1024
                    || (!entries.empty() && entries.back().end > value.begin)) throw std::runtime_error("Invalid facial actor timing");
                entries.push_back(value);
            }
        }
        if (!r.finished()) throw std::runtime_error("Trailing facial demo data");
        return result;
    }
}
