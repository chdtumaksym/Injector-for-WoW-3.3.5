#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>
#include <vector>
#include <algorithm>

// --- ПРАВИЛЬНЫЕ АДРЕСА 3.3.5a (12340) ---
#define ADDR_S_CUR_MGR          0x00C79CE0
#define OFFSET_OBJECT_MANAGER   0x2ED0
#define ADDR_CLICK_TO_MOVE      0x00727400

// [!!!] ИСТИННЫЙ АДРЕС ТАРГЕТА: 0x00BD07B0
// 0x00BD07B8 - это был LastTargetGUID (Предыдущая цель). 
// Вот почему интерфейс не обновлялся!
#define ADDR_TARGET_GUID        0x00BD07B0 

#define CTM_LOOT                6
#define CTM_ATTACK              11

#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

typedef void(__fastcall* tClickToMove)(uintptr_t ecx, uintptr_t edx, int type, uint64_t* guid, float* pos, float prec);

bool g_Active = false;
std::vector<uint64_t> g_Blacklist;
uint64_t g_BotTarget = 0;

float GetDistance3D(float x1, float y1, float z1, float x2, float y2, float z2) {
    return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2) + pow(z2 - z1, 2));
}

// Отправляем приказ из статичной памяти, чтобы игра не читала мусор
void ActionCTM(uintptr_t pLocal, int type, uint64_t guid, float x, float y, float z) {
    static uint64_t ctmGuid = 0;
    static float ctmPos[3] = { 0, 0, 0 };

    ctmGuid = guid;
    ctmPos[0] = x; ctmPos[1] = y; ctmPos[2] = z;
    
    ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, 0, type, &ctmGuid, ctmPos, 0.5f);
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

    if (g_BotTarget != 0) {
        cur = *(uintptr_t*)(mgr + 0xAC);
        while (cur && (cur & 1) == 0) {
            if (*(uint64_t*)(cur + 0x30) == g_BotTarget) {
                int objType = *(int*)(cur + 0x14);
                if (objType == 3 || objType == 4) {
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

    static uint64_t lastActionGuid = 0;
    static int lastActionType = 0;

    if (hasTarget) {
        // [!] Жестко берем в таргет по ПРАВИЛЬНОМУ АДРЕСУ
        *(uint64_t*)ADDR_TARGET_GUID = g_BotTarget; 

        if (targetHp > 0) {
            // Отдаем приказ АТАКА ровно 1 раз. Игра сама добежит и начнет бить!
            if (lastActionGuid != g_BotTarget || lastActionType != CTM_ATTACK) {
                ActionCTM(pLocal, CTM_ATTACK, g_BotTarget, tX, tY, tZ);
                lastActionGuid = g_BotTarget;
                lastActionType = CTM_ATTACK;
                printf("Attacking Enemy! Engine AI Activated.\n");
            }
        }
        else if (targetHp <= 0 && (targetFlags & 1)) {
            // Отдаем приказ ЛУТ ровно 1 раз. Игра сама добежит и залутает!
            if (lastActionGuid != g_BotTarget || lastActionType != CTM_LOOT) {
                ActionCTM(pLocal, CTM_LOOT, g_BotTarget, tX, tY, tZ);
                lastActionGuid = g_BotTarget;
                lastActionType = CTM_LOOT;
                printf("Looting Corpse!\n");
            }
        }
        else {
            g_Blacklist.push_back(g_BotTarget);
            g_BotTarget = 0;
            lastActionGuid = 0;
            lastActionType = 0;
            *(uint64_t*)ADDR_TARGET_GUID = 0;
            printf("Target Dead & Empty. Blacklisted.\n");
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

DWORD WINAPI BotThread(LPVOID) {
    AllocConsole(); freopen("CONOUT$", "w", stdout);
    printf("--- Bot v19.0: The Final Awakening ---\n");
    printf("[+] Memory Offsets and Pathfinding issues eradicated.\n");
    printf("[!] Make sure 'Click-to-Move' and 'Auto Loot' are ON.\n");
    printf("[+] Press [INSERT] to start the bot.\n");

    bool isPressed = false;
    DWORD lastPulse = 0;

    while (true) {
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

        if (g_Active && (GetTickCount() - lastPulse > 400)) {
            __try {
                BotPulse();
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
            lastPulse = GetTickCount();
        }

        Sleep(50);
    }
    return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(0, 0, BotThread, 0, 0, 0);
    }
    return TRUE;
}
