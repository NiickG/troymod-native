// Creating UI components from own layout files (what Attila's Lua method CreateComponent does; Rome 2's
// Lua has no such method, the engine function exists though). Found structurally, no fixed addresses:
//   "CreateFromLayout" string -> the layout loader using it (thiscall on the UI manager)
//   -> the one cdecl wrapper calling it: create(parent, const string* path, const string* name, const float* pos, 0, 0, 0)
//      -> new component or 0. Rome 2 build 69ce2041: loader 0x11046C70, wrapper 0x110470F0.
#pragma once
#include "lua_api.h"
#include <cstdint>

namespace uicreate {

struct Found {
    BYTE* loader = nullptr;
    BYTE* create = nullptr;
};

// Start of the function containing p (after int3 padding), at most 0x2000 bytes back.
inline BYTE* funcStart(BYTE* p) {
    for (BYTE* lo = p - 0x2000; p > lo; --p)
        if (p[-1] == 0xCC && p[-2] == 0xCC) return p;
    return nullptr;
}

inline bool find(HMODULE game, Found& out, char* err, size_t errSize) {
    luascan::Image m;
    if (!luascan::imageOf(game, m)) return sprintf_s(err, errSize, "kein .text"), false;
    BYTE* use = luascan::pushOfString(m, "CreateFromLayout");
    if (!use) return sprintf_s(err, errSize, "CreateFromLayout fehlt"), false;
    BYTE* loader = funcStart(use);
    // sub esp, imm8; push ebx; push ebp; push esi; push edi; mov ebx, ecx
    static const BYTE kLoader[] = {0x53, 0x55, 0x56, 0x57, 0x8B, 0xD9};
    if (!loader || loader[0] != 0x83 || loader[1] != 0xEC || std::memcmp(loader + 3, kLoader, sizeof kLoader) != 0)
        return sprintf_s(err, errSize, "Layout-Lader unerwartet"), false;
    // the wrapper: starts "mov eax,[esp+10h]; test eax,eax; je" (optional position), loads the UI manager
    // (mov ecx,[imm32]) and calls the loader
    static const BYTE kCreate[] = {0x8B, 0x44, 0x24, 0x10, 0x85, 0xC0, 0x74};
    BYTE* create = nullptr;
    for (BYTE* p = m.text; p + 5 <= m.textEnd; ++p) {
        if (*p != 0xE8 || p + 5 + *reinterpret_cast<int32_t*>(p + 1) != loader) continue;
        BYTE* f = funcStart(p);
        if (!f || p - f > 0x60 || std::memcmp(f, kCreate, sizeof kCreate) != 0) continue;
        bool manager = false;
        for (BYTE* q = f; q + 6 <= p; ++q) manager |= q[0] == 0x8B && q[1] == 0x0D;
        if (!manager) continue;
        if (create) return sprintf_s(err, errSize, "Erzeuger mehrfach"), false;
        create = f;
    }
    if (!create) return sprintf_s(err, errSize, "Erzeuger fehlt"), false;
    out.loader = loader;
    out.create = create;
    return true;
}

// The game's string: length, capacity, chars. The 4 bytes before the chars hold the start of the
// allocation (0 = not allocated, the destructor then frees nothing).
struct GameString {
    uint32_t len, cap;
    const char* data;
};

}  // namespace uicreate
