#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "utils/near_memory.hpp"
#include "component/arxan/breakpoints.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main()
{
    try
    {
        SYSTEM_INFO info{}; GetSystemInfo(&info);
        std::vector<void*> allocations;
        for (const auto address : mp::int2d_breakpoint_addresses)
        {
            auto* block = utils::hook::detail::allocate_near(reinterpret_cast<void*>(address),
                info.dwAllocationGranularity, info.dwAllocationGranularity);
            check(block != nullptr, "Cannot allocate a startup trampoline");
            const auto displacement = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(block))
                - static_cast<std::int64_t>(address + 5);
            check(displacement >= INT32_MIN && displacement <= INT32_MAX, "Trampoline outside rel32 reach");
            check(reinterpret_cast<std::uintptr_t>(block) % info.dwAllocationGranularity == 0,
                "Trampoline is not allocation-aligned");
            block[0] = 0xC3; // Confirm the allocated memory is committed and writable.
            allocations.push_back(block);
        }
        for (auto* block : allocations) VirtualFree(block, 0, MEM_RELEASE);
        auto* low = utils::hook::detail::allocate_near(reinterpret_cast<void*>(1), 4096,
            info.dwAllocationGranularity);
        check(low != nullptr, "Low patch address underflowed its search bounds");
        VirtualFree(low, 0, MEM_RELEASE);
        // Occupy the entire upper rel32 window; allocation must search below
        // the patch instead of returning null after seeing reservations.
        auto* reservation = VirtualAlloc(reinterpret_cast<void*>(0x200000000ull), 0x80020000ull,
            MEM_RESERVE, PAGE_NOACCESS);
        check(reservation != nullptr, "Cannot reserve the fragmented-address-space fixture");
        auto* below = utils::hook::detail::allocate_near(reinterpret_cast<void*>(0x200010000ull),
            info.dwAllocationGranularity, info.dwAllocationGranularity);
        VirtualFree(reservation, 0, MEM_RELEASE);
        check(below != nullptr && reinterpret_cast<std::uintptr_t>(below) < 0x200000000ull,
            "Allocator failed to use the lower free region when the upper range was occupied");
        VirtualFree(below, 0, MEM_RELEASE);
        check(!utils::hook::detail::allocate_near(nullptr, 0, info.dwAllocationGranularity),
            "Zero allocation size accepted");
        check(!utils::hook::detail::allocate_near(nullptr, 4096, 3), "Invalid granularity accepted");
        std::cout << "PASS " << allocations.size()
                  << " startup trampoline addresses, alignment, reach, low bounds and occupied-range fallback\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
