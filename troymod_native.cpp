// Native Lua module for Rome 2 (32-bit), loaded from TroyMod's Lua via
//   local f = package.loadlib("troymod_native.dll", "luaopen_troymod_native")
// The game links Lua 5.1 statically into empire.retail.dll and exports nothing; lua_api.h finds
// the Lua functions at load time (no fixed addresses, so game patches that move code are fine).
#include "lua_api.h"

static void writeLog(const char* msg) {
    FILE* f = nullptr;
    if (fopen_s(&f, "troymod_native.log", "a") == 0 && f) {  // game folder = working directory
        SYSTEMTIME t; GetLocalTime(&t);
        fprintf(f, "%02d:%02d:%02d %s\n", t.wHour, t.wMinute, t.wSecond, msg);
        fclose(f);
    }
}

// Lua: troymod_native_log("text") -> "ok"
static int nativeLog(lua_State* L) {
    const char* s = lua::tolstring(L, 1, nullptr);
    writeLog(s ? s : "(kein Text)");
    lua::pushstring(L, "ok");
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
        writeLog("troymod_native 0.2.0 geladen (Lua-Funktionen per Suche gefunden)");
    }
    lua::pushcclosure(L, nativeLog, 0);
    return 1;
}
