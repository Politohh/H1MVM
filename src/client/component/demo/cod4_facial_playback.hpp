#pragma once
#include "cod4_facial_session.hpp"
#include "cod4_facial_calibration.hpp"

namespace cod4::face
{
    inline bool firing_expression(std::string_view name)
    { return name.find("shoot") != name.npos || name.find("_fire") != name.npos; }
    struct expression_sample
    {
        const actor_interval* active{};
        const actor_interval* outgoing{};
        int outgoing_time{};
        float outgoing_weight{};
        const actor_interval* rig_state() const { return active ? active : outgoing; }
    };
    inline expression_sample sample_expression(const session& data, unsigned entity, int time, float blend_seconds)
    {
        if (!std::isfinite(blend_seconds) || blend_seconds < 0 || blend_seconds > 1)
            throw std::runtime_error("Invalid facial release duration");
        expression_sample result;
        const auto found = data.actors.find(entity);
        if (found == data.actors.end()) return result;
        const auto& entries = found->second;
        const auto next = std::upper_bound(entries.begin(), entries.end(), time,
            [](int t, const actor_interval& value) { return t < value.begin; });
        if (next == entries.begin()) return result;
        auto previous = next - 1;
        if (time < previous->end)
        {
            result.active = &*previous;
            const auto& clip = data.clips.at(previous->animation);
            const auto token = previous->token & 511u;
            const auto body = token < source_body_animations.size() ? source_body_animations[token] : std::string_view{};
            // New shooting and death expressions remain responsive. Only
            // release a verified shooting expression into idle/neutral.
            if (firing_expression(clip.first) || clip.first.find("death") != std::string::npos
                || firing_expression(body) || body.find("death") != std::string_view::npos
                || previous == entries.begin()) return result;
            --previous;
            if (previous->end != result.active->begin || previous->rig != result.active->rig
                || previous->client != result.active->client) return result;
        }
        else
        {
            // Older facial conversions omitted missing idle clips. A short
            // tail is possible only with the same recorded identity ahead.
            // Terminal removal and a different client's reuse never fade.
            if (next == entries.end() || previous->rig != next->rig || previous->client != next->client) return result;
        }
        const auto& clip = data.clips.at(previous->animation);
        if (blend_seconds == 0 || !firing_expression(clip.first) || clip.second.tracks.empty()) return result;
        const double elapsed = (static_cast<double>(time) - previous->end) / 1000.0;
        if (elapsed < 0 || elapsed >= blend_seconds) return result;
        const float fraction = static_cast<float>(elapsed / blend_seconds);
        result.outgoing = &*previous;
        result.outgoing_time = previous->end - 1; // Freeze the last recorded shooting pose.
        result.outgoing_weight = 1 - fraction * fraction * (3 - 2 * fraction);
        return result;
    }
    inline std::vector<pose> retarget_expression(const session& data, const expression_sample& sample, int time,
        unsigned source_head, const skeleton& target, unsigned target_head, std::span<const correspondence> mapping,
        float translation_scale, const expression_strength& strength)
    {
        std::vector<pose> result;
        if (sample.active)
        {
            const auto& value = *sample.active;
            result = retarget(data.rigs.at(value.rig).bones, data.clips.at(value.animation).second,
                (static_cast<double>(time) - value.started) / 1000.0, source_head, target, target_head, mapping, translation_scale, strength);
        }
        else for (const auto& bone : target) result.push_back(bone.rest);
        if (sample.outgoing)
        {
            const auto& value = *sample.outgoing;
            const auto outgoing = retarget(data.rigs.at(value.rig).bones, data.clips.at(value.animation).second,
                (static_cast<double>(sample.outgoing_time) - value.started) / 1000.0,
                source_head, target, target_head, mapping, translation_scale, strength);
            for (unsigned i = 0; i < result.size(); ++i)
            {
                result[i].rotation = interpolate(result[i].rotation, outgoing[i].rotation, sample.outgoing_weight);
                for (unsigned c = 0; c < 3; ++c)
                    result[i].translation[c] += sample.outgoing_weight * (outgoing[i].translation[c] - result[i].translation[c]);
            }
        }
        return result;
    }
}
