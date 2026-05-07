#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "user32.lib")

// --- АДРЕСА И СИГНАТУРЫ 3.3.5a (12340) ---
#define ADDR_LUA_EXECUTE      0x00819210
#define ADDR_CLICK_TO_MOVE    0x00611130
#define OFFSET_S_CUR_MGR      0x00879CE0 

typedef void(__cdecl* tFrameScript_Execute)(const char* command, const char* filename, void* reserved);
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetActivePlayer)();

tFrameScript_Execute Lua_DoString = (tFrameScript_Execute)ADDR_LUA_EXECUTE;
tClickToMove ClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;
tGetActivePlayer GetPlayer = (tGetActivePlayer)0x004038BE; // Базовый адрес

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ (БЕЗОПАСНЫЕ) ---
HWND g_WowWnd = NULL;
WNDPROC oWndProc = NULL;

volatile uintptr_t v_pLocal = 0;
volatile uint64_t v_targetGuid = 0;
volatile float v_mX = 0, v_mY = 0, v_mZ = 0;

volatile bool g_runMove = false;
volatile bool g_runAttack = false;
volatile bool g_runInteract = false;
volatile bool g_runLoot = false;

// --- БЕЗОПАСНАЯ ОКОННАЯ ПРОЦЕДУРА ---
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_USER + 1) {
        uintptr_t pLocal = GetPlayer(); // Берем свежий указатель из движка
        
        if (pLocal && !IsBadReadPtr((void*)pLocal, 0x1000)) {
            __try {
                // Проверяем тип объекта (должен быть 4 для игрока)
                if (*(int*)(pLocal + 0x14) == 4) {
                    if (g_runMove) {
                        float pos[3] = { v_mX, v_mY, v_mZ };
                        uint64_t zero = 0;
                        ClickToMove(pLocal, 4, &zero, pos, 0.5f);
                        g_runMove = false;
                    }
                    if (g_runAttack) {
                        Lua_DoString("if UnitExists('target') and not IsCurrentSpell(6603) then StartAttack() end", "bot", 0);
                        g_runAttack = false;
                    }
                    if (g_runInteract) {
                        Lua_DoString("if UnitExists('target') then InteractUnit('target') end", "bot", 0);
                        g_runInteract = false;
                    }
                    if (g_runLoot) {
                        Lua_DoString("if LootFrame:IsVisible() then for i=1,GetNumLootItems() do LootSlot(i) end CloseLoot() end", "bot", 0);
                        g_runLoot = false;
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                // Если даже так крашнулось - значит адреса функцийClickToMove/Lua битые
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

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Imba Bot v6.1 (Defense Internal) ---\n");
    
    g_WowWnd = FindWindowA(NULL, "World of Warcraft");
    if (!g_WowWnd) return 0;

    oWndProc = (WNDPROC)SetWindowLongA(g_WowWnd, GWL_WNDPROC, (LONG)HookedWndProc);
    printf("[+] WndProc Hooked. Safely executing in Main Thread.\n");

    HMODULE base = GetModuleHandleA(NULL);
    uintptr_t connectionAddr = (uintptr_t)base + OFFSET_S_CUR_MGR;
    
    int state = 0; // 0: Search, 1: Move, 2: Combat, 3: Loot
    uint64_t targetGuid = 0;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConn = 0;
        __try { clientConn = *(uintptr_t*)connectionAddr; } __except(1) { clientConn = 0; }
        
        SetConsoleCursor(0, 5);

        if (clientConn) {
            uintptr_t mgr = *(uintptr_t*)(clientConn + 0x2ED0);
            if (mgr) {
                uint64_t myGuid = *(uint64_t*)(mgr + 0xC0);
                uintptr_t pLocal = GetPlayer();

                if (pLocal) {
                    uintptr_t pDesc = *(uintptr_t*)(pLocal + 0x8);
                    int hp = *(int*)(pDesc + 0x60), lvl = *(int*)(pDesc + 0xD8);
                    float myX = *(float*)(pLocal + 0x798), myY = *(float*)(pLocal + 0x79C);

                    printf("[ME] Lvl:%d HP:%d POS:%.1f, %.1f          \n", lvl, hp, myX, myY);
                    printf("--------------------------------------------------\n");

                    if (state == 0) { // SEARCH
                        float bestDist = 45.0f; targetGuid = 0;
                        uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
                        while (cur != 0 && (cur & 1) == 0) {
                            if (*(int*)(cur + 0x14) == 3) {
                                uintptr_t d = *(uintptr_t*)(cur + 0x8);
                                if (*(int*)(d + 0x60) > 0 && *(int*)(d + 0xD8) <= lvl + 2) {
                                    float x = *(float*)(cur + 0x798), y = *(float*)(cur + 0x79C);
                                    float dist = sqrt(pow(x-myX, 2) + pow(y-myY, 2));
                                    if (dist < bestDist) { bestDist = dist; targetGuid = *(uint64_t*)(cur + 0x30); }
                                }
                            }
                            cur = *(uintptr_t*)(cur + 0x3C);
                        }
                        if (targetGuid) { state = 1; *(uint64_t*)(pDesc + 0x48) = targetGuid; }
                    }

                    if (targetGuid) {
                        uintptr_t tObj = 0; uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
                        while (cur != 0 && (cur & 1) == 0) { if(*(uint64_t*)(cur+0x30) == targetGuid) { tObj = cur; break; } cur = *(uintptr_t*)(cur+0x3C); }
                        
                        if (tObj) {
                            uintptr_t td = *(uintptr_t*)(tObj + 0x8);
                            int thp = *(int*)(td + 0x60);
                            float tx = *(float*)(tObj + 0x798), ty = *(float*)(tObj + 0x79C), tz = *(float*)(tObj + 0x7A0);
                            float dist = sqrt(pow(tx-myX, 2) + pow(ty-myY, 2));

                            printf("[TARGET] HP:%d Dist:%.2f yds                  \n", thp, dist);
                            
                            if (thp > 0) {
                                if (dist > 4.5f) {
                                    state = 1; v_mX = tx; v_mY = ty; v_mZ = tz; g_runMove = true;
                                    PostMessageA(g_WowWnd, WM_USER + 1, 0, 0);
                                } else {
                                    state = 2; g_runAttack = true;
                                    PostMessageA(g_WowWnd, WM_USER + 1, 0, 0);
                                }
                            } else {
                                state = 3; g_runInteract = true;
                                PostMessageA(g_WowWnd, WM_USER + 1, 0, 0);
                                Sleep(1000);
                                g_runLoot = true;
                                PostMessageA(g_WowWnd, WM_USER + 1, 0, 0);
                                Sleep(500);
                                *(uint64_t*)(pDesc + 0x48) = 0; targetGuid = 0; state = 0;
                            }
                        } else { targetGuid = 0; state = 0; }
                    }
                    printf("[STATE] %s                                     \n", (state==0)?"SEARCH":(state==1)?"MOVE":(state==2)?"COMBAT":"LOOT");
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
