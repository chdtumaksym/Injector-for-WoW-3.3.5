#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <vector>

#pragma comment(lib, "user32.lib")

// --- АДРЕСА 3.3.5a (12340) ---
#define ADDR_LUA_EXECUTE      0x00819210
#define ADDR_CLICK_TO_MOVE    0x00611130
#define ADDR_GET_PLAYER       0x004038BE
#define OFFSET_S_CUR_MGR      0x00879CE0 

typedef void(__cdecl* tFrameScript_Execute)(const char* command, const char* filename, void* reserved);
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetActivePlayer)();

tFrameScript_Execute Lua_DoString = (tFrameScript_Execute)ADDR_LUA_EXECUTE;
tClickToMove ClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;
tGetActivePlayer GetPlayer = (tGetActivePlayer)ADDR_GET_PLAYER;

// --- СТРУКТУРА КОМАНД ---
struct BotCommand {
    volatile int type; // 1: Move, 2: Attack, 3: Loot, 4: Target
    volatile uint64_t guid;
    volatile float x, y, z;
} g_Cmd;

HWND g_WowWnd = NULL;
WNDPROC oWndProc = NULL;
uintptr_t g_ConnAddr = 0;

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

// --- БЕЗОПАСНЫЙ ХУК (Main Thread) ---
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_USER + 0x999) {
        // Проверка: мы вообще в игре?
        uintptr_t client = *(uintptr_t*)g_ConnAddr;
        if (!client) return 0;

        uintptr_t pLocal = GetPlayer();
        if (pLocal && *(int*)(pLocal + 0x14) == 4) {
            __try {
                if (g_Cmd.type == 1) { // MOVE
                    float pos[3] = { g_Cmd.x, g_Cmd.y, g_Cmd.z };
                    uint64_t zero = 0;
                    ClickToMove(pLocal, 4, &zero, pos, 0.5f);
                } 
                else if (g_Cmd.type == 2) { // ATTACK
                    Lua_DoString("if not IsCurrentSpell(6603) then StartAttack() end", "bot", 0);
                    // Паладинские баффы
                    Lua_DoString("if not UnitBuff('player', 'Seal of Righteousness') then CastSpellByID(21084) end", "bot", 0);
                } 
                else if (g_Cmd.type == 3) { // LOOT
                    Lua_DoString("if LootFrame:IsVisible() then for i=1,GetNumLootItems() do LootSlot(i) end CloseLoot() end", "bot", 0);
                }
                else if (g_Cmd.type == 4) { // TARGET
                    // Используем Lua для таргета - это в 100 раз стабильнее прямой записи GUID
                    char cmd[128];
                    sprintf_s(cmd, "TargetUnit('0x%llX')", g_Cmd.guid);
                    Lua_DoString(cmd, "bot", 0);
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        g_Cmd.type = 0; // Сбрасываем команду
        return 0;
    }
    return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
}

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

// --- ОСНОВНАЯ ЛОГИКА ---
DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin Industrial v9.0 (Safe Post-Message) ---\n");
    g_WowWnd = FindWindowA(NULL, "World of Warcraft");
    if (!g_WowWnd) return 0;

    g_ConnAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    oWndProc = (WNDPROC)SetWindowLongA(g_WowWnd, GWL_WNDPROC, (LONG)HookedWndProc);

    printf("[+] Hooked. Safe Main-Thread execution active.\n");
    printf("[*] Background mode: ENABLED.\n");

    BotState state = STATE_SEARCH;
    uint64_t currentTarget = 0;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t client = 0;
        __try { client = *(uintptr_t*)g_ConnAddr; } __except(1) {}
        
        SetConsoleCursor(0, 7);
        if (client) {
            uintptr_t mgr = *(uintptr_t*)(client + 0x2ED0);
            if (mgr) {
                uintptr_t pLocal = GetPlayer();
                if (pLocal) {
                    uintptr_t pDesc = *(uintptr_t*)(pLocal + 0x8);
                    int hp = *(int*)(pDesc + 0x60), lvl = *(int*)(pDesc + 0xD8);
                    float myX = *(float*)(pLocal + 0x798), myY = *(float*)(pLocal + 0x79C);

                    printf("[ME] HP:%d/%d | LVL:%d | POS:%.1f, %.1f          \n", hp, *(int*)(pDesc+0x80), lvl, myX, myY);
                    printf("--------------------------------------------------\n");

                    // --- СТАДИИ ---
                    if (state == STATE_SEARCH) {
                        float bestDist = 45.0f;
                        uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
                        while (cur != 0 && (cur & 1) == 0) {
                            if (*(int*)(cur + 0x14) == 3) {
                                uintptr_t d = *(uintptr_t*)(cur + 0x8);
                                if (*(int*)(d + 0x60) > 0 && *(int*)(d + 0xD8) <= lvl + 2 && *(uint64_t*)(d + 0x38) == 0) {
                                    float dist = sqrt(pow(*(float*)(cur + 0x798) - myX, 2) + pow(*(float*)(cur + 0x79C) - myY, 2));
                                    if (dist < bestDist) { bestDist = dist; currentTarget = *(uint64_t*)(cur + 0x30); }
                                }
                            }
                            cur = *(uintptr_t*)(cur + 0x3C);
                        }
                        if (currentTarget) { 
                            state = STATE_MOVE; 
                            g_Cmd.guid = currentTarget; g_Cmd.type = 4; // Команда TARGET
                            PostMessageA(g_WowWnd, WM_USER + 0x999, 0, 0);
                        }
                    }

                    if (currentTarget != 0) {
                        uintptr_t tObj = 0; uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
                        while (cur != 0 && (cur & 1) == 0) { if(*(uint64_t*)(cur+0x30) == currentTarget) { tObj = cur; break; } cur = *(uintptr_t*)(cur+0x3C); }
                        
                        if (tObj) {
                            uintptr_t td = *(uintptr_t*)(tObj + 0x8);
                            int thp = *(int*)(td + 0x60);
                            float tx = *(float*)(tObj + 0x798), ty = *(float*)(tObj + 0x79C), tz = *(float*)(tObj + 0x7A0);
                            float dist = sqrt(pow(tx-myX, 2) + pow(ty-myY, 2));

                            printf("[TARGET] HP:%d | DIST:%.2f yds                  \n", thp, dist);
                            
                            if (thp > 0) {
                                if (dist > 4.0f) {
                                    state = STATE_MOVE;
                                    g_Cmd.x = tx; g_Cmd.y = ty; g_Cmd.z = tz; g_Cmd.type = 1; // Команда MOVE
                                    PostMessageA(g_WowWnd, WM_USER + 0x999, 0, 0);
                                } else {
                                    state = STATE_COMBAT;
                                    g_Cmd.type = 2; // Команда ATTACK
                                    PostMessageA(g_WowWnd, WM_USER + 0x999, 0, 0);
                                }
                            } else {
                                state = STATE_LOOT;
                                // Взаимодействие через Lua - это безопаснее всего
                                Lua_DoString("InteractUnit('target')", "bot", 0);
                                Sleep(1000);
                                g_Cmd.type = 3; // Команда LOOT
                                PostMessageA(g_WowWnd, WM_USER + 0x999, 0, 0);
                                Sleep(500);
                                currentTarget = 0; state = STATE_SEARCH;
                            }
                        } else { currentTarget = 0; state = STATE_SEARCH; }
                    }
                    printf("[STATE] %-15s                                  \n", stateNames[state]);
                }
            }
        }
        Sleep(150);
    }

    SetWindowLongA(g_WowWnd, GWL_WNDPROC, (LONG)oWndProc);
    fclose(f); FreeConsole(); FreeLibraryAndExitThread((HMODULE)lpParam, 0); return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,MainThread,h,0,0); }
    return TRUE;
}
