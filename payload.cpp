#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <vector>

#pragma comment(lib, "user32.lib")

// --- АДРЕСА И СМЕЩЕНИЯ 3.3.5a (12340) ---
#define ADDR_LUA_EXECUTE      0x00819210
#define ADDR_CLICK_TO_MOVE    0x00611130
#define ADDR_GET_PLAYER       0x004038BE
#define ADDR_TARGET_GUID      0x00BD07B0 // Глобальный адрес текущей цели в UI
#define OFFSET_S_CUR_MGR      0x00879CE0 

typedef void(__cdecl* tFrameScript_Execute)(const char* command, const char* filename, void* reserved);
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetActivePlayer)();

tFrameScript_Execute Lua_DoString = (tFrameScript_Execute)ADDR_LUA_EXECUTE;
tClickToMove ClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;
tGetActivePlayer GetPlayer = (tGetActivePlayer)ADDR_GET_PLAYER;

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ---
HWND g_WowWnd = NULL;
WNDPROC oWndProc = NULL;

uint64_t g_TargetGuid = 0;
float g_MoveX = 0, g_MoveY = 0, g_MoveZ = 0;

bool g_DoMove = false;
bool g_DoAttack = false;
bool g_DoTarget = false;
bool g_DoLoot = false;

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

// --- ГЛАВНЫЙ ПОТОК ВЫПОЛНЕНИЯ (WndProc) ---
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_USER + 0x1337) { // Наше кастомное сообщение
        uintptr_t pLocal = GetPlayer();
        if (pLocal && *(int*)(pLocal + 0x14) == 4) { // Проверка, что мы - игрок
            __try {
                if (g_DoTarget) {
                    // 1. Пишем GUID в глобальную переменную таргета
                    *(uint64_t*)ADDR_TARGET_GUID = g_TargetGuid;
                    // 2. Вызываем Lua для обновления интерфейса
                    Lua_DoString("TargetUnit('target')", "bot", 0);
                    g_DoTarget = false;
                }
                if (g_DoMove) {
                    float pos[3] = { g_MoveX, g_MoveY, g_MoveZ };
                    uint64_t zero = 0;
                    ClickToMove(pLocal, 4, &zero, pos, 0.5f);
                    g_DoMove = false;
                }
                if (g_DoAttack) {
                    Lua_DoString("if not IsCurrentSpell(6603) then StartAttack() end", "bot", 0);
                    // Паладин: Печать праведности (Seal of Righteousness)
                    Lua_DoString("if not UnitBuff('player', 'Seal of Righteousness') then CastSpellByID(21084) end", "bot", 0);
                    g_DoAttack = false;
                }
                if (g_DoLoot) {
                    // Лутаем только если окно открыто. Это спасет от краша 0x14
                    Lua_DoString("if LootFrame:IsVisible() then for i=1,GetNumLootItems() do LootSlot(i) end CloseLoot() end", "bot", 0);
                    g_DoLoot = false;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                // Если тут упали - значит всё-таки что-то не так с адресами
            }
        }
        return 0;
    }
    return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
}

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

void WriteLog(const char* msg, int lvl, int hp, float dist) {
    FILE* log;
    if (fopen_s(&log, "bot_log.txt", "a") == 0) {
        fprintf(log, "[LOG] %-12s | Lvl: %d | HP: %d | Dist: %.1f\n", msg, lvl, hp, dist);
        fclose(log);
    }
}

// --- ПОТОК ЛОГИКИ (Сканер) ---
DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin Internal Bot v6.0 (WndProc Hook) ---\n");
    g_WowWnd = FindWindowA(NULL, "World of Warcraft");
    if (!g_WowWnd) return 0;

    oWndProc = (WNDPROC)SetWindowLongA(g_WowWnd, GWL_WNDPROC, (LONG)HookedWndProc);
    printf("[+] Target Logic Hooked. Background mode active.\n");

    HMODULE base = GetModuleHandleA(NULL);
    uintptr_t connectionAddr = (uintptr_t)base + OFFSET_S_CUR_MGR;
    
    BotState state = STATE_SEARCH;
    uint64_t currentTargetGuid = 0;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConn = 0;
        __try { clientConn = *(uintptr_t*)connectionAddr; } __except(1) { clientConn = 0; }
        
        SetConsoleCursor(0, 6);

        if (clientConn) {
            uintptr_t mgr = *(uintptr_t*)(clientConn + 0x2ED0);
            if (mgr) {
                uint64_t myGuid = *(uint64_t*)(mgr + 0xC0);
                uintptr_t pLocal = GetPlayer();

                if (pLocal) {
                    uintptr_t pDesc = *(uintptr_t*)(pLocal + 0x8);
                    int hp = *(int*)(pDesc + 0x60), lvl = *(int*)(pDesc + 0xD8);
                    float myX = *(float*)(pLocal + 0x798), myY = *(float*)(pLocal + 0x79C);

                    printf("[ME] HP:%d/%d | LVL:%d | POS:%.1f, %.1f          \n", hp, *(int*)(pDesc+0x80), lvl, myX, myY);
                    
                    // Инвентарь
                    printf("[BAG] ");
                    for(int i=0; i<10; i++) {
                        uint64_t iGuid = *(uint64_t*)(pDesc + 0x640 + (i*8));
                        if (iGuid) printf("%d ", i+1);
                    }
                    printf("                                       \n");
                    printf("--------------------------------------------------\n");

                    // --- СТАДИЯ: ПОИСК ---
                    if (state == STATE_SEARCH) {
                        float bestDist = 45.0f; currentTargetGuid = 0;
                        uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
                        while (cur != 0 && (cur & 1) == 0) {
                            if (*(int*)(cur + 0x14) == 3) { // NPC
                                uintptr_t d = *(uintptr_t*)(cur + 0x8);
                                if (*(int*)(d + 0x60) > 0 && *(int*)(d + 0xD8) <= lvl + 2) {
                                    float x = *(float*)(cur + 0x798), y = *(float*)(cur + 0x79C);
                                    float dX = x - myX, dY = y - myY;
                                    float dist = sqrt(dX*dX + dY*dY);
                                    if (dist < bestDist) { bestDist = dist; currentTargetGuid = *(uint64_t*)(cur + 0x30); }
                                }
                            }
                            cur = *(uintptr_t*)(cur + 0x3C);
                        }
                        if (currentTargetGuid) { 
                            state = STATE_MOVE;
                            g_TargetGuid = currentTargetGuid;
                            g_DoTarget = true; // Команда: Взять в таргет
                            SendMessageA(g_WowWnd, WM_USER + 0x1337, 0, 0);
                            WriteLog("New Target", lvl, hp, bestDist);
                        }
                    }

                    // --- СТАДИЯ: РАБОТА С ЦЕЛЬЮ ---
                    if (currentTargetGuid != 0) {
                        uintptr_t tObj = 0; uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
                        while (cur != 0 && (cur & 1) == 0) { if(*(uint64_t*)(cur+0x30) == currentTargetGuid) { tObj = cur; break; } cur = *(uintptr_t*)(cur+0x3C); }
                        
                        if (tObj) {
                            uintptr_t td = *(uintptr_t*)(tObj + 0x8);
                            int thp = *(int*)(td + 0x60);
                            float tx = *(float*)(tObj + 0x798), ty = *(float*)(tObj + 0x79C), tz = *(float*)(tObj + 0x7A0);
                            float dist = sqrt(pow(tx-myX, 2) + pow(ty-myY, 2));

                            printf("[TARGET] HP:%d | DIST:%.2f yds                  \n", thp, dist);
                            
                            if (thp > 0) {
                                if (dist > 4.2f) {
                                    state = STATE_MOVE;
                                    g_MoveX = tx; g_MoveY = ty; g_MoveZ = tz;
                                    g_DoMove = true;
                                    SendMessageA(g_WowWnd, WM_USER + 0x1337, 0, 0);
                                } else {
                                    state = STATE_COMBAT;
                                    g_DoAttack = true;
                                    SendMessageA(g_WowWnd, WM_USER + 0x1337, 0, 0);
                                }
                            } else {
                                // ЛУТ
                                state = STATE_LOOT;
                                printf("[STATE] LOOTING...                               \n");
                                Lua_DoString("InteractUnit('target')", "bot", 0); // Прямой вызов Interact
                                Sleep(800); 
                                g_DoLoot = true;
                                SendMessageA(g_WowWnd, WM_USER + 0x1337, 0, 0);
                                Sleep(1000);
                                
                                targetGuid = 0; currentTargetGuid = 0; state = STATE_SEARCH;
                            }
                        } else { currentTargetGuid = 0; state = STATE_SEARCH; }
                    }
                    printf("[FSM] %-15s                                  \n", stateNames[state]);
                }
            }
        }
        Sleep(150);
    }

    SetWindowLongA(g_WowWnd, GWL_WNDPROC, (LONG)oWndProc);
    fclose(f); FreeConsole(); 
    FreeLibraryAndExitThread((HMODULE)lpParam, 0); 
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,MainThread,h,0,0); }
    return TRUE;
}
