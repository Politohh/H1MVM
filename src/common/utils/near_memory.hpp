#pragma once
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <limits>

namespace utils::hook::detail
{
    inline std::uint8_t* allocate_near(const void* address, std::size_t size, std::size_t granularity)
    {
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        const auto source = reinterpret_cast<std::uintptr_t>(address);
        const auto minimum = reinterpret_cast<std::uintptr_t>(system.lpMinimumApplicationAddress);
        const auto maximum = reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress);
        if (!size || !granularity || (granularity & (granularity - 1)) || size > maximum
            || source > maximum - 5) return nullptr;
        const auto origin = source + 5; // Address following a rel32 call/jump.
        constexpr std::uintptr_t reach = std::uintptr_t{1} << 31;
        const auto lower = (std::max)(minimum, origin >= reach ? origin - reach : 0);
        const auto upper = (std::min)(maximum - size + 1,
            origin + static_cast<std::uintptr_t>((std::numeric_limits<std::int32_t>::max)()));
        if (lower > upper) return nullptr;
        const auto mask = static_cast<std::uintptr_t>(granularity - 1);
        const auto aligned = source & ~mask;
        const auto try_region = [&](std::uintptr_t candidate, const MEMORY_BASIC_INFORMATION& region)
            -> std::uint8_t*
        {
            const auto begin = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
            const auto end = begin + region.RegionSize;
            if (region.State != MEM_FREE || candidate < lower || candidate > upper
                || candidate < begin || size > end - candidate) return nullptr;
            return static_cast<std::uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
        };
        // Search free regions above the patch first, rather than allocating
        // at the distant -2 GiB edge. Skip occupied reservations in one query.
        auto cursor = (std::max)(aligned, (lower + mask) & ~mask);
        while (cursor <= upper)
        {
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region))) break;
            if (auto* result = try_region(cursor, region)) return result;
            const auto end = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
            const auto next = region.State == MEM_FREE ? cursor + granularity : (end + mask) & ~mask;
            if (next <= cursor) break;
            cursor = next;
        }
        cursor = aligned;
        while (cursor >= lower && cursor >= granularity)
        {
            cursor -= granularity;
            if (cursor < lower) break;
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region))) break;
            if (auto* result = try_region(cursor, region)) return result;
            if (region.State != MEM_FREE)
                cursor = reinterpret_cast<std::uintptr_t>(region.BaseAddress) & ~mask;
        }
        return nullptr;
    }
}
