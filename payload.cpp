#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <vector>

#pragma comment(lib, "user32.lib")

// --- АДРЕСА ФУНКЦИЙ И СМЕЩЕНИЯ 3.3.5a (12340) ---
#define ADDR_LUA_EXECUTE      0x00819210
#define ADDR_CLICK_TO_MOVE    0x00611130
#define ADDR_GET_PLAYER       0x004038BE
#define ADDR_TARGET_UNIT      0x0052D170 
#define ADDR_INTERACT         0x00609390 
#define OFFSET_S_CUR_MGR      0x00879CE0 

typedef void(__cdecl* tFrameScript_Execute)(const char* command, const char* filename, void* reserved);
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetActivePlayer)();
typedef void(__cdecl* tTargetUnit)(uint64_t guid);
typedef void(__thiscall* tInteract)(uintptr_t unitPtr);

tFrameScript_Execute Lua_DoString = (tFrameScript_Execute)ADDR_LUA_EXECUTE;
tClickToMove ClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;
tGetActivePlayer GetPlayer = (tGetActivePlayer)ADDR_GET_PLAYER;
tTargetUnit TargetByGuid = (tTargetUnit)ADDR_TARGET_UNIT;
tInteract InternalInteract = (tInteract)ADDR_INTERACT;

// --- КОМАНДЫ И ГЛОБАЛЫ ---
struct BotCommand {
    int type; // 0: None, 1: Target, 2: Move, 3: Attack, 4: Interact, 5: Loot
    uint64_t guid;
    uintptr_t ptr;
    float x, y, z;
} g_Cmd;

HWND g_WowWnd = NULL;
WNDPROC oWndProc = NULL;

// Список мусора (Item ID)
const int trashList[] = { 7073, 7005, 159, 21232, 4599, 15305 }; 
const int trashCount = sizeof(trashList) / sizeof(int);

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

// --- ГЛАВНЫЙ ПОТОК (WndProc Hook) ---
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_USER + 0x777) {
        uintptr_t pLocal = GetPlayer();
        if (pLocal && *(int*)(pLocal + 0x14) == 4) {
            __try {
                if (g_Cmd.type == 1) { // TARGET
                    TargetByGuid(g_Cmd.guid);
                } else if (g_Cmd.type == 2) { // MOVE
                    float pos[3] = { g_Cmd.x, g_Cmd.y, g_Cmd.z };
                    uint64_t zero = 0;
                    ClickToMove(pLocal, 4, &zero, pos, 0.5f);
                } else if (g_Cmd.type == 3) { // ATTACK
                    Lua_DoString("if not IsCurrentSpell(6603) then StartAttack() end", "bot", 0);
                    Lua_DoString("if not UnitBuff('player', 'Seal of Righteousness') then CastSpellByID(21084) end", "bot", 0);
                    Lua_DoString("if not UnitBuff('player', 'Devotion Aura') then CastSpellByID(465) end", "bot", 0);
                } else if (g_Cmd.type == 4) { // INTERACT
                    if (g_Cmd.ptr) InternalInteract(g_Cmd.ptr);
                } else if (g_Cmd.type == 5) { // LOOT
                    Lua_DoString("if LootFrame:IsVisible() then for i=1,GetNumLootItems() do LootSlot(i) end CloseLoot() end", "bot", 0);
                }
            } __except(1) {}
        }
        g_Cmd.type = 0;
        return 0;
    }
    return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
}

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

void WriteLog(const char* msg, float d, int hp, float x, float y) {
    FILE* log;
    if (fopen_s(&log, "bot_log.txt", "a") == 0) {
        fprintf(log, "[LOG] %-15s | Dist: %.2f | HP: %d | POS: %.1f, %.1f\n", msg, d, hp, x, y);
        fclose(log);
    }
}

// --- ПОТОК ЛОГИКИ ---
DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin Industrial v8.0 (Fixed Scope & Bags) ---\n");
    g_WowWnd = FindWindowA(NULL, "World of Warcraft");
    if (!g_WowWnd) return 0;

    oWndProc = (WNDPROC)SetWindowLongA(g_WowWnd, GWL_WNDPROC, (LONG)HookedWndProc);
    printf("[+] Hooked! Thread safe. Press END to exit.\n");
    printf("--------------------------------------------------\n");

    uintptr_t connAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    BotState state = STATE_SEARCH;
    uint64_t activeTargetGuid = 0;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t client;
        __try { client = *(uintptr_t*)connAddr; } __except(1) { client = 0; }
        
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
                    
                    // --- ЧТЕНИЕ ИНВЕНТАРЯ ---
                    printf("[BAG] ");
                    int invCount = 0;
                    for(int i=0; i<16; i++) {
                        uint64_t iGuid = *(uint64_t*)(pDesc + 0x640 + (i*8));
                        if (iGuid) {
                            uintptr_t icur = *(uintptr_t*)(mgr + 0xAC);
                            while(icur != 0 && (icur & 1) == 0) {
                                if(*(uint64_t*)(icur + 0x30) == iGuid) {
                                    int id = *(int*)(*(uintptr_t*)(icur + 0x8) + 0xC);
                                    bool isTrash = false;
                                    for(int t=0; t<trashCount; t++) if(id == trashList[t]) isTrash = true;
                                    printf("%d%s ", id, isTrash ? "*" : "");
                                    invCount++; break;
                                }
                                icur = *(uintptr_t*)(icur + 0x3C);
                            }
                        }
                    }
                    printf("(Total: %d)                                \n", invCount);
                    printf("--------------------------------------------------\n");

                    // --- FSM ---
                    if (state == STATE_SEARCH) {
                        float bestDist = 45.0f; activeTargetGuid = 0;
                        uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
                        while (cur != 0 && (cur & 1) == 0) {
                            if (*(int*)(cur + 0x14) == 3) {
                                uintptr_t d = *(uintptr_t*)(cur + 0x8);
                                if (*(int*)(d + 0x60) > 0 && *(int*)(d + 0xD8) <= lvl + 2 && *(uint64_t*)(d + 0x38) == 0) {
                                    float dist = sqrt(pow(*(float*)(cur + 0x798) - myX, 2) + pow(*(float*)(cur + 0x79C) - myY, 2));
                                    if (dist < bestDist) { bestDist = dist; activeTargetGuid = *(uint64_t*)(cur + 0x30); }
                                }
                            }
                            cur = *(uintptr_t*)(cur + 0x3C);
                        }
                        if (activeTargetGuid) { 
                            state = STATE_MOVE; 
                            g_Cmd.guid = activeTargetGuid; g_Cmd.type = 1; // TARGET
                            SendMessageA(g_WowWnd, WM_USER + 0x777, 0, 0);
                            WriteLog("Lock", bestDist, hp, myX, myY);
                        }
                    }

                    if (activeTargetGuid != 0) {
                        uintptr_t tObj = 0; uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
                        while (cur != 0 && (cur & 1) == 0) { if(*(uint64_t*)(cur+0x30) == activeTargetGuid) { tObj = cur; break; } cur = *(uintptr_t*)(cur+0x3C); }
                        
                        if (tObj) {
                            uintptr_t td = *(uintptr_t*)(tObj + 0x8);
                            int thp = *(int*)(td + 0x60);
                            float tx = *(float*)(tObj + 0x798), ty = *(float*)(tObj + 0x79C), tz = *(float*)(tObj + 0x7A0);
                            float dist = sqrt(pow(tx-myX, 2) + pow(ty-myY, 2));

                            printf("[TARGET] HP:%d | DIST:%.2f yds                  \n", thp, dist);
                            
                            if (thp > 0) {
                                if (dist > 4.0f) {
                                    state = STATE_MOVE;
                                    g_Cmd.x = tx; g_Cmd.y = ty; g_Cmd.z = tz; g_Cmd.type = 2; // MOVE
                                    SendMessageA(g_WowWnd, WM_USER + 0x777, 0, 0);
                                } else {
                                    state = STATE_COMBAT;
                                    g_Cmd.type = 3; // ATTACK
                                    SendMessageA(g_WowWnd, WM_USER + 0x777, 0, 0);
                                }
                            } else {
                                state = STATE_LOOT;
                                printf("[STATE] LOOTING...                               \n");
                                g_Cmd.ptr = tObj; g_Cmd.type = 4; // INTERACT
                                SendMessageA(g_WowWnd, WM_USER + 0x777, 0, 0);
                                Sleep(1000);
                                g_Cmd.type = 5; // LOOT
                                SendMessageA(g_WowWnd, WM_USER + 0x777, 0, 0);
                                Sleep(800);
                                activeTargetGuid = 0; state = STATE_SEARCH;
                            }
                        } else { activeTargetGuid = 0; state = STATE_SEARCH; }
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
