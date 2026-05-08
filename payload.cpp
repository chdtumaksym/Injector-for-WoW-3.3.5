#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "user32.lib")

// --- ОФФСЕТЫ 3.3.5a (12340) ---
#define OFFSET_S_CUR_MGR         0x00879CE0
#define OFFSET_OBJECT_MANAGER    0x2ED0
#define ADDR_TARGET_GUID         0x00BD07B0 // Адрес текущей цели в UI

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

// --- АППАРАТНАЯ ЭМУЛЯЦИЯ НАЖАТИЙ ---
void SetKey(WORD vKey, bool down) {
    INPUT ip = {0}; 
    ip.type = INPUT_KEYBOARD; 
    ip.ki.wVk = vKey;
    ip.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &ip, sizeof(INPUT));
}

void TapKey(WORD vKey) {
    SetKey(vKey, true);
    Sleep(40);
    SetKey(vKey, false);
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin Ultimate Hardware v14.0 (Timeout Edition) ---\n");
    printf("[!] BIND REQUIRES: '1' - Attack, 'G' - Interact, '9' - Heal.\n");
    printf("[!] AUTO-LOOT MUST BE ENABLED IN GAME SETTINGS.\n");
    printf("[!] GAME WINDOW MUST BE FOCUSED TO RUN AND ATTACK.\n");
    printf("--------------------------------------------------\n");

    uintptr_t connectionAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    HWND wowWnd = FindWindowA(NULL, "World of Warcraft");
    
    BotState state = STATE_SEARCH;
    uint64_t activeTargetGuid = 0;
    bool isMoving = false;
    int moveTimer = 0;
    
    // Переменные для отслеживания зависаний (спасибо Копайлоту за идею)
    DWORD stateStartTime = GetTickCount();

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConn = 0;
        __try { clientConn = *(uintptr_t*)connectionAddr; } __except(1) { clientConn = 0; }
        
        SetConsoleCursor(0, 6);

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

                        printf("[PLAYER] HP: %d/%d | LVL: %d | POS: %.1f, %.1f      \n", hp, maxHp, lvl, myX, myY);
                        printf("--------------------------------------------------\n");

                        if (hp > 0 && (hp * 100 / maxHp) < 40) {
                            if (GetForegroundWindow() == wowWnd) {
                                if (isMoving) { SetKey('W', false); isMoving = false; }
                                TapKey('9'); 
                                Sleep(1500); 
                            }
                        }

                        DWORD elapsed = GetTickCount() - stateStartTime;

                        // --- FSM ---
                        if (state == STATE_SEARCH) {
                            float bestDist = 45.0f; activeTargetGuid = 0;
                            cur = *(uintptr_t*)(mgr + 0xAC);
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
                                stateStartTime = GetTickCount(); // Обновляем таймер
                                *(uint64_t*)(pDesc + 0x48) = activeTargetGuid; 
                                *(uint64_t*)ADDR_TARGET_GUID = activeTargetGuid; 
                            }
                        }

                        if (activeTargetGuid != 0) {
                            *(uint64_t*)(pDesc + 0x48) = activeTargetGuid;
                            *(uint64_t*)ADDR_TARGET_GUID = activeTargetGuid;

                            uintptr_t tObj = 0; cur = *(uintptr_t*)(mgr + 0xAC);
                            while (cur != 0 && (cur & 1) == 0) { if(*(uint64_t*)(cur+0x30) == activeTargetGuid) { tObj = cur; break; } cur = *(uintptr_t*)(cur+0x3C); }
                            
                            if (tObj) {
                                uintptr_t td = *(uintptr_t*)(tObj + 0x8);
                                int thp = *(int*)(td + 0x60);
                                float tx = *(float*)(tObj + 0x798), ty = *(float*)(tObj + 0x79C);
                                float dx = tx - myX, dy = ty - myY;
                                float dist = sqrt(dx*dx + dy*dy);
                                float cYaw = atan2(dy, dx); if (cYaw < 0) cYaw += 6.283185f;

                                printf("[TARGET] HP: %-5d | DIST: %.2f yds                  \n", thp, dist);
                                
                                if (thp > 0) {
                                    if (GetForegroundWindow() == wowWnd) {
                                        *(float*)(pLocal + 0x7A8) = cYaw; 
                                        
                                        if (dist > 4.2f) {
                                            // Если бежим дольше 12 секунд - застряли. Откат.
                                            if (state == STATE_MOVE && elapsed > 12000) {
                                                printf("[!] ЗАСТРЯЛ! Отменяю цель...                 \n");
                                                if (isMoving) { SetKey('W', false); isMoving = false; }
                                                activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                                                continue;
                                            }

                                            if (state != STATE_MOVE) { state = STATE_MOVE; stateStartTime = GetTickCount(); }
                                            
                                            if (!isMoving || ++moveTimer > 15) { 
                                                SetKey('W', true); 
                                                isMoving = true; 
                                                moveTimer = 0;
                                            }
                                        } else {
                                            // Если бой длится дольше 25 секунд - что-то сломалось. Откат.
                                            if (state == STATE_COMBAT && elapsed > 25000) {
                                                printf("[!] БОЙ ЗАБАГАЛСЯ! Ищу новую цель...         \n");
                                                if (isMoving) { SetKey('W', false); isMoving = false; }
                                                activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                                                continue;
                                            }

                                            if (state != STATE_COMBAT) { state = STATE_COMBAT; stateStartTime = GetTickCount(); }

                                            if (isMoving) { SetKey('W', false); isMoving = false; }
                                            TapKey('1'); 
                                            Sleep(100); 
                                        }
                                    } else {
                                        printf("[!] ОКНО НЕ В ФОКУСЕ! Пауза...                  \n");
                                        if (isMoving) { SetKey('W', false); isMoving = false; }
                                    }
                                } else {
                                    if (state != STATE_LOOT) { state = STATE_LOOT; stateStartTime = GetTickCount(); }
                                    
                                    if (isMoving) { SetKey('W', false); isMoving = false; }
                                    
                                    printf("[STATE] ОЖИДАНИЕ СЕРВЕРА...                      \n");
                                    Sleep(1200); 
                                    
                                    if (GetForegroundWindow() == wowWnd) {
                                        printf("[STATE] СБОР ЛУТА (НАЖАТИЕ G)...                 \n");
                                        TapKey('G'); 
                                        Sleep(200);
                                        TapKey('G'); 
                                        
                                        Sleep(2000); 
                                    }
                                    
                                    *(uint64_t*)(pDesc + 0x48) = 0; 
                                    *(uint64_t*)ADDR_TARGET_GUID = 0; 
                                    activeTargetGuid = 0; 
                                    state = STATE_SEARCH;
                                    stateStartTime = GetTickCount();
                                }
                            } else { 
                                activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                                if (isMoving) { SetKey('W', false); isMoving = false; }
                            }
                        } else { 
                            printf("[TARGET] ПОИСК ЦЕЛИ...                            \n");
                            if (isMoving) { SetKey('W', false); isMoving = false; }
                            stateStartTime = GetTickCount(); // Сброс таймера в поиске
                        }
                        
                        printf("[FSM] %-15s | RUN KEY: %s          \n", stateNames[state], isMoving ? "PRESSED" : "RELEASED");
                    }
                } __except(1) {}
            }
        }
        Sleep(100);
    }

    if (isMoving) SetKey('W', false);
    fclose(f); FreeConsole(); FreeLibraryAndExitThread((HMODULE)lpParam, 0); return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,MainThread,h,0,0); }
    return TRUE;
}
