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
#define ADDR_TARGET_GUID         0x00BD07B0 
#define ADDR_CLICK_TO_MOVE       0x00611130
#define ADDR_GET_PLAYER          0x004038BE
#define ADDR_D3D9_DEVICE         0x00C5DF88
#define ADDR_LUA_EXECUTE         0x00819210

typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetPlayer)();
typedef HRESULT(__stdcall* tEndScene)(LPDIRECT3DDEVICE9 pDevice);
typedef void(__cdecl* tFrameScript_Execute)(const char* script, const char* name, void* state);

tEndScene oEndScene = nullptr;
bool g_BotActive = false;
uintptr_t g_BaseAddress = 0; // Кэшируем базу модуля

// Убираем убогий VirtualQuery из горячего цикла, доверяем валидности указателей игры
template<typename T> T Read(uintptr_t addr) {
    if (!addr) return T();
    return *(T*)addr;
}

template<typename T> void Write(uintptr_t addr, T val) {
    if (addr) *(T*)addr = val;
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

    // Вывод логов из твоего старого main.h теперь привязан к нормальным событиям, а не к бесконечному циклу
    static DWORD lastLogTime = 0;
    if (pLocal && GetTickCount() - lastLogTime > 1000) {
        float x = Read<float>(pLocal + 0x798);
        float y = Read<float>(pLocal + 0x79C);
        float z = Read<float>(pLocal + 0x7A0);
        printf("My Pos: X: %.2f | Y: %.2f | Z: %.2f | Bot Active: %d\r", x, y, z, g_BotActive);
        lastLogTime = GetTickCount();
    }

    // Тот самый тестовый мувмент, который был в мертвом main.h
    if (GetAsyncKeyState(VK_F1) & 0x8000) {
        if (!keyStateF1 && pLocal) {
            float x = Read<float>(pLocal + 0x798);
            float y = Read<float>(pLocal + 0x79C);
            float z = Read<float>(pLocal + 0x7A0);
            float pos[3] = { x + 5.0f, y, z };
            uint64_t zeroGuid = 0;
            std::cout << "\n[!] Движение по тестовым координатам..." << std::endl;
            ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, 4, &zeroGuid, pos, 0.5f);
            keyStateF1 = true;
        }
    } else {
        keyStateF1 = false;
    }

    if (g_BotActive && mgr && pLocal && Read<int>(pLocal + 0x14) == 4) {
        float myX = Read<float>(pLocal + 0x798), myY = Read<float>(pLocal + 0x79C);
        uintptr_t pDesc = Read<uintptr_t>(pLocal + 0x8);
        
        uint64_t bestGuid = 0; 
        float bestDist = 40.0f; 
        uintptr_t bestObj = 0;
        uintptr_t cur = Read<uintptr_t>(mgr + 0xAC);
        
        while (cur && (cur & 1) == 0) {
            if (Read<int>(cur + 0x14) == 3) { // Unit
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
            
            Write<uint64_t>(pDesc + 0x48, bestGuid);
            Write<uint64_t>(ADDR_TARGET_GUID, bestGuid);

            if (bestDist > 4.5f) {
                if (GetTickCount() - lastAction > 300) {
                    float pos[3] = { tx, ty, tz };
                    ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, 4, &bestGuid, pos, 0.5f);
                    lastAction = GetTickCount();
                }
            } else {
                if (GetTickCount() - lastAction > 1500) {
                    float dy = ty - myY, dx = tx - myX;
                    float angle = atan2(dy, dx);
                    if (angle < 0) angle += 6.283185f;
                    Write<float>(pLocal + 0x7A8, angle);
                    
                    ((tFrameScript_Execute)ADDR_LUA_EXECUTE)("InteractUnit('target')", "Bot", NULL);
                    lastAction = GetTickCount();
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
    g_BaseAddress = (uintptr_t)GetModuleHandleA(NULL); // Кэшируем один раз при старте

    // Вместо тупого слипа в 10 секунд адекватно ждем появления модуля директикса
    while (!GetModuleHandleA("d3d9.dll")) {
        Sleep(100);
    }
    Sleep(2000); // Даем игре немного времени завершить инициализацию D3D окна

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
