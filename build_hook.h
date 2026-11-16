// "Construction started" for every faction. In empire.retail.dll one function starts a building
// for any faction (checks and takes the gold, creates the construction order) and at its end fires
// BuildingConstructionIssuedByPlayer - but only if the faction is human (cmp byte [faction+X], 0).
// findBuildStart() locates that function structurally:
//   "BuildingConstructionIssuedByPlayer" -> its name getter (mov eax, str; ret) -> the event context
//   vtable that holds the getter -> the "mov [esp+N], vtable" in the start function -> function start.
// enable: NOP the human check (event then fires for AI too, with the same Lua context) and hook the
// function entry to remember the building level record, readable via lastBuildRecord().
#pragma once
#include "lua_api.h"

namespace buildhook {

struct Found {
    BYTE* start = nullptr;      // function start (expects 83 EC 2C 53 8B D9)
    BYTE* humanJump = nullptr;  // the "je" after cmp byte [eax+disp32], 0
};

inline BYTE* findBytes(BYTE* from, BYTE* to, const BYTE* pat, size_t n) {
    for (BYTE* p = from; p + n <= to; ++p)
        if (std::memcmp(p, pat, n) == 0) return p;
    return nullptr;
}

inline bool findBuildStart(HMODULE game, Found& out, char* err, size_t errSize) {
    luascan::Image m;
    if (!game || !luascan::imageOf(game, m)) return sprintf_s(err, errSize, "kein Modul"), false;
    const DWORD name = luascan::stringVa(m, "BuildingConstructionIssuedByPlayer");
    if (!name) return sprintf_s(err, errSize, "Ereignisname fehlt"), false;
    BYTE getterPat[6] = {0xB8, 0, 0, 0, 0, 0xC3};
    std::memcpy(getterPat + 1, &name, 4);
    BYTE* getter = findBytes(m.text, m.textEnd, getterPat, 6);
    if (!getter) return sprintf_s(err, errSize, "Namensfunktion fehlt"), false;
    // vtable slot holding the getter; the vtable starts two slots earlier (dtor, ?, name)
    const DWORD g = static_cast<DWORD>(reinterpret_cast<uintptr_t>(getter));
    BYTE* slot = nullptr;
    int slots = 0;
    for (BYTE* p = m.base; p + 4 <= m.end; p += 4)
        if (*reinterpret_cast<DWORD*>(p) == g) slot = p, ++slots;
    if (slots != 1) return sprintf_s(err, errSize, "vtable %d-mal", slots), false;
    const DWORD vtable = static_cast<DWORD>(reinterpret_cast<uintptr_t>(slot - 8));
    // mov dword ptr [esp+disp8], vtable  (C7 44 24 xx imm32)
    BYTE* use = nullptr;
    for (BYTE* p = m.text; p + 8 <= m.textEnd; ++p)
        if (p[0] == 0xC7 && p[1] == 0x44 && p[2] == 0x24 && *reinterpret_cast<DWORD*>(p + 4) == vtable) {
            if (use) return sprintf_s(err, errSize, "vtable mehrfach benutzt"), false;
            use = p;
        }
    if (!use) return sprintf_s(err, errSize, "vtable-Zuweisung fehlt"), false;
    BYTE* f = use;
    while (f > use - 0x800 && !(f[-1] == 0xCC && f[-2] == 0xCC)) --f;
    static const BYTE kPrologue[] = {0x83, 0xEC, 0x2C, 0x53, 0x8B, 0xD9};
    // E9 rel32 90 = already hooked by an earlier copy of this DLL (dev reload loads a fresh copy): hook again
    const bool hooked = f[0] == 0xE9 && f[5] == 0x90;
    if (!hooked && std::memcmp(f, kPrologue, sizeof kPrologue) != 0) return sprintf_s(err, errSize, "Funktionsanfang unerwartet"), false;
    // last "cmp byte ptr [eax+disp32], 0; je rel8" before the vtable use = human check
    BYTE* jump = nullptr;
    for (BYTE* p = f; p + 9 <= use; ++p)
        if (p[0] == 0x80 && p[1] == 0xB8 && p[6] == 0x00 && (p[7] == 0x74 || (hooked && p[7] == 0x90 && p[8] == 0x90))) jump = p + 7;
    if (!jump) return sprintf_s(err, errSize, "Mensch-Pruefung fehlt"), false;
    out.start = f;
    out.humanJump = jump;
    return true;
}

// ---- runtime part (only in the DLL)
inline void* g_record = nullptr;  // building level record of the last construction start
inline void* g_slot = nullptr;    // "this" of that call
inline BYTE* g_back = nullptr;    // start + 6

#ifdef BUILDHOOK_RUNTIME
__declspec(naked) inline void entryStub() {
    __asm {
        mov eax, [esp + 4]
        mov eax, [eax]
        mov g_record, eax
        mov g_slot, ecx
        sub esp, 0x2c  // the 6 replaced bytes
        push ebx
        mov ebx, ecx
        jmp g_back
    }
}

inline bool writeCode(BYTE* at, const BYTE* bytes, size_t n) {
    DWORD old;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(at, bytes, n);
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return true;
}

inline bool enable(char* err, size_t errSize) {
    static bool done = false;
    if (done) return true;
    Found f;
    if (!findBuildStart(GetModuleHandleA("empire.retail.dll"), f, err, errSize)) return false;
    g_back = f.start + 6;
    BYTE jmp[6] = {0xE9, 0, 0, 0, 0, 0x90};
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<BYTE*>(&entryStub) - (f.start + 5));
    std::memcpy(jmp + 1, &rel, 4);
    static const BYTE nops[2] = {0x90, 0x90};
    if (!writeCode(f.humanJump, nops, 2) || !writeCode(f.start, jmp, 6))
        return sprintf_s(err, errSize, "Code nicht beschreibbar"), false;
    done = true;
    return true;
}
#endif

}  // namespace buildhook
