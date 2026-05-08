#include <windows.h>
#include <cmath>
#include <d3d9.h>
#include <stdint.h>
#include <iostream>

#pragma comment(lib, "d3d9.lib")

// --- СТАТИЧНЫЕ АДРЕСА 3.3.5a (12340) ---
#define ADDR_S_CUR_MGR          0x00879CE0
#define OFFSET_OBJECT_MANAGER   0x2ED0
#define ADDR_CLICK_TO_MOVE      0x00611130
#define ADDR_GET_PLAYER         0x004038BE
#define ADDR_D3D9_DEVICE        0x00C5DF88
#define ADDR_IS_IN_GAME         0x00BD0792 // 1 - в мире, 0 - в меню

// Отключаем проверки стека и безопасности, которые конфликтуют с Manual Map
#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

typedef void(__fastcall* tClickToMove)(uintptr_t ecx, void* edx, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetPlayer)();
typedef HRESULT(__stdcall* tEndScene)(LPDIRECT3DDEVICE9 pDevice);

tEndScene oEndScene = nullptr;
bool g_BotActive = false;

// Безопасный вызов CTM
void SafeMove(uintptr_t pLocal, float x, float y, float z) {
    static uint64_t s_zeroGuid = 0;
    static float s_pos[3];
    if (!pLocal) return;
    
    s_pos[0] = x; s_pos[1] = y; s_pos[2] = z;
    
    // Используем тип 3 (Movement) — он никогда не вызывает Lua и не крашит игру
    auto fnCTM = (tClickToMove)ADDR_CLICK_TO_MOVE;
    fnCTM(pLocal, nullptr, 3, &s_zeroGuid, s_pos, 0.5f);
}

HRESULT __stdcall HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    // Проверка кнопки активации
    if (GetAsyncKeyState(VK_END) & 0x8000) {
        static bool keyState = false;
        if (!keyState) {
            g_BotActive = !g_BotActive;
            keyState = true;
            Beep(g_BotActive ? 800 : 400, 100);
            printf("\n[!] Bot %s\n", g_BotActive ? "ENABLED" : "DISABLED");
        }
    } else {
        static bool keyState = false;
        keyState = false;
    }

    if (g_BotActive && *(BYTE*)ADDR_IS_IN_GAME) {
        uintptr_t conn = *(uintptr_t*)ADDR_S_CUR_MGR;
        uintptr_t mgr = conn ? *(uintptr_t*)(conn + OFFSET_OBJECT_MANAGER) : 0;
        uintptr_t pLocal = ((tGetPlayer)ADDR_GET_PLAYER)();

        if (mgr && pLocal) {
            float myX = *(float*)(pLocal + 0x798);
            float myY = *(float*)(pLocal + 0x79C);
            
            uint64_t bestGuid = 0; 
            float bestDist = 30.0f; 
            float tPos[3] = {0, 0, 0};
            
            uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
            while (cur && (cur & 1) == 0) {
                if (*(int*)(cur + 0x14) == 3) { // Unit
                    uintptr_t desc = *(uintptr_t*)(cur + 0x8);
                    if (desc && *(int*)(desc + 0x60) > 0) { // Живой
                        float dx = *(float*)(cur + 0x798) - myX;
                        float dy = *(float*)(cur + 0x79C) - myY;
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

            static DWORD lastMove = 0;
            if (bestGuid && GetTickCount() - lastMove > 500) {
                printf("Tracking: %llu | Dist: %.1f\r", bestGuid, bestDist);
                // Только бежим к цели, не пытаемся «взаимодействовать», чтобы не злить Lua-движок
                SafeMove(pLocal, tPos[0], tPos[1], tPos[2]);
                lastMove = GetTickCount();
            }
        }
    }
    return oEndScene(pDevice);
}

void Setup() {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    printf("--- Ultra Stable Bot Core ---\n");
    
    // Ждем девайс
    while (!*(uintptr_t*)ADDR_D3D9_DEVICE) Sleep(100);
    
    uintptr_t* vTable = *(uintptr_t**)*(uintptr_t*)ADDR_D3D9_DEVICE;
    if (vTable) {
        DWORD old;
        VirtualProtect(&vTable[42], 4, PAGE_EXECUTE_READWRITE, &old);
        oEndScene = (tEndScene)vTable[42];
        vTable[42] = (uintptr_t)HookedEndScene;
        VirtualProtect(&vTable[42], 4, old, &old);
        printf("[+] Hooked EndScene. Press END when in world.\n");
    }
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Setup, NULL, 0, NULL);
    }
    return TRUE;
}
