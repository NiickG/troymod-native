// Native Lua module for Rome 2 (32-bit), loaded from TroyMod's Lua via
//   local log, enable_build_events, build_dump, last_building, create_component, block_panels, trace_panels = package.loadlib("troymod_native.dll", "luaopen_troymod_native")()
// The game links Lua 5.1 statically into empire.retail.dll and exports nothing; lua_api.h finds
// the Lua functions at load time (no fixed addresses, so game patches that move code are fine).
#define BUILDHOOK_RUNTIME
#include "build_hook.h"
#include "ui_create.h"
#define PANELHOOK_RUNTIME
#include "panel_hook.h"
#include <string>
#include <cstdlib>

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

// Lua: create_component(parent, name, layout_path) -> true | nil, error
// parent = address from Find() or UIComponent:Address() (or a UIComponent); layout_path like "ui/troymod/trade_ship_panel".
// The new component is a child of parent named <name>: get it with UIComponent(parent):Find(name).
static uicreate::Found g_create;
static luascan::Image g_image;

// Component pointer = object whose vtable lies in empire.retail.dll.
static void* asComponent(void* p) {
    if (!readable(p, 4)) return nullptr;
    auto vt = *static_cast<BYTE**>(p);
    return vt >= g_image.base && vt < g_image.end ? p : nullptr;
}

typedef void* (__cdecl* CreateFn)(void*, const uicreate::GameString*, const uicreate::GameString*, const float*, int, int, int);

static void* callCreate(void* parent, const uicreate::GameString* name, const uicreate::GameString* path, DWORD* code) {
    __try {
        return reinterpret_cast<CreateFn>(g_create.create)(parent, path, name, nullptr, 0, 0, 0);  // path first (game calls: "ui\Common UI\dialogue_box", "dialogue_box")
    } __except (*code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// shortcut: the chars are never freed (a few bytes per window), free them once a test shows the game copies them
static uicreate::GameString gameString(const char* s) {
    const uint32_t n = static_cast<uint32_t>(std::strlen(s));
    auto raw = static_cast<char*>(std::calloc(n + 9, 1));  // 4 header (0) + aligned chars + 0
    char* data = reinterpret_cast<char*>((reinterpret_cast<uintptr_t>(raw) + 7) & ~uintptr_t(3));
    std::memcpy(data, s, n);
    return {n, n, data};
}

static int createComponent(lua_State* L) {
    const char* err = nullptr;
    void* parent = nullptr;
    const char* name = lua::tolstring(L, 2, nullptr);
    const char* path = lua::tolstring(L, 3, nullptr);
    if (!g_create.create) {
        char e[160] = "";
        luascan::imageOf(GetModuleHandleA("empire.retail.dll"), g_image);
        if (!uicreate::find(GetModuleHandleA("empire.retail.dll"), g_create, e, sizeof e)) {
            char msg[220];
            sprintf_s(msg, "FEHLER Layout-Funktion: %s", e);
            writeLog(msg);
            g_create = {};
            lua::pushnil(L);
            lua::pushstring(L, msg);
            return 2;
        }
    }
    void* p = lua::touserdata(L, 1);  // light userdata (Find) = the component, full userdata (UIComponent) holds it first
    parent = asComponent(p);
    for (int depth = 0; !parent && depth < 2 && readable(p, 4); ++depth) {  // Lunar-style: userdata -> wrapper -> component
        p = *static_cast<void**>(p);
        parent = asComponent(p);
    }
    char why[80];
    if (!parent) sprintf_s(why, "parent ist keine Komponente (Lua-Typ %d)", lua::type(L, 1)), err = why;
    else if (!name || !*name || !path || !*path) err = "Name oder Pfad fehlt";
    void* made = nullptr;
    DWORD code = 0;
    if (!err) {
        const auto n = gameString(name), l = gameString(path);
        made = callCreate(parent, &n, &l, &code);
        if (code) err = "Absturz im Spiel abgefangen";
        else if (!made) err = "Layout nicht geladen (Pfad?)";
    }
    char msg[300];
    sprintf_s(msg, "create_component %s %s: %s (code %08lX)", name ? name : "?", path ? path : "?", err ? err : "ok", code);
    writeLog(msg);
    if (!err) {
        lua::pushstring(L, "true");
        return 1;
    }
    lua::pushnil(L);
    lua::pushstring(L, err);
    return 2;
}

// Lua: block_panels("units_panel", "character_information", ...) -> true | nil, error
// The game does not open panels with these names any more (until the next call; no names = open all again).
static bool panelHookOn(lua_State* L) {
    char err[160] = "";
    panelhook::g_log = writeLog;
    if (panelhook::enable(err, sizeof err)) return true;
    char msg[220];
    sprintf_s(msg, "FEHLER Panel-Sperre: %s", err);
    writeLog(msg);
    lua::pushnil(L);
    lua::pushstring(L, msg);
    return false;
}

static int blockPanels(lua_State* L) {
    if (!panelHookOn(L)) return 2;
    panelhook::g_blocked.clear();
    for (int i = 1; lua::type(L, i) > 0; ++i)  // LUA_TNONE = -1, LUA_TNIL = 0
        if (const char* s = lua::tolstring(L, i, nullptr)) panelhook::g_blocked.push_back(s);
    lua::pushstring(L, "true");
    return 1;
}

// Lua: trace_panels(true) / trace_panels() -> on / off: every panel the game opens by name goes to troymod_native.log
static int tracePanels(lua_State* L) {
    if (!panelHookOn(L)) return 2;
    panelhook::g_trace = lua::type(L, 1) > 0;  // any argument = on, none = off (no lua_toboolean here)
    lua::pushstring(L, "true");
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
        writeLog("troymod_native 0.4.0 geladen (Lua-Funktionen per Suche gefunden)");
    }
    lua::pushcclosure(L, nativeLog, 0);
    lua::pushcclosure(L, enableBuildEvents, 0);
    lua::pushcclosure(L, buildDump, 0);
    lua::pushcclosure(L, lastBuilding, 0);
    lua::pushcclosure(L, createComponent, 0);
    lua::pushcclosure(L, blockPanels, 0);
    lua::pushcclosure(L, tracePanels, 0);
    return 7;
}
