#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>

// --- АДРЕСА 3.3.5a (12340) ---
#define ADDR_S_CUR_MGR          0x00C79CE0
#define OFFSET_OBJECT_MANAGER   0x2ED0
#define ADDR_CLICK_TO_MOVE      0x00611130
#define ADDR_TARGET_GUID        0x00BD07B8

// Константы ClickToMove для 3.3.5a
#define CTM_LOOT                6
#define CTM_ATTACK_GUID         11

#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

typedef void(__thiscall* tClickToMove)(uintptr_t pThis, int type, uint64_t* guid, float* pos, float prec);

bool g_Active = false;
WNDPROC oWndProc = nullptr;

struct CTM_BUFFER { uint64_t guid; float pos[3]; };
static CTM_BUFFER g_ctm;

void SafeAction(uintptr_t pLocal, int type, uint64_t guid, float x, float y, float z) {
    if (!pLocal) return;
    g_ctm.guid = guid; g_ctm.pos[0] = x; g_ctm.pos[1] = y; g_ctm.pos[2] = z;
    ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, type, &g_ctm.guid, g_ctm.pos, 0.5f);
}

void BotPulse() {
    if (!g_Active) return;

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

    uint64_t attackGuid = 0; float attackDist = 40.0f; float aPos[3] = {0};
    uint64_t lootGuid = 0;   float lootDist = 15.0f;   float lPos[3] = {0};

    cur = *(uintptr_t*)(mgr + 0xAC);
    while (cur && (cur & 1) == 0) {
        if (*(int*)(cur + 0x14) == 3) { // Unit (Игроки/Мобы)
            uintptr_t desc = *(uintptr_t*)(cur + 0x8);
            if (desc) {
                uint64_t objGuid = *(uint64_t*)(cur + 0x30);
                if (objGuid != localGuid) { // Игнорируем себя
                    int hp = *(int*)(desc + 0x60);
                    uint32_t flags = *(uint32_t*)(desc + 0x114);
                    float tX = *(float*)(cur + 0x798);
                    float tY = *(float*)(cur + 0x79C);
                    float dist = sqrt(pow(tX - myX, 2) + pow(tY - myY, 2));

                    if (hp > 0) {
                        // Ищем ближайшую живую цель
                        if (dist < attackDist) {
                            attackDist = dist; attackGuid = objGuid;
                            aPos[0] = tX; aPos[1] = tY; aPos[2] = *(float*)(cur + 0x7A0);
                        }
                    } else if (hp <= 0 && (flags & 1)) { // Флаг 1 = Lootable
                        // Ищем ближайший труп с лутом
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
    // Ограничиваем вызовы: 2 раза в секунду, чтобы движок успевал обрабатывать путь
    if (GetTickCount() - lastAction > 500) {
        if (lootGuid && lootDist < 5.0f) {
            *(uint64_t*)ADDR_TARGET_GUID = lootGuid;
            SafeAction(pLocal, CTM_LOOT, lootGuid, lPos[0], lPos[1], lPos[2]);
            printf("Looting: %llu               \r", lootGuid);
            lastAction = GetTickCount();
        } 
        else if (attackGuid) {
            *(uint64_t*)ADDR_TARGET_GUID = attackGuid;
            SafeAction(pLocal, CTM_ATTACK_GUID, attackGuid, aPos[0], aPos[1], aPos[2]);
            printf("Attacking: %llu | Dist: %.1f  \r", attackGuid, attackDist);
            lastAction = GetTickCount();
        } 
        else if (lootGuid) {
            *(uint64_t*)ADDR_TARGET_GUID = lootGuid;
            SafeAction(pLocal, CTM_LOOT, lootGuid, lPos[0], lPos[1], lPos[2]);
            printf("Running to Loot: %llu       \r", lootGuid);
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
    printf("--- Bot v120: Perfect Executioner ---\n");

    HWND hwnd = FindWindowA(NULL, "World of Warcraft");
    if (!hwnd) {
        printf("[-] ERROR: WoW window not found!\n");
        return 0;
    }

    oWndProc = (WNDPROC)SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)HookedWndProc);
    SetTimer(hwnd, 1337, 500, NULL);

    printf("[+] Setup Complete.\n");
    printf("[!] CRITICAL: Make sure 'Click-to-Move' is enabled in Interface -> Mouse.\n");
    printf("[+] Focus WoW window and press [INSERT] to start farming.\n");
    return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) CreateThread(0, 0, Setup, 0, 0, 0);
    return TRUE;
}
