#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>

// --- АДРЕСА 3.3.5a (12340) ---
#define ADDR_S_CUR_MGR          0x00C79CE0
#define OFFSET_OBJECT_MANAGER   0x2ED0
#define ADDR_CLICK_TO_MOVE      0x00611130
#define ADDR_TARGET_GUID        0x00BD07B8
#define ADDR_LUA_EXECUTE        0x00819210

#define CTM_MOVE                4 // Используем ТОЛЬКО безопасный тип движения

#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

typedef void(__thiscall* tClickToMove)(uintptr_t pThis, int type, uint64_t* guid, float* pos, float prec);
typedef void(__cdecl* tLuaExecute)(const char* code, const char* fileName, int state);

bool g_Active = false;
WNDPROC oWndProc = nullptr;

struct CTM_BUFFER { uint64_t guid; float pos[3]; };
static CTM_BUFFER g_ctm;

// Безопасное выполнение макросов в главном потоке
void ExecuteLua(const char* command) {
    ((tLuaExecute)ADDR_LUA_EXECUTE)(command, "bot_core", 0);
}

// Движение по координатам. Никакого взаимодействия с целями!
void SafeMove(uintptr_t pLocal, float x, float y, float z) {
    if (!pLocal) return;
    g_ctm.guid = 0; 
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

    // Читаем GUID текущей цели, которую игра ВЫБРАЛА САМА
    uint64_t targetGuid = *(uint64_t*)ADDR_TARGET_GUID;
    bool hasTarget = false;
    int targetHp = 0;
    uint32_t targetFlags = 0;
    float targetPos[3] = {0};

    if (targetGuid) {
        cur = *(uintptr_t*)(mgr + 0xAC);
        while (cur && (cur & 1) == 0) {
            if (*(uint64_t*)(cur + 0x30) == targetGuid) {
                uintptr_t desc = *(uintptr_t*)(cur + 0x8);
                if (desc) {
                    targetHp = *(int*)(desc + 0x60);
                    targetFlags = *(uint32_t*)(desc + 0x114);
                    targetPos[0] = *(float*)(cur + 0x798);
                    targetPos[1] = *(float*)(cur + 0x79C);
                    targetPos[2] = *(float*)(cur + 0x7A0);
                    hasTarget = true;
                }
                break;
            }
            cur = *(uintptr_t*)(cur + 0x3C);
        }
    }

    static DWORD lastAction = 0;
    if (GetTickCount() - lastAction > 500) {
        if (hasTarget) {
            float dist = sqrt(pow(targetPos[0] - myX, 2) + pow(targetPos[1] - myY, 2));

            if (targetHp > 0) {
                // Живая цель - подбегаем и бьем
                if (dist > 4.5f) {
                    SafeMove(pLocal, targetPos[0], targetPos[1], targetPos[2]);
                    printf("Moving to Enemy... Dist: %.1f \r", dist);
                } else {
                    ExecuteLua("AttackTarget()");
                    printf("Attacking Enemy!              \r");
                }
            } else if (targetHp <= 0 && (targetFlags & 1)) {
                // Мертвая цель с лутом - подбегаем и лутаем
                if (dist > 4.5f) {
                    SafeMove(pLocal, targetPos[0], targetPos[1], targetPos[2]);
                    printf("Moving to Loot... Dist: %.1f  \r", dist);
                } else {
                    ExecuteLua("InteractUnit('target')");
                    printf("Looting Corpse!               \r");
                }
            } else {
                // Цель мертва и пуста - сбрасываем таргет
                ExecuteLua("ClearTarget()");
                printf("Target Empty. Clearing...     \r");
            }
        } else {
            // Если нет цели - доверяем движку найти ближайшего врага
            ExecuteLua("TargetNearestEnemy()");
            printf("Scanning for enemies...       \r");
        }
        lastAction = GetTickCount();
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
    printf("--- Bot v125: Perfect Auto-Grinder ---\n");

    HWND hwnd = FindWindowA(NULL, "World of Warcraft");
    if (!hwnd) {
        printf("[-] ERROR: WoW window not found!\n");
        return 0;
    }

    oWndProc = (WNDPROC)SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)HookedWndProc);
    SetTimer(hwnd, 1337, 500, NULL);

    printf("[+] Smart Lua Targeting Initialized.\n");
    printf("[!] Make sure 'Click-to-Move' and 'Auto Loot' are ON.\n");
    printf("[+] Press [INSERT] to unleash the bot.\n");
    return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) CreateThread(0, 0, Setup, 0, 0, 0);
    return TRUE;
}
