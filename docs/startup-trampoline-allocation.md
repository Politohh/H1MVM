# Startup trampoline allocation

Two Windows crash dumps from 2026-10-04 identify an unhandled
`std::runtime_error` while creating a rel32 trampoline for the startup patch at
`0x151AD7F8A`. The stack runs through `utils::hook::get_memory_near`, the
trampoline pool constructor and `patch_int2d_trap`, before demo playback starts.
The game did not write a new H1 minidump; Windows saved the dumps in the user's
local `CrashDumps` directory.

The previous allocator probed pages from the distant negative 2 GiB boundary.
The replacement queries free regions above and below the patch, skips occupied
reservations and clamps the search to valid application addresses and signed
32-bit branch reach. Pool ownership and trampoline encoding are unchanged.

`tests\run-hook-near-memory.cmd` checks real executable-memory allocations for
all 155 precomputed startup patch addresses, alignment, writable committed pages,
low-address bounds and fallback when the entire upper search window is reserved.
These checks pass with the Release compiler. Both the original allocator and
the replacement succeed in an otherwise empty standalone test process; the
captured startup exception therefore still requires validation in the client.

Release/x64 builds successfully. The correction is installed with verified
executable/PDB parity and a rollback backup; native startup verification is pending.
Evidence is in `build/startup-allocator-validation.json` and its referenced logs.
