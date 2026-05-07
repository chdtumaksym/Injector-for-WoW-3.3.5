#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "user32.lib")

// --- АДРЕСА ФУНКЦИЙ 3.3.5a (12340) ---
#define ADDR_LUA_EXECUTE      0x00819210
#define ADDR_CLICK_TO_MOVE    0x00611130
#define OFFSET_S_CUR_MGR      0x00879CE0 

typedef void(__cdecl* tFrameScript_Execute)(const char* command, const char* filename, void* reserved);
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);

tFrameScript_Execute Lua_DoString = (tFrameScript_Execute)ADDR_LUA_EXECUTE;
tClickToMove ClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ---
HWND g_WowWnd = NULL;
WNDPROC oWndProc = NULL;

uintptr_t g_LocalPlayerObj = 0;
uint64_t g_TargetGuid = 0;
float g_MoveX = 0, g_MoveY = 0, g_MoveZ = 0;

bool g_QueuedMove = false;
bool g_QueuedAttack = false;
bool g_QueuedInteract = false;
bool g_QueuedLoot = false;

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

// --- WNDPROC ХУК (100% ГАРАНТИЯ MAIN THREAD) ---
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Ловим наше кастомное сообщение
    if (uMsg == WM_USER + 1) {
        // ЭТОТ БЛОК ВЫПОЛНЯЕТСЯ СТРОГО В MAIN THREAD! Никаких крашей TLS.
        if (g_LocalPlayerObj != 0) {
            __try {
                if (g_QueuedMove) {
                    float pos[3] = { g_MoveX, g_MoveY, g_MoveZ };
                    uint64_t emptyGuid = 0; // Пустой GUID для избежания поиска мертвецов
                    ClickToMove(g_LocalPlayerObj, 4, &emptyGuid, pos, 0.5f); 
                    g_QueuedMove = false;
                }
                if (g_QueuedAttack) {
                    Lua_DoString("if UnitExists('target') and not IsCurrentSpell(6603) then StartAttack() end", "bot", 0);
                    g_QueuedAttack = false;
                }
                if (g_QueuedInteract) {
                    Lua_DoString("if UnitExists('target') then InteractUnit('target') end", "bot", 0);
                    g_QueuedInteract = false;
                }
                if (g_QueuedLoot) {
                    Lua_DoString("if LootFrame:IsVisible() then for i=1,GetNumLootItems() do LootSlot(i) end CloseLoot() end", "bot", 0);
                    g_QueuedLoot = false;
                }
            } __except(1) {} // Глушим всё, чтобы не уронить клиент
        }
        return 0; // Сообщение обработано
    }
    // Всё остальное (клики мыши, клавиатура) отдаем оригинальной игре
    return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
}

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

// --- ОСНОВНОЙ ЦИКЛ СКАНИРОВАНИЯ ---
DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Imba Bot Internal v6.0 (WNDPROC HOOK) ---\n");
    
    g_WowWnd = FindWindowA(NULL, "World of Warcraft");
    if (!g_WowWnd) {
        printf("[-] WoW Window not found!\n");
        return 0;
    }

    // Ставим хук на оконную процедуру игры
    oWndProc = (WNDPROC)SetWindowLongA(g_WowWnd, GWL_WNDPROC, (LONG)HookedWndProc);
    if (!oWndProc) {
        printf("[-] Failed to hook WndProc!\n");
        return 0;
    }

    printf("[+] WndProc Hooked! 100%% Thread-Safe Main Thread Execution.\n");
    printf("[*] D3D9 completely bypassed. Crashes eliminated.\n");
    printf("[*] Press END to stop safely.\n");
    printf("--------------------------------------------------\n");

    HMODULE base = GetModuleHandleA(NULL);
    uintptr_t connectionAddr = (uintptr_t)base + OFFSET_S_CUR_MGR;
    
    BotState state = STATE_SEARCH;
    uint64_t currentTargetGuid = 0;
    int hudStartY = 7;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConn = 0;
        __try { clientConn = *(uintptr_t*)connectionAddr; } __except(1) { clientConn = 0; }
        
        SetConsoleCursor(0, hudStartY);

        if (clientConn) {
            uintptr_t mgr = *(uintptr_t*)(clientConn + 0x2ED0);
            if (mgr) {
                uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
                uint64_t myGuid = *(uint64_t*)(mgr + 0xC0);
                
                __try {
                    g_LocalPlayerObj = 0;
                    while (cur != 0 && (cur & 1) == 0) {
                        if (*(uint64_t*)(cur + 0x30) == myGuid && myGuid != 0) { g_LocalPlayerObj = cur; break; }
                        cur = *(uintptr_t*)(cur + 0x3C);
                    }

                    if (g_LocalPlayerObj) {
                        uintptr_t localDesc = *(uintptr_t*)(g_LocalPlayerObj + 0x8);
                        int hp = *(int*)(localDesc + 0x60), maxHp = *(int*)(localDesc + 0x80), lvl = *(int*)(localDesc + 0xD8);
                        float myX = *(float*)(g_LocalPlayerObj + 0x798), myY = *(float*)(g_LocalPlayerObj + 0x79C);

                        printf("[STATUS] Lvl:%d HP:%d/%d POS:%.1f, %.1f          \n", lvl, hp, maxHp, myX, myY);
                        
                        printf("[BAG] ");
                        int itemsFound = 0;
                        for(int i=0; i<10; i++) {
                            uint64_t iGuid = *(uint64_t*)(localDesc + 0x640 + (i*8));
                            if (iGuid) {
                                uintptr_t icur = *(uintptr_t*)(mgr + 0xAC);
                                while(icur != 0 && (icur & 1) == 0) {
                                    if(*(uint64_t*)(icur + 0x30) == iGuid) {
                                        printf("%d ", *(int*)(*(uintptr_t*)(icur + 0x8) + 0xC));
                                        itemsFound++; break;
                                    }
                                    icur = *(uintptr_t*)(icur + 0x3C);
                                }
                            }
                        }
                        printf("(%d items)                                   \n", itemsFound);
                        printf("--------------------------------------------------\n");

                        // --- ПРИНЯТИЕ РЕШЕНИЙ ---
                        if (state == STATE_SEARCH) {
                            float closestDist = 45.0f;
                            currentTargetGuid = 0;
                            cur = *(uintptr_t*)(mgr + 0xAC);
                            while (cur != 0 && (cur & 1) == 0) {
                                if (*(int*)(cur + 0x14) == 3) {
                                    uintptr_t d = *(uintptr_t*)(cur + 0x8);
                                    if (*(int*)(d + 0x60) > 0 && *(int*)(d + 0xD8) <= lvl + 2 && *(uint64_t*)(d + 0x38) == 0) {
                                        float x = *(float*)(cur + 0x798), y = *(float*)(cur + 0x79C);
                                        float dist = sqrt(pow(x-myX, 2) + pow(y-myY, 2));
                                        if (dist < closestDist) { closestDist = dist; currentTargetGuid = *(uint64_t*)(cur + 0x30); }
                                    }
                                }
                                cur = *(uintptr_t*)(cur + 0x3C);
                            }
                            if (currentTargetGuid != 0) { 
                                state = STATE_MOVE; 
                                *(uint64_t*)(localDesc + 0x48) = currentTargetGuid; 
                            }
                        }

                        if (currentTargetGuid != 0) {
                            uintptr_t targetObj = 0;
                            cur = *(uintptr_t*)(mgr + 0xAC);
                            while (cur != 0 && (cur & 1) == 0) { if (*(uint64_t*)(cur + 0x30) == currentTargetGuid) { targetObj = cur; break; } cur = *(uintptr_t*)(cur + 0x3C); }
                            
                            if (targetObj) {
                                uintptr_t td = *(uintptr_t*)(targetObj + 0x8);
                                int thp = *(int*)(td + 0x60);
                                float tx = *(float*)(targetObj + 0x798), ty = *(float*)(targetObj + 0x79C), tz = *(float*)(targetObj + 0x7A0);
                                float dist = sqrt(pow(tx-myX, 2) + pow(ty-myY, 2));

                                printf("[TARGET] HP:%-5d Dist:%.2f yds                  \n", thp, dist);
                                
                                if (thp > 0) {
                                    if (dist > 4.5f) {
                                        state = STATE_MOVE;
                                        g_MoveX = tx; g_MoveY = ty; g_MoveZ = tz;
                                        g_QueuedMove = true;
                                        SendMessageA(g_WowWnd, WM_USER + 1, 0, 0); // Вызов в Main Thread
                                    } else {
                                        state = STATE_COMBAT;
                                        g_QueuedAttack = true; 
                                        SendMessageA(g_WowWnd, WM_USER + 1, 0, 0);
                                    }
                                } else {
                                    state = STATE_LOOT;
                                    printf("[STATE] LOOTING...                               \n");
                                    g_QueuedInteract = true;
                                    SendMessageA(g_WowWnd, WM_USER + 1, 0, 0);
                                    Sleep(1000); 
                                    
                                    g_QueuedLoot = true;
                                    SendMessageA(g_WowWnd, WM_USER + 1, 0, 0);
                                    Sleep(500);
                                    
                                    *(uint64_t*)(localDesc + 0x48) = 0; 
                                    currentTargetGuid = 0;
                                    state = STATE_SEARCH;
                                }
                            } else {
                                currentTargetGuid = 0;
                                state = STATE_SEARCH;
                                *(uint64_t*)(localDesc + 0x48) = 0;
                            }
                        } else {
                            printf("[TARGET] NONE                                    \n");
                        }
                        
                        printf("[FSM] %-10s                                       \n", stateNames[state]);
                    }
                } __except(1) {}
            }
        }
        Sleep(150);
    }

    // Снимаем хук окна и возвращаем управление оригинальной процедуре
    SetWindowLongA(g_WowWnd, GWL_WNDPROC, (LONG)oWndProc);
    fclose(f); FreeConsole(); 
    FreeLibraryAndExitThread((HMODULE)lpParam, 0); 
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        CreateThread(NULL, 0, MainThread, hinstDLL, 0, NULL);
    }
    return TRUE;
}
