#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace demo_seek
{
    struct anchor
    {
        std::int64_t offset{};
        std::int32_t time{};
        std::int32_t sequence{};
    };

    // Only successfully decoded, independent converted snapshots enter this
    // index. Build it during playback, without a startup scan or a sidecar file.
    class index
    {
    public:
        void clear() { anchors_.clear(); }
        std::size_t size() const { return anchors_.size(); }

        void observe(anchor value)
        {
            if (value.offset < 0 || value.time < 0 || value.sequence <= 0) return;
            if (!anchors_.empty())
            {
                const auto& last = anchors_.back();
                // Replaying previously visited frames must not duplicate anchors.
                if (value.offset <= last.offset || value.sequence <= last.sequence ||
                    static_cast<std::int64_t>(value.time) - last.time < interval) return;
            }
            anchors_.push_back(value);
        }

        std::optional<anchor> before(std::int32_t target) const
        {
            if (anchors_.empty() || target < anchors_.front().time) return std::nullopt;
            // Warm up prediction/interpolation and animation from a frame before
            // the destination, instead of exposing a half-initialized first frame.
            const auto warmup = static_cast<std::int64_t>(target) - interval;
            auto it = std::upper_bound(anchors_.begin(), anchors_.end(), warmup,
                [](std::int64_t time, const anchor& value) { return time < value.time; });
            if (it != anchors_.begin()) --it;
            // Snapshot spacing can straddle the one-second sampling boundary.
            if (it->time > target || static_cast<std::int64_t>(target) - it->time > 2500)
                return std::nullopt; // discontinuity or an unvisited destination
            return *it;
        }

    private:
        static constexpr std::int32_t interval = 1000;
        std::vector<anchor> anchors_;
    };
}
