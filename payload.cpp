#include <windows.h>
#include <cmath>
#include <d3d9.h>
#include <stdint.h>
#include <iostream>

#pragma comment(lib, "d3d9.lib")

// --- СТАТИЧНЫЕ АДРЕСА 3.3.5a (12340) ---
// ВНИМАНИЕ: Эти адреса уже включают базу 0x400000.
#define ADDR_S_CUR_MGR          0x00879CE0
#define OFFSET_OBJECT_MANAGER   0x2ED0
#define ADDR_CLICK_TO_MOVE      0x00611130
#define ADDR_GET_PLAYER         0x004038BE
#define ADDR_D3D9_DEVICE        0x00C5DF88

#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

typedef void(__fastcall* tClickToMove)(uintptr_t ecx, void* edx, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetPlayer)();
typedef HRESULT(__stdcall* tEndScene)(LPDIRECT3DDEVICE9 pDevice);

tEndScene oEndScene = nullptr;
bool g_BotActive = false;
uintptr_t g_Base = 0;

// Корректно вычисляем адрес, учитывая возможную разницу в базе модуля
uintptr_t FixAddr(uintptr_t staticAddr) {
    return g_Base + (staticAddr - 0x400000);
}

struct CTM_DATA {
    uint64_t guid;
    float pos[3];
};
static CTM_DATA g_ctmData;

void SafeCTM(uintptr_t pLocal, int clickType, uint64_t guid, float x, float y, float z) {
    if (!pLocal) return;
    g_ctmData.guid = guid;
    g_ctmData.pos[0] = x; g_ctmData.pos[1] = y; g_ctmData.pos[2] = z;
    
    auto fnCTM = (tClickToMove)FixAddr(ADDR_CLICK_TO_MOVE);
    fnCTM(pLocal, nullptr, clickType, &g_ctmData.guid, g_ctmData.pos, 0.5f);
}

HRESULT __stdcall HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    static bool keyState = false;
    if (GetAsyncKeyState(VK_END) & 0x8000) {
        if (!keyState) {
            g_BotActive = !g_BotActive;
            keyState = true;
            Beep(g_BotActive ? 800 : 400, 100);
            printf("\n[!] Bot Status: %s\n", g_BotActive ? "ACTIVE" : "DISABLED");
        }
    } else keyState = false;

    if (g_BotActive) {
        // Исправлено: Читаем указатель по скорректированному адресу
        uintptr_t connAddr = FixAddr(ADDR_S_CUR_MGR);
        uintptr_t conn = (connAddr) ? *(uintptr_t*)connAddr : 0;
        uintptr_t mgr = conn ? *(uintptr_t*)(conn + OFFSET_OBJECT_MANAGER) : 0;
        
        auto fnGetPlayer = (tGetPlayer)FixAddr(ADDR_GET_PLAYER);
        uintptr_t pLocal = fnGetPlayer();

        if (mgr && pLocal) {
            float myX = *(float*)(pLocal + 0x798);
            float myY = *(float*)(pLocal + 0x79C);
            
            uint64_t bestGuid = 0; 
            float bestDist = 35.0f; 
            float tPos[3] = {0, 0, 0};
            
            uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
            // Безопасный обход списка объектов
            while (cur && (cur & 1) == 0) {
                if (*(int*)(cur + 0x14) == 3) { // Unit
                    uintptr_t desc = *(uintptr_t*)(cur + 0x8);
                    // Проверка здоровья и того, что это не наш GUID
                    if (desc && *(int*)(desc + 0x60) > 0) { 
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

            static DWORD lastAction = 0;
            if (bestGuid && GetTickCount() - lastAction > 800) {
                printf("Target: %llu | Distance: %.2f\r", bestGuid, bestDist);
                SafeCTM(pLocal, 4, bestGuid, tPos[0], tPos[1], tPos[2]);
                lastAction = GetTickCount();
            }
        }
    }
    return oEndScene(pDevice);
}

void Setup() {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    printf("--- Final Bot Core (FixAddr Edition) ---\n");
    printf("[+] Module Base: 0x%p\n", (void*)g_Base);
    
    uintptr_t deviceAddr = 0;
    uintptr_t staticDevicePtr = FixAddr(ADDR_D3D9_DEVICE);
    
    while (!(deviceAddr = *(uintptr_t*)staticDevicePtr)) Sleep(100);
    
    uintptr_t* vTable = *(uintptr_t**)deviceAddr;
    if (vTable) {
        DWORD old;
        VirtualProtect(&vTable[42], 4, PAGE_EXECUTE_READWRITE, &old);
        oEndScene = (tEndScene)vTable[42];
        vTable[42] = (uintptr_t)HookedEndScene;
        VirtualProtect(&vTable[42], 4, old, &old);
        printf("[+] D3D9 Hook Success. Bot is ready.\n");
    }
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        g_Base = (uintptr_t)GetModuleHandle(NULL);
        HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Setup, NULL, 0, NULL);
        if (hThread) CloseHandle(hThread);
    }
    return TRUE;
}
