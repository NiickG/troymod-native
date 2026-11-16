// Offline check of findLuaApi(): maps empire.retail.dll without running it and compares the found
// functions with known addresses: Rome 2 build 69ce2041 (verified in game) or, with "attila", the
// Attila DLL (read from its disassembly) as a differently compiled build. Usage: scantest <dll> [attila]
#include "lua_api.h"

int main(int argc, char** argv) {
    if (argc < 2) return std::printf("Aufruf: scantest <empire.retail.dll>\n"), 2;
    HMODULE h = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!h) return std::printf("kann %s nicht laden (%lu)\n", argv[1], GetLastError()), 2;
    char err[160] = "";
    if (!luascan::findLuaApi(h, err, sizeof err)) return std::printf("FEHLER: %s\n", err), 1;
    auto rva = [&](const void* p) { return static_cast<unsigned>(reinterpret_cast<const BYTE*>(p) - reinterpret_cast<BYTE*>(h)); };
    const bool attila = argc > 2 && std::strcmp(argv[2], "attila") == 0;
    struct { const char* name; const void* fn; unsigned expected; unsigned attila; } rows[] = {
        {"lua_tolstring", lua::tolstring, 0xF4B9D0, 0},     {"luaL_checklstring", lua::checklstring, 0xF4C050, 0x12c79e0},
        {"lua_pushstring", lua::pushstring, 0xF4B2F0, 0x12c6d20},   {"lua_pushfstring", lua::pushfstring, 0xF4B200, 0x12c6c30},
        {"lua_pushnil", lua::pushnil, 0xF4B2B0, 0x12c6ce0},         {"lua_pushvalue", lua::pushvalue, 0xF4B380, 0x12c6d80},
        {"lua_pushcclosure", lua::pushcclosure, 0xF4B160, 0x12c6ba0}, {"lua_insert", lua::insert, 0xF4ADC0, 0x12c6810},
        {"lua_type", lua::type, 0xF4BB50, 0x12c74e0},               {"lua_touserdata", lua::touserdata, 0xF4BB20, 0x12c74b0},
        {"lua_settop", lua::settop, 0xF4B810, 0x12c7210},           {"lua_newuserdata", lua::newuserdata, 0xF4AFB0, 0x12c6a00},
        {"lua_getfield", lua::getfield, 0xF4AC40, 0x12c66d0},       {"lua_settable", lua::settable, 0xF4B7E0, 0x12c71e0},
        {"lua_gettable", lua::gettable, 0xF4ACF0, 0x12c6780},       {"lua_setmetatable", lua::setmetatable, 0xF4B730, 0x12c7130},
    };
    int bad = 0;
    for (auto& r : rows) {
        const unsigned want = attila ? r.attila : r.expected;
        const bool ok = want == 0 || rva(r.fn) == want;  // 0 = not known beforehand
        bad += !ok;
        std::printf("%-20s %06X %s\n", r.name, rva(r.fn), ok ? "ok" : "FALSCH");
    }
    std::printf(bad ? "%d falsch\n" : "alle %d richtig\n", bad ? bad : static_cast<int>(std::size(rows)));
    return bad ? 1 : 0;
}
