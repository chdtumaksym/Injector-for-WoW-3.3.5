#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <clocale>
#include <stdint.h>
#include <d3d9.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "d3d9.lib")

#define OFFSET_S_CUR_MGR         0x00879CE0
#define OFFSET_OBJECT_MANAGER    0x2ED0
#define ADDR_TARGET_GUID         0x00BD07B0 
#define ADDR_CLICK_TO_MOVE       0x00611130
#define ADDR_GET_PLAYER          0x004038BE
#define ADDR_D3D9_DEVICE         0x00C5DF88
#define ADDR_LUA_EXECUTE         0x00819210

typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetActivePlayer)();
typedef HRESULT(__stdcall* tEndScene)(LPDIRECT3DDEVICE9 pDevice);
typedef void(__cdecl* tFrameScript_Execute)(const char* script, const char* name, void* state);

tClickToMove EngineClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;
tGetActivePlayer GetPlayer = (tGetActivePlayer)ADDR_GET_PLAYER;
tFrameScript_Execute LuaExecute = (tFrameScript_Execute)ADDR_LUA_EXECUTE;
tEndScene oEndScene = nullptr;

struct BotCommand {
    volatile int type;
    volatile uint64_t guid;
    volatile float x, y, z;
} g_Cmd;

// Замена SEH на VirtualQuery для совместимости с кривыми Manual Map инжекторами
bool IsValidPtr(uintptr_t address) {
    if (!address) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi))) {
        bool isCommit = (mbi.State == MEM_COMMIT);
        bool isReadable = (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE));
        bool isGuarded = (mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS);
        return isCommit && isReadable && !isGuarded;
    }
    return false;
}

template <typename T>
T SafeRead(uintptr_t address) {
    if (IsValidPtr(address)) {
        return *(T*)address;
    }
    return T();
}

template <typename T>
void SafeWrite(uintptr_t address, T value) {
    if (IsValidPtr(address)) {
        *(T*)address = value;
    }
}

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

HRESULT __stdcall HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    int cmd = g_Cmd.type;
    if (cmd != 0) {
        uintptr_t pLocal = GetPlayer();
        if (pLocal && SafeRead<int>(pLocal + 0x14) == 4) {
            if (cmd == 1) {
                float pos[3] = { g_Cmd.x, g_Cmd.y, g_Cmd.z };
                EngineClickToMove(pLocal, 4, (uint64_t*)&g_Cmd.guid, pos, 0.5f);
            } 
            else if (cmd == 2) {
                LuaExecute("InteractUnit('target')", "InternalBot", NULL);
            }
        }
        g_Cmd.type = 0;
    }
    return oEndScene(pDevice);
}

bool InstallDXHook() {
    uintptr_t* pDevicePtr = (uintptr_t*)ADDR_D3D9_DEVICE;
    if (!IsValidPtr((uintptr_t)pDevicePtr) || !*pDevicePtr) return false;
    
    uintptr_t* vTable = *(uintptr_t**)*pDevicePtr;
    if (!IsValidPtr((uintptr_t)vTable)) return false;

    oEndScene = (tEndScene)vTable[42];
    DWORD oldProtect;
    VirtualProtect(&vTable[42], sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect);
    vTable[42] = (uintptr_t)HookedEndScene;
    VirtualProtect(&vTable[42], sizeof(uintptr_t), oldProtect, &oldProtect);
    return true;
}

void SendActionAndWait(int type, uint64_t guid = 0, float x = 0, float y = 0, float z = 0) {
    g_Cmd.guid = guid; g_Cmd.x = x; g_Cmd.y = y; g_Cmd.z = z; g_Cmd.type = type;
    for (int i = 0; i < 50 && g_Cmd.type != 0; i++) Sleep(5);
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    setlocale(LC_ALL, "Russian");
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin DX9 VirtualQuery Edition v46.0 ---\n");
    printf("[+] Полный отказ от SEH. Защита от крашей Manual Map.\n");
    
    while (!IsValidPtr(ADDR_D3D9_DEVICE) || SafeRead<uintptr_t>(ADDR_D3D9_DEVICE) == 0) {
        Sleep(100);
    }
    
    if (InstallDXHook()) printf("[+] EndScene хук: ОК. Контекст потока идеален.\n");
    else { printf("[-] ОШИБКА: D3D9 Hook провален.\n"); return 0; }
    printf("[!] ВКЛЮЧИ АВТОСБОР (Auto Loot) В НАСТРОЙКАХ ИГРЫ!\n");
    printf("--------------------------------------------------\n");

    uintptr_t connAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    int state = 0; 
    uint64_t activeGuid = 0;
    DWORD lastCombatTick = 0;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t conn = SafeRead<uintptr_t>(connAddr);
        SetConsoleCursor(0, 6);
        if (!conn) { printf("[!] Ожидание загрузки мира...                  \n"); Sleep(500); continue; }

        uintptr_t mgr = SafeRead<uintptr_t>(conn + OFFSET_OBJECT_MANAGER);
        if (mgr) {
            uintptr_t cur = SafeRead<uintptr_t>(mgr + 0xAC);
            uint64_t myGuid = SafeRead<uint64_t>(mgr + 0xC0);
            uintptr_t pLocal = 0;

            while (cur && (cur & 1) == 0) {
                if (SafeRead<uint64_t>(cur + 0x30) == myGuid) { pLocal = cur; break; }
                cur = SafeRead<uintptr_t>(cur + 0x3C);
            }

            if (pLocal) {
                uintptr_t pDesc = SafeRead<uintptr_t>(pLocal + 0x8);
                int hp = SafeRead<int>(pDesc + 0x60), maxHp = SafeRead<int>(pDesc + 0x80);
                float myX = SafeRead<float>(pLocal + 0x798), myY = SafeRead<float>(pLocal + 0x79C);

                printf("[ИГРОК] HP: %d/%d | Координаты: %.1f, %.1f      \n", hp, maxHp, myX, myY);

                if (state == 0) {
                    float bestDist = 45.0f; activeGuid = 0;
                    cur = SafeRead<uintptr_t>(mgr + 0xAC);
                    while (cur && (cur & 1) == 0) {
                        if (SafeRead<int>(cur + 0x14) == 3) {
                            uintptr_t d = SafeRead<uintptr_t>(cur + 0x8);
                            if (SafeRead<int>(d + 0x60) > 0 && SafeRead<uint64_t>(d + 0x38) == 0) {
                                float dx = SafeRead<float>(cur + 0x798) - myX, dy = SafeRead<float>(cur + 0x79C) - myY;
                                float dist = sqrt(dx*dx + dy*dy);
                                if (dist < bestDist) { bestDist = dist; activeGuid = SafeRead<uint64_t>(cur + 0x30); }
                            }
                        }
                        cur = SafeRead<uintptr_t>(cur + 0x3C);
                    }
                    if (activeGuid) state = 1;
                }

                if (activeGuid) {
                    SafeWrite<uint64_t>(pDesc + 0x48, activeGuid);
                    SafeWrite<uint64_t>(ADDR_TARGET_GUID, activeGuid);

                    uintptr_t tObj = 0; cur = SafeRead<uintptr_t>(mgr + 0xAC);
                    while (cur && (cur & 1) == 0) { if(SafeRead<uint64_t>(cur+0x30)==activeGuid){tObj=cur;break;} cur=SafeRead<uintptr_t>(cur+0x3C); }

                    if (tObj) {
                        int thp = SafeRead<int>(SafeRead<uintptr_t>(tObj + 0x8) + 0x60);
                        float tx = SafeRead<float>(tObj + 0x798), ty = SafeRead<float>(tObj + 0x79C), tz = SafeRead<float>(tObj + 0x7A0);
                        float dx = tx - myX, dy = ty - myY;
                        float dist = sqrt(dx*dx + dy*dy);

                        if (thp > 0) {
                            SafeWrite<float>(pLocal + 0x7A8, atan2(dy, dx) < 0 ? atan2(dy, dx) + 6.283185f : atan2(dy, dx));
                            if (dist > 4.5f) { 
                                state = 1; SendActionAndWait(1, activeGuid, tx, ty, tz); 
                            } else { 
                                state = 2; 
                                if (GetTickCount() - lastCombatTick > 2000) {
                                    SendActionAndWait(2); 
                                    lastCombatTick = GetTickCount();
                                }
                            }
                        } else {
                            state = 3; 
                            printf("[ЛОГ] Цель убита. Лутаем через Lua...           \n");
                            Sleep(1000); SendActionAndWait(2); Sleep(2000); 
                            SafeWrite<uint64_t>(pDesc + 0x48, 0); SafeWrite<uint64_t>(ADDR_TARGET_GUID, 0);
                            activeGuid = 0; state = 0;
                        }
                    } else { activeGuid = 0; state = 0; }
                }
                const char* sNames[] = { "ПОИСК ЦЕЛИ", "БЕГ (ПРОГРАММНЫЙ)", "АТАКА (LUA)", "СБОР ЛУТА (LUA)" };
                printf("[СТАТУС] %-25s \n", sNames[state]);
            }
        }
        Sleep(100);
    }
    fclose(f); FreeConsole(); FreeLibraryAndExitThread((HMODULE)lpParam, 0); return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,MainThread,h,0,0); }
    return TRUE;
}
