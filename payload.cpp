#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>

// --- АДРЕСА 3.3.5a (12340) ---
#define ADDR_PLAYER_BASE        0x00BD07E0
#define ADDR_S_CUR_MGR          0x00C79CE0
#define OFFSET_OBJECT_MANAGER   0x2ED0
#define ADDR_CLICK_TO_MOVE      0x00611130

// Отключаем проверки компилятора для Manual Map
#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

typedef void(__fastcall* tClickToMove)(uintptr_t ecx, void* edx, int type, uint64_t* guid, float* pos, float prec);

bool g_Active = false;

// Статика для безопасной передачи параметров
struct CTM_BUFFER {
    uint64_t guid;
    float pos[3];
};
static CTM_BUFFER g_ctm;

void SafeAction(uintptr_t pLocal, int type, uint64_t guid, float x, float y, float z) {
    if (!pLocal) return;
    g_ctm.guid = guid;
    g_ctm.pos[0] = x; g_ctm.pos[1] = y; g_ctm.pos[2] = z;
    ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, nullptr, type, &g_ctm.guid, g_ctm.pos, 0.5f);
}

DWORD WINAPI BackgroundBotThread(LPVOID) {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    
    // Перешли на английский, чтобы не было кракозябр в консоли
    printf("--- Background Thread Bot v115 ---\n");
    printf("[+] NO HOOKS. 100%% safe from Render Thread crashes.\n");
    printf("[!] Press [INSERT] to toggle bot.\n");

    while (true) {
        // Клавиша INSERT для активации
        if (GetAsyncKeyState(VK_INSERT) & 0x8000) {
            g_Active = !g_Active;
            Beep(g_Active ? 800 : 400, 100);
            printf("\n[!] Bot Status: %s\n", g_Active ? "ACTIVE" : "PAUSED");
            Sleep(300); // Защита от двойного клика
        }

        if (g_Active) {
            uintptr_t pLocal = *(uintptr_t*)ADDR_PLAYER_BASE;
            uintptr_t conn = *(uintptr_t*)ADDR_S_CUR_MGR;
            uintptr_t mgr = conn ? *(uintptr_t*)(conn + OFFSET_OBJECT_MANAGER) : 0;

            if (pLocal && mgr) {
                float myX = *(float*)(pLocal + 0x798);
                float myY = *(float*)(pLocal + 0x79C);
                
                uint64_t bestGuid = 0;
                float bestDist = 40.0f;
                float tPos[3] = {0};

                uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
                while (cur && (cur & 1) == 0) {
                    if (*(int*)(cur + 0x14) == 3) { // Unit
                        uintptr_t desc = *(uintptr_t*)(cur + 0x8);
                        if (desc && *(int*)(desc + 0x60) > 0) { // Alive
                            float tX = *(float*)(cur + 0x798);
                            float tY = *(float*)(cur + 0x79C);
                            float dist = sqrt(pow(tX - myX, 2) + pow(tY - myY, 2));

                            if (dist < bestDist) {
                                bestDist = dist;
                                bestGuid = *(uint64_t*)(cur + 0x30);
                                tPos[0] = tX; tPos[1] = tY; tPos[2] = *(float*)(cur + 0x7A0);
                            }
                        }
                    }
                    cur = *(uintptr_t*)(cur + 0x3C);
                }

                if (bestGuid) {
                    printf("Target: %llu | Dist: %.1f\r", bestGuid, bestDist);
                    // Тип 4 = Interact (Атака/Взаимодействие)
                    SafeAction(pLocal, 4, bestGuid, tPos[0], tPos[1], tPos[2]);
                }
            }
        }
        // Пауза 500мс (2 тика в секунду). Нельзя спамить движок чаще, иначе краш.
        Sleep(500); 
    }
    return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        CreateThread(0, 0, BackgroundBotThread, 0, 0, 0);
    }
    return TRUE;
}
