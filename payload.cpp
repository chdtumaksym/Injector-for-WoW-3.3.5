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
#define ADDR_D3D9_DEVICE         0x00C5DF88 // Указатель на IDirect3DDevice9 в 3.3.5a

typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetActivePlayer)();
typedef HRESULT(__stdcall* tEndScene)(LPDIRECT3DDEVICE9 pDevice);

tClickToMove EngineClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;
tGetActivePlayer GetPlayer = (tGetActivePlayer)ADDR_GET_PLAYER;
tEndScene oEndScene = nullptr;

struct BotCommand {
    volatile int actionType; 
    volatile uint64_t guid;
    volatile float x, y, z;
} g_Cmd;

template <typename T>
T SafeRead(uintptr_t address) {
    T buffer = T();
    if (!address) return buffer;
    __try { buffer = *(T*)address; } __except(1) {}
    return buffer;
}

template <typename T>
void SafeWrite(uintptr_t address, T value) {
    if (!address) return;
    __try { *(T*)address = value; } __except(1) {}
}

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

// Наш код выполняется прямо перед отрисовкой кадра, когда контекст памяти идеален
HRESULT __stdcall HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    int currentAction = g_Cmd.actionType;
    if (currentAction != 0) {
        uintptr_t pLocal = GetPlayer();
        if (pLocal && SafeRead<int>(pLocal + 0x14) == 4) {
            __try {
                float pos[3] = { g_Cmd.x, g_Cmd.y, g_Cmd.z };
                EngineClickToMove(pLocal, currentAction, (uint64_t*)&g_Cmd.guid, pos, 0.5f);
            } __except(1) {}
        }
        g_Cmd.actionType = 0;
    }
    return oEndScene(pDevice);
}

bool InstallDXHook() {
    uintptr_t* pDevicePtr = (uintptr_t*)ADDR_D3D9_DEVICE;
    if (!pDevicePtr) return false;
    
    uintptr_t pDevice = *pDevicePtr;
    if (!pDevice) return false;

    uintptr_t* vTable = *(uintptr_t**)pDevice;
    if (!vTable) return false;

    // В DirectX 9 функция EndScene всегда находится под индексом 42 в таблице виртуальных методов
    oEndScene = (tEndScene)vTable[42];

    DWORD oldProtect;
    VirtualProtect(&vTable[42], sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect);
    vTable[42] = (uintptr_t)HookedEndScene;
    VirtualProtect(&vTable[42], sizeof(uintptr_t), oldProtect, &oldProtect);

    return true;
}

void SendCTMCommand(int actionType, uint64_t guid, float x, float y, float z) {
    g_Cmd.guid = guid; g_Cmd.x = x; g_Cmd.y = y; g_Cmd.z = z; g_Cmd.actionType = actionType;
    // Ожидаем пока поток DirectX проглотит команду
    for (int i = 0; i < 50 && g_Cmd.actionType != 0; i++) Sleep(5);
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    setlocale(LC_ALL, "Russian");
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin DX9 Render Injector v42.0 ---\n");
    // Ждем инициализации D3D9 движком игры
    while (SafeRead<uintptr_t>(ADDR_D3D9_DEVICE) == 0) Sleep(100);

    if (InstallDXHook()) printf("[+] DirectX 9 EndScene хук установлен. TLS контекст безопасен.\n");
    else { printf("[-] ОШИБКА: Не удалось получить доступ к D3D9 Device.\n"); return 0; }
    printf("--------------------------------------------------\n");

    uintptr_t connAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    int state = 0; 
    uint64_t activeGuid = 0;
    DWORD lastActionTime = 0;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t conn = SafeRead<uintptr_t>(connAddr);
        SetConsoleCursor(0, 4);
        if (!conn) { printf("[!] Ожидание подключения к миру... \n"); Sleep(500); continue; }

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
                        float dist = sqrt(pow(tx-myX, 2) + pow(ty-myY, 2));

                        if (thp > 0) {
                            if (dist > 4.5f) { 
                                state = 1; 
                                SendCTMCommand(4, activeGuid, tx, ty, tz); 
                            } else { 
                                state = 2; 
                                if (GetTickCount() - lastActionTime > 1500) {
                                    SendCTMCommand(11, activeGuid, tx, ty, tz); 
                                    lastActionTime = GetTickCount();
                                }
                            }
                        } else {
                            state = 3; 
                            printf("[ЛОГ] Цель мертва, собираем лут из потока D3D... \n");
                            Sleep(1000);
                            SendCTMCommand(6, activeGuid, tx, ty, tz); 
                            Sleep(2000); 
                            activeGuid = 0; state = 0;
                        }
                    } else { activeGuid = 0; state = 0; }
                }
                const char* sNames[] = { "ПОИСК", "БЕГ", "АТАКА", "СБОР ЛУТА" };
                printf("[СТАТУС] Текущее действие: %-15s \n", sNames[state]);
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
