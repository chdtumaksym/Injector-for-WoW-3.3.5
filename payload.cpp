#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>

// --- АДРЕСА 3.3.5a (12340) ---
#define ADDR_S_CUR_MGR          0x00C79CE0
#define OFFSET_OBJECT_MANAGER   0x2ED0
#define ADDR_CLICK_TO_MOVE      0x00611130
#define ADDR_MOUSEOVER_GUID     0x00BD07A0
#define ADDR_LUA_EXECUTE        0x00819210

// Используем только Move (Тип 4). Никаких Attack или Loot для CTM!
#define CTM_MOVE                4

#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

typedef void(__thiscall* tClickToMove)(uintptr_t pThis, int type, uint64_t* guid, float* pos, float prec);
typedef void(__cdecl* tLuaExecute)(const char* code, const char* fileName, int state);

bool g_Active = false;
WNDPROC oWndProc = nullptr;

struct CTM_BUFFER { uint64_t guid; float pos[3]; };
static CTM_BUFFER g_ctm;

// Безопасное выполнение Lua-кода
void ExecuteLua(const char* command) {
    ((tLuaExecute)ADDR_LUA_EXECUTE)(command, "bot_core", 0);
}

// Захват цели через мышь (безопасно для движка)
void SelectTarget(uint64_t guid) {
    *(uint64_t*)ADDR_MOUSEOVER_GUID = guid;
    ExecuteLua("TargetUnit('mouseover')");
}

// Безопасное передвижение (Без крашей UnitReaction)
void SafeMove(uintptr_t pLocal, float x, float y, float z) {
    if (!pLocal) return;
    g_ctm.guid = 0; // Для передвижения GUID должен быть 0!
    g_ctm.pos[0] = x; g_ctm.pos[1] = y; g_ctm.pos[2] = z;
    ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, CTM_MOVE, &g_ctm.guid, g_ctm.pos, 0.5f);
}

void BotPulse() {
    if (!g_Active) return;

    uintptr_t conn = *(uintptr_t*)ADDR_S_CUR_MGR;
    uintptr_t mgr = conn ? *(uintptr_t*)(conn + OFFSET_OBJECT_MANAGER) : 0;
    if (!mgr) return;

    uint64_t localGuid = *(uint64_t*)(mgr + 0xC0);
    uintptr_t pLocal = 0;
    float myX = 0, myY = 0, myZ = 0;

    // Ищем себя
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

    uint64_t attackGuid = 0; float attackDist = 40.0f; float aPos[3] = {0};
    uint64_t lootGuid = 0;   float lootDist = 15.0f;   float lPos[3] = {0};

    cur = *(uintptr_t*)(mgr + 0xAC);
    while (cur && (cur & 1) == 0) {
        if (*(int*)(cur + 0x14) == 3) { // Unit
            uintptr_t desc = *(uintptr_t*)(cur + 0x8);
            if (desc) {
                uint64_t objGuid = *(uint64_t*)(cur + 0x30);
                if (objGuid != localGuid) {
                    int hp = *(int*)(desc + 0x60);
                    uint32_t flags = *(uint32_t*)(desc + 0x114);
                    float tX = *(float*)(cur + 0x798);
                    float tY = *(float*)(cur + 0x79C);
                    float dist = sqrt(pow(tX - myX, 2) + pow(tY - myY, 2));

                    if (hp > 0) {
                        if (dist < attackDist) {
                            attackDist = dist; attackGuid = objGuid;
                            aPos[0] = tX; aPos[1] = tY; aPos[2] = *(float*)(cur + 0x7A0);
                        }
                    } else if (hp <= 0 && (flags & 1)) {
                        if (dist < lootDist) {
                            lootDist = dist; lootGuid = objGuid;
                            lPos[0] = tX; lPos[1] = tY; lPos[2] = *(float*)(cur + 0x7A0);
                        }
                    }
                }
            }
        }
        cur = *(uintptr_t*)(cur + 0x3C);
    }

    static DWORD lastAction = 0;
    // Тик раз в 500мс
    if (GetTickCount() - lastAction > 500) {
        
        // ПРИОРИТЕТ 1: ЛУТ
        if (lootGuid) {
            if (lootDist > 4.5f) {
                // Если далеко - просто бежим туда (CTM Type 4)
                SafeMove(pLocal, lPos[0], lPos[1], lPos[2]);
                printf("Moving to Loot... Dist: %.1f    \r", lootDist);
            } else {
                // Если близко - лутаем через безопасный Lua API
                SelectTarget(lootGuid);
                ExecuteLua("InteractUnit('target')");
                printf("Looting!                        \r");
            }
            lastAction = GetTickCount();
        } 
        // ПРИОРИТЕТ 2: АТАКА
        else if (attackGuid) {
            if (attackDist > 4.5f) {
                // Бежим к врагу
                SafeMove(pLocal, aPos[0], aPos[1], aPos[2]);
                printf("Moving to Target... Dist: %.1f  \r", attackDist);
            } else {
                // Бьем врага через Lua
                SelectTarget(attackGuid);
                ExecuteLua("AttackTarget()");
                printf("Attacking!                      \r");
            }
            lastAction = GetTickCount();
        }
    }
}

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_INSERT) {
        g_Active = !g_Active;
        Beep(g_Active ? 800 : 400, 100);
        printf("\n[!] BOT STATUS: %s\n", g_Active ? "ACTIVE" : "PAUSED");
    }
    if (uMsg == WM_TIMER && wParam == 1337) {
        BotPulse();
    }
    return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
}

DWORD WINAPI Setup(LPVOID) {
    AllocConsole(); freopen("CONOUT$", "w", stdout);
    printf("--- Bot v122: Safe-Mode Taxi Engine ---\n");

    HWND hwnd = FindWindowA(NULL, "World of Warcraft");
    if (!hwnd) {
        printf("[-] ERROR: WoW window not found!\n");
        return 0;
    }

    oWndProc = (WNDPROC)SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)HookedWndProc);
    SetTimer(hwnd, 1337, 500, NULL);

    printf("[+] Setup Complete. Crash-proof logic activated.\n");
    printf("[+] Focus WoW window and press [INSERT] to start farming.\n");
    return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) CreateThread(0, 0, Setup, 0, 0, 0);
    return TRUE;
}
