#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace spai_probe
{
    // Records belong to authenticated connections, not to a client allocation
    // (the native server may replace that allocation between maps).
    class peer_capabilities
    {
        struct record
        {
            std::string endpoint;
            uint64_t guid{};
            std::string capability;
        };
        std::array<record, 64> peers_{};
    public:
        void remember(size_t slot, const std::string& endpoint, uint64_t guid,
            const std::string& capability)
        {
            if (slot < peers_.size()) peers_[slot] = {endpoint, guid, capability};
        }
        void forget(size_t slot)
        {
            if (slot < peers_.size()) peers_[slot] = {};
        }
        bool supports(size_t slot, const std::string& endpoint, uint64_t guid,
            const std::string& required) const
        {
            if (slot >= peers_.size() || !guid || endpoint.empty() || required.empty()) return false;
            const auto& peer = peers_[slot];
            return peer.guid == guid && peer.endpoint == endpoint && peer.capability == required;
        }
    };
}
