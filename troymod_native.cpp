// Native Lua module for Rome 2 (32-bit), loaded from TroyMod's Lua via
//   local log, enable_build_events, build_dump, last_building = package.loadlib("troymod_native.dll", "luaopen_troymod_native")()
// The game links Lua 5.1 statically into empire.retail.dll and exports nothing; lua_api.h finds
// the Lua functions at load time (no fixed addresses, so game patches that move code are fine).
#define BUILDHOOK_RUNTIME
#include "build_hook.h"
#include <string>

static void writeLog(const char* msg) {
    FILE* f = nullptr;
    if (fopen_s(&f, "troymod_native.log", "a") == 0 && f) {  // game folder = working directory
        SYSTEMTIME t; GetLocalTime(&t);
        fprintf(f, "%02d:%02d:%02d %s\n", t.wHour, t.wMinute, t.wSecond, msg);
        fclose(f);
    }
}

static bool readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi;
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT) return false;
    if (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return static_cast<const BYTE*>(p) + n <= static_cast<const BYTE*>(mi.BaseAddress) + mi.RegionSize;
}

// Printable text at p (ASCII or UTF-16), at least 3 chars, else "".
static std::string textAt(const void* p) {
    std::string s;
    if (!readable(p, 64)) return s;
    auto a = static_cast<const unsigned char*>(p);
    for (int i = 0; i < 60 && a[i] >= 32 && a[i] < 127; ++i) s += char(a[i]);
    if (s.size() >= 3) return s;
    s.clear();
    auto w = static_cast<const wchar_t*>(p);
    for (int i = 0; i < 30 && w[i] >= 32 && w[i] < 127; ++i) s += char(w[i]);
    return s.size() >= 3 ? "u\"" + s + "\"" : std::string();
}

// Lua: log("text") -> "ok"
static int nativeLog(lua_State* L) {
    const char* s = lua::tolstring(L, 1, nullptr);
    writeLog(s ? s : "(kein Text)");
    lua::pushstring(L, "ok");
    return 1;
}

// Lua: enable_build_events() -> true | nil, error
// After this BuildingConstructionIssuedByPlayer also fires for AI factions.
static int enableBuildEvents(lua_State* L) {
    char err[160] = "";
    if (buildhook::enable(err, sizeof err)) {
        writeLog("Bau-Ereignis fuer alle Fraktionen aktiv");
        lua::pushstring(L, "true");
        return 1;
    }
    char msg[220];
    sprintf_s(msg, "FEHLER Bau-Ereignis: %s", err);
    writeLog(msg);
    lua::pushnil(L);
    lua::pushstring(L, err);
    return 2;
}

// Lua: build_dump(tag) -> writes the last building level record to the log (to locate its key),
// returns the found texts as one string.
static int buildDump(lua_State* L) {
    const char* tag = lua::tolstring(L, 1, nullptr);
    auto rec = static_cast<BYTE*>(buildhook::g_record);
    char line[300];
    sprintf_s(line, "DUMP %s record=%p slot=%p", tag ? tag : "", rec, buildhook::g_slot);
    writeLog(line);
    std::string found;
    for (int off = 0; rec && off < 0x100 && readable(rec + off, 4); off += 4) {
        void* v = *reinterpret_cast<void**>(rec + off);
        std::string direct = textAt(rec + off), viaPtr = textAt(v), viaPtrPtr;
        if (readable(v, 4)) viaPtrPtr = textAt(*static_cast<void**>(v));
        if (direct.empty() && viaPtr.empty() && viaPtrPtr.empty()) continue;
        sprintf_s(line, "  +%02X %08X  direkt[%s]  ptr[%s]  ptrptr[%s]", off, static_cast<unsigned>(reinterpret_cast<uintptr_t>(v)),
                  direct.c_str(), viaPtr.c_str(), viaPtrPtr.c_str());
        writeLog(line);
        char part[200];
        sprintf_s(part, "+%02X:%s|%s|%s ", off, direct.c_str(), viaPtr.c_str(), viaPtrPtr.c_str());
        found += part;
    }
    lua::pushstring(L, found.c_str());
    return 1;
}

// Lua: last_building() -> key of the building level whose construction just started, or nil.
// Call it inside BuildingConstructionIssuedByPlayer (after enable_build_events()).
// The level record holds a pointer to its key at +0x0C (found in game 2026-11-16, e.g. "pel_port_2").
static int lastBuilding(lua_State* L) {
    auto rec = static_cast<BYTE*>(buildhook::g_record);
    const char* key = nullptr;
    if (rec && readable(rec + 0x0C, 4)) {
        auto p = *reinterpret_cast<const char**>(rec + 0x0C);
        if (readable(p, 2) && p[0] >= 'a' && p[0] <= 'z') key = p;  // keys are lower-case identifiers
    }
    if (key) lua::pushstring(L, key);
    else lua::pushnil(L);
    return 1;
}

extern "C" __declspec(dllexport) int luaopen_troymod_native(lua_State* L) {
    static bool ready = false;
    if (!ready) {
        char err[160] = "";
        if (!luascan::findLuaApi(GetModuleHandleA("empire.retail.dll"), err, sizeof err)) {
            char msg[220];
            sprintf_s(msg, "FEHLER: Lua-Funktionen nicht gefunden: %s", err);
            writeLog(msg);
            return 0;  // returns nothing -> Lua sees nil, no crash
        }
        ready = true;
        writeLog("troymod_native 0.3.0 geladen (Lua-Funktionen per Suche gefunden)");
    }
    lua::pushcclosure(L, nativeLog, 0);
    lua::pushcclosure(L, enableBuildEvents, 0);
    lua::pushcclosure(L, buildDump, 0);
    lua::pushcclosure(L, lastBuilding, 0);
    return 4;
}
