#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>
#include <vector>
#include <algorithm>

// --- АДРЕСА 3.3.5a (12340) ---
#define ADDR_S_CUR_MGR          0x00C79CE0
#define OFFSET_OBJECT_MANAGER   0x2ED0
#define ADDR_CLICK_TO_MOVE      0x00727400 
#define ADDR_TARGET_GUID        0x00BD07B8 

// [!] СЕКРЕТНОЕ ОРУЖИЕ: Настоящая функция Правого Клика [!]
#define ADDR_ON_RIGHT_CLICK     0x0060BEA0 
typedef void(__thiscall* tOnRightClick)(uintptr_t pUnit);
typedef void(__fastcall* tClickToMove)(uintptr_t ecx, uintptr_t edx, int type, uint64_t* guid, float* pos, float prec);

#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

bool g_Active = false;
WNDPROC oWndProc = nullptr;
std::vector<uint64_t> g_Blacklist; 
uint64_t g_BotTarget = 0; 

float GetDistance3D(float x1, float y1, float z1, float x2, float y2, float z2) {
    return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2) + pow(z2 - z1, 2));
}

void ActionCTM(uintptr_t pLocal, int type, uint64_t guid, float x, float y, float z) {
    static uint64_t lastGuid = 0;
    static int lastType = 0;
    static float lastX = 0, lastY = 0, lastZ = 0;

    float diff = sqrt(pow(x - lastX, 2) + pow(y - lastY, 2) + pow(z - lastZ, 2));

    if (guid != lastGuid || type != lastType || diff > 2.0f) {
        uint64_t ctmGuid = guid;
        float ctmPos[3] = { x, y, z };
        
        ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, 0, type, &ctmGuid, ctmPos, 0.5f);
        
        lastGuid = guid; lastType = type;
        lastX = x; lastY = y; lastZ = z;
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
    uintptr_t targetPtr = 0; // Сохраняем указатель на моба в памяти!

    if (g_BotTarget != 0) {
        cur = *(uintptr_t*)(mgr + 0xAC);
        while (cur && (cur & 1) == 0) {
            if (*(uint64_t*)(cur + 0x30) == g_BotTarget) {
                int objType = *(int*)(cur + 0x14);
                if (objType == 3 || objType == 4) { 
                    targetPtr = cur; // ЗАПОМНИЛИ УКАЗАТЕЛЬ ДЛЯ ПРАВОГО КЛИКА
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

    static uint64_t lastAttacked = 0;
    static uint64_t lastLooted = 0;

    if (hasTarget && targetPtr) {
        *(uint64_t*)ADDR_TARGET_GUID = g_BotTarget; // Берем в таргет в интерфейсе

        float dist = GetDistance3D(myX, myY, myZ, tX, tY, tZ);

        if (targetHp > 0) {
            // Моб жив
            if (dist > 4.5f) {
                ActionCTM(pLocal, 4, g_BotTarget, tX, tY, tZ); // Бежим (CTM_MOVE)
            } else {
                // Подошли в упор! Эмулируем правый клик!
                if (g_BotTarget != lastAttacked) {
                    ((tOnRightClick)ADDR_ON_RIGHT_CLICK)(targetPtr);
                    lastAttacked = g_BotTarget;
                    printf("Engaging Melee (Right-Click)!     \r");
                }
            }
        } 
        else if (targetHp <= 0 && (targetFlags & 1)) {
            // Моб мертв, есть лут
            if (dist > 4.5f) {
                ActionCTM(pLocal, 4, g_BotTarget, tX, tY, tZ); // Бежим к трупу
            } else {
                // В упор к трупу! Эмулируем правый клик (сбор лута)!
                if (g_BotTarget != lastLooted) {
                    ((tOnRightClick)ADDR_ON_RIGHT_CLICK)(targetPtr);
                    lastLooted = g_BotTarget;
                    printf("Looting Corpse (Right-Click)!     \r");
                }
            }
        } 
        else {
            // Моб облутан. В ЧС и сбрасываем всё.
            g_Blacklist.push_back(g_BotTarget);
            g_BotTarget = 0; 
            lastAttacked = 0;
            lastLooted = 0;
            *(uint64_t*)ADDR_TARGET_GUID = 0; 
            printf("Target Empty. Blacklisted.          \n");
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
            printf("\nFound new target! Dist: %.1f        \n", bestDist);
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
    printf("--- Bot v15.0: The Right-Click Emulator ---\n");

    HWND hwnd = FindWindowA(NULL, "World of Warcraft");
    if (!hwnd) {
        return 0;
    }

    oWndProc = (WNDPROC)SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)HookedWndProc);
    SetTimer(hwnd, 1337, 50, NULL); 

    printf("[+] Direct Memory Interaction (OnRightClick) Activated.\n");
    printf("[!] Make sure 'Click-to-Move' and 'Auto Loot' are ON.\n");
    printf("[+] Press [INSERT] to unleash the bot.\n");
    return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(0, 0, Setup, 0, 0, 0);
    }
    return TRUE;
}
