#include <windows.h>
#include <cmath>
#include <d3d9.h>
#include <stdint.h>
#include <iostream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "d3d9.lib")

// --- СТАТИКА ДЛЯ 3.3.5a (12340) ---
#define ADDR_PLAYER_BASE         0x00BD07E0 // Твой любимый статический указатель
#define ADDR_CLICK_TO_MOVE       0x00611130
#define ADDR_D3D9_DEVICE         0x00C5DF88
#define OFFSET_S_CUR_MGR         0x00879CE0
#define OFFSET_OBJECT_MANAGER    0x2ED0

typedef HRESULT(__stdcall* tEndScene)(LPDIRECT3DDEVICE9 pDevice);
tEndScene oEndScene = nullptr;
bool g_BotActive = false;

// Прямой вызов через ASM, чтобы исключить любые ошибки соглашения о вызовах (Calling Convention)
void __stdcall DirectCTM(int clickType, uint64_t* guid, float* pos) {
    uintptr_t pLocal = *(uintptr_t*)ADDR_PLAYER_BASE;
    if (!pLocal) return;

    __asm {
        push 0x3F000000 // precision (0.5f)
        push pos        // координаты [x, y, z]
        push guid       // указатель на GUID
        push clickType  // тип клика
        mov ecx, pLocal // 'this' в ECX
        mov eax, ADDR_CLICK_TO_MOVE
        call eax
    }
}

HRESULT __stdcall HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    if (GetAsyncKeyState(VK_END) & 0x8000) {
        static bool keyState = false;
        if (!keyState) { g_BotActive = !g_BotActive; keyState = true; Beep(g_BotActive ? 800 : 400, 100); }
    } else { static bool keyState = false; keyState = false; }

    if (g_BotActive) {
        uintptr_t mgr = *(uintptr_t*)(*(uintptr_t*)((uintptr_t)GetModuleHandle(NULL) + OFFSET_S_CUR_MGR) + OFFSET_OBJECT_MANAGER);
        uintptr_t pLocal = *(uintptr_t*)ADDR_PLAYER_BASE;

        if (mgr && pLocal) {
            float myX = *(float*)(pLocal + 0x798), myY = *(float*)(pLocal + 0x79C);
            uint64_t bestGuid = 0; float bestDist = 40.0f; float tPos[3] = {0};
            uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);

            while (cur && (cur & 1) == 0) {
                if (*(int*)(cur + 0x14) == 3) { // Unit
                    uintptr_t desc = *(uintptr_t*)(cur + 0x8);
                    if (desc && *(int*)(desc + 0x60) > 0) { // Живой
                        float dx = *(float*)(cur + 0x798) - myX, dy = *(float*)(cur + 0x79C) - myY;
                        float dist = sqrt(dx*dx + dy*dy);
                        if (dist < bestDist) {
                            bestDist = dist;
                            bestGuid = *(uint64_t*)(cur + 0x30);
                            tPos[0] = *(float*)(cur + 0x798);
                            tPos[1] = *(float*)(cur + 0x79C);
                            tPos[2] = *(float*)(cur + 0x7A0);
                        }
                    }
                }
                cur = *(uintptr_t*)(cur + 0x3C);
            }

            static DWORD lastAction = 0;
            if (bestGuid && GetTickCount() - lastAction > 500) {
                DirectCTM(4, &bestGuid, tPos); // Взаимодействие
                lastAction = GetTickCount();
            }
        }
    }
    return oEndScene(pDevice);
}

DWORD WINAPI Init(LPVOID) {
    uintptr_t* vTable = *(uintptr_t**)*(uintptr_t*)ADDR_D3D9_DEVICE;
    if (vTable) {
        DWORD old;
        VirtualProtect(&vTable[42], 4, PAGE_EXECUTE_READWRITE, &old);
        oEndScene = (tEndScene)vTable[42];
        vTable[42] = (uintptr_t)HookedEndScene;
        VirtualProtect(&vTable[42], 4, old, &old);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) CreateThread(0,0,Init,0,0,0);
    return TRUE;
}
