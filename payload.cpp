#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <vector>

#pragma comment(lib, "user32.lib")

// --- ОФФСЕТЫ 3.3.5a (12340) ---
#define OFFSET_S_CUR_MGR         0x00879CE0
#define OFFSET_OBJECT_MANAGER    0x2ED0
#define ADDR_TARGET_GUID         0x00BD07B0 // Адрес текущей цели в UI

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

// --- КРАШ ЛОГГЕР ---
LONG WINAPI CrashHandler(EXCEPTION_POINTERS* pExceptionInfo) {
    FILE* f;
    if (fopen_s(&f, "bot_crash_log.txt", "w") == 0) {
        fprintf(f, "=== ВОТ ТВОЙ КРАШ ЛОГ ===\n");
        fprintf(f, "Exception Code: 0x%X\n", pExceptionInfo->ExceptionRecord->ExceptionCode);
        fprintf(f, "Exception Address: 0x%p\n", pExceptionInfo->ExceptionRecord->ExceptionAddress);
        fprintf(f, "Если код 0xC0000005 - это Access Violation (Неверный адрес памяти).\n");
        fclose(f);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

// --- АППАРАТНАЯ ЭМУЛЯЦИЯ КЛАВИАТУРЫ (НЕ ИГНОРИРУЕТСЯ ИГРОЙ) ---
void PressKeyStr(WORD vKey) {
    INPUT ip = {0}; 
    ip.type = INPUT_KEYBOARD; 
    ip.ki.wScan = MapVirtualKey(vKey, MAPVK_VK_TO_VSC);
    ip.ki.dwFlags = KEYEVENTF_SCANCODE;
    SendInput(1, &ip, sizeof(INPUT));
}

void ReleaseKeyStr(WORD vKey) {
    INPUT ip = {0}; 
    ip.type = INPUT_KEYBOARD; 
    ip.ki.wScan = MapVirtualKey(vKey, MAPVK_VK_TO_VSC);
    ip.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    SendInput(1, &ip, sizeof(INPUT));
}

void TapKey(WORD vKey) {
    PressKeyStr(vKey); 
    Sleep(40); // Даем игре время заметить нажатие
    ReleaseKeyStr(vKey);
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    SetUnhandledExceptionFilter(CrashHandler); // Включаем сбор крашей
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin Hardware Grind v10.0 (Anti-Crash Edition) ---\n");
    printf("[!] ОВЕРЛЕИ DISCORD И STEAM ДОЛЖНЫ БЫТЬ ВЫКЛЮЧЕНЫ!\n");
    printf("[*] Binds: '1' Attack, 'G' Interact, '9' Heal.\n");
    printf("[*] Auto-Loot MUST be ON. Keep game window focused.\n");
    printf("[*] Hooks removed. Using DirectInput Emulation.\n");
    printf("--------------------------------------------------\n");

    uintptr_t connectionAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    HWND wowWnd = FindWindowA(NULL, "World of Warcraft");
    
    BotState state = STATE_SEARCH;
    uint64_t activeTargetGuid = 0;
    bool isMoving = false;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConn = 0;
        __try { clientConn = *(uintptr_t*)connectionAddr; } __except(1) { clientConn = 0; }
        
        SetConsoleCursor(0, 7);

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
                        
                        // Автохил паладина
                        if (hp > 0 && (hp * 100 / maxHp) < 40) {
                            if (GetForegroundWindow() == wowWnd) {
                                if (isMoving) { ReleaseKeyStr('W'); isMoving = false; }
                                TapKey('9'); 
                                Sleep(1500); // Ждем каст
                            }
                        }

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
                                *(uint64_t*)(pDesc + 0x48) = activeTargetGuid; // Память: таргет
                                *(uint64_t*)ADDR_TARGET_GUID = activeTargetGuid; // UI: таргет
                            }
                        }

                        if (activeTargetGuid != 0) {
                            // Форсим таргет каждый цикл
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
                                        *(float*)(pLocal + 0x7A8) = cYaw; // Поворот лица
                                        
                                        if (dist > 4.2f) {
                                            state = STATE_MOVE;
                                            if (!isMoving) { PressKeyStr('W'); isMoving = true; }
                                        } else {
                                            state = STATE_COMBAT;
                                            if (isMoving) { ReleaseKeyStr('W'); isMoving = false; }
                                            TapKey('1'); // Атака
                                        }
                                    } else {
                                        printf("[!] ОКНО ИГРЫ НЕ АКТИВНО! Пауза...              \n");
                                        if (isMoving) { ReleaseKeyStr('W'); isMoving = false; }
                                    }
                                } else {
                                    state = STATE_LOOT;
                                    if (isMoving) { ReleaseKeyStr('W'); isMoving = false; }
                                    printf("[LOOT] Нажимаю Interact...                        \n");
                                    if (GetForegroundWindow() == wowWnd) {
                                        TapKey('G'); // Двойной клик на лут для надежности
                                        Sleep(200);
                                        TapKey('G');
                                        Sleep(2000); // Время на открытие окна и автосбор
                                    }
                                    *(uint64_t*)(pDesc + 0x48) = 0; *(uint64_t*)ADDR_TARGET_GUID = 0; 
                                    activeTargetGuid = 0; state = STATE_SEARCH;
                                }
                            } else { 
                                activeTargetGuid = 0; state = STATE_SEARCH; 
                                if (isMoving) { ReleaseKeyStr('W'); isMoving = false; }
                            }
                        } else { 
                            printf("[TARGET] ИЩУ ЦЕЛЬ...                              \n");
                            if (isMoving) { ReleaseKeyStr('W'); isMoving = false; }
                        }
                        
                        printf("[STATE] %-15s | КНОПКА БЕГА: %s          \n", stateNames[state], isMoving ? "НАЖАТА" : "ОТПУЩЕНА");
                    }
                } __except(1) { printf("[!] Ошибка чтения памяти.                         \n"); }
            }
        }
        Sleep(100);
    }

    if (isMoving) ReleaseKeyStr('W');
    fclose(f); FreeConsole(); FreeLibraryAndExitThread((HMODULE)lpParam, 0); return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,MainThread,h,0,0); }
    return TRUE;
}
