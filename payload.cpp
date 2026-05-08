#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>
#include <vector>
#include <algorithm>

// --- АДРЕСА И ОФФСЕТЫ 3.3.5a (12340) ---
#define ADDR_S_CUR_MGR          0x00C79CE0
#define OFFSET_OBJECT_MANAGER   0x2ED0
#define ADDR_CLICK_TO_MOVE      0x00611130
#define ADDR_TARGET_GUID        0x00BD07B8

// Типы Click-To-Move (Встроенный ИИ игры)
#define CTM_MOVE                4 // Просто бежать
#define CTM_INTERACT            5 // Взаимодействовать (в т.ч. Лут)
#define CTM_ATTACK              7 // Бежать к цели и атаковать

#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

typedef void(__thiscall* tClickToMove)(uintptr_t pThis, int type, uint64_t* guid, float* pos, float prec);

bool g_Active = false;
WNDPROC oWndProc = nullptr;
std::vector<uint64_t> g_Blacklist; // Черный список пустых/багнутых мобов

struct CTM_BUFFER { uint64_t guid; float pos[3]; };
static CTM_BUFFER g_ctm;

// 1. Честная 3D дистанция
float GetDistance3D(float x1, float y1, float z1, float x2, float y2, float z2) {
    return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2) + pow(z2 - z1, 2));
}

// 2. Безопасная смена таргета напрямую в памяти (Без Lua!)
void SetTarget(uint64_t guid) {
    *(uint64_t*)ADDR_TARGET_GUID = guid;
}

// 3. Умный Click-To-Move с защитой от спама
void ActionCTM(uintptr_t pLocal, int type, uint64_t guid, float x, float y, float z) {
    static uint64_t lastGuid = 0;
    static int lastType = 0;
    static DWORD lastTime = 0;

    // Не спамим движок игры каждые 500мс, обновляем приказ раз в 3 сек или при смене цели
    if (guid != lastGuid || type != lastType || (GetTickCount() - lastTime > 3000)) {
        g_ctm.guid = guid;
        g_ctm.pos[0] = x; g_ctm.pos[1] = y; g_ctm.pos[2] = z;
        ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, type, &g_ctm.guid, g_ctm.pos, 0.5f);
        
        lastGuid = guid;
        lastType = type;
        lastTime = GetTickCount();
    }
}

void BotPulse() {
    if (!g_Active) return;

    uintptr_t conn = *(uintptr_t*)ADDR_S_CUR_MGR;
    uintptr_t mgr = conn ? *(uintptr_t*)(conn + OFFSET_OBJECT_MANAGER) : 0;
    if (!mgr) return;

    uint64_t localGuid = *(uint64_t*)(mgr + 0xC0);
    uintptr_t pLocal = 0;
    float myX = 0, myY = 0, myZ = 0;

    // Ищем своего персонажа
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

    uint64_t targetGuid = *(uint64_t*)ADDR_TARGET_GUID;
    bool hasTarget = false;
    int targetHp = 0, targetMaxHp = 0;
    uint32_t targetFlags = 0;
    float tX = 0, tY = 0, tZ = 0;

    // Читаем инфу о текущем таргете
    if (targetGuid) {
        cur = *(uintptr_t*)(mgr + 0xAC);
        while (cur && (cur & 1) == 0) {
            if (*(uint64_t*)(cur + 0x30) == targetGuid) {
                uintptr_t desc = *(uintptr_t*)(cur + 0x8);
                if (desc) {
                    targetHp = *(int*)(desc + 0x60);
                    targetMaxHp = *(int*)(desc + 0x70);
                    targetFlags = *(uint32_t*)(desc + 0x114);
                    tX = *(float*)(cur + 0x798);
                    tY = *(float*)(cur + 0x79C);
                    tZ = *(float*)(cur + 0x7A0);
                    hasTarget = true;
                }
                break;
            }
            cur = *(uintptr_t*)(cur + 0x3C);
        }
    }

    if (hasTarget) {
        float dist = GetDistance3D(myX, myY, myZ, tX, tY, tZ);

        if (targetHp > 0) {
            // Цель жива -> Используем встроенный механизм игры "Атаковать"
            // Игра сама добежит до цели и начнет бить!
            ActionCTM(pLocal, CTM_ATTACK, targetGuid, tX, tY, tZ);
            printf("Attacking Target... Dist: %.1f      \r", dist);
        } 
        else if (targetHp <= 0 && (targetFlags & 1)) {
            // Цель мертва и есть лут -> Используем механизм "Взаимодействовать"
            // (В настройках игры ДОЛЖЕН быть включен "Автоматический сбор добычи")
            ActionCTM(pLocal, CTM_INTERACT, targetGuid, tX, tY, tZ);
            printf("Looting Corpse... Dist: %.1f        \r", dist);
        } 
        else {
            // Цель мертва и пуста -> Заносим в ЧС и сбрасываем таргет
            g_Blacklist.push_back(targetGuid);
            SetTarget(0);
            printf("Target Empty. Blacklisted.          \r");
        }
    } 
    else {
        // --- 4. УМНЫЙ ПОИСК ЦЕЛИ ЧЕРЕЗ ПАМЯТЬ ---
        uint64_t bestTarget = 0;
        float bestDist = 40.0f; // Максимальный радиус агра 40 метров

        cur = *(uintptr_t*)(mgr + 0xAC);
        while (cur && (cur & 1) == 0) {
            int type = *(int*)(cur + 0x14);
            
            if (type == 3) { // Ищем ТОЛЬКО NPC/Монстров (Type = 3)
                uint64_t guid = *(uint64_t*)(cur + 0x30);
                
                // Проверяем, нет ли моба в черном списке
                if (std::find(g_Blacklist.begin(), g_Blacklist.end(), guid) == g_Blacklist.end()) {
                    uintptr_t desc = *(uintptr_t*)(cur + 0x8);
                    if (desc) {
                        int hp = *(int*)(desc + 0x60);
                        int maxHp = *(int*)(desc + 0x70);
                        
                        // Если моб живой
                        if (hp > 0 && maxHp > 0) {
                            float mX = *(float*)(cur + 0x798);
                            float mY = *(float*)(cur + 0x79C);
                            float mZ = *(float*)(cur + 0x7A0);
                            float dist = GetDistance3D(myX, myY, myZ, mX, mY, mZ);

                            if (dist < bestDist) {
                                bestDist = dist;
                                bestTarget = guid;
                            }
                        }
                    }
                }
            }
            cur = *(uintptr_t*)(cur + 0x3C);
        }

        if (bestTarget) {
            SetTarget(bestTarget);
            printf("Found new target! Dist: %.1f        \r", bestDist);
        } else {
            printf("Scanning for enemies...             \r");
        }
    }
}

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_INSERT) {
        g_Active = !g_Active;
        Beep(g_Active ? 800 : 400, 100);
        printf("\n[!] BOT STATUS: %s\n", g_Active ? "ACTIVE" : "PAUSED");
        if (!g_Active) { SetTarget(0); g_Blacklist.clear(); } // Очищаем ЧС при паузе
    }
    if (uMsg == WM_TIMER && wParam == 1337) {
        __try {
            BotPulse();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Перехватываем краши, если таймер выстрелил в момент выгрузки памяти игрой
        }
    }
    return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
}

DWORD WINAPI Setup(LPVOID) {
    AllocConsole(); freopen("CONOUT$", "w", stdout);
    printf("--- Bot v2.0: Native Memory Grinder ---\n");

    HWND hwnd = FindWindowA(NULL, "World of Warcraft");
    if (!hwnd) {
        printf("[-] ERROR: WoW window not found!\n");
        return 0;
    }

    oWndProc = (WNDPROC)SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)HookedWndProc);
    SetTimer(hwnd, 1337, 500, NULL);

    printf("[+] Object Manager Scanner Active.\n");
    printf("[!] Make sure 'Click-to-Move' and 'Auto Loot' are ON.\n");
    printf("[+] Press [INSERT] to start/stop the bot.\n");
    return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) CreateThread(0, 0, Setup, 0, 0, 0);
    return TRUE;
}
