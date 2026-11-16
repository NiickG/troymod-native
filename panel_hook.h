// Opening campaign panels by name (units_panel, character_information, ...). The panel manager's
// open function (thiscall, open(const string* name, a2, a3), ret 0Ch; Rome 2 build 69ce2041: 0x110F04E0)
// looks the name up among the panels registered with their layout path and creates the layout via the
// layout loader. Found structurally: the one call of the layout loader (ui_create.h) preceded by
//   mov ecx,[ui manager]; push 1; push esi; push esi; push esi; push [imm32]; push esi; push esi; push ebp; push ebx
// and the function start "sub esp,24h; push ebx; push ebp; mov ebp,[esp+30h]".
// Runtime: the entry is hooked; a name on the block list returns 0 without opening (the function itself
// returns 0 for an unknown name), and trace mode logs every name.
#pragma once
#include "ui_create.h"
#include <string>
#include <vector>

namespace panelhook {

static const BYTE kPrologue[] = {0x83, 0xEC, 0x24, 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x30};

inline BYTE* find(HMODULE game, char* err, size_t errSize) {
    uicreate::Found u;
    if (!uicreate::find(game, u, err, errSize)) return nullptr;
    luascan::Image m;
    luascan::imageOf(game, m);
    static const BYTE kArgs[] = {0x6A, 0x01, 0x56, 0x56, 0x56, 0xFF, 0x35};  // push 1; push esi x3; push [imm32]
    static const BYTE kTail[] = {0x56, 0x56, 0x55, 0x53, 0xE8};              // push esi; push esi; push ebp; push ebx; call
    BYTE* found = nullptr;
    for (BYTE* p = m.text + 21; p + 5 <= m.textEnd; ++p) {
        if (*p != 0xE8 || p + 5 + *reinterpret_cast<int32_t*>(p + 1) != u.loader) continue;
        BYTE* s = p - 15;  // kArgs (7) + imm32 (4) + tail (4) before the call; mov ecx,[imm32] (6) before that
        if (std::memcmp(p - 4, kTail, 4) != 0 || s[-6] != 0x8B || s[-5] != 0x0D || std::memcmp(s, kArgs, sizeof kArgs) != 0) continue;
        BYTE* f = uicreate::funcStart(p);
        // hooked by an earlier copy of this DLL (dev reload): E9 rel32 then the rest of the prologue
        const bool hooked = f && f[0] == 0xE9 && std::memcmp(f + 5, kPrologue + 5, 4) == 0;
        if (!f || (!hooked && std::memcmp(f, kPrologue, sizeof kPrologue) != 0)) continue;
        if (found) return sprintf_s(err, errSize, "Panel-Oeffner mehrfach"), nullptr;
        found = f;
    }
    if (!found) sprintf_s(err, errSize, "Panel-Oeffner fehlt");
    return found;
}

#ifdef PANELHOOK_RUNTIME
inline std::vector<std::string> g_blocked;
inline bool g_trace = false;
inline BYTE* g_back = nullptr;  // start + 5
inline void (*g_log)(const char*) = nullptr;

inline bool __cdecl blockCheck(const uicreate::GameString* name) {
    if (!name || !name->data) return false;
    const std::string n(name->data, name->len);
    bool block = false;
    for (const std::string& b : g_blocked) block |= _stricmp(b.c_str(), n.c_str()) == 0;
    if (g_trace && g_log) {
        char line[200];
        sprintf_s(line, "Panel %s%s", n.c_str(), block ? " GESPERRT" : "");
        g_log(line);
    }
    return block;
}

__declspec(naked) inline void entryStub() {
    __asm {
        pushad                     // keep every register: callers may keep values in eax/edx across this call
        push dword ptr [esp + 36]  // name (first argument; 32 bytes pushad + return address)
        call blockCheck
        add esp, 4
        test al, al
        popad                      // leaves the flags alone
        jnz blocked
        sub esp, 0x24  // the 5 replaced bytes
        push ebx
        push ebp
        jmp g_back
    blocked:
        xor eax, eax
        ret 0xc
    }
}

inline bool enable(char* err, size_t errSize) {
    static bool done = false;
    if (done) return true;
    BYTE* f = find(GetModuleHandleA("empire.retail.dll"), err, errSize);
    if (!f) return false;
    g_back = f + 5;
    BYTE jmp[5] = {0xE9};
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<BYTE*>(&entryStub) - (f + 5));
    std::memcpy(jmp + 1, &rel, 4);
    DWORD old;
    if (!VirtualProtect(f, 5, PAGE_EXECUTE_READWRITE, &old)) return sprintf_s(err, errSize, "Code nicht beschreibbar"), false;
    std::memcpy(f, jmp, 5);
    VirtualProtect(f, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), f, 5);
    done = true;
    return true;
}
#endif

}  // namespace panelhook
