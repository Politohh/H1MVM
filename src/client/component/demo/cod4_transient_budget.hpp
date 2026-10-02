#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace cod4
{
    // H1's shared transient slot table is 0x1B90 bytes, with 24 bytes per slot.
    // View weapons are the last category reserved by the native initializer.
    inline constexpr unsigned native_transient_slots = 294;
    inline constexpr unsigned replay_view_weapon_slots = 64;

    inline unsigned view_weapon_slot_budget(bool replay, unsigned native_count, unsigned used_slots)
    {
        if (!replay) return native_count;
        if (used_slots > native_transient_slots || native_count > native_transient_slots - used_slots)
            throw std::runtime_error("Invalid native transient slot reservation");
        return std::max(native_count, std::min(replay_view_weapon_slots, native_transient_slots - used_slots));
    }
}
