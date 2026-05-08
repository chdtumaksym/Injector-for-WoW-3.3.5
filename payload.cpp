#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "user32.lib")

// --- ОФФСЕТЫ 3.3.5a (12340) ---
#define OFFSET_S_CUR_MGR         0x00879CE0
#define OFFSET_OBJECT_MANAGER    0x2ED0
#define ADDR_TARGET_GUID         0x00BD07B0 

#define ADDR_LUA_EXECUTE         0x00819210
#define ADDR_CLICK_TO_MOVE       0x00611130
#define ADDR_GET_PLAYER          0x004038BE
#define ADDR_CGGAMEUI_UPDATE     0x007CDE10 

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

// --- ФУНКЦИИ ДВИЖКА ---
typedef void(__cdecl* tFrameScript_Execute)(const char* command, const char* filename, void* reserved);
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetActivePlayer)();

tFrameScript_Execute Lua_DoString = (tFrameScript_Execute)ADDR_LUA_EXECUTE;
tClickToMove ClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;
tGetActivePlayer GetPlayer = (tGetActivePlayer)ADDR_GET_PLAYER;

// --- СИНХРОННАЯ ОЧЕРЕДЬ (Для Копайлота) ---
struct BotCommand {
    volatile int type; // 1: Move, 2: Attack, 3: Interact, 4: Loot, 5: Init Settings
    volatile uint64_t guid;
    volatile float x, y, z;
} g_Cmd;

void* g_Trampoline = nullptr;
HWND g_WowWnd = NULL;

// --- БЕЗОПАСНОЕ ЧТЕНИЕ ПАМЯТИ ---
template <typename T>
T SafeRead(uintptr_t address) {
    T buffer = T();
    if (address == 0) return buffer;
    __try { buffer = *(T*)address; } 
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return buffer;
}

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

void TapKey(WORD vKey) {
    INPUT ip = {0}; ip.type = INPUT_KEYBOARD; 
    ip.ki.wVk = vKey; ip.ki.dwFlags = 0; SendInput(1, &ip, sizeof(INPUT));
    Sleep(40);
    ip.ki.dwFlags = KEYEVENTF_KEYUP; SendInput(1, &ip, sizeof(INPUT));
}

// --- ЛОГИКА БОТА ДЛЯ MAIN THREAD ---
void __stdcall DoBotLogic() {
    int cmd = g_Cmd.type;
    if (cmd == 0) return;

    uintptr_t pLocal = GetPlayer();
    if (pLocal && SafeRead<int>(pLocal + 0x14) == 4) {
        __try {
            if (cmd == 1) { 
                float pos[3] = { g_Cmd.x, g_Cmd.y, g_Cmd.z };
                // Передаем GUID цели в CTM, чтобы движок понял, куда мы бежим
                ClickToMove(pLocal, 4, (uint64_t*)&g_Cmd.guid, pos, 0.5f);
            } else if (cmd == 2) { 
                Lua_DoString("if not IsCurrentSpell(6603) then StartAttack() end", "bot", 0);
            } else if (cmd == 3) { 
                char cmdBuf[128];
                sprintf_s(cmdBuf, "TargetUnit('0x%llX') InteractUnit('target')", g_Cmd.guid);
                Lua_DoString(cmdBuf, "bot", 0);
            } else if (cmd == 4) { 
                Lua_DoString("if LootFrame:IsVisible() then for i=1,GetNumLootItems() do LootSlot(i) end CloseLoot() end", "bot", 0);
            } else if (cmd == 5) {
                // Принудительно включаем Click-To-Move и AutoLoot
                Lua_DoString("SetCVar('autoInteract', '1') SetCVar('autoLootDefault', '1')", "bot", 0);
            }
        } __except(1) {}
    }
    // ЖЕЛЕЗОБЕТОННЫЙ СБРОС КОМАНДЫ, чтобы внешний поток никогда не зависал
    g_Cmd.type = 0;
}

// --- ГОЛЫЙ ХУК (NAKED) ---
__declspec(naked) void Hook_CGGameUI_Update() {
    __asm {
        pushad
        pushfd
        call DoBotLogic
        popfd
        popad
        mov eax, g_Trampoline // Безопасный прыжок
        jmp eax
    }
}

// --- УСТАНОВКА DETOUR ХУКА ---
bool InstallDetourHook() {
    void* target = (void*)ADDR_CGGAMEUI_UPDATE;
    int len = 6; 
    
    g_Trampoline = VirtualAlloc(NULL, len + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_Trampoline) return false;
    
    memcpy(g_Trampoline, target, len);
    
    uintptr_t gate = (uintptr_t)target + len;
    *(BYTE*)((uintptr_t)g_Trampoline + len) = 0xE9; 
    *(uintptr_t*)((uintptr_t)g_Trampoline + len + 1) = gate - ((uintptr_t)g_Trampoline + len) - 5;
    
    DWORD old;
    VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &old);
    memset(target, 0x90, len); 
    *(BYTE*)target = 0xE9; 
    *(uintptr_t*)((uintptr_t)target + 1) = (uintptr_t)Hook_CGGameUI_Update - (uintptr_t)target - 5;
    VirtualProtect(target, len, old, &old);
    
    return true;
}

void RemoveDetourHook() {
    if (g_Trampoline) {
        void* target = (void*)ADDR_CGGAMEUI_UPDATE;
        DWORD old;
        VirtualProtect(target, 6, PAGE_EXECUTE_READWRITE, &old);
        memcpy(target, g_Trampoline, 6); 
        VirtualProtect(target, 6, old, &old);
        VirtualFree(g_Trampoline, 0, MEM_RELEASE);
    }
}

// --- ОСНОВНОЙ ПОТОК ---
DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin GodMode v19.0 (Anti-Idiot Settings) ---\n");
    if (InstallDetourHook()) {
        printf("[+] CGGameUI::Update Hooked! FSM Sync verified.\n");
    } else {
        printf("[-] Hook failed. Run as Administrator!\n");
        return 0;
    }
    printf("[!] CTM & AutoLoot will be forced via Lua.\n");
    printf("--------------------------------------------------\n");

    uintptr_t connectionAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    g_WowWnd = FindWindowA(NULL, "World of Warcraft");
    BotState state = STATE_SEARCH;
    uint64_t activeTargetGuid = 0;
    DWORD stateStartTime = GetTickCount();

    // Запускаем инициализацию настроек игры
    g_Cmd.type = 5;
    while(g_Cmd.type != 0) Sleep(10);

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConn = SafeRead<uintptr_t>(connectionAddr);
        SetConsoleCursor(0, 6);

        if (clientConn) {
            uintptr_t mgr = SafeRead<uintptr_t>(clientConn + OFFSET_OBJECT_MANAGER);
            if (mgr) {
                uintptr_t cur = SafeRead<uintptr_t>(mgr + 0xAC);
                uint64_t myGuid = SafeRead<uint64_t>(mgr + 0xC0);
                uintptr_t pLocal = 0;

                while (cur != 0 && (cur & 1) == 0) {
                    if (SafeRead<uint64_t>(cur + 0x30) == myGuid && myGuid != 0) { pLocal = cur; break; }
                    cur = SafeRead<uintptr_t>(cur + 0x3C);
                }

                if (pLocal) {
                    uintptr_t pDesc = SafeRead<uintptr_t>(pLocal + 0x8);
                    int hp = SafeRead<int>(pDesc + 0x60), maxHp = SafeRead<int>(pDesc + 0x80), lvl = SafeRead<int>(pDesc + 0xD8);
                    float myX = SafeRead<float>(pLocal + 0x798), myY = SafeRead<float>(pLocal + 0x79C);

                    printf("[PLAYER] HP: %-5d/%-5d | LVL: %d | POS: %.1f, %.1f      \n", hp, maxHp, lvl, myX, myY);
                    printf("--------------------------------------------------\n");

                    // Единственный безопасный хардверный каст (Хил)
                    if (hp > 0 && maxHp > 0 && (hp * 100 / maxHp) < 40) {
                        if (GetForegroundWindow() == g_WowWnd) {
                            TapKey('9'); 
                            Sleep(1500); 
                        }
                    }

                    DWORD elapsed = GetTickCount() - stateStartTime;

                    // --- FSM ---
                    if (state == STATE_SEARCH) {
                        float bestDist = 45.0f; activeTargetGuid = 0;
                        cur = SafeRead<uintptr_t>(mgr + 0xAC);
                        
                        while (cur != 0 && (cur & 1) == 0) {
                            if (SafeRead<int>(cur + 0x14) == 3) { 
                                uintptr_t d = SafeRead<uintptr_t>(cur + 0x8);
                                int mHp = SafeRead<int>(d + 0x60), mLvl = SafeRead<int>(d + 0xD8);
                                uint64_t mSummoner = SafeRead<uint64_t>(d + 0x38);
                                
                                if (mHp > 0 && mLvl <= lvl + 2 && mSummoner == 0) {
                                    float dx = SafeRead<float>(cur + 0x798) - myX, dy = SafeRead<float>(cur + 0x79C) - myY;
                                    float dist = sqrt(dx*dx + dy*dy);
                                    if (dist < bestDist) { bestDist = dist; activeTargetGuid = SafeRead<uint64_t>(cur + 0x30); }
                                }
                            }
                            cur = SafeRead<uintptr_t>(cur + 0x3C);
                        }
                        
                        if (activeTargetGuid != 0) { state = STATE_MOVE; stateStartTime = GetTickCount(); }
                    }

                    if (activeTargetGuid != 0) {
                        if (pDesc) *(uint64_t*)(pDesc + 0x48) = activeTargetGuid; 
                        *(uint64_t*)ADDR_TARGET_GUID = activeTargetGuid;

                        uintptr_t tObj = 0; cur = SafeRead<uintptr_t>(mgr + 0xAC);
                        while (cur != 0 && (cur & 1) == 0) { 
                            if(SafeRead<uint64_t>(cur+0x30) == activeTargetGuid) { tObj = cur; break; } 
                            cur = SafeRead<uintptr_t>(cur+0x3C); 
                        }
                        
                        if (tObj) {
                            uintptr_t td = SafeRead<uintptr_t>(tObj + 0x8);
                            int thp = SafeRead<int>(td + 0x60);
                            float tx = SafeRead<float>(tObj + 0x798), ty = SafeRead<float>(tObj + 0x79C), tz = SafeRead<float>(tObj + 0x7A0);
                            float dx = tx - myX, dy = ty - myY;
                            float dist = sqrt(dx*dx + dy*dy);
                            float cYaw = atan2(dy, dx); if (cYaw < 0) cYaw += 6.283185f;

                            printf("[TARGET] HP: %-5d | DIST: %.2f yds                  \n", thp, dist);
                            
                            if (thp > 0) {
                                *(float*)(pLocal + 0x7A8) = cYaw; 
                                
                                if (dist > 4.2f) {
                                    if (state == STATE_MOVE && elapsed > 12000) {
                                        printf("[!] ЗАСТРЯЛ! Сбрасываю таргет...              \n");
                                        activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                                        continue;
                                    }
                                    if (state != STATE_MOVE) { state = STATE_MOVE; stateStartTime = GetTickCount(); }
                                    
                                    g_Cmd.guid = activeTargetGuid;
                                    g_Cmd.x = tx; g_Cmd.y = ty; g_Cmd.z = tz; 
                                    g_Cmd.type = 1;
                                    while(g_Cmd.type != 0) Sleep(10);
                                } else {
                                    if (state == STATE_COMBAT && elapsed > 25000) {
                                        printf("[!] БОЙ ЗАБАГАЛСЯ! Сбрасываю...               \n");
                                        activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                                        continue;
                                    }
                                    if (state != STATE_COMBAT) { state = STATE_COMBAT; stateStartTime = GetTickCount(); }

                                    g_Cmd.type = 2;
                                    while(g_Cmd.type != 0) Sleep(10);
                                    Sleep(100);
                                }
                            } else {
                                if (state != STATE_LOOT) { state = STATE_LOOT; stateStartTime = GetTickCount(); }
                                
                                printf("[STATE] ЖДЕМ ГЕНЕРАЦИЮ ТРУПА СЕРВЕРОМ...         \n");
                                Sleep(1500); 
                                
                                printf("[STATE] INTERNAL ВЗАИМОДЕЙСТВИЕ (LUA)...         \n");
                                g_Cmd.guid = activeTargetGuid; g_Cmd.type = 3;
                                while(g_Cmd.type != 0) Sleep(10);
                                
                                Sleep(1500); 
                                
                                printf("[STATE] LUA АВТОСБОР...                            \n");
                                g_Cmd.type = 4;
                                while(g_Cmd.type != 0) Sleep(10);
                                
                                Sleep(1000); 
                                
                                if (pDesc) *(uint64_t*)(pDesc + 0x48) = 0; 
                                *(uint64_t*)ADDR_TARGET_GUID = 0; 
                                activeTargetGuid = 0; 
                                state = STATE_SEARCH;
                                stateStartTime = GetTickCount();
                            }
                        } else { 
                            activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                        }
                    } else { 
                        printf("[TARGET] ПОИСК НОВОЙ ЦЕЛИ...                      \n");
                        stateStartTime = GetTickCount();
                    }
                    printf("[FSM] %-15s | JMP HOOK: ACTIVE         \n", stateNames[state]);
                }
            }
        }
        Sleep(100);
    }

    RemoveDetourHook();
    fclose(f); FreeConsole(); FreeLibraryAndExitThread((HMODULE)lpParam, 0); return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,MainThread,h,0,0); }
    return TRUE;
}
