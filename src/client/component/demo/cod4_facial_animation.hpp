#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cod4::face
{
    using vec3 = std::array<float, 3>;
    using quat = std::array<float, 4>;
    struct pose { quat rotation{0, 0, 0, 1}; vec3 translation{}; };
    template<typename T> struct key { unsigned frame{}; T value{}; };
    struct track
    {
        std::string bone;
        std::vector<key<quat>> rotations;
        std::vector<key<vec3>> translations;
    };
    struct clip { unsigned frames{}, fps{}; bool loop{}; std::vector<track> tracks; };

    inline bool facial_bone(std::string_view name)
    {
        constexpr std::array<std::string_view, 20> bones{
            "j_jaw", "j_brow_le", "j_brow_ri", "j_cheek_le", "j_cheek_ri",
            "j_eye_lid_bot_le", "j_eye_lid_bot_ri", "j_eye_lid_top_le", "j_eye_lid_top_ri",
            "j_eyeball_le", "j_eyeball_ri", "j_levator_le", "j_levator_ri",
            "j_lip_top_le", "j_lip_top_ri", "j_lip_bot_le", "j_lip_bot_ri",
            "j_mouth_le", "j_mouth_ri", "j_chin_skinroll"};
        return std::find(bones.begin(), bones.end(), name) != bones.end();
    }

    inline quat normalized(quat q)
    {
        float length{};
        for (float v : q) length += v * v;
        if (!std::isfinite(length) || length < 1e-12f)
            throw std::runtime_error("Invalid facial quaternion");
        const float scale = 1.0f / std::sqrt(length);
        for (auto& v : q) v *= scale;
        return q;
    }
    inline quat conjugate(quat q) { return {-q[0], -q[1], -q[2], q[3]}; }
    inline quat multiply(const quat& a, const quat& b)
    {
        return {a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],
            a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
            a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],
            a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};
    }
    inline quat interpolate(const quat& a, const quat& b, float fraction)
    {
        float dot{};
        for (unsigned i = 0; i < 4; ++i) dot += a[i] * b[i];
        const float sign = dot < 0 ? -1.0f : 1.0f;
        quat q;
        for (unsigned i = 0; i < 4; ++i) q[i] = a[i] + fraction * (sign*b[i] - a[i]);
        return normalized(q);
    }

    class reader
    {
        std::span<const std::uint8_t> bytes;
        std::size_t cursor{};
    public:
        explicit reader(std::span<const std::uint8_t> input) : bytes(input) {}
        std::span<const std::uint8_t> take(std::size_t count)
        {
            if (count > bytes.size() - cursor) throw std::runtime_error("Truncated CoD4 facial animation");
            const auto result = bytes.subspan(cursor, count); cursor += count; return result;
        }
        template<typename T> T read()
        {
            T result{};
            const auto data = take(sizeof(T)); std::memcpy(&result, data.data(), sizeof(T));
            return result;
        }
        float real()
        {
            const auto value = read<float>();
            if (!std::isfinite(value)) throw std::runtime_error("Non-finite CoD4 facial translation");
            return value;
        }
        std::string text()
        {
            std::string result;
            while (const auto c = read<std::uint8_t>())
            {
                if (c < 32 || c > 126 || result.size() >= 128)
                    throw std::runtime_error("Invalid CoD4 facial bone/notify name");
                result.push_back(static_cast<char>(c));
            }
            return result;
        }
        bool finished() const { return cursor == bytes.size(); }
    };

    // CoD4 Mod Tools compiled XAnim v17. Consume the complete clip, retain
    // only authored face controls; body curves and animation tags never enter
    // the layer. The optional v17 two-component delta root is consumed before
    // the bone tracks and is never applied to a facial control.
    inline clip decode(std::span<const std::uint8_t> bytes, bool retain_all_bones = false)
    {
        if (bytes.size() > 16*1024*1024) throw std::runtime_error("Oversized CoD4 facial animation");
        reader r(bytes);
        if (r.read<std::uint16_t>() != 17) throw std::runtime_error("Unsupported CoD4 facial XAnim version");
        const unsigned frames = r.read<std::uint16_t>();
        const unsigned bones = r.read<std::uint16_t>();
        const auto flags = r.read<std::uint8_t>();
        r.read<std::uint8_t>(); // Authored asset type.
        const unsigned fps = r.read<std::uint16_t>();
        if (!frames || frames > 4096 || !bones || bones > 256 || !fps || fps > 1000 || (flags & ~3u))
            throw std::runtime_error("Unsupported CoD4 facial XAnim bounds/root flags");
        const unsigned loop_frames = frames + (flags & 1u);
        clip result{loop_frames - 1, fps, (flags & 1u) != 0, {}};
        const auto indices = [&](unsigned count)
        {
            if (count > loop_frames) throw std::runtime_error("Too many CoD4 facial keys");
            std::vector<unsigned> values(count);
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned frame = count < 2 ? 0 : count == loop_frames ? i
                    : result.frames < 256 ? r.read<std::uint8_t>() : r.read<std::uint16_t>();
                if (frame > result.frames || (i && frame <= values[i-1]))
                    throw std::runtime_error("Invalid CoD4 facial key frame sequence");
                values[i] = frame;
            }
            return values;
        };
        const auto read_track = [&](bool half_quat, bool flip_quat)
        {
            track t;
            const auto rotation_count = r.read<std::uint16_t>();
            const auto rotation_indices = indices(rotation_count);
            for (unsigned k = 0; k < rotation_count; ++k)
            {
                quat q{};
                if (half_quat) q[2] = static_cast<float>(r.read<std::int16_t>());
                else for (unsigned c = 0; c < 3; ++c) q[c] = static_cast<float>(r.read<std::int16_t>());
                const float sum = q[0]*q[0] + q[1]*q[1] + q[2]*q[2];
                q[3] = std::floor(std::sqrt(std::max(0.0f, 32767.0f*32767.0f - sum)) + 0.5f);
                if (flip_quat) for (auto& v : q) v = -v;
                t.rotations.push_back({rotation_indices[k], normalized(q)});
            }
            const auto translation_count = r.read<std::uint16_t>();
            const auto translation_indices = indices(translation_count);
            if (translation_count == 1)
                t.translations.push_back({0, {r.real(), r.real(), r.real()}});
            else if (translation_count > 1)
            {
                const auto byte_packed = r.read<std::uint8_t>();
                if (byte_packed > 1) throw std::runtime_error("Invalid CoD4 facial translation packing");
                const vec3 minimum{r.real(),r.real(),r.real()}, range{r.real(),r.real(),r.real()};
                for (unsigned k = 0; k < translation_count; ++k)
                {
                    vec3 value{};
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        const unsigned packed = byte_packed ? r.read<std::uint8_t>() : r.read<std::uint16_t>();
                        value[c] = minimum[c] + range[c] * (static_cast<float>(packed) / (byte_packed ? 255.0f : 65535.0f));
                        if (!std::isfinite(value[c])) throw std::runtime_error("Overflowed CoD4 facial translation");
                    }
                    t.translations.push_back({translation_indices[k], value});
                }
            }
            return t;
        };
        if (flags & 2u) read_track(true, false);
        const auto flip = r.take((bones + 7) / 8), half = r.take((bones + 7) / 8);
        std::vector<std::string> names;
        for (unsigned i = 0; i < bones; ++i)
        {
            auto name = r.text();
            if (name.empty() || std::find(names.begin(), names.end(), name) != names.end())
                throw std::runtime_error("Duplicate/empty CoD4 facial bone name");
            names.push_back(std::move(name));
        }
        for (unsigned i = 0; i < bones; ++i)
        {
            auto t = read_track((half[i/8] & (1u << (i%8))) != 0, (flip[i/8] & (1u << (i%8))) != 0);
            t.bone = names[i];
            if (retain_all_bones || facial_bone(t.bone)) result.tracks.push_back(std::move(t));
        }
        const auto notifications = r.read<std::uint8_t>();
        for (unsigned i = 0; i < notifications; ++i)
        {
            r.text();
            if (r.read<std::uint16_t>() > result.frames) throw std::runtime_error("Invalid CoD4 facial notify frame");
        }
        if (!r.finished()) throw std::runtime_error("Trailing CoD4 facial XAnim data");
        return result;
    }

    template<typename T> std::pair<unsigned, float> interval(const std::vector<key<T>>& keys, float frame)
    {
        if (keys.size() < 2 || frame <= static_cast<float>(keys.front().frame)) return {0,0.0f};
        if (frame >= static_cast<float>(keys.back().frame)) return {static_cast<unsigned>(keys.size()-1),0.0f};
        const auto next = std::upper_bound(keys.begin(), keys.end(), frame,
            [](float f, const key<T>& k) { return f < static_cast<float>(k.frame); });
        const unsigned i = static_cast<unsigned>(next - keys.begin() - 1);
        return {i, (frame-static_cast<float>(keys[i].frame)) / static_cast<float>(keys[i+1].frame-keys[i].frame)};
    }
    inline pose sample(const clip& c, const track& t, double seconds, const pose& rest)
    {
        if (!std::isfinite(seconds) || !c.fps) throw std::runtime_error("Invalid facial sample time");
        double frame = std::max(0.0, seconds * c.fps);
        if (c.loop && c.frames) frame = std::fmod(frame, static_cast<double>(c.frames));
        else frame = std::min(frame, static_cast<double>(c.frames));
        pose result = rest;
        if (!t.rotations.empty())
        {
            const auto [i, f] = interval(t.rotations, static_cast<float>(frame));
            result.rotation = f ? interpolate(t.rotations[i].value, t.rotations[i+1].value, f) : t.rotations[i].value;
        }
        if (!t.translations.empty())
        {
            const auto [i, f] = interval(t.translations, static_cast<float>(frame));
            result.translation = t.translations[i].value;
            if (f) for (unsigned k = 0; k < 3; ++k)
                result.translation[k] += f * (t.translations[i+1].value[k] - result.translation[k]);
        }
        return result;
    }
}
