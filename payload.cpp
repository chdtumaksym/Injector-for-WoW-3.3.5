#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <d3d9.h>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "user32.lib")

// --- ОФФСЕТЫ 3.3.5a (12340) ---
#define OFFSET_S_CUR_MGR         0x00879CE0
#define OFFSET_OBJECT_MANAGER    0x2ED0
#define ADDR_TARGET_GUID         0x00BD07B0 

#define ADDR_LUA_EXECUTE         0x00819210
#define ADDR_CLICK_TO_MOVE       0x00611130
#define ADDR_GET_PLAYER          0x004038BE

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

// --- ФУНКЦИИ ДВИЖКА ---
typedef void(__cdecl* tFrameScript_Execute)(const char* command, const char* filename, void* reserved);
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetActivePlayer)();

tFrameScript_Execute Lua_DoString = (tFrameScript_Execute)ADDR_LUA_EXECUTE;
tClickToMove ClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;
tGetActivePlayer GetPlayer = (tGetActivePlayer)ADDR_GET_PLAYER;

// --- СИНХРОННАЯ ОЧЕРЕДЬ ДЛЯ ИНТЕРНАЛА ---
struct BotCommand {
    volatile int type; // 0: None, 1: Move, 2: Attack, 3: Loot/Interact, 4: Heal
    volatile uint64_t guid;
    volatile float x, y, z;
} g_Cmd;

// --- БЕЗОПАСНОЕ ЧТЕНИЕ ПАМЯТИ ---
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

// --- VTABLE ХУК D3D9 ---
typedef HRESULT(__stdcall* tEndScene)(IDirect3DDevice9*);
tEndScene oEndScene = nullptr;

HRESULT __stdcall HookedEndScene(IDirect3DDevice9* pDevice) {
    if (!pDevice) return oEndScene(pDevice);

    int cmd = g_Cmd.type;
    if (cmd != 0) {
        uintptr_t pLocal = GetPlayer();
        if (pLocal && SafeRead<int>(pLocal + 0x14) == 4) {
            __try {
                // ЗВУКОВОЙ ИНДИКАТОР: Если хук дошел сюда, он работает.
                Beep(1500, 50); 

                if (cmd == 1) { 
                    float pos[3] = { g_Cmd.x, g_Cmd.y, g_Cmd.z };
                    ClickToMove(pLocal, 4, (uint64_t*)&g_Cmd.guid, pos, 0.5f);
                } 
                else if (cmd == 2) { 
                    Lua_DoString("if UnitExists('target') and not IsCurrentSpell(6603) then StartAttack() end", "bot", 0);
                    Lua_DoString("if not UnitBuff('player', 'Seal of Righteousness') then CastSpellByID(21084) end", "bot", 0);
                } 
                else if (cmd == 3) { 
                    char cmdBuf[128];
                    sprintf_s(cmdBuf, "TargetUnit('0x%llX') InteractUnit('target')", g_Cmd.guid);
                    Lua_DoString(cmdBuf, "bot", 0);
                    // Лутаем открытое окно
                    Lua_DoString("if LootFrame:IsVisible() then for i=1,GetNumLootItems() do LootSlot(i) end CloseLoot() end", "bot", 0);
                }
                else if (cmd == 4) {
                    Lua_DoString("CastSpellByID(19750)", "bot", 0); // Вспышка Света
                }
            } __except(1) {}
        }
        g_Cmd.type = 0; // Снимаем команду после выполнения
    }

    return oEndScene(pDevice);
}

// Установка хука через фиктивное устройство, чтобы не крашить систему
bool InstallVTableHook() {
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return false;
    HWND dummyWindow = CreateWindowA("STATIC", "Dummy", WS_OVERLAPPED, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    D3DPRESENT_PARAMETERS d3dpp = {0}; d3dpp.Windowed = TRUE; d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD; d3dpp.hDeviceWindow = dummyWindow;
    IDirect3DDevice9* pDev = nullptr;
    if (FAILED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, dummyWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDev))) {
        pD3D->Release(); DestroyWindow(dummyWindow); return false;
    }
    uintptr_t* pVTable = *(uintptr_t**)pDev;
    oEndScene = (tEndScene)pVTable[42];
    DWORD old;
    VirtualProtect(&pVTable[42], sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &old);
    pVTable[42] = (uintptr_t)HookedEndScene;
    VirtualProtect(&pVTable[42], sizeof(uintptr_t), old, &old);
    pDev->Release(); pD3D->Release(); DestroyWindow(dummyWindow);
    return true;
}

void UnhookVTable() {
    if (!oEndScene) return;
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return;
    HWND dummyWindow = CreateWindowA("STATIC", "Dummy", WS_OVERLAPPED, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    D3DPRESENT_PARAMETERS d3dpp = {0}; d3dpp.Windowed = TRUE; d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD; d3dpp.hDeviceWindow = dummyWindow;
    IDirect3DDevice9* pDev = nullptr;
    if (SUCCEEDED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, dummyWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDev))) {
        uintptr_t* pVTable = *(uintptr_t**)pDev;
        DWORD old;
        VirtualProtect(&pVTable[42], sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &old);
        pVTable[42] = (uintptr_t)oEndScene;
        VirtualProtect(&pVTable[42], sizeof(uintptr_t), old, &old);
        pDev->Release();
    }
    pD3D->Release(); DestroyWindow(dummyWindow);
}

// Ожидание выполнения команды с таймаутом
void SendCommandAndWait(int type, uint64_t guid = 0, float x = 0, float y = 0, float z = 0) {
    g_Cmd.guid = guid; g_Cmd.x = x; g_Cmd.y = y; g_Cmd.z = z; g_Cmd.type = type;
    int timeout = 0;
    while (g_Cmd.type != 0 && timeout < 100) { // 1 секунда таймаут
        Sleep(10);
        timeout++;
    }
    if (g_Cmd.type != 0) g_Cmd.type = 0; // Сбрасываем, если хук молчит
}

// --- ОСНОВНОЙ ПОТОК ---
DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin Pure Internal v22.0 (Zero Binds) ---\n");
    if (InstallVTableHook()) {
        printf("[+] EndScene Hooked! \n");
    } else {
        printf("[-] Hook failed. Run as Administrator!\n");
        return 0;
    }
    printf("[!] КРИТИЧЕСКИ ВАЖНО: Напиши в чате /console gxMultithread 0\n");
    printf("[!] Иначе игра не будет обрабатывать вызовы!\n");
    printf("--------------------------------------------------\n");

    uintptr_t connectionAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    BotState state = STATE_SEARCH;
    uint64_t activeTargetGuid = 0;
    DWORD stateStartTime = GetTickCount();

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConn = SafeRead<uintptr_t>(connectionAddr);
        SetConsoleCursor(0, 6);

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

                // Внутренний Хил через Lua
                if (hp > 0 && maxHp > 0 && (hp * 100 / maxHp) < 40) {
                    SendCommandAndWait(4); // Cast Heal
                    Sleep(1500); 
                }

                DWORD elapsed = GetTickCount() - stateStartTime;

                // --- FSM ---
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
                    // Память обновляем для UI
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
                            *(float*)(pLocal + 0x7A8) = cYaw; // Поворот
                            
                            if (dist > 4.2f) {
                                if (state == STATE_MOVE && elapsed > 12000) {
                                    printf("[!] ЗАСТРЯЛ! Сбрасываю таргет...              \n");
                                    activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                                    continue;
                                }
                                if (state != STATE_MOVE) { state = STATE_MOVE; stateStartTime = GetTickCount(); }
                                
                                // ВНУТРЕННЕЕ ПЕРЕМЕЩЕНИЕ
                                SendCommandAndWait(1, activeTargetGuid, tx, ty, tz);
                            } else {
                                if (state == STATE_COMBAT && elapsed > 25000) {
                                    printf("[!] БОЙ ЗАБАГАЛСЯ! Сбрасываю...               \n");
                                    activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                                    continue;
                                }
                                if (state != STATE_COMBAT) { state = STATE_COMBAT; stateStartTime = GetTickCount(); }

                                // ВНУТРЕННЯЯ АТАКА
                                SendCommandAndWait(2);
                            }
                        } else {
                            if (state != STATE_LOOT) { state = STATE_LOOT; stateStartTime = GetTickCount(); }
                            
                            printf("[STATE] ЖДЕМ ГЕНЕРАЦИЮ ТРУПА СЕРВЕРОМ...         \n");
                            Sleep(1200); 
                            
                            printf("[STATE] INTERNAL ВЗАИМОДЕЙСТВИЕ И СБОР...        \n");
                            // ВНУТРЕННИЙ ЛУТ
                            SendCommandAndWait(3, activeTargetGuid);
                            
                            Sleep(2000); // Даем окну лута время на автосбор
                            
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
                printf("[FSM] %-15s | LUA ENGINE: ACTIVE       \n", stateNames[state]);
            }
        }
        Sleep(100);
    }

    UnhookVTable();
    fclose(f); FreeConsole(); FreeLibraryAndExitThread((HMODULE)lpParam, 0); return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,MainThread,h,0,0); }
    return TRUE;
}
