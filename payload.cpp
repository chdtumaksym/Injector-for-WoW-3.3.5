#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "user32.lib")

// --- ЖЕЛЕЗОБЕТОННЫЕ ОФФСЕТЫ 3.3.5a (12340) ---
#define STATIC_CLIENT_CONNECTION 0x00C79CE0
#define OFFSET_OBJECT_MANAGER    0x2ED0

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

// --- ГЛАВНЫЙ ЦИКЛ БЕЗОПАСНОГО БОТА ---
DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Final Safe Paladin Bot (Zero Hooks Edition) ---\n");
    printf("[*] No internal calls = No crashes.\n");
    printf("[*] Bind '1': Attack | 'G': Interact | '9': Flash of Light.\n");
    printf("--------------------------------------------------\n");

    uintptr_t connectionAddr = STATIC_CLIENT_CONNECTION;
    HWND wowWnd = FindWindowA(NULL, "World of Warcraft");
    
    BotState state = STATE_SEARCH;
    uint64_t targetGuid = 0;
    bool isMoving = false;
    int hudStartY = 6;

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

                        printf("[ME] Lvl:%d HP:%d/%d | POS:%.1f, %.1f          \n", lvl, hp, maxHp, myX, myY);
                        
                        // САМОХИЛ ПАЛАДИНА (Если меньше 40% ХП)
                        if (hp > 0 && (hp * 100 / maxHp) < 40) {
                            if (GetForegroundWindow() == wowWnd) {
                                keybd_event('9', 0, 0, 0); Sleep(50); keybd_event('9', 0, KEYEVENTF_KEYUP, 0);
                            }
                        }

                        // --- FSM ---
                        if (state == STATE_SEARCH) {
                            float bestDist = 40.0f; targetGuid = 0;
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
                            if (targetGuid) { state = STATE_MOVE; *(uint64_t*)(pDesc + 0x48) = targetGuid; }
                        }

                        if (targetGuid) {
                            uintptr_t tObj = 0; cur = *(uintptr_t*)(mgr + 0xAC);
                            while (cur != 0 && (cur & 1) == 0) { if(*(uint64_t*)(cur+0x30) == targetGuid) { tObj = cur; break; } cur = *(uintptr_t*)(cur+0x3C); }
                            
                            if (tObj) {
                                uintptr_t td = *(uintptr_t*)(tObj + 0x8);
                                int thp = *(int*)(td + 0x60);
                                float tx = *(float*)(tObj + 0x798), ty = *(float*)(tObj + 0x79C);
                                float dx = tx - myX, dy = ty - myY;
                                float dist = sqrt(dx*dx + dy*dy);
                                float cYaw = atan2(dy, dx); if (cYaw < 0) cYaw += 6.283185f;

                                printf("[TARGET] HP:%d Dist:%.2f yds                  \n", thp, dist);
                                
                                if (thp > 0) {
                                    if (GetForegroundWindow() == wowWnd) {
                                        *(float*)(pLocal + 0x7A8) = cYaw; // Поворот
                                        
                                        if (dist > 4.5f) {
                                            state = STATE_MOVE;
                                            if (!isMoving) { keybd_event('W', 0x11, 0, 0); isMoving = true; }
                                        } else {
                                            state = STATE_COMBAT;
                                            if (isMoving) { keybd_event('W', 0x11, KEYEVENTF_KEYUP, 0); isMoving = false; }
                                            keybd_event('1', 0, 0, 0); keybd_event('1', 0, KEYEVENTF_KEYUP, 0);
                                        }
                                    }
                                } else {
                                    state = STATE_LOOT;
                                    if (isMoving) { keybd_event('W', 0x11, KEYEVENTF_KEYUP, 0); isMoving = false; }
                                    if (GetForegroundWindow() == wowWnd) {
                                        keybd_event('G', 0, 0, 0); Sleep(100); keybd_event('G', 0, KEYEVENTF_KEYUP, 0);
                                        Sleep(1500); 
                                    }
                                    *(uint64_t*)(pDesc + 0x48) = 0; targetGuid = 0; state = STATE_SEARCH;
                                }
                            } else { targetGuid = 0; state = STATE_SEARCH; *(uint64_t*)(pDesc + 0x48) = 0; }
                        } else { 
                            printf("[TARGET] NONE                                    \n");
                            if (isMoving) { keybd_event('W', 0x11, KEYEVENTF_KEYUP, 0); isMoving = false; }
                        }
                        
                        printf("[FSM] %-10s                                       \n", stateNames[state]);
                    }
                } __except(1) {}
            }
        }
        Sleep(100);
    }

    if (isMoving) keybd_event('W', 0x11, KEYEVENTF_KEYUP, 0);
    fclose(f); FreeConsole(); FreeLibraryAndExitThread((HMODULE)lpParam, 0); return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,MainThread,h,0,0); }
    return TRUE;
}
