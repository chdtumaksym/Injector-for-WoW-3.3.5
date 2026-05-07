#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <d3d9.h>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "user32.lib")

// --- АДРЕСА ФУНКЦИЙ И ОФФСЕТЫ 3.3.5a (12340) ---
#define ADDR_LUA_EXECUTE 0x00819210
#define ADDR_CLICK_TO_MOVE 0x00611130
#define OFFSET_S_CUR_MGR 0x00879CE0 // ПРАВИЛЬНЫЙ ОФФСЕТ! (0xC79CE0 - 0x400000)

typedef void(__cdecl* tFrameScript_Execute)(const char* command, const char* filename, void* reserved);
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);

tFrameScript_Execute Lua_DoString = (tFrameScript_Execute)ADDR_LUA_EXECUTE;
tClickToMove ClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;

// --- ГЛОБАЛЫ ДЛЯ СИНХРОНИЗАЦИИ ---
BYTE originalEndSceneBytes[5];
uintptr_t endSceneAddr = 0;
uintptr_t g_LocalPlayerObj = 0;
uint64_t g_TargetGuid = 0;
float g_MoveX = 0, g_MoveY = 0, g_MoveZ = 0;

bool g_DoMove = false;
bool g_DoAttack = false;
bool g_DoInteract = false;
bool g_DoLoot = false;

// --- D3D9 ХУК ---
void HookEndScene();
void UnhookEndScene();

HRESULT __stdcall HookedEndScene(IDirect3DDevice9* pDevice) {
    UnhookEndScene(); 
    if (g_LocalPlayerObj) {
        if (g_DoMove) {
            float pos[3] = { g_MoveX, g_MoveY, g_MoveZ };
            ClickToMove(g_LocalPlayerObj, 4, &g_TargetGuid, pos, 0.5f); 
            g_DoMove = false;
        }
        if (g_DoAttack) { Lua_DoString("StartAttack()", "bot", 0); g_DoAttack = false; }
        if (g_DoInteract) { Lua_DoString("InteractUnit('target')", "bot", 0); g_DoInteract = false; }
        if (g_DoLoot) { Lua_DoString("for i=1,GetNumLootItems() do LootSlot(i) end CloseLoot()", "bot", 0); g_DoLoot = false; }
    }
    typedef HRESULT(__stdcall* tEndScene)(IDirect3DDevice9*);
    HRESULT res = ((tEndScene)endSceneAddr)(pDevice);
    HookEndScene();
    return res;
}

void HookEndScene() {
    DWORD old; VirtualProtect((void*)endSceneAddr, 5, PAGE_EXECUTE_READWRITE, &old);
    *(BYTE*)endSceneAddr = 0xE9;
    *(uintptr_t*)(endSceneAddr + 1) = (uintptr_t)HookedEndScene - endSceneAddr - 5;
    VirtualProtect((void*)endSceneAddr, 5, old, &old);
}

void UnhookEndScene() {
    DWORD old; VirtualProtect((void*)endSceneAddr, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)endSceneAddr, originalEndSceneBytes, 5);
    VirtualProtect((void*)endSceneAddr, 5, old, &old);
}

uintptr_t GetEndSceneAddress() {
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return 0;
    D3DPRESENT_PARAMETERS d3dpp = {0}; d3dpp.Windowed = TRUE; d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* pDev = nullptr;
    if (FAILED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, GetDesktopWindow(), D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDev))) return 0;
    uintptr_t addr = (*(uintptr_t**)pDev)[42];
    pDev->Release(); pD3D->Release();
    return addr;
}

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

enum BotState { STATE_IDLE, STATE_MOVE, STATE_COMBAT, STATE_LOOT };

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Internal Paladin Bot v2.0 (Fixed Address) ---\n");
    endSceneAddr = GetEndSceneAddress();
    if (!endSceneAddr) return 0;

    memcpy(originalEndSceneBytes, (void*)endSceneAddr, 5);
    HookEndScene();
    printf("[+] Hooked! Bot is active in background.\n");

    HMODULE base = GetModuleHandleA(NULL);
    uintptr_t connectionAddr = (uintptr_t)base + OFFSET_S_CUR_MGR;
    
    BotState state = STATE_IDLE;
    uint64_t targetGuid = 0;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConn = 0;
        __try { clientConn = *(uintptr_t*)connectionAddr; } __except(1) {}
        
        SetConsoleCursor(0, 5);

        if (clientConn) {
            uintptr_t mgr = *(uintptr_t*)(clientConn + 0x2ED0);
            if (mgr) {
                uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
                uint64_t myGuid = *(uint64_t*)(mgr + 0xC0);
                
                __try {
                    g_LocalPlayerObj = 0;
                    while (cur != 0 && (cur & 1) == 0) {
                        if (*(uint64_t*)(cur + 0x30) == myGuid) { g_LocalPlayerObj = cur; break; }
                        cur = *(uintptr_t*)(cur + 0x3C);
                    }

                    if (g_LocalPlayerObj) {
                        uintptr_t pDesc = *(uintptr_t*)(g_LocalPlayerObj + 0x8);
                        int hp = *(int*)(pDesc + 0x60), maxHp = *(int*)(pDesc + 0x80), lvl = *(int*)(pDesc + 0xD8);
                        float myX = *(float*)(g_LocalPlayerObj + 0x798), myY = *(float*)(g_LocalPlayerObj + 0x79C);

                        printf("[ME] Lvl: %d | HP: %d/%d | POS: %.1f, %.1f          \n", lvl, hp, maxHp, myX, myY);
                        
                        // --- ИНВЕНТАРЬ ---
                        int items = 0; printf("[BAG] ");
                        for(int i=0; i<10; i++) {
                            uint64_t itemGuid = *(uint64_t*)(pDesc + 0x640 + (i*8));
                            if (itemGuid) {
                                uintptr_t icur = *(uintptr_t*)(mgr + 0xAC);
                                while(icur != 0 && (icur & 1) == 0) {
                                    if(*(uint64_t*)(icur + 0x30) == itemGuid) {
                                        printf("%d ", *(int*)(*(uintptr_t*)(icur + 0x8) + 0xC));
                                        items++; break;
                                    }
                                    icur = *(uintptr_t*)(icur + 0x3C);
                                }
                            }
                        }
                        printf(" (Total: %d)                               \n", items);
                        printf("--------------------------------------------------\n");

                        // --- FSM ---
                        if (state == STATE_IDLE) {
                            float bestDist = 40.0f;
                            cur = *(uintptr_t*)(mgr + 0xAC);
                            while (cur != 0 && (cur & 1) == 0) {
                                if (*(int*)(cur + 0x14) == 3) {
                                    uintptr_t d = *(uintptr_t*)(cur + 0x8);
                                    if (*(int*)(d + 0x60) > 0 && *(int*)(d + 0xD8) <= lvl + 2 && *(uint64_t*)(d + 0x38) == 0) {
                                        float x = *(float*)(cur + 0x798), y = *(float*)(cur + 0x79C);
                                        float dist = sqrt(pow(x-myX, 2) + pow(y-myY, 2));
                                        if (dist < bestDist) { bestDist = dist; targetGuid = *(uint64_t*)(cur + 0x30); }
                                    }
                                }
                                cur = *(uintptr_t*)(cur + 0x3C);
                            }
                            if (targetGuid) { state = STATE_MOVE; *(uint64_t*)(pDesc + 0x48) = targetGuid; }
                        }

                        if (targetGuid) {
                            uintptr_t tObj = 0; cur = *(uintptr_t*)(mgr + 0xAC);
                            while (cur != 0 && (cur & 1) == 0) { if(*(uint64_t*)(cur+0x30) == targetGuid) { tObj = cur; break; } cur = *(uintptr_t*)(cur+0x3C); }
                            
                            if (tObj) {
                                uintptr_t td = *(uintptr_t*)(tObj + 0x8);
                                int thp = *(int*)(td + 0x60);
                                float tx = *(float*)(tObj + 0x798), ty = *(float*)(tObj + 0x79C), tz = *(float*)(tObj + 0x7A0);
                                float dist = sqrt(pow(tx-myX, 2) + pow(ty-myY, 2));

                                printf("[TARGET] HP: %d | Dist: %.2f yds                  \n", thp, dist);
                                
                                if (thp > 0) {
                                    if (dist > 4.5f) {
                                        state = STATE_MOVE; g_MoveX = tx; g_MoveY = ty; g_MoveZ = tz; g_TargetGuid = targetGuid; g_DoMove = true;
                                    } else {
                                        state = STATE_COMBAT; g_DoAttack = true;
                                    }
                                } else {
                                    state = STATE_LOOT; g_DoInteract = true; Sleep(600); g_DoLoot = true; Sleep(1000);
                                    *(uint64_t*)(pDesc + 0x48) = 0; targetGuid = 0; state = STATE_IDLE;
                                }
                            } else { targetGuid = 0; state = STATE_IDLE; *(uint64_t*)(pDesc + 0x48) = 0; }
                        } else { printf("[TARGET] NONE                                    \n"); }
                        
                        printf("[STATE] %s                                        \n", 
                               (state == STATE_IDLE) ? "IDLE" : (state == STATE_MOVE) ? "MOVING" : (state == STATE_COMBAT) ? "FIGHTING" : "LOOTING");
                    }
                } __except(1) {}
            }
        }
        Sleep(150);
    }
    UnhookEndScene(); fclose(f); FreeConsole(); FreeLibraryAndExitThread((HMODULE)lpParam, 0); return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,MainThread,h,0,0); }
    return TRUE;
}
