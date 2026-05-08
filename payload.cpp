#include <windows.h>
#include <cmath>
#include <d3d9.h>
#include <stdint.h>
#include <psapi.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "psapi.lib")

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

bool IsValidRead(uintptr_t ptr) {
    if (!ptr) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((LPCVOID)ptr, &mbi, sizeof(mbi))) {
        return (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) && !(mbi.Protect & PAGE_GUARD));
    }
    return false;
}

template<typename T> T Read(uintptr_t addr) {
    return IsValidRead(addr) ? *(T*)addr : T();
}

template<typename T> void Write(uintptr_t addr, T val) {
    if (IsValidRead(addr)) *(T*)addr = val;
}

HRESULT __stdcall HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    static bool keyState = false;
    // Проверка нажатия END для активации/деактивации
    if (GetAsyncKeyState(VK_END) & 0x8000) {
        if (!keyState) {
            g_BotActive = !g_BotActive;
            keyState = true;
            if (g_BotActive) Beep(750, 200); else Beep(300, 200);
        }
    } else {
        keyState = false;
    }

    if (g_BotActive) {
        uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
        uintptr_t conn = Read<uintptr_t>(base + OFFSET_S_CUR_MGR);
        if (conn) {
            uintptr_t mgr = Read<uintptr_t>(conn + OFFSET_OBJECT_MANAGER);
            uintptr_t pLocal = ((tGetPlayer)ADDR_GET_PLAYER)();
            
            if (mgr && pLocal && Read<int>(pLocal + 0x14) == 4) {
                float myX = Read<float>(pLocal + 0x798), myY = Read<float>(pLocal + 0x79C);
                uintptr_t pDesc = Read<uintptr_t>(pLocal + 0x8);
                
                uint64_t bestGuid = 0; float bestDist = 40.0f; uintptr_t bestObj = 0;
                uintptr_t cur = Read<uintptr_t>(mgr + 0xAC);
                
                // Поиск ближайшей живой цели
                while (cur && (cur & 1) == 0) {
                    if (Read<int>(cur + 0x14) == 3) { // Unit
                        uintptr_t desc = Read<uintptr_t>(cur + 0x8);
                        if (Read<int>(desc + 0x60) > 0 && Read<uint64_t>(desc + 0x38) == 0) {
                            float dx = Read<float>(cur + 0x798) - myX, dy = Read<float>(cur + 0x79C) - myY;
                            float dist = sqrt(dx*dx + dy*dy);
                            if (dist < bestDist) { bestDist = dist; bestGuid = Read<uint64_t>(cur + 0x30); bestObj = cur; }
                        }
                    }
                    cur = Read<uintptr_t>(cur + 0x3C);
                }
                
                static DWORD lastAction = 0;
                if (bestGuid) {
                    float tx = Read<float>(bestObj + 0x798), ty = Read<float>(bestObj + 0x79C), tz = Read<float>(bestObj + 0x7A0);
                    
                    // Жесткая установка таргета в памяти, чтобы Lua видел 'target'
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
                            // Поворот к цели
                            float dy = ty - myY, dx = tx - myX;
                            Write<float>(pLocal + 0x7A8, atan2(dy, dx) < 0 ? atan2(dy, dx) + 6.283185f : atan2(dy, dx));
                            
                            // Атака через Lua (безопасно для UI)
                            ((tFrameScript_Execute)ADDR_LUA_EXECUTE)("InteractUnit('target')", "Bot", NULL);
                            lastAction = GetTickCount();
                        }
                    }
                } else {
                    // Если цели нет, проверяем трупы рядом для лута
                    if (GetTickCount() - lastAction > 2000) {
                        cur = Read<uintptr_t>(mgr + 0xAC);
                        while (cur && (cur & 1) == 0) {
                            if (Read<int>(cur + 0x14) == 3) {
                                uintptr_t desc = Read<uintptr_t>(cur + 0x8);
                                if (Read<int>(desc + 0x60) == 0) { // Труп
                                    float dx = Read<float>(cur + 0x798) - myX, dy = Read<float>(cur + 0x79C) - myY;
                                    if (sqrt(dx*dx + dy*dy) < 5.0f) {
                                        uint64_t deadGuid = Read<uint64_t>(cur + 0x30);
                                        Write<uint64_t>(pDesc + 0x48, deadGuid);
                                        Write<uint64_t>(ADDR_TARGET_GUID, deadGuid);
                                        ((tFrameScript_Execute)ADDR_LUA_EXECUTE)("InteractUnit('target')", "Loot", NULL);
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
        }
    }
    return oEndScene(pDevice);
}

DWORD WINAPI InitThread(LPVOID lpParam) {
    Sleep(10000); // Ожидание инициализации игры
    uintptr_t* pDevicePtr = (uintptr_t*)ADDR_D3D9_DEVICE;
    if (IsValidRead((uintptr_t)pDevicePtr) && *pDevicePtr) {
        uintptr_t* vTable = *(uintptr_t**)*pDevicePtr;
        if (IsValidRead((uintptr_t)vTable)) {
            HMODULE hD3D9 = GetModuleHandleA("d3d9.dll");
            MODULEINFO mi;
            if (hD3D9 && GetModuleInformation(GetCurrentProcess(), hD3D9, &mi, sizeof(mi))) {
                DWORD old;
                VirtualProtect(&vTable[42], 4, PAGE_EXECUTE_READWRITE, &old);
                oEndScene = (tEndScene)vTable[42];
                vTable[42] = (uintptr_t)HookedEndScene;
                VirtualProtect(&vTable[42], 4, old, &old);
                Beep(1000, 300); // СИГНАЛ УСПЕШНОГО ХУКА
            }
        }
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,InitThread,h,0,0); }
    return TRUE;
}
