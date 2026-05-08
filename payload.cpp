#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <intrin.h>
#include <d3d9.h>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "user32.lib")

// --- АДРЕСА 3.3.5a (12340) ---
#define ADDR_LUA_EXECUTE      0x00819210
#define ADDR_CLICK_TO_MOVE    0x00611130
#define ADDR_GET_PLAYER       0x004038BE
#define ADDR_TLS_INDEX        0x00D415B8 // Индекс TLS для проверки потока
#define OFFSET_S_CUR_MGR      0x00879CE0 

typedef void(__cdecl* tFrameScript_Execute)(const char* command, const char* filename, void* reserved);
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetActivePlayer)();

tFrameScript_Execute Lua_DoString = (tFrameScript_Execute)ADDR_LUA_EXECUTE;
tClickToMove ClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;
tGetActivePlayer GetPlayer = (tGetActivePlayer)ADDR_GET_PLAYER;

// --- КОМАНДЫ ДЛЯ ГЛАВНОГО ПОТОКА ---
struct BotCommand {
    volatile int type; // 0: None, 1: Move, 2: Combat, 3: Loot, 4: Target
    volatile uint64_t guid;
    volatile float x, y, z;
} g_Cmd;

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

// --- VTABLE ХУК D3D9 С ПРОВЕРКОЙ TLS (ЗАЩИТА ОТ КРАША 0x14) ---
typedef HRESULT(__stdcall* tEndScene)(IDirect3DDevice9*);
tEndScene oEndScene = nullptr;

HRESULT __stdcall HookedEndScene(IDirect3DDevice9* pDevice) {
    if (!pDevice) return oEndScene(pDevice);

    // Достаем массив TLS текущего потока из TEB (Thread Environment Block)
    void** tlsArray = (void**)__readfsdword(0x2C);
    DWORD wowTlsIndex = *(DWORD*)ADDR_TLS_INDEX;

    // КРИТИЧЕСКАЯ ПРОВЕРКА: Если мы не в главном потоке WoW, пропускаем выполнение!
    if (tlsArray && tlsArray[wowTlsIndex] != nullptr) {
        uintptr_t pLocal = GetPlayer();
        if (pLocal && *(int*)(pLocal + 0x14) == 4) { // Проверяем, что объект - игрок
            __try {
                if (g_Cmd.type == 1) { // MOVE
                    float pos[3] = { g_Cmd.x, g_Cmd.y, g_Cmd.z };
                    uint64_t zero = 0;
                    ClickToMove(pLocal, 4, &zero, pos, 0.5f);
                    g_Cmd.type = 0;
                } 
                else if (g_Cmd.type == 2) { // COMBAT
                    Lua_DoString("if UnitExists('target') and not IsCurrentSpell(6603) then StartAttack() end", "bot", 0);
                    Lua_DoString("if not UnitBuff('player', 'Seal of Righteousness') then CastSpellByID(21084) end", "bot", 0);
                    g_Cmd.type = 0;
                } 
                else if (g_Cmd.type == 3) { // LOOT
                    Lua_DoString("if LootFrame:IsVisible() then for i=1,GetNumLootItems() do LootSlot(i) end CloseLoot() end", "bot", 0);
                    g_Cmd.type = 0;
                }
                else if (g_Cmd.type == 4) { // TARGET & INTERACT
                    char cmd[256];
                    // Lua-таргет надежнее записи в память
                    sprintf_s(cmd, "TargetUnit('0x%llX') if UnitIsDead('target') then InteractUnit('target') end", g_Cmd.guid);
                    Lua_DoString(cmd, "bot", 0);
                    g_Cmd.type = 0;
                }
            } __except(1) {} // Глушим любые исключения движка
        }
    }

    return oEndScene(pDevice);
}

bool InstallVTableHook() {
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return false;
    D3DPRESENT_PARAMETERS d3dpp = {0}; d3dpp.Windowed = TRUE; d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* pDev = nullptr;
    
    if (FAILED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, GetDesktopWindow(), D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDev))) {
        pD3D->Release(); return false;
    }

    uintptr_t* pVTable = *(uintptr_t**)pDev;
    oEndScene = (tEndScene)pVTable[42];

    DWORD old;
    VirtualProtect(&pVTable[42], sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &old);
    pVTable[42] = (uintptr_t)HookedEndScene;
    VirtualProtect(&pVTable[42], sizeof(uintptr_t), old, &old);

    pDev->Release(); pD3D->Release();
    return true;
}

void UnhookVTable() {
    if (!oEndScene) return;
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return;
    D3DPRESENT_PARAMETERS d3dpp = {0}; d3dpp.Windowed = TRUE; d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* pDev = nullptr;
    if (SUCCEEDED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, GetDesktopWindow(), D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDev))) {
        uintptr_t* pVTable = *(uintptr_t**)pDev;
        DWORD old;
        VirtualProtect(&pVTable[42], sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &old);
        pVTable[42] = (uintptr_t)oEndScene;
        VirtualProtect(&pVTable[42], sizeof(uintptr_t), old, &old);
        pDev->Release();
    }
    pD3D->Release();
}

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

// --- ОСНОВНОЙ ПОТОК ЛОГИКИ ---
DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin Imba Internal v11.0 (TLS Verified) ---\n");
    if (InstallVTableHook()) {
        printf("[+] D3D9 Hooked. Thread Context Validated via TEB.\n");
    } else {
        printf("[-] Failed to install VTable hook!\n");
        return 0;
    }
    printf("[!] ЕСЛИ БОТ СТОИТ И НЕ ДВИГАЕТСЯ:\n");
    printf("[!] Введи в чат игры: /console gxMultithread 0\n");
    printf("[!] Затем перезапусти игру. Это нужно для стабильности!\n");
    printf("--------------------------------------------------\n");

    uintptr_t connectionAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    BotState state = STATE_SEARCH;
    uint64_t activeTargetGuid = 0;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConn = 0;
        __try { clientConn = *(uintptr_t*)connectionAddr; } __except(1) { clientConn = 0; }
        
        SetConsoleCursor(0, 7);

        if (clientConn) {
            uintptr_t mgr = *(uintptr_t*)(clientConn + 0x2ED0);
            if (mgr) {
                uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
                uint64_t myGuid = *(uint64_t*)(mgr + 0xC0);
                
                __try {
                    uintptr_t pLocal = 0;
                    while (cur != 0 && (cur & 1) == 0) {
                        if (*(uint64_t*)(cur + 0x30) == myGuid && myGuid != 0) { pLocal = cur; break; }
                        cur = *(uintptr_t*)(cur + 0x3C);
                    }

                    if (pLocal) {
                        uintptr_t pDesc = *(uintptr_t*)(pLocal + 0x8);
                        int hp = *(int*)(pDesc + 0x60), maxHp = *(int*)(pDesc + 0x80), lvl = *(int*)(pDesc + 0xD8);
                        float myX = *(float*)(pLocal + 0x798), myY = *(float*)(pLocal + 0x79C);

                        printf("[PLAYER] HP: %d/%d | LVL: %d | POS: %.1f, %.1f      \n", hp, maxHp, lvl, myX, myY);
                        printf("--------------------------------------------------\n");

                        // --- FSM ---
                        if (state == STATE_SEARCH) {
                            float bestDist = 45.0f; activeTargetGuid = 0;
                            cur = *(uintptr_t*)(mgr + 0xAC);
                            while (cur != 0 && (cur & 1) == 0) {
                                if (*(int*)(cur + 0x14) == 3) {
                                    uintptr_t d = *(uintptr_t*)(cur + 0x8);
                                    if (*(int*)(d + 0x60) > 0 && *(int*)(d + 0xD8) <= lvl + 2 && *(uint64_t*)(d + 0x38) == 0) {
                                        float dist = sqrt(pow(*(float*)(cur + 0x798) - myX, 2) + pow(*(float*)(cur + 0x79C) - myY, 2));
                                        if (dist < bestDist) { bestDist = dist; activeTargetGuid = *(uint64_t*)(cur + 0x30); }
                                    }
                                }
                                cur = *(uintptr_t*)(cur + 0x3C);
                            }
                            if (activeTargetGuid) { 
                                state = STATE_MOVE; 
                                g_Cmd.guid = activeTargetGuid; 
                                g_Cmd.type = 4; // TARGET COMMAND
                            }
                        }

                        if (activeTargetGuid != 0) {
                            uintptr_t tObj = 0; cur = *(uintptr_t*)(mgr + 0xAC);
                            while (cur != 0 && (cur & 1) == 0) { if(*(uint64_t*)(cur+0x30) == activeTargetGuid) { tObj = cur; break; } cur = *(uintptr_t*)(cur+0x3C); }
                            
                            if (tObj) {
                                uintptr_t td = *(uintptr_t*)(tObj + 0x8);
                                int thp = *(int*)(td + 0x60);
                                float tx = *(float*)(tObj + 0x798), ty = *(float*)(tObj + 0x79C), tz = *(float*)(tObj + 0x7A0);
                                float dist = sqrt(pow(tx-myX, 2) + pow(ty-myY, 2));

                                printf("[TARGET] HP: %-5d | DIST: %.2f yds                  \n", thp, dist);
                                
                                if (thp > 0) {
                                    if (dist > 4.2f) {
                                        state = STATE_MOVE;
                                        g_Cmd.x = tx; g_Cmd.y = ty; g_Cmd.z = tz;
                                        g_Cmd.type = 1; // MOVE COMMAND
                                    } else {
                                        state = STATE_COMBAT;
                                        g_Cmd.type = 2; // ATTACK COMMAND
                                    }
                                } else {
                                    state = STATE_LOOT;
                                    printf("[STATE] INTERACTING & WAITING FOR SERVER...      \n");
                                    g_Cmd.guid = activeTargetGuid;
                                    g_Cmd.type = 4; // INTERACT (via Target command Lua block)
                                    Sleep(1200); // КРИТИЧЕСКИ ВАЖНО: Ждем пинг сервера на смерть
                                    
                                    g_Cmd.type = 3; // LOOT COMMAND
                                    Sleep(1500); // Ждем автосбор
                                    
                                    activeTargetGuid = 0; state = STATE_SEARCH;
                                }
                            } else { activeTargetGuid = 0; state = STATE_SEARCH; }
                        } else { printf("[TARGET] SEARCHING FOR TARGET...                  \n"); }
                        
                        printf("[FSM] %-15s                                  \n", stateNames[state]);
                    }
                } __except(1) {}
            }
        }
        Sleep(150);
    }

    UnhookVTable();
    fclose(f); FreeConsole(); FreeLibraryAndExitThread((HMODULE)lpParam, 0); return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,MainThread,h,0,0); }
    return TRUE;
}
