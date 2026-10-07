#pragma once
namespace theater_library
{
    // Match demoplay's frontend rule: a loaded lobby or a disconnected client.
    inline bool can_launch(bool demo, bool conversion, bool cgame, bool lobby)
    { return !demo && !conversion && (!cgame || lobby); }
}
