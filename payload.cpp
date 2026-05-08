#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <clocale>
#include <stdint.h>

#pragma comment(lib, "user32.lib")

#define OFFSET_S_CUR_MGR         0x00879CE0
#define OFFSET_OBJECT_MANAGER    0x2ED0
#define ADDR_TARGET_GUID         0x00BD07B0 

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

template <typename T>
T SafeRead(uintptr_t address) {
    T buffer = T();
    if (!address) return buffer;
    __try { buffer = *(T*)address; } __except(1) {}
    return buffer;
}

template <typename T>
void SafeWrite(uintptr_t address, T value) {
    if (!address) return;
    __try { *(T*)address = value; } __except(1) {}
}

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

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

DWORD WINAPI MainThread(LPVOID lpParam) {
    setlocale(LC_ALL, "Russian");
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin Hybrid Internal v40.0 ---\n");
    printf("[+] Внутренняя память (0ms) + Внешние клики (Anti-Crash)\n");
    printf("[!] БИНДЫ: Атака(1), Хил(9), Бег(W), Взаимодействие(F)\n");
    printf("--------------------------------------------------\n");

    uintptr_t connectionAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    HWND wowWnd = FindWindowA(NULL, "World of Warcraft");
    
    BotState state = STATE_SEARCH;
    uint64_t activeTargetGuid = 0;
    bool isMoving = false;
    DWORD stateStartTime = GetTickCount();
    DWORD lastActionTime = 0;

    while (!GetAsyncKeyState(VK_END)) {
        wowWnd = FindWindowA(NULL, "World of Warcraft");
        uintptr_t clientConn = SafeRead<uintptr_t>(connectionAddr);
        SetConsoleCursor(0, 5);

        if (!clientConn || !wowWnd) {
            printf("[!] Ожидание процесса игры...                                \n");
            Sleep(500); continue;
        }

        uintptr_t mgr = SafeRead<uintptr_t>(clientConn + OFFSET_OBJECT_MANAGER);
        if (mgr) {
            uintptr_t cur = SafeRead<uintptr_t>(mgr + 0xAC);
            uint64_t myGuid = SafeRead<uint64_t>(mgr + 0xC0);
            uintptr_t pLocal = 0;

            while (cur && (cur & 1) == 0) {
                if (SafeRead<uint64_t>(cur + 0x30) == myGuid && myGuid != 0) { pLocal = cur; break; }
                cur = SafeRead<uintptr_t>(cur + 0x3C);
            }

            if (pLocal) {
                uintptr_t pDesc = SafeRead<uintptr_t>(pLocal + 0x8);
                int hp = SafeRead<int>(pDesc + 0x60), maxHp = SafeRead<int>(pDesc + 0x80), lvl = SafeRead<int>(pDesc + 0xD8);
                float myX = SafeRead<float>(pLocal + 0x798), myY = SafeRead<float>(pLocal + 0x79C);

                printf("[PLAYER] HP: %-5d/%-5d | LVL: %d | POS: %.1f, %.1f      \n", hp, maxHp, lvl, myX, myY);

                if (hp > 0 && maxHp > 0 && (hp * 100 / maxHp) < 40) {
                    if (isMoving) { SetBackgroundKey(wowWnd, 'W', false); isMoving = false; }
                    TapBackgroundKey(wowWnd, '9'); Sleep(2000); 
                }

                DWORD elapsed = GetTickCount() - stateStartTime;

                if (state == STATE_SEARCH) {
                    float bestDist = 45.0f; activeTargetGuid = 0;
                    cur = SafeRead<uintptr_t>(mgr + 0xAC);
                    while (cur && (cur & 1) == 0) {
                        if (SafeRead<int>(cur + 0x14) == 3) { 
                            uintptr_t d = SafeRead<uintptr_t>(cur + 0x8);
                            int mHp = SafeRead<int>(d + 0x60), mLvl = SafeRead<int>(d + 0xD8);
                            if (mHp > 0 && mLvl <= lvl + 2 && SafeRead<uint64_t>(d + 0x38) == 0) {
                                float dx = SafeRead<float>(cur + 0x798) - myX, dy = SafeRead<float>(cur + 0x79C) - myY;
                                float dist = sqrt(dx*dx + dy*dy);
                                if (dist < bestDist) { bestDist = dist; activeTargetGuid = SafeRead<uint64_t>(cur + 0x30); }
                            }
                        }
                        cur = SafeRead<uintptr_t>(cur + 0x3C);
                    }
                    if (activeTargetGuid != 0) { state = STATE_MOVE; stateStartTime = GetTickCount(); }
                }

                if (activeTargetGuid != 0) {
                    SafeWrite<uint64_t>(pDesc + 0x48, activeTargetGuid);
                    SafeWrite<uint64_t>(ADDR_TARGET_GUID, activeTargetGuid);

                    uintptr_t tObj = 0; cur = SafeRead<uintptr_t>(mgr + 0xAC);
                    while (cur && (cur & 1) == 0) { 
                        if(SafeRead<uint64_t>(cur+0x30) == activeTargetGuid) { tObj = cur; break; } 
                        cur = SafeRead<uintptr_t>(cur+0x3C); 
                    }
                    
                    if (tObj) {
                        int thp = SafeRead<int>(SafeRead<uintptr_t>(tObj + 0x8) + 0x60);
                        float tx = SafeRead<float>(tObj + 0x798), ty = SafeRead<float>(tObj + 0x79C);
                        float dx = tx - myX, dy = ty - myY;
                        float dist = sqrt(dx*dx + dy*dy);
                        float cYaw = atan2(dy, dx); if (cYaw < 0) cYaw += 6.283185f;
                        
                        if (thp > 0) {
                            SafeWrite<float>(pLocal + 0x7A8, cYaw); // Мгновенный поворот
                            
                            if (dist > 4.5f) {
                                if (state == STATE_MOVE && elapsed > 12000) {
                                    if (isMoving) { SetBackgroundKey(wowWnd, 'W', false); isMoving = false; }
                                    activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                                    continue;
                                }
                                if (state != STATE_MOVE) { state = STATE_MOVE; stateStartTime = GetTickCount(); }
                                if (!isMoving) { SetBackgroundKey(wowWnd, 'W', true); isMoving = true; }
                            } else {
                                if (state == STATE_COMBAT && elapsed > 25000) {
                                    if (isMoving) { SetBackgroundKey(wowWnd, 'W', false); isMoving = false; }
                                    activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                                    continue;
                                }
                                if (state != STATE_COMBAT) { state = STATE_COMBAT; stateStartTime = GetTickCount(); }
                                if (isMoving) { SetBackgroundKey(wowWnd, 'W', false); isMoving = false; }
                                if (GetTickCount() - lastActionTime > 1000) {
                                    TapBackgroundKey(wowWnd, '1'); 
                                    lastActionTime = GetTickCount();
                                }
                            }
                        } else {
                            if (state != STATE_LOOT) { state = STATE_LOOT; stateStartTime = GetTickCount(); }
                            if (isMoving) { SetBackgroundKey(wowWnd, 'W', false); isMoving = false; }
                            Sleep(1200); 
                            TapBackgroundKey(wowWnd, 'F'); Sleep(300); TapBackgroundKey(wowWnd, 'F');
                            Sleep(2000); 
                            SafeWrite<uint64_t>(pDesc + 0x48, 0); SafeWrite<uint64_t>(ADDR_TARGET_GUID, 0);
                            activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                        }
                    } else { 
                        activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                        if (isMoving) { SetBackgroundKey(wowWnd, 'W', false); isMoving = false; }
                    }
                } else { 
                    stateStartTime = GetTickCount();
                    if (isMoving) { SetBackgroundKey(wowWnd, 'W', false); isMoving = false; }
                }
                printf("[FSM] State: %-15s | Hybrid PostMessage: ACTIVE \n", stateNames[state]);
            }
        }
        Sleep(50);
    }

    if (isMoving) SetBackgroundKey(wowWnd, 'W', false);
    fclose(f); FreeConsole(); FreeLibraryAndExitThread((HMODULE)lpParam, 0); return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,MainThread,h,0,0); }
    return TRUE;
}
