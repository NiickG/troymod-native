// Proof of concept: native Lua module for Rome 2 (32-bit), loaded from campaign Lua via
//   local f = package.loadlib("troymod_native.dll", "luaopen_troymod_native")
// The game links Lua 5.1 statically into empire.retail.dll and exports nothing, so the
// Lua API functions are called at fixed RVAs found by disassembling ll_loadlib/ll_loadfunc.
// shortcut: RVAs only valid for the build checked below (PE timestamp); after a game patch
// they must be found again (or replaced by byte-signature scanning like bukowa/twdll).
#include <windows.h>
#include <cstdio>

struct lua_State;
typedef int (*lua_CFunction)(lua_State*);
typedef const char* (*tolstring_t)(lua_State*, int, size_t*);
typedef void (*pushstring_t)(lua_State*, const char*);
typedef void (*pushcclosure_t)(lua_State*, lua_CFunction, int);

static const DWORD kBuildStamp = 0x69ce2041;  // empire.retail.dll TimeDateStamp of the analysed build
static tolstring_t lua_tolstring;
static pushstring_t lua_pushstring;
static pushcclosure_t lua_pushcclosure;

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
    const char* s = lua_tolstring(L, 1, nullptr);
    writeLog(s ? s : "(kein Text)");
    lua_pushstring(L, "ok");
    return 1;
}

extern "C" __declspec(dllexport) int luaopen_troymod_native(lua_State* L) {
    auto base = (BYTE*)GetModuleHandleA("empire.retail.dll");
    if (!base) { writeLog("FEHLER: empire.retail.dll nicht geladen"); return 0; }
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != kBuildStamp) {
        char b[96]; sprintf_s(b, "FEHLER: unbekannter Spiel-Build %08lx, Adressen ungueltig", nt->FileHeader.TimeDateStamp);
        writeLog(b);
        return 0;  // returns nothing -> Lua sees nil, no crash
    }
    lua_tolstring    = (tolstring_t)(base + 0xF4B9D0);
    lua_pushstring   = (pushstring_t)(base + 0xF4B2F0);
    lua_pushcclosure = (pushcclosure_t)(base + 0xF4B160);
    writeLog("troymod_native geladen");
    lua_pushcclosure(L, nativeLog, 0);
    return 1;
}
