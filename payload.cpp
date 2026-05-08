#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>

#define ADDR_ON_UPDATE          0x0047E7B0
#define ADDR_CLICK_TO_MOVE      0x00611130
#define ADDR_GET_PLAYER         0x004038BE
#define ADDR_S_CUR_MGR          0x00C79CE0 // ИСПРАВЛЕНО: добавлена база 0x400000
#define OFFSET_OBJECT_MANAGER   0x2ED0
#define ADDR_IS_IN_GAME         0x00BD0792

#pragma runtime_checks("", off)
#pragma check_stack(off)

typedef void(__fastcall* tClickToMove)(uintptr_t ecx, void* edx, int type, uint64_t* guid, float* pos, float prec);
typedef uintptr_t(__cdecl* tGetPlayer)();

bool g_Active = false;
static uint64_t s_guid;
static float s_pos[3];

void SafeAction(uintptr_t p, int t, uint64_t g, float x, float y, float z) {
    if (!p) return;
    s_guid = g; s_pos[0] = x; s_pos[1] = y; s_pos[2] = z;
    ((tClickToMove)ADDR_CLICK_TO_MOVE)(p, nullptr, t, &s_guid, s_pos, 0.5f);
}

void BotPulse() {
    if (GetAsyncKeyState(VK_END) & 0x8000) {
        static bool ks = false;
        if (!ks) { g_Active = !g_Active; ks = true; Beep(g_Active ? 800 : 400, 100); }
    } else { static bool ks = false; ks = false; }

    if (g_Active && *(BYTE*)ADDR_IS_IN_GAME) {
        uintptr_t pLocal = ((tGetPlayer)ADDR_GET_PLAYER)();
        uintptr_t conn = *(uintptr_t*)ADDR_S_CUR_MGR;
        uintptr_t mgr = conn ? *(uintptr_t*)(conn + OFFSET_OBJECT_MANAGER) : 0;
        if (!pLocal || !mgr) return;

        float myX = *(float*)(pLocal + 0x798), myY = *(float*)(pLocal + 0x79C);
        uint64_t target = 0; float dist = 40.0f; float tPos[3];

        uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
        while (cur && (cur & 1) == 0) {
            if (*(int*)(cur + 0x14) == 3) {
                uintptr_t d = *(uintptr_t*)(cur + 0x8);
                if (d) {
                    int hp = *(int*)(d + 0x60);
                    uint32_t flags = *(uint32_t*)(d + 0x114);
                    float dx = *(float*)(cur + 0x798) - myX, dy = *(float*)(cur + 0x79C) - myY;
                    float d_curr = sqrt(dx*dx + dy*dy);

                    if (hp > 0 && d_curr < dist) {
                        dist = d_curr; target = *(uint64_t*)(cur + 0x30);
                        tPos[0] = *(float*)(cur + 0x798); tPos[1] = *(float*)(cur + 0x79C); tPos[2] = *(float*)(cur + 0x7A0);
                    }
                    else if (hp <= 0 && (flags & 1) && d_curr < 5.0f && !target) {
                        dist = d_curr; target = *(uint64_t*)(cur + 0x30);
                        tPos[0] = *(float*)(cur + 0x798); tPos[1] = *(float*)(cur + 0x79C); tPos[2] = *(float*)(cur + 0x7A0);
                    }
                }
            }
            cur = *(uintptr_t*)(cur + 0x3C);
        }

        static DWORD last = 0;
        if (target && GetTickCount() - last > 800) {
            SafeAction(pLocal, 4, target, tPos[0], tPos[1], tPos[2]);
            last = GetTickCount();
        }
    }
}

void __declspec(naked) Hook() {
    __asm {
        pushad
        call BotPulse
        popad
        mov edi, edi
        push ebp
        mov ebp, esp
        push 0x0047E7B5
        ret
    }
}

DWORD WINAPI Setup(LPVOID) {
    AllocConsole(); freopen("CONOUT$", "w", stdout);
    printf("[+] Bot v111: Memory Addr Fixed. Ready to crush.\n");
    DWORD o; VirtualProtect((void*)ADDR_ON_UPDATE, 5, PAGE_EXECUTE_READWRITE, &o);
    *(BYTE*)ADDR_ON_UPDATE = 0xE9; *(DWORD*)(ADDR_ON_UPDATE + 1) = (DWORD)Hook - ADDR_ON_UPDATE - 5;
    VirtualProtect((void*)ADDR_ON_UPDATE, 5, o, &o);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) CreateThread(0, 0, Setup, 0, 0, 0);
    return TRUE;
}
