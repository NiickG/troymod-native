// Offline check of findLuaApi(): maps empire.retail.dll without running it and compares the found
// functions with known addresses: Rome 2 build 69ce2041 (verified in game) or, with "attila", the
// Attila DLL (read from its disassembly) as a differently compiled build. Usage: scantest <dll> [attila]
#include "build_hook.h"
#include "ui_create.h"
#include "panel_hook.h"

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
    // Bau-Start-Funktion und Mensch-Pruefung (Rome 2: 8C7E00 / 8C812A aus dem Disassembly)
    buildhook::Found f;
    // Layout-Erzeuger (Rome 2: 1046C70 / 10470F0, Attila: 13A0680 / 13A09E0 aus dem Disassembly)
    uicreate::Found u;
    if (!uicreate::find(h, u, err, sizeof err)) {
        std::printf("Layout: FEHLER %s\n", err);
        ++bad;
    } else {
        const bool uok = attila ? rva(u.loader) == 0x13A0680 && rva(u.create) == 0x13A09E0
                                : rva(u.loader) == 0x1046C70 && rva(u.create) == 0x10470F0;
        std::printf("Layout-Lader %06X, Erzeuger %06X %s\n", rva(u.loader), rva(u.create), uok ? "ok" : "FALSCH");
        bad += !uok;
    }
    // Panel-Oeffner (Rome 2: 10F04E0 aus dem Disassembly; Attila darf fehlen)
    if (BYTE* ph = panelhook::find(h, err, sizeof err)) {
        const bool pok = attila || rva(ph) == 0x10F04E0;
        std::printf("Panel-Oeffner %06X %s\n", rva(ph), pok ? "ok" : "FALSCH");
        bad += !pok;
    } else {
        std::printf("Panel-Oeffner: FEHLER %s\n", err);
        bad += !attila;
    }
    if (!buildhook::findBuildStart(h, f, err, sizeof err)) {
        std::printf("Bau-Start: FEHLER %s\n", err);
        return attila ? (bad ? 1 : 0) : 1;  // Attila may differ (fails safe), Rome 2 must work
    }
    const bool bok = attila || (rva(f.start) == 0x8C7E00 && rva(f.humanJump) == 0x8C812A);
    std::printf("Bau-Start %06X, Mensch-Sprung %06X %s\n", rva(f.start), rva(f.humanJump), bok ? "ok" : "FALSCH");
    bad += !bok;
    return bad ? 1 : 0;
}
