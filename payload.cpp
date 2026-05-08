#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <d3d9.h>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "user32.lib")

// --- СТАТИЧНЫЕ ОФФСЕТЫ 3.3.5a (12340) ---
#define OFFSET_S_CUR_MGR         0x00879CE0
#define OFFSET_OBJECT_MANAGER    0x2ED0
#define ADDR_TARGET_GUID         0x00BD07B0 

#define ADDR_CLICK_TO_MOVE       0x00611130
#define ADDR_INTERACT            0x00609390

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

// --- ФУНКЦИИ ДВИЖКА ---
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef void(__thiscall* tInteract)(uintptr_t playerPtr, uintptr_t unitPtr);

tClickToMove EngineClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;
tInteract EngineInteractUnit = (tInteract)ADDR_INTERACT;

// --- СИНХРОННАЯ ОЧЕРЕДЬ КОМАНД ---
struct BotCommand {
    volatile int type; // 0: None, 1: Move, 2: Interact (Attack/Loot)
    volatile uintptr_t pLocal;
    volatile uintptr_t pTarget;
    volatile uint64_t guid;
    volatile float x, y, z;
} g_Cmd;

void* g_EndSceneTrampoline = nullptr;
uintptr_t g_EndSceneAddr = 0;

template <typename T>
T SafeRead(uintptr_t address) {
    T buffer = T();
    if (address == 0) return buffer;
    __try { buffer = *(T*)address; } 
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return buffer;
}

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

// --- ГЛОБАЛЬНЫЙ ХУК D3D9.DLL ЧЕРЕЗ ТРАМПЛИН ---
typedef HRESULT(__stdcall* tEndScene)(IDirect3DDevice9*);

HRESULT __stdcall HookedEndScene(IDirect3DDevice9* pDevice) {
    int cmd = g_Cmd.type;
    if (cmd != 0 && g_Cmd.pLocal != 0) {
        __try {
            if (cmd == 1) { 
                float pos[3] = { g_Cmd.x, g_Cmd.y, g_Cmd.z };
                EngineClickToMove(g_Cmd.pLocal, 4, (uint64_t*)&g_Cmd.guid, pos, 0.5f);
            } 
            else if (cmd == 2 && g_Cmd.pTarget != 0) { 
                EngineInteractUnit(g_Cmd.pLocal, g_Cmd.pTarget); 
            }
        } __except(1) {}
        g_Cmd.type = 0; 
    }
    // Вызываем оригинальный EndScene через трамплин (стек остается целым)
    return ((tEndScene)g_EndSceneTrampoline)(pDevice);
}

bool InstallGlobalTrampoline() {
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return false;
    HWND dummyWindow = CreateWindowA("STATIC", "Dummy", WS_OVERLAPPED, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    D3DPRESENT_PARAMETERS d3dpp = {0}; d3dpp.Windowed = TRUE; d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD; d3dpp.hDeviceWindow = dummyWindow;
    IDirect3DDevice9* pDev = nullptr;
    
    if (FAILED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, dummyWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDev))) {
        pD3D->Release(); DestroyWindow(dummyWindow); return false;
    }
    
    uintptr_t* pVTable = *(uintptr_t**)pDev;
    g_EndSceneAddr = pVTable[42]; // Получаем реальный адрес d3d9.dll!EndScene
    
    // Создаем трамплин
    g_EndSceneTrampoline = VirtualAlloc(NULL, 10, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_EndSceneTrampoline) { pDev->Release(); pD3D->Release(); DestroyWindow(dummyWindow); return false; }
    
    memcpy(g_EndSceneTrampoline, (void*)g_EndSceneAddr, 5); // Копируем 5 оригинальных байт
    *(BYTE*)((uintptr_t)g_EndSceneTrampoline + 5) = 0xE9; // JMP обратно
    *(uintptr_t*)((uintptr_t)g_EndSceneTrampoline + 6) = (g_EndSceneAddr + 5) - ((uintptr_t)g_EndSceneTrampoline + 5) - 5;
    
    // Ставим хук
    DWORD old;
    VirtualProtect((void*)g_EndSceneAddr, 5, PAGE_EXECUTE_READWRITE, &old);
    *(BYTE*)g_EndSceneAddr = 0xE9; // JMP на наш хук
    *(uintptr_t*)(g_EndSceneAddr + 1) = (uintptr_t)HookedEndScene - g_EndSceneAddr - 5;
    VirtualProtect((void*)g_EndSceneAddr, 5, old, &old);
    
    pDev->Release(); pD3D->Release(); DestroyWindow(dummyWindow);
    return true;
}

void RemoveGlobalTrampoline() {
    if (g_EndSceneTrampoline && g_EndSceneAddr) {
        DWORD old;
        VirtualProtect((void*)g_EndSceneAddr, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void*)g_EndSceneAddr, g_EndSceneTrampoline, 5); // Возвращаем байты на место
        VirtualProtect((void*)g_EndSceneAddr, 5, old, &old);
        VirtualFree(g_EndSceneTrampoline, 0, MEM_RELEASE);
    }
}

void SendCommandAndWait(int type, uintptr_t pLocal, uint64_t guid = 0, float x = 0, float y = 0, float z = 0, uintptr_t pTarget = 0) {
    g_Cmd.pLocal = pLocal; g_Cmd.guid = guid; g_Cmd.x = x; g_Cmd.y = y; g_Cmd.z = z; g_Cmd.pTarget = pTarget; g_Cmd.type = type;
    int timeout = 0;
    while (g_Cmd.type != 0 && timeout < 50) { // 500мс таймаут
        Sleep(10);
        timeout++;
    }
    if (g_Cmd.type != 0) g_Cmd.type = 0; 
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin Global Detour v28.0 (No Binds) ---\n");
    if (InstallGlobalTrampoline()) {
        printf("[+] D3D9.dll Global Hooked! Команды дойдут 100%%.\n");
    } else {
        printf("[-] Hook failed. Run as Administrator!\n");
        return 0;
    }
    printf("[!] ГАЛОЧКИ 'Перемещение по щелчку' И 'Автосбор' ДОЛЖНЫ БЫТЬ ВКЛЮЧЕНЫ!\n");
    printf("[!] Иначе движок игры заблокирует выполнение команд.\n");
    printf("--------------------------------------------------\n");

    uintptr_t connectionAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    BotState state = STATE_SEARCH;
    uint64_t activeTargetGuid = 0;
    DWORD stateStartTime = GetTickCount();
    DWORD lastActionTime = 0;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConn = SafeRead<uintptr_t>(connectionAddr);
        SetConsoleCursor(0, 7);

        if (!clientConn) {
            printf("[!] Ожидание подключения к миру...                           \n");
            Sleep(500);
            continue;
        }

        uintptr_t mgr = SafeRead<uintptr_t>(clientConn + OFFSET_OBJECT_MANAGER);
        if (mgr) {
            uintptr_t cur = SafeRead<uintptr_t>(mgr + 0xAC);
            uint64_t myGuid = SafeRead<uint64_t>(mgr + 0xC0);
            uintptr_t pLocal = 0;

            while (cur != 0 && (cur & 1) == 0) {
                if (SafeRead<uint64_t>(cur + 0x30) == myGuid && myGuid != 0) { pLocal = cur; break; }
                cur = SafeRead<uintptr_t>(cur + 0x3C);
            }

            if (pLocal) {
                uintptr_t pDesc = SafeRead<uintptr_t>(pLocal + 0x8);
                int hp = SafeRead<int>(pDesc + 0x60), maxHp = SafeRead<int>(pDesc + 0x80), lvl = SafeRead<int>(pDesc + 0xD8);
                float myX = SafeRead<float>(pLocal + 0x798), myY = SafeRead<float>(pLocal + 0x79C);

                printf("[PLAYER] HP: %-5d/%-5d | LVL: %d | POS: %.1f, %.1f      \n", hp, maxHp, lvl, myX, myY);
                printf("--------------------------------------------------\n");

                DWORD elapsed = GetTickCount() - stateStartTime;

                if (state == STATE_SEARCH) {
                    float bestDist = 45.0f; activeTargetGuid = 0;
                    cur = SafeRead<uintptr_t>(mgr + 0xAC);
                    
                    while (cur != 0 && (cur & 1) == 0) {
                        if (SafeRead<int>(cur + 0x14) == 3) { 
                            uintptr_t d = SafeRead<uintptr_t>(cur + 0x8);
                            int mHp = SafeRead<int>(d + 0x60), mLvl = SafeRead<int>(d + 0xD8);
                            uint64_t mSummoner = SafeRead<uint64_t>(d + 0x38);
                            
                            if (mHp > 0 && mLvl <= lvl + 2 && mSummoner == 0) {
                                float dx = SafeRead<float>(cur + 0x798) - myX, dy = SafeRead<float>(cur + 0x79C) - myY;
                                float dist = sqrt(dx*dx + dy*dy);
                                if (dist < bestDist) { bestDist = dist; activeTargetGuid = SafeRead<uint64_t>(cur + 0x30); }
                            }
                        }
                        cur = SafeRead<uintptr_t>(cur + 0x3C);
                    }
                    
                    if (activeTargetGuid != 0) { 
                        state = STATE_MOVE; 
                        stateStartTime = GetTickCount(); 
                    }
                }

                if (activeTargetGuid != 0) {
                    if (pDesc) *(uint64_t*)(pDesc + 0x48) = activeTargetGuid; 
                    *(uint64_t*)ADDR_TARGET_GUID = activeTargetGuid;

                    uintptr_t tObj = 0; cur = SafeRead<uintptr_t>(mgr + 0xAC);
                    while (cur != 0 && (cur & 1) == 0) { 
                        if(SafeRead<uint64_t>(cur+0x30) == activeTargetGuid) { tObj = cur; break; } 
                        cur = SafeRead<uintptr_t>(cur+0x3C); 
                    }
                    
                    if (tObj) {
                        uintptr_t td = SafeRead<uintptr_t>(tObj + 0x8);
                        int thp = SafeRead<int>(td + 0x60);
                        float tx = SafeRead<float>(tObj + 0x798), ty = SafeRead<float>(tObj + 0x79C), tz = SafeRead<float>(tObj + 0x7A0);
                        float dx = tx - myX, dy = ty - myY;
                        float dist = sqrt(dx*dx + dy*dy);
                        float cYaw = atan2(dy, dx); if (cYaw < 0) cYaw += 6.283185f;

                        printf("[TARGET] HP: %-5d | DIST: %.2f yds                  \n", thp, dist);
                        
                        if (thp > 0) {
                            *(float*)(pLocal + 0x7A8) = cYaw; 
                            
                            if (dist > 4.2f) {
                                if (state == STATE_MOVE && elapsed > 12000) {
                                    printf("[!] ЗАСТРЯЛ! Сбрасываю таргет...              \n");
                                    activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                                    continue;
                                }
                                if (state != STATE_MOVE) { state = STATE_MOVE; stateStartTime = GetTickCount(); }
                                
                                // Внутренний бег
                                SendCommandAndWait(1, pLocal, activeTargetGuid, tx, ty, tz, 0);
                            } else {
                                if (state == STATE_COMBAT && elapsed > 25000) {
                                    printf("[!] БОЙ ЗАБАГАЛСЯ! Сбрасываю...               \n");
                                    activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                                    continue;
                                }
                                if (state != STATE_COMBAT) { state = STATE_COMBAT; stateStartTime = GetTickCount(); }

                                // Внутренняя автоатака каждые 2 сек
                                if (GetTickCount() - lastActionTime > 2000) {
                                    SendCommandAndWait(2, pLocal, activeTargetGuid, 0, 0, 0, tObj);
                                    lastActionTime = GetTickCount();
                                }
                            }
                        } else {
                            if (state != STATE_LOOT) { state = STATE_LOOT; stateStartTime = GetTickCount(); }
                            
                            printf("[STATE] ЖДЕМ ГЕНЕРАЦИЮ ТРУПА СЕРВЕРОМ...         \n");
                            Sleep(1200); 
                            
                            printf("[STATE] NATIVE ЛУТ (АВТОСБОР)...                 \n");
                            SendCommandAndWait(2, pLocal, activeTargetGuid, 0, 0, 0, tObj);
                            
                            Sleep(2000); 
                            
                            if (pDesc) *(uint64_t*)(pDesc + 0x48) = 0; 
                            *(uint64_t*)ADDR_TARGET_GUID = 0; 
                            activeTargetGuid = 0; 
                            state = STATE_SEARCH;
                            stateStartTime = GetTickCount();
                        }
                    } else { 
                        activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                    }
                } else { 
                    printf("[TARGET] ПОИСК НОВОЙ ЦЕЛИ...                      \n");
                    stateStartTime = GetTickCount();
                }
                printf("[FSM] %-15s | GLOBAL DETOUR: ACTIVE    \n", stateNames[state]);
            }
        }
        Sleep(100);
    }

    RemoveGlobalTrampoline();
    fclose(f); FreeConsole(); FreeLibraryAndExitThread((HMODULE)lpParam, 0); return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,MainThread,h,0,0); }
    return TRUE;
}
