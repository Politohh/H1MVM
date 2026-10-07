extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <iostream>
int main()
{
    auto* lua = luaL_newstate();
    luaL_openlibs(lua);
    const auto result = luaL_dofile(lua, "tests/depot_lui.lua");
    if (result) std::cerr << lua_tostring(lua, -1) << '\n';
    lua_close(lua);
    return result ? 1 : 0;
}
