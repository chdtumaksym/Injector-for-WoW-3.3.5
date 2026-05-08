#include <windows.h>
#include <cmath>
#include <d3d9.h>

#define OFFSET_S_CUR_MGR 0x00879CE0
#define OFFSET_OBJECT_MANAGER 0x2ED0
#define ADDR_CLICK_TO_MOVE 0x00611130
#define ADDR_GET_PLAYER 0x004038BE
#define ADDR_D3D9_DEVICE 0x00C5DF88

typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetPlayer)();
typedef HRESULT(__stdcall* tEndScene)(LPDIRECT3DDEVICE9 pDevice);

tEndScene oEndScene = nullptr;
bool g_BotActive = false;

bool IsValidRead(uintptr_t ptr) {
    if (!ptr) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((LPCVOID)ptr, &mbi, sizeof(mbi))) {
        return (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) && !(mbi.Protect & PAGE_GUARD));
    }
    return false;
}

template<typename T> T Read(uintptr_t addr) {
    return IsValidRead(addr) ? *(T*)addr : T();
}

HRESULT __stdcall HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    static bool keyState = false;
    if (GetAsyncKeyState(VK_END) & 0x8000) {
        if (!keyState) {
            g_BotActive = !g_BotActive;
            keyState = true;
            if (g_BotActive) Beep(750, 150); else Beep(300, 150);
        }
    } else {
        keyState = false;
    }

    if (g_BotActive) {
        uintptr_t conn = Read<uintptr_t>((uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR);
        if (conn) {
            uintptr_t mgr = Read<uintptr_t>(conn + OFFSET_OBJECT_MANAGER);
            uintptr_t pLocal = ((tGetPlayer)ADDR_GET_PLAYER)();
            if (mgr && pLocal && Read<int>(pLocal + 0x14) == 4) {
                float myX = Read<float>(pLocal + 0x798), myY = Read<float>(pLocal + 0x79C);
                uint64_t bestGuid = 0; float bestDist = 45.0f; uintptr_t bestObj = 0;
                uintptr_t cur = Read<uintptr_t>(mgr + 0xAC);
                
                while (cur && (cur & 1) == 0) {
                    if (Read<int>(cur + 0x14) == 3) {
                        uintptr_t desc = Read<uintptr_t>(cur + 0x8);
                        if (Read<int>(desc + 0x60) > 0 && Read<uint64_t>(desc + 0x38) == 0) {
                            float dx = Read<float>(cur + 0x798) - myX, dy = Read<float>(cur + 0x79C) - myY;
                            float dist = sqrt(dx*dx + dy*dy);
                            if (dist < bestDist) { bestDist = dist; bestGuid = Read<uint64_t>(cur + 0x30); bestObj = cur; }
                        }
                    }
                    cur = Read<uintptr_t>(cur + 0x3C);
                }
                
                static DWORD lastAction = 0;
                if (bestGuid && GetTickCount() - lastAction > 1500) {
                    float tx = Read<float>(bestObj + 0x798), ty = Read<float>(bestObj + 0x79C), tz = Read<float>(bestObj + 0x7A0);
                    float pos[3] = { tx, ty, tz };
                    if (bestDist > 4.5f) {
                        ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, 4, &bestGuid, pos, 0.5f);
                    } else {
                        ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, 11, &bestGuid, pos, 0.5f);
                    }
                    lastAction = GetTickCount();
                } else if (!bestGuid && GetTickCount() - lastAction > 2000) {
                    cur = Read<uintptr_t>(mgr + 0xAC);
                    while (cur && (cur & 1) == 0) {
                        if (Read<int>(cur + 0x14) == 3) {
                            uintptr_t desc = Read<uintptr_t>(cur + 0x8);
                            if (Read<int>(desc + 0x60) == 0) {
                                float dx = Read<float>(cur + 0x798) - myX, dy = Read<float>(cur + 0x79C) - myY;
                                if (sqrt(dx*dx + dy*dy) < 5.0f) {
                                    uint64_t deadGuid = Read<uint64_t>(cur + 0x30);
                                    float pos[3] = { Read<float>(cur + 0x798), Read<float>(cur + 0x79C), Read<float>(cur + 0x7A0) };
                                    ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, 6, &deadGuid, pos, 0.5f);
                                    break;
                                }
                            }
                        }
                        cur = Read<uintptr_t>(cur + 0x3C);
                    }
                    lastAction = GetTickCount();
                }
            }
        }
    }
    return oEndScene(pDevice);
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        uintptr_t* pDevicePtr = (uintptr_t*)ADDR_D3D9_DEVICE;
        if (IsValidRead((uintptr_t)pDevicePtr) && *pDevicePtr) {
            uintptr_t* vTable = *(uintptr_t**)*pDevicePtr;
            if (IsValidRead((uintptr_t)vTable)) {
                DWORD old;
                VirtualProtect(&vTable[42], 4, PAGE_EXECUTE_READWRITE, &old);
                oEndScene = (tEndScene)vTable[42];
                vTable[42] = (uintptr_t)HookedEndScene;
                VirtualProtect(&vTable[42], 4, old, &old);
            }
        }
    }
    return TRUE;
}
