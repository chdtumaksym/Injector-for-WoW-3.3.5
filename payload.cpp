#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <vector>

#pragma comment(lib, "user32.lib")

// --- ОФФСЕТЫ 3.3.5a (12340) ---
#define STATIC_CLIENT_CONNECTION 0x00C79CE0
#define OFFSET_OBJECT_MANAGER    0x2ED0

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

// Функция для безопасного нажатия клавиш
void SendKey(BYTE vKey, bool up = false) {
    UINT scanCode = MapVirtualKey(vKey, 0);
    keybd_event(vKey, (BYTE)scanCode, up ? KEYEVENTF_KEYUP : 0, 0);
}

void WriteLog(const char* msg, int lvl, int hp) {
    FILE* log;
    if (fopen_s(&log, "bot_log.txt", "a") == 0) {
        fprintf(log, "[LOG] %-15s | Lvl: %2d | HP: %d\n", msg, lvl, hp);
        fclose(log);
    }
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin Solid Grind v5.0 (Restored & Polished) ---\n");
    printf("[*] Bindings: '1' Attack, 'G' Interact, '9' Heal.\n");
    printf("[*] Status: External Logic (Crash-Safe).\n");
    printf("--------------------------------------------------\n");

    uintptr_t connectionAddr = STATIC_CLIENT_CONNECTION;
    HWND wowWnd = FindWindowA(NULL, "World of Warcraft");
    
    BotState state = STATE_SEARCH;
    uint64_t targetGuid = 0;
    bool isMoving = false;
    int hudStartY = 6;
    int moveRefreshTimer = 0;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConn = 0;
        __try { clientConn = *(uintptr_t*)connectionAddr; } __except(1) { clientConn = 0; }
        
        SetConsoleCursor(0, hudStartY);

        if (clientConn) {
            uintptr_t mgr = *(uintptr_t*)(clientConn + OFFSET_OBJECT_MANAGER);
            if (mgr) {
                uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
                uint64_t myGuid = *(uint64_t*)(mgr + 0xC0);
                
                __try {
                    uintptr_t pLocal = 0;
                    while (cur != 0 && (cur & 1) == 0) {
                        if (*(uint64_t*)(cur + 0x30) == myGuid && myGuid != 0) { pLocal = cur; break; }
                        cur = *(uintptr_t*)(cur + 0x3C);
                    }

                    if (pLocal) {
                        uintptr_t pDesc = *(uintptr_t*)(pLocal + 0x8);
                        int hp = *(int*)(pDesc + 0x60), maxHp = *(int*)(pDesc + 0x80), lvl = *(int*)(pDesc + 0xD8);
                        float myX = *(float*)(pLocal + 0x798), myY = *(float*)(pLocal + 0x79C);

                        printf("[ME] Lvl:%d HP:%d/%d POS:%.1f, %.1f          \n", lvl, hp, maxHp, myX, myY);
                        
                        // --- ИНВЕНТАРЬ ---
                        printf("[BAG] ");
                        int items = 0;
                        for(int i=0; i<12; i++) {
                            uint64_t iGuid = *(uint64_t*)(pDesc + 0x640 + (i*8));
                            if (iGuid) {
                                uintptr_t icur = *(uintptr_t*)(mgr + 0xAC);
                                while(icur != 0 && (icur & 1) == 0) {
                                    if(*(uint64_t*)(icur + 0x30) == iGuid) {
                                        printf("%d ", *(int*)(*(uintptr_t*)(icur + 0x8) + 0xC));
                                        items++; break;
                                    }
                                    icur = *(uintptr_t*)(icur + 0x3C);
                                }
                            }
                        }
                        printf("(Total: %d)                                \n", items);
                        printf("--------------------------------------------------\n");

                        // Хил если прижало
                        if (hp > 0 && (hp * 100 / maxHp) < 45) {
                            if (GetForegroundWindow() == wowWnd) {
                                SendKey('9'); Sleep(50); SendKey('9', true);
                            }
                        }

                        // --- FSM ЛОГИКА ---
                        if (state == STATE_SEARCH) {
                            float bestDist = 50.0f; targetGuid = 0;
                            cur = *(uintptr_t*)(mgr + 0xAC);
                            while (cur != 0 && (cur & 1) == 0) {
                                if (*(int*)(cur + 0x14) == 3) {
                                    uintptr_t d = *(uintptr_t*)(cur + 0x8);
                                    if (*(int*)(d + 0x60) > 0 && *(int*)(d + 0xD8) <= lvl + 2 && *(uint64_t*)(d + 0x38) == 0) {
                                        float x = *(float*)(cur + 0x798), y = *(float*)(cur + 0x79C);
                                        float dist = sqrt(pow(x-myX, 2) + pow(y-myY, 2));
                                        if (dist < bestDist) { bestDist = dist; targetGuid = *(uint64_t*)(cur + 0x30); }
                                    }
                                }
                                cur = *(uintptr_t*)(cur + 0x3C);
                            }
                            if (targetGuid) { 
                                state = STATE_MOVE; 
                                *(uint64_t*)(pDesc + 0x48) = targetGuid; 
                                WriteLog("Target Locked", lvl, hp);
                            }
                        }

                        if (targetGuid != 0) {
                            uintptr_t tObj = 0; cur = *(uintptr_t*)(mgr + 0xAC);
                            while (cur != 0 && (cur & 1) == 0) { if(*(uint64_t*)(cur + 0x30) == targetGuid) { tObj = cur; break; } cur = *(uintptr_t*)(cur + 0x3C); }
                            
                            if (tObj) {
                                uintptr_t td = *(uintptr_t*)(tObj + 0x8);
                                int thp = *(int*)(td + 0x60);
                                float tx = *(float*)(tObj + 0x798), ty = *(float*)(tObj + 0x79C);
                                float dx = tx - myX, dy = ty - myY;
                                float dist = sqrt(dx*dx + dy*dy);
                                float cYaw = atan2(dy, dx); if (cYaw < 0) cYaw += 6.283185f;

                                printf("[TARGET] HP:%-5d Dist:%.2f yds                  \n", thp, dist);
                                
                                if (thp > 0) {
                                    if (GetForegroundWindow() == wowWnd) {
                                        *(float*)(pLocal + 0x7A8) = cYaw; // Пишем поворот
                                        
                                        if (dist > 3.8f) {
                                            state = STATE_MOVE;
                                            // Если долго бежим или только начали - освежаем нажатие W
                                            if (!isMoving || ++moveRefreshTimer > 20) { 
                                                SendKey('W'); 
                                                isMoving = true; 
                                                moveRefreshTimer = 0;
                                            }
                                        } else {
                                            state = STATE_COMBAT;
                                            if (isMoving) { SendKey('W', true); isMoving = false; }
                                            SendKey('1'); Sleep(30); SendKey('1', true);
                                        }
                                    } else {
                                        if (isMoving) { SendKey('W', true); isMoving = false; }
                                    }
                                } else {
                                    state = STATE_LOOT;
                                    if (isMoving) { SendKey('W', true); isMoving = false; }
                                    if (GetForegroundWindow() == wowWnd) {
                                        SendKey('G'); Sleep(50); SendKey('G', true);
                                        Sleep(1500); 
                                    }
                                    *(uint64_t*)(pDesc + 0x48) = 0; targetGuid = 0; state = STATE_SEARCH;
                                }
                            } else { 
                                targetGuid = 0; state = STATE_SEARCH; 
                                if (isMoving) { SendKey('W', true); isMoving = false; }
                            }
                        } else { 
                            printf("[TARGET] NONE                                    \n");
                            if (isMoving) { SendKey('W', true); isMoving = false; }
                        }
                        
                        printf("[FSM] %-10s | MOVING: %-3s                      \n", stateNames[state], isMoving ? "YES" : "NO");
                    }
                } __except(1) { printf("Memory exception.                                \n"); }
            }
        }
        Sleep(100);
    }

    if (isMoving) SendKey('W', true);
    fclose(f); FreeConsole(); FreeLibraryAndExitThread((HMODULE)lpParam, 0); return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,MainThread,h,0,0); }
    return TRUE;
}
