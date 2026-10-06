#pragma once
#include "cod4_facial_retarget.hpp"

namespace cod4::face
{
    // H1 XModel local rest data: roots are implicit, non-root parents are
    // backward distances and rotations contain all four signed components.
    // This owns no game pointers and is also usable on read-only captures.
    inline skeleton native_rest(std::span<const std::string> names, unsigned roots,
        std::span<const std::uint8_t> parents,
        std::span<const std::array<std::int16_t, 4>> rotations,
        std::span<const vec3> translations)
    {
        if (names.empty() || names.size() > 254 || !roots || roots > names.size()
            || parents.size() != names.size() - roots || rotations.size() != parents.size()
            || translations.size() != parents.size())
            throw std::runtime_error("Invalid native facial rest bounds");
        skeleton result;
        for (unsigned i = 0; i < names.size(); ++i)
        {
            if (names[i].empty() || names[i].size() > 128
                || std::find(names.begin(), names.begin() + i, names[i]) != names.begin() + i)
                throw std::runtime_error("Invalid native facial bone name");
            bone value{names[i], -1, {}};
            if (i >= roots)
            {
                const auto child = i - roots;
                if (!parents[child] || parents[child] > i)
                    throw std::runtime_error("Invalid native facial parent distance");
                value.parent = static_cast<int>(i - parents[child]);
                const auto& q = rotations[child];
                value.rest.rotation = normalized({static_cast<float>(q[0]), static_cast<float>(q[1]),
                    static_cast<float>(q[2]), static_cast<float>(q[3])});
                value.rest.translation = translations[child];
            }
            result.push_back(value);
        }
        (void)rest_model_space(result);
        return result;
    }

    // Local animation translations are offsets: H1's hierarchy stage adds the
    // model tag position. Writing an absolute rest translation here doubles it.
    // The caller must provide the native translation scale for the evaluated
    // model; no guessed scale, live bone writes or hooks belong in this helper.
    inline pose native_animation_pose(const pose& rest, const pose& local, float scale)
    {
        if (!std::isfinite(scale) || scale <= 0)
            throw std::runtime_error("Invalid native facial translation scale");
        pose result{normalized(local.rotation), {}};
        for (unsigned i = 0; i < 3; ++i)
        {
            result.translation[i] = (local.translation[i] - rest.translation[i]) / scale;
            if (!std::isfinite(result.translation[i]))
                throw std::runtime_error("Invalid native facial translation offset");
        }
        return result;
    }
}
