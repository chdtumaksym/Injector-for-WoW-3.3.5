#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "user32.lib")

// --- СТАТИЧНЫЕ ОФФСЕТЫ 3.3.5a (12340) ---
#define OFFSET_S_CUR_MGR         0x00879CE0
#define OFFSET_OBJECT_MANAGER    0x2ED0
#define ADDR_TARGET_GUID         0x00BD07B0 

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

// --- БЕЗОПАСНОЕ ЧТЕНИЕ ПАМЯТИ ---
template <typename T>
T SafeRead(uintptr_t address) {
    T buffer = T();
    if (address == 0) return buffer;
    __try { buffer = *(T*)address; } 
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return buffer;
}

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

// --- ФОНОВАЯ ЭМУЛЯЦИЯ СООБЩЕНИЙ (PostMessage) ---
// Работает без хуков, не крашит игру, работает в фоне
void SetBackgroundKey(HWND hWnd, WORD vKey, bool down) {
    if (!hWnd) return;
    UINT scanCode = MapVirtualKey(vKey, MAPVK_VK_TO_VSC);
    if (down) {
        LPARAM lParamDown = (scanCode << 16) | 1;
        PostMessageA(hWnd, WM_KEYDOWN, vKey, lParamDown);
    } else {
        LPARAM lParamUp = (1 << 31) | (1 << 30) | (scanCode << 16) | 1;
        PostMessageA(hWnd, WM_KEYUP, vKey, lParamUp);
    }
}

void TapBackgroundKey(HWND hWnd, WORD vKey) {
    SetBackgroundKey(hWnd, vKey, true);
    Sleep(50); 
    SetBackgroundKey(hWnd, vKey, false);
}

// Эмуляция правого клика мыши по центру окна (заменяет кнопку взаимодействия/лута)
void SimulateRightClickCenter(HWND hWnd) {
    if (!hWnd) return;
    RECT rect;
    GetClientRect(hWnd, &rect);
    int centerX = (rect.right - rect.left) / 2;
    int centerY = (rect.bottom - rect.top) / 2;
    // Слегка смещаем вверх, так как модель моба обычно чуть выше центра
    centerY -= 30; 
    
    LPARAM lParam = MAKELPARAM(centerX, centerY);
    PostMessageA(hWnd, WM_RBUTTONDOWN, MK_RBUTTON, lParam);
    Sleep(50);
    PostMessageA(hWnd, WM_RBUTTONUP, 0, lParam);
}

// --- ОСНОВНОЙ ПОТОК ---
DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin Phantom v29.0 (No Hooks, Mouse Sim) ---\n");
    printf("[!] ИНТЕРНАЛ ХУКИ ВЫРЕЗАНЫ. КРАШЕЙ БОЛЬШЕ НЕ БУДЕТ.\n");
    printf("[!] ТРЕБОВАНИЯ:\n");
    printf("1. Автосбор добычи ОБЯЗАН БЫТЬ ВКЛЮЧЕН в настройках.\n");
    printf("2. Кнопки атаки и лута НЕ НУЖНЫ (эмулируется мышь).\n");
    printf("3. Бег остался на кнопке 'W'.\n");
    printf("--------------------------------------------------\n");

    uintptr_t connectionAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    HWND wowWnd = FindWindowA(NULL, "World of Warcraft");
    
    BotState state = STATE_SEARCH;
    uint64_t activeTargetGuid = 0;
    bool isMoving = false;
    int moveTimer = 0;
    DWORD stateStartTime = GetTickCount();
    DWORD lastActionTime = 0;

    while (!GetAsyncKeyState(VK_END)) {
        wowWnd = FindWindowA(NULL, "World of Warcraft");
        uintptr_t clientConn = SafeRead<uintptr_t>(connectionAddr);
        SetConsoleCursor(0, 8);

        if (!clientConn || !wowWnd) {
            printf("[!] Ожидание процесса игры...                                \n");
            Sleep(500);
            continue;
        }

        uintptr_t mgr = SafeRead<uintptr_t>(clientConn + OFFSET_OBJECT_MANAGER);
        if (mgr) {
            uintptr_t cur = SafeRead<uintptr_t>(mgr + 0xAC);
            uint64_t myGuid = SafeRead<uint64_t>(mgr + 0xC0);
            uintptr_t pLocal = 0;

            while (cur != 0 && (cur & 1) == 0) {
                if (SafeRead<uint64_t>(cur + 0x30) == myGuid && myGuid != 0) { pLocal = cur; break; }
                cur = SafeRead<uintptr_t>(cur + 0x3C);
            }

            if (pLocal) {
                uintptr_t pDesc = SafeRead<uintptr_t>(pLocal + 0x8);
                int hp = SafeRead<int>(pDesc + 0x60), maxHp = SafeRead<int>(pDesc + 0x80), lvl = SafeRead<int>(pDesc + 0xD8);
                float myX = SafeRead<float>(pLocal + 0x798), myY = SafeRead<float>(pLocal + 0x79C);

                printf("[PLAYER] HP: %-5d/%-5d | LVL: %d | POS: %.1f, %.1f      \n", hp, maxHp, lvl, myX, myY);
                printf("--------------------------------------------------\n");

                DWORD elapsed = GetTickCount() - stateStartTime;

                // --- FSM ---
                if (state == STATE_SEARCH) {
                    float bestDist = 45.0f; activeTargetGuid = 0;
                    cur = SafeRead<uintptr_t>(mgr + 0xAC);
                    
                    while (cur != 0 && (cur & 1) == 0) {
                        if (SafeRead<int>(cur + 0x14) == 3) { 
                            uintptr_t d = SafeRead<uintptr_t>(cur + 0x8);
                            int mHp = SafeRead<int>(d + 0x60), mLvl = SafeRead<int>(d + 0xD8);
                            uint64_t mSummoner = SafeRead<uint64_t>(d + 0x38);
                            
                            if (mHp > 0 && mLvl <= lvl + 2 && mSummoner == 0) {
                                float dx = SafeRead<float>(cur + 0x798) - myX, dy = SafeRead<float>(cur + 0x79C) - myY;
                                float dist = sqrt(dx*dx + dy*dy);
                                if (dist < bestDist) { bestDist = dist; activeTargetGuid = SafeRead<uint64_t>(cur + 0x30); }
                            }
                        }
                        cur = SafeRead<uintptr_t>(cur + 0x3C);
                    }
                    
                    if (activeTargetGuid != 0) { 
                        state = STATE_MOVE; 
                        stateStartTime = GetTickCount(); 
                    }
                }

                if (activeTargetGuid != 0) {
                    // Память обновляем мгновенно
                    if (pDesc) *(uint64_t*)(pDesc + 0x48) = activeTargetGuid; 
                    *(uint64_t*)ADDR_TARGET_GUID = activeTargetGuid;

                    uintptr_t tObj = 0; cur = SafeRead<uintptr_t>(mgr + 0xAC);
                    while (cur != 0 && (cur & 1) == 0) { 
                        if(SafeRead<uint64_t>(cur+0x30) == activeTargetGuid) { tObj = cur; break; } 
                        cur = SafeRead<uintptr_t>(cur+0x3C); 
                    }
                    
                    if (tObj) {
                        uintptr_t td = SafeRead<uintptr_t>(tObj + 0x8);
                        int thp = SafeRead<int>(td + 0x60);
                        float tx = SafeRead<float>(tObj + 0x798), ty = SafeRead<float>(tObj + 0x79C);
                        float dx = tx - myX, dy = ty - myY;
                        float dist = sqrt(dx*dx + dy*dy);
                        float cYaw = atan2(dy, dx); if (cYaw < 0) cYaw += 6.283185f;

                        printf("[TARGET] HP: %-5d | DIST: %.2f yds                  \n", thp, dist);
                        
                        if (thp > 0) {
                            *(float*)(pLocal + 0x7A8) = cYaw; // Мгновенный поворот
                            
                            if (dist > 4.2f) {
                                if (state == STATE_MOVE && elapsed > 12000) {
                                    printf("[!] ЗАСТРЯЛ! Сбрасываю таргет...              \n");
                                    if (isMoving) { SetBackgroundKey(wowWnd, 'W', false); isMoving = false; }
                                    activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                                    continue;
                                }
                                if (state != STATE_MOVE) { state = STATE_MOVE; stateStartTime = GetTickCount(); }
                                
                                if (!isMoving || ++moveTimer > 15) { 
                                    SetBackgroundKey(wowWnd, 'W', true); 
                                    isMoving = true; 
                                    moveTimer = 0;
                                }
                            } else {
                                if (state == STATE_COMBAT && elapsed > 25000) {
                                    printf("[!] БОЙ ЗАБАГАЛСЯ! Сбрасываю...               \n");
                                    if (isMoving) { SetBackgroundKey(wowWnd, 'W', false); isMoving = false; }
                                    activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                                    continue;
                                }
                                if (state != STATE_COMBAT) { state = STATE_COMBAT; stateStartTime = GetTickCount(); }

                                if (isMoving) { SetBackgroundKey(wowWnd, 'W', false); isMoving = false; }
                                
                                // Симуляция правого клика запускает автоатаку
                                if (GetTickCount() - lastActionTime > 1500) {
                                    SimulateRightClickCenter(wowWnd);
                                    lastActionTime = GetTickCount();
                                }
                            }
                        } else {
                            if (state != STATE_LOOT) { state = STATE_LOOT; stateStartTime = GetTickCount(); }
                            if (isMoving) { SetBackgroundKey(wowWnd, 'W', false); isMoving = false; }
                            
                            printf("[STATE] ЖДЕМ ГЕНЕРАЦИЮ ТРУПА СЕРВЕРОМ...         \n");
                            Sleep(1200); 
                            
                            printf("[STATE] ФОНОВЫЙ КЛИК (СБОР ЛУТА)...              \n");
                            // Правый клик по центру трупа для лута
                            SimulateRightClickCenter(wowWnd);
                            Sleep(300);
                            SimulateRightClickCenter(wowWnd); // Дубль для надежности
                            Sleep(2000); 
                            
                            if (pDesc) *(uint64_t*)(pDesc + 0x48) = 0; 
                            *(uint64_t*)ADDR_TARGET_GUID = 0; 
                            activeTargetGuid = 0; 
                            state = STATE_SEARCH;
                            stateStartTime = GetTickCount();
                        }
                    } else { 
                        activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                        if (isMoving) { SetBackgroundKey(wowWnd, 'W', false); isMoving = false; }
                    }
                } else { 
                    printf("[TARGET] ПОИСК НОВОЙ ЦЕЛИ...                      \n");
                    stateStartTime = GetTickCount();
                    if (isMoving) { SetBackgroundKey(wowWnd, 'W', false); isMoving = false; }
                }
                printf("[FSM] %-15s | MOUSE SIM: ACTIVE        \n", stateNames[state]);
            }
        }
        Sleep(100);
    }

    if (isMoving) SetBackgroundKey(wowWnd, 'W', false);
    fclose(f); FreeConsole(); FreeLibraryAndExitThread((HMODULE)lpParam, 0); return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,MainThread,h,0,0); }
    return TRUE;
}
