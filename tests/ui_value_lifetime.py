"""Compile the production HKS value owner against a checked registry fixture.

Only the native registry/stack API is mocked. Extract the class and its method
definitions verbatim so this tests the real ownership code without launching H1.
"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
header = (root / "src/client/game/ui_scripting/script_value.hpp").read_text()
source = (root / "src/client/game/ui_scripting/script_value.cpp").read_text()
declaration = header[header.index("\tclass hks_object\n"):header.index("\tusing arguments =")]
implementation = source[source.index("\thks_object::hks_object("):source.index("\t/***************************************************************")]
preamble = r'''
#include <cassert>
#include <iostream>
#include <map>
#include <utility>
#include <vector>
namespace game::hks {
enum HksObjectType { TNONE, TTABLE };
struct HksObject { HksObjectType t; union { int object; void* ptr; } v; };
struct lua_State { struct { HksObject* top; } m_apistack; };
HksObject stack[64]{};
lua_State state{{stack}};
lua_State* current = &state;
lua_State** lua_state = &current;
std::map<int, HksObject> registry;
int next_ref = 1;
int hksi_luaL_ref(lua_State* s, int) {
    const auto id = next_ref++;
    registry.emplace(id, *--s->m_apistack.top);
    return id;
}
void hksi_luaL_unref(lua_State*, int, int id) {
    assert(registry.erase(id) == 1 && "double/stale registry release");
}
bool rooted(int object) {
    for (const auto& [id, value] : registry)
        if (value.t == TTABLE && value.v.object == object) return true;
    return false;
}
}
namespace ui_scripting {
void push_value(const game::hks::HksObject& value) {
    *game::hks::state.m_apistack.top++ = value;
}
'''
checks = r'''
}
using ui_scripting::hks_object;
using namespace game::hks;
HksObject object(int id) { return {TTABLE, {id}}; }
int main() {
    // The temporary dies immediately, as in script_value's scalar/table constructors.
    {
        hks_object destination;
        destination = hks_object(object(1));
        assert(rooted(1) && "move assignment must retain the Lua root");
        assert(registry.size() == 1);
        destination = std::move(destination);
        assert(rooted(1));
    }
    assert(registry.empty());
    {
        hks_object first(object(2));
        hks_object moved(std::move(first));
        assert(first.get().t == TNONE && rooted(2));
        first = hks_object(object(3)); // Reuse the moved-from owner.
        assert(rooted(2) && rooted(3));
        hks_object copy(moved);
        moved = hks_object(object(4));
        assert(rooted(2) && rooted(4));
        copy = copy;
        assert(rooted(2));
        copy = hks_object{}; // Clear an existing owner without a second unref.
        assert(!rooted(2));
        moved = std::move(first); // Release destination's previous root exactly once.
        assert(!rooted(4) && rooted(3));
    }
    assert(registry.empty());
    {
        std::vector<hks_object> values;
        for (int i = 1; i <= 4096; ++i) values.emplace_back(object(i));
        for (int i = 1; i <= 4096; ++i) assert(rooted(i));
        assert(registry.size() == values.size());
    }
    assert(registry.empty());
    assert(state.m_apistack.top == stack);
    std::cout << "PASS HKS ownership: temporary assignment, moves, copies, reuse, clearing, vector relocation\n";
}
'''
build = root / "build"
build.mkdir(exist_ok=True)
fixture = build / "ui_value_lifetime.cpp"
fixture.write_text(preamble + declaration + implementation + checks)
exe = build / "ui_value_lifetime.exe"
subprocess.run(["cl", "/nologo", "/std:c++20", "/EHsc", "/W4", "/O2",
                str(fixture), f"/Fe:{exe}", f"/Fo:{build / 'ui_value_lifetime.obj'}"], check=True)
subprocess.run([str(exe)], check=True)
