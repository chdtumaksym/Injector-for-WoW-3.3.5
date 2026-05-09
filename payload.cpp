#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>
#include <vector>
#include <algorithm>

// ==========================================
// --- АДРЕСА ПАМЯТИ WoW 3.3.5a (12340) ---
// ==========================================
#define ADDR_S_CUR_MGR          0x00C79CE0
#define OFFSET_OBJECT_MANAGER   0x2ED0
#define ADDR_CLICK_TO_MOVE      0x00727400 
#define ADDR_TARGET_GUID        0x00BD07B8 
#define ADDR_MOUSEOVER_GUID     0x00BD07A0 
#define ADDR_LUA_EXECUTE        0x00819210 

// Возвращаем правильные боевые типы!
#define CTM_LOOT                6  
#define CTM_ATTACK              11 

#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

typedef void(__fastcall* tClickToMove)(uintptr_t ecx, uintptr_t edx, int type, uint64_t* guid, float* pos, float prec);
typedef void(__cdecl* tLuaExecute)(const char* code, const char* fileName, int state);

bool g_Active = false;
WNDPROC oWndProc = nullptr;
HWND g_WoWHwnd = NULL;
HINSTANCE g_hModule = NULL; 

std::vector<uint64_t> g_Blacklist; 
uint64_t g_BotTarget = 0; 

float GetDistance3D(float x1, float y1, float z1, float x2, float y2, float z2) {
    return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2) + pow(z2 - z1, 2));
}

void ExecuteLua(const char* command) {
    ((tLuaExecute)ADDR_LUA_EXECUTE)(command, "bot_core", 0);
}

void ProgrammaticTarget(uint64_t guid) {
    if (*(uint64_t*)ADDR_TARGET_GUID != guid) {
        *(uint64_t*)ADDR_MOUSEOVER_GUID = guid;
        ExecuteLua("TargetUnit('mouseover')");
    }
}

void BotPulse() {
    uintptr_t conn = *(uintptr_t*)ADDR_S_CUR_MGR;
    uintptr_t mgr = conn ? *(uintptr_t*)(conn + OFFSET_OBJECT_MANAGER) : 0;
    if (!mgr) return;

    uint64_t localGuid = *(uint64_t*)(mgr + 0xC0);
    uintptr_t pLocal = 0;
    float myX = 0, myY = 0, myZ = 0;

    uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
    while (cur && (cur & 1) == 0) {
        if (*(uint64_t*)(cur + 0x30) == localGuid) {
            pLocal = cur;
            myX = *(float*)(cur + 0x798); 
            myY = *(float*)(cur + 0x79C); 
            myZ = *(float*)(cur + 0x7A0);
            break;
        }
        cur = *(uintptr_t*)(cur + 0x3C);
    }
    if (!pLocal) return;

    bool hasTarget = false;
    int targetHp = 0;
    uint32_t targetFlags = 0;
    float tX = 0, tY = 0, tZ = 0;

    if (g_BotTarget != 0) {
        cur = *(uintptr_t*)(mgr + 0xAC);
        while (cur && (cur & 1) == 0) {
            if (*(uint64_t*)(cur + 0x30) == g_BotTarget) {
                int objType = *(int*)(cur + 0x14);
                if (objType == 3 || objType == 4) { 
                    uintptr_t desc = *(uintptr_t*)(cur + 0x8);
                    if (desc) {
                        targetHp = *(int*)(desc + 0x60); 
                        targetFlags = *(uint32_t*)(desc + 0x114);
                        tX = *(float*)(cur + 0x798);
                        tY = *(float*)(cur + 0x79C);
                        tZ = *(float*)(cur + 0x7A0);
                        hasTarget = true;
                    }
                }
                break;
            }
            cur = *(uintptr_t*)(cur + 0x3C);
        }
        if (!hasTarget) g_BotTarget = 0; 
    }

    static DWORD stateTimer = 0;
    static bool hasAttemptedLoot = false;

    if (hasTarget) {
        ProgrammaticTarget(g_BotTarget);
        float dist = GetDistance3D(myX, myY, myZ, tX, tY, tZ);

        if (targetHp > 0) {
            //[СОСТОЯНИЕ: БОЙ] - ВЕРНУЛИ ТВОЮ РАБОЧУЮ ЛОГИКУ!
            hasAttemptedLoot = false;
            
            static DWORD lastChase = 0;
            if (GetTickCount() - lastChase > 1500) {
                uint64_t ctmGuid = g_BotTarget;
                float ctmPos[3] = { tX, tY, tZ };
                ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, 0, CTM_ATTACK, &ctmGuid, ctmPos, 0.5f);
                lastChase = GetTickCount();
            }

            if (dist < 5.0f) {
                static DWORD lastAtk = 0;
                if (GetTickCount() - lastAtk > 1500) {
                    ExecuteLua("StartAttack()");
                    lastAtk = GetTickCount();
                }
            }
            
            printf("\r[COMBAT] Dist: %-5.1f | HP: %-5d | Flags: %-3u       ", dist, targetHp, targetFlags);
        } 
        else {
            // [СОСТОЯНИЕ: МЕРТВ]
            if (targetFlags & 1) { 
                // Флаг лута ЕСТЬ
                if (!hasAttemptedLoot) {
                    hasAttemptedLoot = true;
                    stateTimer = GetTickCount();
                    
                    // Посылаем команду CTM_LOOT (6) СТРОГО ОДИН РАЗ
                    uint64_t ctmGuid = g_BotTarget;
                    float ctmPos[3] = { tX, tY, tZ };
                    ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, 0, CTM_LOOT, &ctmGuid, ctmPos, 0.5f);
                }
                
                DWORD waitTime = GetTickCount() - stateTimer;
                printf("\r[LOOTING] Dist: %-5.1f | Wait: %-4d ms | Flags: %-3u ", dist, waitTime, targetFlags);

                // Страховка: пылесосим лут через Lua, если окно открылось
                static DWORD lastLuaLoot = 0;
                if (GetTickCount() - lastLuaLoot > 500) {
                    ExecuteLua("if LootFrame:IsVisible() then for i=1, GetNumLootItems() do LootSlot(i) end end");
                    lastLuaLoot = GetTickCount();
                }

                // Если за 5 секунд флаг не пропал - забагалось (сумки полные)
                if (waitTime > 5000) {
                    printf("\n[!] Loot Timeout (Bags full?). Blacklisting.\n");
                    g_Blacklist.push_back(g_BotTarget);
                    g_BotTarget = 0; 
                    ExecuteLua("ClearTarget(); CloseLoot();"); 
                }
            } 
            else {
                // Флага лута НЕТ
                if (hasAttemptedLoot) {
                    // Если мы уже пытались лутать, и флаг пропал -> МЫ УСПЕШНО СОБРАЛИ ЛУТ!
                    printf("\n[+] Loot Successful! Moving on.\n");
                    g_Blacklist.push_back(g_BotTarget);
                    g_BotTarget = 0; 
                    ExecuteLua("ClearTarget(); CloseLoot();"); 
                } 
                else {
                    // Мы еще не лутали. Ждем, пока сервер пришлет флаг (до 1.5 сек)
                    static DWORD waitFlagTimer = 0;
                    static uint64_t waitGuid = 0;
                    
                    if (waitGuid != g_BotTarget) {
                        waitGuid = g_BotTarget;
                        waitFlagTimer = GetTickCount();
                    }

                    DWORD waitTime = GetTickCount() - waitFlagTimer;
                    printf("\r[WAIT_FLAG] Dist: %-5.1f | Wait: %-4d ms | Flags: %-3u ", dist, waitTime, targetFlags);

                    if (waitTime > 1500) {
                        printf("\n[-] No loot on this corpse. Blacklisting.\n");
                        g_Blacklist.push_back(g_BotTarget);
                        g_BotTarget = 0; 
                        ExecuteLua("ClearTarget()"); 
                    }
                }
            }
        }
    } 
    else {
        //[СОСТОЯНИЕ: ПОИСК]
        uint64_t bestGuid = 0;
        float bestDist = 40.0f; 

        cur = *(uintptr_t*)(mgr + 0xAC);
        while (cur && (cur & 1) == 0) {
            int type = *(int*)(cur + 0x14);
            if (type == 3) { 
                uint64_t guid = *(uint64_t*)(cur + 0x30);
                
                if (std::find(g_Blacklist.begin(), g_Blacklist.end(), guid) == g_Blacklist.end()) {
                    uintptr_t desc = *(uintptr_t*)(cur + 0x8);
                    if (desc) {
                        int hp = *(int*)(desc + 0x60); 
                        int maxHp = *(int*)(desc + 0x80); 
                        
                        if (hp > 0 && maxHp > 1) {
                            float mX = *(float*)(cur + 0x798);
                            float mY = *(float*)(cur + 0x79C);
                            float mZ = *(float*)(cur + 0x7A0);
                            float dist = GetDistance3D(myX, myY, myZ, mX, mY, mZ);
                            
                            if (dist < bestDist && dist > 0.1f) {
                                bestDist = dist;
                                bestGuid = guid;
                            }
                        }
                    }
                }
            }
            cur = *(uintptr_t*)(cur + 0x3C);
        }

        if (bestGuid) {
            g_BotTarget = bestGuid; 
            ProgrammaticTarget(g_BotTarget);
            printf("\n[+] Found new target! Dist: %.1f\n", bestDist);
        } else {
            printf("\r[SCANNING] Looking for enemies...                      ");
        }
    }
}

// ==========================================
// --- ПОТОК ВЫГРУЗКИ (КНОПКА END) ---
// ==========================================
DWORD WINAPI EjectThread(LPVOID) {
    SetWindowLongA(g_WoWHwnd, GWL_WNDPROC, (LONG)oWndProc); 
    KillTimer(g_WoWHwnd, 1337);
    Sleep(100);
    FreeConsole();
    FreeLibraryAndExitThread(g_hModule, 0); 
    return 0;
}

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_TIMER && wParam == 1337) {
        
        if (GetAsyncKeyState(VK_END) & 0x8000) {
            g_Active = false;
            printf("\n\n[!] EJECTING BOT... You can close this console.\n");
            CreateThread(0, 0, EjectThread, 0, 0, 0);
            return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
        }

        static bool isPressed = false;
        if (GetAsyncKeyState(VK_INSERT) & 0x8000) {
            if (!isPressed) {
                isPressed = true;
                g_Active = !g_Active;
                Beep(g_Active ? 800 : 400, 100);
                printf("\n\n[!] BOT STATUS: %s\n", g_Active ? "ACTIVE" : "PAUSED");
                
                if (!g_Active) { 
                    g_BotTarget = 0; 
                    ExecuteLua("ClearTarget()");
                    g_Blacklist.clear(); 
                }
            }
        } else {
            isPressed = false;
        }

        static DWORD lastTick = 0;
        static bool isPulsing = false; 

        if (g_Active && !isPulsing && (GetTickCount() - lastTick > 200)) { 
            isPulsing = true;
            __try {
                BotPulse();
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
            lastTick = GetTickCount();
            isPulsing = false;
        }
    }
    return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
}

DWORD WINAPI Setup(LPVOID) {
    AllocConsole(); 
    freopen("CONOUT$", "w", stdout);
    printf("--- Bot v143: The Perfect Hybrid ---\n");

    g_WoWHwnd = FindWindowA(NULL, "World of Warcraft");
    if (!g_WoWHwnd) return 0;

    oWndProc = (WNDPROC)SetWindowLongA(g_WoWHwnd, GWL_WNDPROC, (LONG)HookedWndProc);
    SetTimer(g_WoWHwnd, 1337, 50, NULL); 

    printf("[+] Combat Engine: RESTORED (CTM_ATTACK + StartAttack).\n");
    printf("[+] Loot State-Machine: ACTIVE.\n");
    printf("[!] Press [INSERT] to Start/Pause.\n");
    printf("[!] Press [END] to Unload the Bot.\n\n");
    return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        g_hModule = h; 
        DisableThreadLibraryCalls(h);
        CreateThread(0, 0, Setup, 0, 0, 0);
    }
    return TRUE;
}
