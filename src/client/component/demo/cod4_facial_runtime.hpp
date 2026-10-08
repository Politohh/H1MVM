#pragma once
#include "runtime_assets.hpp"
#include "cod4_facial_playback.hpp"
#include "cod4_snipers.hpp"
#include <memory>

namespace cod4_camera::facial
{
    inline std::atomic<std::shared_ptr<const cod4::face::session>> current;
    inline std::atomic<std::shared_ptr<const cod4::face::expression_strength>> strengths{
        std::make_shared<const cod4::face::expression_strength>()};
    inline utils::hook::detour evaluate_hook;
    inline bool supported{};
    inline std::atomic_uint64_t applied{}, rejected{};
    template<typename T> T field(const void* data, std::size_t offset)
    { T result; std::memcpy(&result, static_cast<const std::uint8_t*>(data) + offset, sizeof(T)); return result; }
    inline unsigned index(const cod4::face::skeleton& rig, std::string_view name)
    {
        const auto found = std::find_if(rig.begin(), rig.end(), [&](const auto& b) { return b.name == name; });
        if (found == rig.end()) throw std::runtime_error("Native facial control missing: " + std::string(name));
        return static_cast<unsigned>(found - rig.begin());
    }
    struct calibration
    {
        cod4::face::skeleton target;
        std::vector<cod4::face::correspondence> mapping;
        unsigned source_head{}, target_head{};
        float expression_scale{}, native_scale{};
    };
    inline calibration calibrate(const void* model, const cod4::face::skeleton& source)
    {
        using namespace cod4::face;
        const auto bones = field<std::uint8_t>(model, 8), roots = field<std::uint8_t>(model, 9);
        if (!bones || roots > bones) throw std::runtime_error("Invalid native facial model bounds");
        const auto* ids = field<const std::uint32_t*>(model, 56);
        const auto* parents = field<const std::uint8_t*>(model, 64);
        const auto* rotations = field<const std::array<std::int16_t, 4>*>(model, 72);
        const auto* translations = field<const vec3*>(model, 80);
        if (!ids || !parents || !rotations || !translations) throw std::runtime_error("Missing native facial rest data");
        std::vector<std::string> names;
        for (unsigned b = 0; b < bones; ++b) names.emplace_back(game::SL_ConvertToString(ids[b]));
        calibration result;
        result.target = native_rest(names, roots, {parents, static_cast<std::size_t>(bones - roots)},
            {rotations, static_cast<std::size_t>(bones - roots)}, {translations, static_cast<std::size_t>(bones - roots)});
        result.source_head = index(source, "j_head"); result.target_head = index(result.target, "j_head");
        auto controls = calibrate_controls(source, result.target);
        result.mapping = std::move(controls.mapping);
        result.expression_scale = controls.translation_scale;
        result.native_scale = field<float>(model, 16);
        if (!std::isfinite(result.expression_scale) || result.expression_scale <= 0 || result.expression_scale > 4)
            throw std::runtime_error("Invalid native facial calibration");
        (void)native_animation_pose({}, {}, result.native_scale);
        return result;
    }
    inline void evaluate(void* object, const std::uint32_t* requested, void* context)
    {
        // Authenticated MP DObj local-pose boundary. H1 composes hierarchy and
        // duplicate/control bones AFTER this function. Never write world poses.
        const auto data = current.load();
        const auto expression = strengths.load();
        std::array<std::uint32_t, 8> fresh{};
        for (unsigned i = 0; i < 8; ++i)
            fresh[i] = requested[i] & ~field<std::uint32_t>(object, 32 + i * 4);
        evaluate_hook.invoke<void>(object, requested, context);
        cod4_snipers::apply_pose(object, fresh.data());
        if (!data || !game::CL_IsCgameInitialized() || game::VirtualLobby_Loaded() || !game::mp::cl.get()) return;
        try
        {
            const auto pool = reinterpret_cast<std::uintptr_t>(object);
            if (pool < 0xAAA3FC0_b || (pool - 0xAAA3FC0_b) % 0x110) return;
            const auto object_index = (pool - 0xAAA3FC0_b) / 0x110;
            if (!object_index || object_index > 32767) return;
            const auto* handles = reinterpret_cast<const std::int16_t*>(0xAC55FC0_b);
            unsigned entity{};
            for (; entity < 50; ++entity) if (handles[entity] == object_index) break;
            if (entity == 50) return;
            const int time = game::mp::cl->serverTime;
            const auto sample = cod4::face::sample_expression(*data, entity, time, expression->blend_seconds);
            const auto* value = sample.rig_state();
            if (!value) return;
            if (!sample.outgoing && data->clips.at(value->animation).second.tracks.empty()) return;
            const auto* models = field<const void* const*>(object, 216);
            auto* matrices = field<std::uint8_t*>(object, 168);
            const auto count = field<std::uint8_t>(object, 15), total = field<std::uint8_t>(object, 16);
            if (!models || !matrices || !count || count > 32 || total > 254) return;
            // Worker-local cache owns rest transforms, not live pose pointers.
            // Holding the session prevents address reuse across conversions.
            thread_local std::shared_ptr<const cod4::face::session> owner;
            thread_local std::map<std::pair<const void*, unsigned>, calibration> cache;
            if (owner != data) { cache.clear(); owner = data; }
            unsigned offset{};
            for (unsigned m = 0; m < count; ++m)
            {
                const auto* model = models[m];
                const auto nb = field<std::uint8_t>(model, 8);
                if (offset + nb > total) return;
                const char* name = field<const char*>(model, 0);
                if (name && cod4::is_lobby_facial_head(name))
                {
                    const auto key = std::pair{model, value->rig};
                    if (!cache.contains(key)) cache.emplace(key, calibrate(model, data->rigs.at(value->rig).bones));
                    const auto& c = cache.at(key);
                    const auto local = cod4::face::retarget_expression(*data, sample, time,
                        c.source_head, c.target, c.target_head, c.mapping, c.expression_scale, *expression);
                    for (const auto& pair : c.mapping)
                    {
                        const unsigned bone = offset + pair.destination;
                        const auto bit = 0x80000000u >> (bone & 31u), word = bone >> 5;
                        if (!(fresh[word] & bit) || ((field<std::uint32_t>(object, 64 + word * 4)
                            | field<std::uint32_t>(object, 96 + word * 4)) & bit)) continue;
                        const auto pose = cod4::face::native_animation_pose(c.target[pair.destination].rest, local[pair.destination], c.native_scale);
                        auto* matrix = matrices + bone * 32;
                        std::memcpy(matrix, pose.rotation.data(), 16);
                        std::memcpy(matrix + 16, pose.translation.data(), 12);
                        const float weight = 0; std::memcpy(matrix + 28, &weight, 4);
                        ++applied;
                    }
                }
                offset += nb;
            }
        }
        catch (const std::exception& error)
        {
            if (rejected.fetch_add(1) == 0) console::warn("COD4 facial layer skipped: %s\n", error.what());
        } // Facial rejection never aborts the engine worker.
    }
    inline void publish(std::shared_ptr<const cod4::face::session> data)
    { current.store(std::move(data)); applied = 0; rejected = 0; }
    inline std::vector<std::uint8_t> asset(std::string_view category, std::string_view name)
    {
        if (!cod4::valid_model_name(name)) throw std::runtime_error("Invalid facial source asset name");
        const auto path = runtime_assets::path("cod4_facial/" + std::string(category) + "/" + std::string(name));
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return {};
        const auto size = file.tellg();
        if (size <= 0 || size > 16 * 1024 * 1024) throw std::runtime_error("Invalid facial source asset size");
        file.seekg(0); std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        file.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!file) throw std::runtime_error("Truncated facial source asset");
        return bytes;
    }
}
