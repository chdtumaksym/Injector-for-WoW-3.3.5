#include <windows.h>
#include <iostream>
#include <cmath>
#include <vector>

struct BotCommand {
    volatile int action;
    volatile uint64_t guid;
    volatile float x, y, z;
} g_Cmd;

template<typename T>
T SafeRead(uintptr_t addr) {
    T val{};
    __try { val = *(T*)addr; } __except(1) {}
    return val;
}

template<typename T>
void SafeWrite(uintptr_t addr, T val) {
    if (!addr) return;
    __try { *(T*)addr = val; } __except(1) {}
}

DWORD WINAPI BotThread(LPVOID lpParam) {
    uintptr_t baseAddr = (uintptr_t)GetModuleHandleA("WoW.exe");
    while (!SafeRead<uintptr_t>(baseAddr + 0x00879CE0)) Sleep(100); // ждём Object Manager

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t mgr = SafeRead<uintptr_t>(baseAddr + 0x00879CE0 + 0x2ED0);
        uintptr_t pLocal = 0;
        if (mgr) {
            uintptr_t cur = SafeRead<uintptr_t>(mgr + 0xAC);
            uint64_t myGuid = SafeRead<uint64_t>(mgr + 0xC0);
            while (cur && (cur & 1) == 0) {
                if (SafeRead<uint64_t>(cur + 0x30) == myGuid) { pLocal = cur; break; }
                cur = SafeRead<uintptr_t>(cur + 0x3C);
            }
        }
        if (pLocal) {
            // безопасная логика движения/атаки
            if (g_Cmd.action) {
                float pos[3] = { g_Cmd.x, g_Cmd.y, g_Cmd.z };
                uintptr_t pDesc = SafeRead<uintptr_t>(pLocal + 0x8);
                if (pDesc) SafeWrite<float>(pLocal + 0x798, pos[0]); // пример безопасного write
                g_Cmd.action = 0;
            }
        }
        Sleep(50);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        CreateThread(0, 0, BotThread, hInst, 0, 0);
    }
    return TRUE;
}
