#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>
#include <vector>
#include <algorithm>

#define ADDR_S_CUR_MGR          0x00C79CE0
#define OFFSET_OBJECT_MANAGER   0x2ED0
#define ADDR_TARGET_GUID        0x00BD07B8 
#define ADDR_ON_RIGHT_CLICK     0x0060BEA0 

#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

// [!] ФИКС КРАША 0xc0000005: Жестко используем __fastcall, чтобы ecx = targetPtr
typedef void(__fastcall* tOnRightClick)(uintptr_t ecx, uintptr_t edx);

bool g_Active = false;
WNDPROC oWndProc = nullptr;
std::vector<uint64_t> g_Blacklist; 
uint64_t g_BotTarget = 0; 

float GetDistance3D(float x1, float y1, float z1, float x2, float y2, float z2) {
    return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2) + pow(z2 - z1, 2));
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
    uintptr_t targetPtr = 0; 

    if (g_BotTarget != 0) {
        cur = *(uintptr_t*)(mgr + 0xAC);
        while (cur && (cur & 1) == 0) {
            if (*(uint64_t*)(cur + 0x30) == g_BotTarget) {
                int objType = *(int*)(cur + 0x14);
                if (objType == 3 || objType == 4) { 
                    targetPtr = cur; 
                    uintptr_t desc = *(uintptr_t*)(cur + 0x8);
                    if (desc) {
                        targetHp = *(int*)(desc + 0x60); 
                        targetFlags = *(uint32_t*)(desc + 0x114);
                        hasTarget = true;
                    }
                }
                break;
            }
            cur = *(uintptr_t*)(cur + 0x3C);
        }
        if (!hasTarget) g_BotTarget = 0; 
    }

    static uint64_t lastAttacked = 0;
    static uint64_t lastLooted = 0;

    if (hasTarget && targetPtr) {
        *(uint64_t*)ADDR_TARGET_GUID = g_BotTarget; 

        if (targetHp > 0) {
            // Цель жива. Кликаем по ней правой кнопкой ОДИН РАЗ. 
            // WoW сам добежит и начнет бить.
            if (g_BotTarget != lastAttacked) {
                ((tOnRightClick)ADDR_ON_RIGHT_CLICK)(targetPtr, 0);
                lastAttacked = g_BotTarget;
                printf("Engaging Enemy (Game Engine AI taking over)...\n");
            }
        } 
        else if (targetHp <= 0 && (targetFlags & 1)) {
            // Цель мертва и с лутом. Кликаем правой кнопкой ОДИН РАЗ.
            // WoW сам подбежит к трупу и соберет лут.
            if (g_BotTarget != lastLooted) {
                ((tOnRightClick)ADDR_ON_RIGHT_CLICK)(targetPtr, 0);
                lastLooted = g_BotTarget;
                printf("Looting Corpse...\n");
            }
        } 
        else {
            g_Blacklist.push_back(g_BotTarget);
            g_BotTarget = 0; 
            lastAttacked = 0;
            lastLooted = 0;
            *(uint64_t*)ADDR_TARGET_GUID = 0; 
            printf("Target Empty. Blacklisted.\n");
        }
    } 
    else {
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
            printf("\nFound new target! Dist: %.1f\n", bestDist);
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
                printf("\n[!] BOT STATUS: %s\n", g_Active ? "ACTIVE" : "PAUSED");
                if (!g_Active) { 
                    g_BotTarget = 0; 
                    *(uint64_t*)ADDR_TARGET_GUID = 0; 
                    g_Blacklist.clear(); 
                }
            }
        } else {
            isPressed = false;
        }

        static DWORD lastTick = 0;
        static bool isPulsing = false; 

        if (g_Active && !isPulsing && (GetTickCount() - lastTick > 400)) {
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
    AllocConsole(); freopen("CONOUT$", "w", stdout);
    printf("--- Bot v16.0: Perfect Right-Click Engine ---\n");

    HWND hwnd = FindWindowA(NULL, "World of Warcraft");
    if (!hwnd) return 0;

    oWndProc = (WNDPROC)SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)HookedWndProc);
    SetTimer(hwnd, 1337, 50, NULL); 

    printf("[+] Memory-safe OnRightClick initialized.\n");
    printf("[!] Auto-attack and Pathfinding now handled by WoW Engine.\n");
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
