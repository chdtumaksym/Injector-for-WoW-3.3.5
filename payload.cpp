#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>

// --- АДРЕСА 3.3.5a (12340) ---
#define ADDR_S_CUR_MGR          0x00C79CE0
#define OFFSET_OBJECT_MANAGER   0x2ED0
#define ADDR_CLICK_TO_MOVE      0x00611130

#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

typedef void(__fastcall* tClickToMove)(uintptr_t ecx, void* edx, int type, uint64_t* guid, float* pos, float prec);

bool g_Active = false;
WNDPROC oWndProc = nullptr;

struct CTM_BUFFER { uint64_t guid; float pos[3]; };
static CTM_BUFFER g_ctm;

void SafeAction(uintptr_t pLocal, int type, uint64_t guid, float x, float y, float z) {
    if (!pLocal) return;
    g_ctm.guid = guid; g_ctm.pos[0] = x; g_ctm.pos[1] = y; g_ctm.pos[2] = z;
    ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, nullptr, type, &g_ctm.guid, g_ctm.pos, 0.5f);
}

// Эта функция теперь безопасно вызывается таймером главного окна
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

    uint64_t bestGuid = 0; 
    float bestDist = 40.0f; 
    float tPos[3] = {0};

    cur = *(uintptr_t*)(mgr + 0xAC);
    while (cur && (cur & 1) == 0) {
        if (*(int*)(cur + 0x14) == 3) { // Unit
            uintptr_t desc = *(uintptr_t*)(cur + 0x8);
            if (desc && *(int*)(desc + 0x60) > 0) { // Живой
                uint64_t objGuid = *(uint64_t*)(cur + 0x30);
                if (objGuid != localGuid) { // Игнорируем себя
                    float tX = *(float*)(cur + 0x798);
                    float tY = *(float*)(cur + 0x79C);
                    float dist = sqrt(pow(tX - myX, 2) + pow(tY - myY, 2));

                    if (dist < bestDist) {
                        bestDist = dist; 
                        bestGuid = objGuid;
                        tPos[0] = tX; tPos[1] = tY; tPos[2] = *(float*)(cur + 0x7A0);
                    }
                }
            }
        }
        cur = *(uintptr_t*)(cur + 0x3C);
    }

    if (bestGuid) {
        printf("Target: %llu | Dist: %.1f            \r", bestGuid, bestDist);
        SafeAction(pLocal, 4, bestGuid, tPos[0], tPos[1], tPos[2]);
    }
}

// Хук окна игры
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Включаем/выключаем бота по нажатию INSERT (окно должно быть активно)
    if (uMsg == WM_KEYDOWN && wParam == VK_INSERT) {
        g_Active = !g_Active;
        Beep(g_Active ? 800 : 400, 100);
        printf("\n[!] BOT STATUS: %s\n", g_Active ? "ACTIVE" : "PAUSED");
    }
    
    // Наш кастомный таймер (тикает каждые 500мс)
    if (uMsg == WM_TIMER && wParam == 1337) {
        BotPulse();
    }
    
    return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
}

DWORD WINAPI Setup(LPVOID) {
    AllocConsole(); freopen("CONOUT$", "w", stdout);
    printf("--- Bot v118: WndProc OS Engine ---\n");

    // Ищем окно игры
    HWND hwnd = FindWindowA(NULL, "World of Warcraft");
    if (!hwnd) {
        printf("[-] ERROR: WoW window not found!\n");
        return 0;
    }

    // Подменяем обработчик оконных сообщений
    oWndProc = (WNDPROC)SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)HookedWndProc);
    
    // Устанавливаем таймер ОС для окна. Он будет посылать WM_TIMER каждые 500мс
    SetTimer(hwnd, 1337, 500, NULL);

    printf("[+] Setup Complete. Focus WoW window and press [INSERT].\n");
    return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) CreateThread(0, 0, Setup, 0, 0, 0);
    return TRUE;
}
