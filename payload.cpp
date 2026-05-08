#include <windows.h>
#include <cmath>
#include <d3d9.h>
#include <stdint.h>
#include <iostream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "d3d9.lib")

// --- СТАТИЧНЫЕ ОФФСЕТЫ 3.3.5a (12340) ---
#define OFFSET_S_CUR_MGR         0x00879CE0
#define OFFSET_OBJECT_MANAGER    0x2ED0
#define ADDR_CLICK_TO_MOVE       0x00611130
#define ADDR_GET_PLAYER          0x004038BE
#define ADDR_D3D9_DEVICE         0x00C5DF88

// __fastcall с фейковым EDX — единственный способ 100% безопасно вызвать __thiscall из своей DLL
typedef void(__fastcall* tClickToMove)(uintptr_t ecx, void* edx, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetPlayer)();
typedef HRESULT(__stdcall* tEndScene)(LPDIRECT3DDEVICE9 pDevice);

tEndScene oEndScene = nullptr;
bool g_BotActive = false;
uintptr_t g_BaseAddress = 0;

template<typename T> T Read(uintptr_t addr) {
    if (!addr) return T();
    return *(T*)addr;
}

// БЕЗОПАСНЫЙ ВЫЗОВ CTM: Статические переменные спасают от висячих указателей на стеке!
void SafeCTM(uintptr_t pLocal, int clickType, uint64_t guid, float x, float y, float z) {
    static uint64_t s_guid = 0;
    static float s_pos[3] = { 0, 0, 0 };
    s_guid = guid;
    s_pos[0] = x; s_pos[1] = y; s_pos[2] = z;
    ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, nullptr, clickType, &s_guid, s_pos, 0.5f);
}

HRESULT __stdcall HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    static bool keyStateEnd = false;
    static bool keyStateF1 = false;
    
    if (GetAsyncKeyState(VK_END) & 0x8000) {
        if (!keyStateEnd) {
            g_BotActive = !g_BotActive;
            keyStateEnd = true;
            if (g_BotActive) Beep(750, 200); else Beep(300, 200);
        }
    } else {
        keyStateEnd = false;
    }

    uintptr_t conn = Read<uintptr_t>(g_BaseAddress + OFFSET_S_CUR_MGR);
    uintptr_t mgr = conn ? Read<uintptr_t>(conn + OFFSET_OBJECT_MANAGER) : 0;
    uintptr_t pLocal = ((tGetPlayer)ADDR_GET_PLAYER)();

    static DWORD lastLogTime = 0;
    if (pLocal && GetTickCount() - lastLogTime > 1000) {
        float x = Read<float>(pLocal + 0x798);
        float y = Read<float>(pLocal + 0x79C);
        float z = Read<float>(pLocal + 0x7A0);
        printf("My Pos: X: %.2f | Y: %.2f | Z: %.2f | Bot Active: %d\r", x, y, z, g_BotActive);
        lastLogTime = GetTickCount();
    }

    if (GetAsyncKeyState(VK_F1) & 0x8000) {
        if (!keyStateF1 && pLocal) {
            float x = Read<float>(pLocal + 0x798);
            float y = Read<float>(pLocal + 0x79C);
            float z = Read<float>(pLocal + 0x7A0);
            std::cout << "\n[!] Движение по тестовым координатам (CTM 3)..." << std::endl;
            // Экшен 3 = Просто движение в точку. Безопасно вызывается с нулевым GUID.
            SafeCTM(pLocal, 3, 0, x + 5.0f, y, z);
            keyStateF1 = true;
        }
    } else {
        keyStateF1 = false;
    }

    if (g_BotActive && mgr && pLocal && Read<int>(pLocal + 0x14) == 4) {
        float myX = Read<float>(pLocal + 0x798), myY = Read<float>(pLocal + 0x79C);
        
        uint64_t bestGuid = 0; 
        float bestDist = 40.0f; 
        uintptr_t bestObj = 0;
        uintptr_t cur = Read<uintptr_t>(mgr + 0xAC);
        
        while (cur && (cur & 1) == 0) {
            if (Read<int>(cur + 0x14) == 3) { 
                uintptr_t desc = Read<uintptr_t>(cur + 0x8);
                if (desc && Read<int>(desc + 0x60) > 0 && Read<uint64_t>(desc + 0x38) == 0) {
                    float dx = Read<float>(cur + 0x798) - myX;
                    float dy = Read<float>(cur + 0x79C) - myY;
                    float dist = sqrt(dx*dx + dy*dy);
                    if (dist < bestDist) { 
                        bestDist = dist; 
                        bestGuid = Read<uint64_t>(cur + 0x30); 
                        bestObj = cur; 
                    }
                }
            }
            cur = Read<uintptr_t>(cur + 0x3C);
        }
        
        static DWORD lastAction = 0;
        if (bestGuid && bestObj) {
            float tx = Read<float>(bestObj + 0x798), ty = Read<float>(bestObj + 0x79C), tz = Read<float>(bestObj + 0x7A0);
            
            if (GetTickCount() - lastAction > 500) {
                // Экшен 4 = Взаимодействие (игра сама бежит и бьет врага, если он враждебен)
                SafeCTM(pLocal, 4, bestGuid, tx, ty, tz);
                lastAction = GetTickCount();
            }
        } else {
            if (GetTickCount() - lastAction > 2000) {
                cur = Read<uintptr_t>(mgr + 0xAC);
                while (cur && (cur & 1) == 0) {
                    if (Read<int>(cur + 0x14) == 3) {
                        uintptr_t desc = Read<uintptr_t>(cur + 0x8);
                        if (desc && Read<int>(desc + 0x60) == 0) { 
                            float dx = Read<float>(cur + 0x798) - myX, dy = Read<float>(cur + 0x79C) - myY;
                            if (sqrt(dx*dx + dy*dy) < 5.0f) {
                                uint64_t deadGuid = Read<uint64_t>(cur + 0x30);
                                float tx = Read<float>(cur + 0x798), ty = Read<float>(cur + 0x79C), tz = Read<float>(cur + 0x7A0);
                                
                                // Тот же Экшен 4 идеально работает для сбора лута с трупов
                                SafeCTM(pLocal, 4, deadGuid, tx, ty, tz);
                                lastAction = GetTickCount();
                                break;
                            }
                        }
                    }
                    cur = Read<uintptr_t>(cur + 0x3C);
                }
            }
        }
    }
    return oEndScene(pDevice);
}

void SetupConsole() {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    std::cout << "--- Ремастер Бота: Загружен Успешно ---" << std::endl;
}

DWORD WINAPI InitThread(LPVOID lpParam) {
    SetupConsole();
    g_BaseAddress = (uintptr_t)GetModuleHandleA(NULL); 

    while (!GetModuleHandleA("d3d9.dll")) {
        Sleep(100);
    }
    Sleep(2000); 

    uintptr_t* pDevicePtr = (uintptr_t*)ADDR_D3D9_DEVICE;
    if (pDevicePtr && *pDevicePtr) {
        uintptr_t* vTable = *(uintptr_t**)*pDevicePtr;
        if (vTable) {
            DWORD old;
            VirtualProtect(&vTable[42], 4, PAGE_EXECUTE_READWRITE, &old);
            oEndScene = (tEndScene)vTable[42];
            vTable[42] = (uintptr_t)HookedEndScene;
            VirtualProtect(&vTable[42], 4, old, &old);
            Beep(1000, 300); 
            std::cout << "[+] Хук EndScene успешно установлен!" << std::endl;
        }
    } else {
        std::cout << "[-] Провал. D3D9 девайс не найден по указанному оффсету." << std::endl;
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) { 
        DisableThreadLibraryCalls(h); 
        CreateThread(0, 0, InitThread, h, 0, 0); 
    }
    return TRUE;
}
