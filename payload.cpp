#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <d3d9.h>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "user32.lib")

// --- АДРЕСА ФУНКЦИЙ 3.3.5a (12340) ---
#define ADDR_LUA_EXECUTE 0x00819210
#define ADDR_CLICK_TO_MOVE 0x00611130

typedef void(__cdecl* tFrameScript_Execute)(const char* command, const char* filename, void* reserved);
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);

tFrameScript_Execute Lua_DoString = (tFrameScript_Execute)ADDR_LUA_EXECUTE;
tClickToMove ClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ДЛЯ СИНХРОНИЗАЦИИ ПОТОКОВ ---
BYTE originalEndSceneBytes[5];
uintptr_t endSceneAddr = 0;

uintptr_t g_LocalPlayerObj = 0;
uint64_t g_TargetGuid = 0;
float g_MoveX = 0, g_MoveY = 0, g_MoveZ = 0;

// Флаги-команды для главного потока игры
bool g_DoMove = false;
bool g_DoAttack = false;
bool g_DoInteract = false;
bool g_DoLoot = false;

// --- D3D9 ХУК ЛОГИКА ---
HRESULT __stdcall HookedEndScene(IDirect3DDevice9* pDevice);

void HookEndScene() {
    DWORD oldProtect;
    VirtualProtect((void*)endSceneAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    *(BYTE*)endSceneAddr = 0xE9; // Опкод JMP
    *(uintptr_t*)(endSceneAddr + 1) = (uintptr_t)HookedEndScene - endSceneAddr - 5; // Вычисляем смещение
    VirtualProtect((void*)endSceneAddr, 5, oldProtect, &oldProtect);
}

void UnhookEndScene() {
    DWORD oldProtect;
    VirtualProtect((void*)endSceneAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy((void*)endSceneAddr, originalEndSceneBytes, 5); // Возвращаем оригинальные байты
    VirtualProtect((void*)endSceneAddr, 5, oldProtect, &oldProtect);
}

// Эта функция выполняется ВНУТРИ главного потока WoW каждый кадр!
HRESULT __stdcall HookedEndScene(IDirect3DDevice9* pDevice) {
    UnhookEndScene(); // Снимаем хук, чтобы иметь возможность вызвать оригинал

    // БЕЗОПАСНЫЙ ВЫЗОВ ФУНКЦИЙ ДВИЖКА
    if (g_LocalPlayerObj) {
        if (g_DoMove) {
            float pos[3] = { g_MoveX, g_MoveY, g_MoveZ };
            ClickToMove(g_LocalPlayerObj, 4, &g_TargetGuid, pos, 0.5f); // 4 - Movement
            g_DoMove = false;
        }
        if (g_DoAttack) {
            Lua_DoString("StartAttack()", "bot", 0);
            g_DoAttack = false;
        }
        if (g_DoInteract) {
            Lua_DoString("InteractUnit('target')", "bot", 0);
            g_DoInteract = false;
        }
        if (g_DoLoot) {
            Lua_DoString("for i=1,GetNumLootItems() do LootSlot(i) end CloseLoot()", "bot", 0);
            g_DoLoot = false;
        }
    }

    typedef HRESULT(__stdcall* tEndScene)(IDirect3DDevice9*);
    HRESULT res = ((tEndScene)endSceneAddr)(pDevice); // Вызов оригинального EndScene

    HookEndScene(); // Возвращаем наш прыжок на место
    return res;
}

// Поиск адреса EndScene через создание фиктивного устройства
uintptr_t GetEndSceneAddress() {
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return 0;
    D3DPRESENT_PARAMETERS d3dpp = { 0 };
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* pDevice = nullptr;
    if (FAILED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, GetDesktopWindow(), D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDevice))) {
        pD3D->Release(); return 0;
    }
    uintptr_t* vtable = *(uintptr_t**)pDevice;
    uintptr_t address = vtable[42]; // 42 - индекс EndScene в VTable
    pDevice->Release();
    pD3D->Release();
    return address;
}

// --- ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ---
void SetConsoleCursor(int x, int y) {
    COORD coord; coord.X = x; coord.Y = y;
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- True Internal FSM Bot (Detour Hook) ---\n");
    
    endSceneAddr = GetEndSceneAddress();
    if (!endSceneAddr) {
        printf("[!] Failed to find EndScene.\n");
        FreeLibraryAndExitThread((HMODULE)lpParam, 0);
        return 0;
    }

    // Сохраняем оригинальные байты и ставим хук
    memcpy(originalEndSceneBytes, (void*)endSceneAddr, 5);
    HookEndScene();
    printf("[+] Successfully Hooked EndScene at 0x%p\n", (void*)endSceneAddr);
    printf("[*] The bot can now run in BACKGROUND (Alt-Tab safely)!\n");
    printf("--------------------------------------------------\n");

    HMODULE base = GetModuleHandleA(NULL);
    uintptr_t connectionAddr = (uintptr_t)base + 0x00C79CE0;
    
    int hudStartY = 6;
    BotState currentState = STATE_SEARCH;
    const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };
    uint64_t currentTargetGuid = 0;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConnection = 0;
        __try { clientConnection = *(uintptr_t*)connectionAddr; } __except (1) {}
        
        SetConsoleCursor(0, hudStartY);

        if (clientConnection) {
            uintptr_t objMgr = *(uintptr_t*)(clientConnection + 0x2ED0);
            if (objMgr) {
                uintptr_t cur = *(uintptr_t*)(objMgr + 0xAC);
                uint64_t localGuid = *(uint64_t*)(objMgr + 0xC0);
                
                __try {
                    g_LocalPlayerObj = 0;
                    while (cur != 0 && (cur & 1) == 0) {
                        if (*(uint64_t*)(cur + 0x30) == localGuid && localGuid != 0) { g_LocalPlayerObj = cur; break; }
                        cur = *(uintptr_t*)(cur + 0x3C);
                    }

                    if (g_LocalPlayerObj) {
                        uintptr_t localDesc = *(uintptr_t*)(g_LocalPlayerObj + 0x8);
                        int hp = *(int*)(localDesc + 0x60), maxHp = *(int*)(localDesc + 0x80), myLevel = *(int*)(localDesc + 0xD8);
                        float myX = *(float*)(g_LocalPlayerObj + 0x798), myY = *(float*)(g_LocalPlayerObj + 0x79C), myZ = *(float*)(g_LocalPlayerObj + 0x7A0);
                        
                        printf("[BOT STATUS] | STATE: %-10s                     \n", stateNames[currentState]);
                        printf("Lvl: %d | HP: %d/%d                              \n", myLevel, hp, maxHp);
                        printf("--------------------------------------------------\n");

                        // --- FSM ---
                        if (currentState == STATE_SEARCH) {
                            float closestDist = 999999.0f;
                            cur = *(uintptr_t*)(objMgr + 0xAC);
                            
                            while (cur != 0 && (cur & 1) == 0) {
                                if (*(int*)(cur + 0x14) == 3) { // NPC
                                    uintptr_t d = *(uintptr_t*)(cur + 0x8);
                                    int npcHp = *(int*)(d + 0x60);
                                    int npcLevel = *(int*)(d + 0xD8);
                                    
                                    if (npcHp > 0 && npcLevel <= myLevel + 2 && *(uint64_t*)(d + 0x38) == 0) {
                                        float x = *(float*)(cur + 0x798), y = *(float*)(cur + 0x79C), z = *(float*)(cur + 0x7A0);
                                        float dist = sqrt(pow(x - myX, 2) + pow(y - myY, 2) + pow(z - myZ, 2));
                                        
                                        if (dist < closestDist) { closestDist = dist; currentTargetGuid = *(uint64_t*)(cur + 0x30); }
                                    }
                                }
                                cur = *(uintptr_t*)(cur + 0x3C);
                            }

                            if (currentTargetGuid != 0) { 
                                currentState = STATE_MOVE; 
                                *(uint64_t*)(localDesc + 0x48) = currentTargetGuid; // Ставим таргет
                            }
                        }

                        if (currentTargetGuid != 0) {
                            uintptr_t targetObj = 0;
                            cur = *(uintptr_t*)(objMgr + 0xAC);
                            while (cur != 0 && (cur & 1) == 0) { if (*(uint64_t*)(cur + 0x30) == currentTargetGuid) { targetObj = cur; break; } cur = *(uintptr_t*)(cur + 0x3C); }
                            
                            if (targetObj) {
                                uintptr_t d = *(uintptr_t*)(targetObj + 0x8);
                                int nHp = *(int*)(d + 0x60);
                                float tX = *(float*)(targetObj + 0x798), tY = *(float*)(targetObj + 0x79C), tZ = *(float*)(targetObj + 0x7A0);
                                float dist = sqrt(pow(tX - myX, 2) + pow(tY - myY, 2) + pow(tZ - myZ, 2));

                                printf("[TARGET LOCK] HP: %d | Dist: %.2f yds                  \n", nHp, dist);

                                if (nHp > 0) {
                                    if (dist > 4.5f) {
                                        currentState = STATE_MOVE;
                                        // Передаем координаты главному потоку
                                        g_MoveX = tX; g_MoveY = tY; g_MoveZ = tZ;
                                        g_TargetGuid = currentTargetGuid;
                                        g_DoMove = true;
                                    } else {
                                        currentState = STATE_COMBAT;
                                        g_DoAttack = true; // Триггерим Lua StartAttack()
                                    }
                                } else {
                                    // МОБ МЕРТВ - ЛУТАЕМ
                                    currentState = STATE_LOOT;
                                    
                                    printf("[LOOTING] Interacting via Lua...                  \n");
                                    g_DoInteract = true; // InteractUnit('target')
                                    Sleep(600); // Ждем пока персонаж наклонится и окно лута откроется
                                    
                                    g_DoLoot = true; // LootSlot()
                                    Sleep(1000); // Ждем пока вещи упадут в сумку
                                    
                                    *(uint64_t*)(localDesc + 0x48) = 0; // Сброс таргета
                                    currentTargetGuid = 0;
                                    currentState = STATE_SEARCH;
                                }
                            } else {
                                currentTargetGuid = 0; currentState = STATE_SEARCH; *(uint64_t*)(localDesc + 0x48) = 0;
                            }
                        }
                    }
                } __except (1) {}
            }
        }
        Sleep(100); 
    }

    // Чистим за собой
    UnhookEndScene();
    fclose(f); FreeConsole(); 
    FreeLibraryAndExitThread((HMODULE)lpParam, 0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(hinstDLL); CreateThread(NULL, 0, MainThread, hinstDLL, 0, NULL); }
    return TRUE;
}
