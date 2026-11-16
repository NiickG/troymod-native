// Lua 5.1 API of Rome 2, found at runtime. empire.retail.dll links Lua statically and exports
// nothing, so the functions are located structurally instead of by fixed addresses:
//   "loadlib" string -> its luaL_Reg entry -> ll_loadlib -> the calls it makes (luaL_checklstring,
//   ll_loadfunc, lua_pushnil, lua_insert, lua_pushstring) -> the calls inside those.
// Lua's own C code does not change between game patches, only where it lies, so this survives
// patches that move code. Every result is cross-checked; on any doubt findLuaApi() fails.
#pragma once
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <vector>

struct lua_State;
typedef int (*lua_CFunction)(lua_State*);

namespace lua {
inline const char* (*tolstring)(lua_State*, int, size_t*);
inline const char* (*checklstring)(lua_State*, int, size_t*);
inline void (*pushstring)(lua_State*, const char*);
inline const char* (*pushfstring)(lua_State*, const char*, ...);
inline void (*pushnil)(lua_State*);
inline void (*pushvalue)(lua_State*, int);
inline void (*pushcclosure)(lua_State*, lua_CFunction, int);
inline void (*insert)(lua_State*, int);
inline int (*type)(lua_State*, int);
inline void* (*touserdata)(lua_State*, int);
inline void (*settop)(lua_State*, int);
inline void* (*newuserdata)(lua_State*, size_t);
inline void (*getfield)(lua_State*, int, const char*);
inline void (*settable)(lua_State*, int);
inline void (*gettable)(lua_State*, int);
inline int (*setmetatable)(lua_State*, int);
}  // namespace lua

namespace luascan {

struct Image {
    BYTE* base = nullptr;
    BYTE* text = nullptr;
    BYTE* textEnd = nullptr;
    BYTE* end = nullptr;
};

inline bool inText(const Image& m, const BYTE* p) { return p >= m.text && p < m.textEnd; }

// Targets of direct calls (E8 rel32) in [f, f+len) whose target lies in .text, in code order.
inline std::vector<BYTE*> callsIn(const Image& m, BYTE* f, size_t len) {
    std::vector<BYTE*> out;
    for (BYTE* p = f; p + 5 <= f + len && p + 5 <= m.textEnd; ++p) {
        if (*p != 0xE8) continue;
        BYTE* t = p + 5 + *reinterpret_cast<int32_t*>(p + 1);
        if (inText(m, t)) out.push_back(t), p += 4;
    }
    return out;
}

// First "push 0; push eax; push <reg>; call rel32" in [f, f+len): lua_pushcclosure(L, f, 0) in ll_loadfunc.
inline BYTE* pushCClosureCall(const Image& m, BYTE* f, size_t len) {
    for (BYTE* p = f; p + 9 <= f + len; ++p)
        if (p[0] == 0x6A && p[1] == 0x00 && p[2] == 0x50 && (p[3] & 0xF8) == 0x50 && p[4] == 0xE8) {
            BYTE* t = p + 9 + *reinterpret_cast<int32_t*>(p + 5);
            return inText(m, t) ? t : nullptr;
        }
    return nullptr;
}

// Address of a zero-terminated string (preceded by 0) anywhere in the image, or 0.
inline DWORD stringVa(const Image& m, const char* s) {
    const size_t n = std::strlen(s) + 1;
    for (BYTE* p = m.base + 1; p + n <= m.end; ++p)
        if (p[-1] == 0 && std::memcmp(p, s, n) == 0) return static_cast<DWORD>(reinterpret_cast<uintptr_t>(p));
    return 0;
}

// First "push imm32" of that string's address in .text.
inline BYTE* pushOfString(const Image& m, const char* s) {
    const DWORD va = stringVa(m, s);
    if (!va) return nullptr;
    for (BYTE* p = m.text; p + 5 <= m.textEnd; ++p)
        if (*p == 0x68 && *reinterpret_cast<DWORD*>(p + 1) == va) return p;
    return nullptr;
}

inline bool imageOf(HMODULE h, Image& m) {
    auto base = reinterpret_cast<BYTE*>(h);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    m.base = base;
    m.end = base + nt->OptionalHeader.SizeOfImage;
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (std::memcmp(sec[i].Name, ".text", 6) == 0) {
            m.text = base + sec[i].VirtualAddress;
            m.textEnd = m.text + sec[i].Misc.VirtualSize;
        }
    return m.text != nullptr;
}

// Fills lua::*. On failure returns false and writes the reason to err.
inline bool findLuaApi(HMODULE game, char* err, size_t errSize) {
    Image m;
    if (!game || !imageOf(game, m)) return sprintf_s(err, errSize, "empire.retail.dll ohne .text"), false;

    // 1) ll_loadlib from the package library's luaL_Reg {"loadlib", ll_loadlib}
    static const char kName[] = "loadlib";
    BYTE* llLoadlib = nullptr;
    int found = 0;
    for (BYTE* s = m.base + 1; s + sizeof kName <= m.end; ++s) {
        if (s[-1] != 0 || std::memcmp(s, kName, sizeof kName) != 0) continue;
        const DWORD va = static_cast<DWORD>(reinterpret_cast<uintptr_t>(s));
        for (BYTE* p = m.base; p + 8 <= m.end; p += 4)
            if (*reinterpret_cast<DWORD*>(p) == va) {
                BYTE* fn = reinterpret_cast<BYTE*>(static_cast<uintptr_t>(*reinterpret_cast<DWORD*>(p + 4)));
                if (inText(m, fn)) llLoadlib = fn, ++found;
            }
    }
    if (found != 1) return sprintf_s(err, errSize, "loadlib-Eintrag %d-mal gefunden (erwartet 1)", found), false;

    // 2) ll_loadlib: checklstring(L,1), checklstring(L,2), ll_loadfunc, pushnil, insert, pushstring
    auto a = callsIn(m, llLoadlib, 0x70);
    if (a.size() < 6 || a[0] != a[1]) return sprintf_s(err, errSize, "ll_loadlib unerwartet (%zu Aufrufe)", a.size()), false;
    lua::checklstring = reinterpret_cast<decltype(lua::checklstring)>(a[0]);
    BYTE* llLoadfunc = a[2];
    lua::pushnil = reinterpret_cast<decltype(lua::pushnil)>(a[3]);
    lua::insert = reinterpret_cast<decltype(lua::insert)>(a[4]);
    lua::pushstring = reinterpret_cast<decltype(lua::pushstring)>(a[5]);

    // 3) luaL_checklstring starts with lua_tolstring
    auto c = callsIn(m, a[0], 0x20);
    if (c.empty()) return sprintf_s(err, errSize, "luaL_checklstring ohne Aufruf"), false;
    lua::tolstring = reinterpret_cast<decltype(lua::tolstring)>(c[0]);

    // 4) ll_register (inlined in Rome 2, own function in Attila) starts at push "LOADLIB: " and calls:
    //    pushfstring, gettable, type, touserdata, settop, newuserdata, getfield, setmetatable,
    //    pushfstring, pushvalue, settable. pushcclosure(L, f, 0) is in ll_loadfunc.
    BYTE* reg = pushOfString(m, "LOADLIB: ");
    if (!reg) return sprintf_s(err, errSize, "push \"LOADLIB: \" nicht gefunden"), false;
    auto r = callsIn(m, reg, 0xA0);
    if (r.size() < 11 || r[0] != r[8]) return sprintf_s(err, errSize, "ll_register unerwartet (%zu Aufrufe)", r.size()), false;
    lua::pushfstring = reinterpret_cast<decltype(lua::pushfstring)>(r[0]);
    lua::gettable = reinterpret_cast<decltype(lua::gettable)>(r[1]);
    lua::type = reinterpret_cast<decltype(lua::type)>(r[2]);
    lua::touserdata = reinterpret_cast<decltype(lua::touserdata)>(r[3]);
    lua::settop = reinterpret_cast<decltype(lua::settop)>(r[4]);
    lua::newuserdata = reinterpret_cast<decltype(lua::newuserdata)>(r[5]);
    lua::getfield = reinterpret_cast<decltype(lua::getfield)>(r[6]);
    lua::setmetatable = reinterpret_cast<decltype(lua::setmetatable)>(r[7]);
    lua::pushvalue = reinterpret_cast<decltype(lua::pushvalue)>(r[9]);
    lua::settable = reinterpret_cast<decltype(lua::settable)>(r[10]);
    lua::pushcclosure = reinterpret_cast<decltype(lua::pushcclosure)>(pushCClosureCall(m, llLoadfunc, 0x140));
    if (!lua::pushcclosure) return sprintf_s(err, errSize, "lua_pushcclosure nicht gefunden"), false;

    // 5) all distinct (except the expected duplicates above)
    const void* all[] = {lua::tolstring, lua::checklstring, lua::pushstring, lua::pushfstring, lua::pushnil, lua::pushvalue,
                         lua::pushcclosure, lua::insert, lua::type, lua::touserdata, lua::settop, lua::newuserdata,
                         lua::getfield, lua::settable, lua::gettable, lua::setmetatable};
    for (size_t i = 0; i < std::size(all); ++i)
        for (size_t j = i + 1; j < std::size(all); ++j)
            if (all[i] == all[j]) return sprintf_s(err, errSize, "Lua-Funktionen %zu und %zu gleich", i, j), false;
    return true;
}

}  // namespace luascan
