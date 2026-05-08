#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>
#include <vector>
#include <algorithm>

// --- АДРЕСА 3.3.5a (12340) ---
#define ADDR_S_CUR_MGR          0x00C79CE0
#define OFFSET_OBJECT_MANAGER   0x2ED0
#define ADDR_CLICK_TO_MOVE      0x00611130
#define ADDR_TARGET_GUID        0x00BD07B8
#define ADDR_LUA_EXECUTE        0x00819210

#define CTM_MOVE                4 
#define CTM_INTERACT            5 
#define CTM_ATTACK              7 

#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

typedef void(__thiscall* tClickToMove)(uintptr_t pThis, int type, uint64_t* guid, float* pos, float prec);
typedef void(__cdecl* tLuaExecute)(const char* code, const char* fileName, int state);

bool g_Active = false;
WNDPROC oWndProc = nullptr;
std::vector<uint64_t> g_Blacklist; 

// Безопасное выполнение незащищенных макросов (например, сброс цели)
void ExecuteLua(const char* command) {
    ((tLuaExecute)ADDR_LUA_EXECUTE)(command, "bot_core", 0);
}

float GetDistance3D(float x1, float y1, float z1, float x2, float y2, float z2) {
    return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2) + pow(z2 - z1, 2));
}

void ActionCTM(uintptr_t pLocal, int type, uint64_t guid, float x, float y, float z) {
    static uint64_t lastGuid = 0;
    static int lastType = 0;
    static DWORD lastTime = 0;

    // Защита от спама команд (раз в 3 сек или при смене цели)
    if (guid != lastGuid || type != lastType || (GetTickCount() - lastTime > 3000)) {
        
        // ВАЖНО: Создаем локальные копии переменных для стека, чтобы игра не читала мусор
        uint64_t ctmGuid = guid;
        float ctmPos[3] = { x, y, z };
        
        ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, type, &ctmGuid, ctmPos, 0.5f);
        
        lastGuid = guid;
        lastType = type;
        lastTime = GetTickCount();
    }
}

void BotPulse() {
    uintptr_t conn = *(uintptr_t*)ADDR_S_CUR_MGR;
    uintptr_t mgr = conn ? *(uintptr_t*)(conn + OFFSET_OBJECT_MANAGER) : 0;
    if (!mgr) return;

    uint64_t localGuid = *(uint64_t*)(mgr + 0xC0);
    uintptr_t pLocal = 0;
    float myX = 0, myY = 0, myZ = 0;

    // 1. Ищем локального игрока
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

    uint64_t targetGuid = *(uint64_t*)ADDR_TARGET_GUID;
    bool hasTarget = false;
    int targetHp = 0, targetMaxHp = 0;
    uint32_t targetFlags = 0;
    float tX = 0, tY = 0, tZ = 0;

    // 2. Читаем инфу о ТЕКУЩЕЙ цели с ЖЕСТКОЙ проверкой типа!
    if (targetGuid) {
        cur = *(uintptr_t*)(mgr + 0xAC);
        while (cur && (cur & 1) == 0) {
            if (*(uint64_t*)(cur + 0x30) == targetGuid) {
                
                int objType = *(int*)(cur + 0x14);
                // Защита от краша 0x0044d9c4 (UnitReaction): 
                // Продолжаем работу ТОЛЬКО если цель это Юнит (3) или Игрок (4)
                if (objType == 3 || objType == 4) {
                    uintptr_t desc = *(uintptr_t*)(cur + 0x8);
                    if (desc) {
                        targetHp = *(int*)(desc + 0x60);
                        targetMaxHp = *(int*)(desc + 0x70);
                        targetFlags = *(uint32_t*)(desc + 0x114);
                        tX = *(float*)(cur + 0x798);
                        tY = *(float*)(cur + 0x79C);
                        tZ = *(float*)(cur + 0x7A0);
                        hasTarget = true;
                    }
                } else {
                    // Игрок взял в таргет что-то не то (сундук, руду). Сбрасываем!
                    ExecuteLua("ClearTarget()"); 
                }
                break;
            }
            cur = *(uintptr_t*)(cur + 0x3C);
        }
    }

    // 3. Логика боя / Лута
    if (hasTarget) {
        float dist = GetDistance3D(myX, myY, myZ, tX, tY, tZ);

        if (targetHp > 0) {
            ActionCTM(pLocal, CTM_ATTACK, targetGuid, tX, tY, tZ);
            printf("Attacking Target... Dist: %.1f      \r", dist);
        } 
        else if (targetHp <= 0 && (targetFlags & 1)) {
            ActionCTM(pLocal, CTM_INTERACT, targetGuid, tX, tY, tZ);
            printf("Looting Corpse... Dist: %.1f        \r", dist);
        } 
        else {
            // Цель мертва и пуста. Заносим в ЧС и БЕЗОПАСНО сбрасываем цель.
            g_Blacklist.push_back(targetGuid);
            ExecuteLua("ClearTarget()");
            printf("Target Empty. Blacklisted.          \r");
        }
    } 
    // 4. Поиск новой цели
    else {
        uint64_t bestTarget = 0;
        float bestDist = 40.0f; // Макс. радиус агра
        float bX = 0, bY = 0, bZ = 0;

        cur = *(uintptr_t*)(mgr + 0xAC);
        while (cur && (cur & 1) == 0) {
            int type = *(int*)(cur + 0x14);
            
            if (type == 3) { // ТОЛЬКО NPC
                uint64_t guid = *(uint64_t*)(cur + 0x30);
                
                if (std::find(g_Blacklist.begin(), g_Blacklist.end(), guid) == g_Blacklist.end()) {
                    uintptr_t desc = *(uintptr_t*)(cur + 0x8);
                    if (desc) {
                        int hp = *(int*)(desc + 0x60);
                        int maxHp = *(int*)(desc + 0x70);
                        
                        if (hp > 0 && maxHp > 0) {
                            float mX = *(float*)(cur + 0x798);
                            float mY = *(float*)(cur + 0x79C);
                            float mZ = *(float*)(cur + 0x7A0);
                            float dist = GetDistance3D(myX, myY, myZ, mX, mY, mZ);

                            if (dist < bestDist) {
                                bestDist = dist;
                                bestTarget = guid;
                                bX = mX; bY = mY; bZ = mZ;
                            }
                        }
                    }
                }
            }
            cur = *(uintptr_t*)(cur + 0x3C);
        }

        if (bestTarget) {
            // Нашли моба. Записываем в память (осторожно, т.к. мы уже проверили type == 3)
            *(uint64_t*)ADDR_TARGET_GUID = bestTarget;
            // И сразу командуем бежать к нему и бить
            ActionCTM(pLocal, CTM_ATTACK, bestTarget, bX, bY, bZ);
            printf("Found new target! Dist: %.1f        \r", bestDist);
        } else {
            printf("Scanning for enemies...             \r");
        }
    }
}

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_TIMER && wParam == 1337) {
        
        static bool isPressed = false;
        if (GetAsyncKeyState(VK_INSERT) & 0x8000) {
            if (!isPressed) {
                isPressed = true;
                g_Active = !g_Active;
                Beep(g_Active ? 800 : 400, 100);
                printf("\n[!] BOT STATUS: %s                                \n", g_Active ? "ACTIVE" : "PAUSED");
                if (!g_Active) { 
                    ExecuteLua("ClearTarget()"); 
                    g_Blacklist.clear(); 
                }
            }
        } else {
            isPressed = false;
        }

        static DWORD lastTick = 0;
        static bool isPulsing = false; // Блокировка от рекурсии/реентерабельности

        if (g_Active && !isPulsing && (GetTickCount() - lastTick > 400)) {
            isPulsing = true;
            __try {
                BotPulse();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                printf("\n[!] Crash Prevented by SEH Block!\n");
            }
            lastTick = GetTickCount();
            isPulsing = false;
        }
    }
    
    return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
}

DWORD WINAPI Setup(LPVOID) {
    AllocConsole(); freopen("CONOUT$", "w", stdout);
    printf("--- Bot v3.0: Crash-Proof Edition ---\n");

    HWND hwnd = FindWindowA(NULL, "World of Warcraft");
    if (!hwnd) {
        printf("[-] ERROR: WoW window not found!\n");
        return 0;
    }

    oWndProc = (WNDPROC)SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)HookedWndProc);
    SetTimer(hwnd, 1337, 50, NULL); 

    printf("[+] Object Type Filtering Active.\n");
    printf("[!] Make sure 'Click-to-Move' and 'Auto Loot' are ON.\n");
    printf("[+] Press [INSERT] to start/stop the bot.\n");
    return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(0, 0, Setup, 0, 0, 0);
    }
    return TRUE;
}
