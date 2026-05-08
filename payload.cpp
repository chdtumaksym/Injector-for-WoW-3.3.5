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
#define ADDR_GET_PLAYER          0x004038BE
#define ADDR_CLICK_TO_MOVE       0x00611130

// --- ФУНКЦИИ ДВИЖКА ---
typedef void(__cdecl* tFrameScript_Execute)(const char* command, const char* filename, void* reserved);
typedef uintptr_t(__cdecl* tGetActivePlayer)();
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);

tFrameScript_Execute Lua_DoString = (tFrameScript_Execute)ADDR_LUA_EXECUTE;
tGetActivePlayer GetPlayer = (tGetActivePlayer)ADDR_GET_PLAYER;
tClickToMove CallClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

// --- LUA UNLOCKER (СНИМАЕТ ЗАЩИТУ BLIZZARD) ---
// Патчим проверку аппаратного события в движке Lua, делая все функции доступными
void UnlockLua() {
    DWORD oldProtect;
    // Оффсет для 3.3.5a: обход проверки isSecure()
    void* addr1 = (void*)0x0081BF24;
    VirtualProtect(addr1, 2, PAGE_EXECUTE_READWRITE, &oldProtect);
    *(WORD*)addr1 = 0x9090; // NOP NOP
    VirtualProtect(addr1, 2, oldProtect, &oldProtect);

    void* addr2 = (void*)0x0081CB63;
    VirtualProtect(addr2, 2, PAGE_EXECUTE_READWRITE, &oldProtect);
    *(WORD*)addr2 = 0x9090; // NOP NOP
    VirtualProtect(addr2, 2, oldProtect, &oldProtect);
}

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

// Выполняем Lua в контексте потока через таймер-хак (без хуков EndScene/WndProc)
void SafeLuaExecute(const char* luaCode) {
    __try {
        Lua_DoString(luaCode, "bot", 0);
    } __except(1) {}
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin Singleplayer GodMode v24.0 (Lua Unlocked) ---\n");
    
    // Взламываем Lua движок
    UnlockLua();
    printf("[+] Lua Engine Unlocked! Защита Близзард отключена.\n");
    printf("[!] БОТ РАБОТАЕТ ПОЛНОСТЬЮ БЕЗ БИНДОВ И НАЖАТИЙ.\n");
    printf("--------------------------------------------------\n");

    uintptr_t connectionAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    BotState state = STATE_SEARCH;
    uint64_t activeTargetGuid = 0;
    DWORD stateStartTime = GetTickCount();
    
    // Принудительно включаем Click-To-Move и AutoLoot
    SafeLuaExecute("SetCVar('autoInteract', '1') SetCVar('autoLootDefault', '1')");

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConn = SafeRead<uintptr_t>(connectionAddr);
        SetConsoleCursor(0, 6);

        if (!clientConn) {
            printf("[!] Ожидание подключения к миру...                           \n");
            Sleep(500);
            continue;
        }

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

                // Хил через Unlocked Lua (без кнопок)
                if (hp > 0 && maxHp > 0 && (hp * 100 / maxHp) < 40) {
                    SafeLuaExecute("CastSpellByID(19750)"); // Вспышка света
                    Sleep(1500); 
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
                                
                                // Бег через нативный вызов (CTM включен принудительно)
                                float pos[3] = { tx, ty, tz };
                                __try { CallClickToMove(pLocal, 4, &activeTargetGuid, pos, 0.5f); } __except(1) {}
                                
                            } else {
                                if (state == STATE_COMBAT && elapsed > 25000) {
                                    printf("[!] БОЙ ЗАБАГАЛСЯ! Сбрасываю...               \n");
                                    activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                                    continue;
                                }
                                if (state != STATE_COMBAT) { state = STATE_COMBAT; stateStartTime = GetTickCount(); }

                                // Атака через разблокированный Lua
                                char atkBuf[128];
                                sprintf_s(atkBuf, "TargetUnit('0x%llX') if not IsCurrentSpell(6603) then StartAttack() end", activeTargetGuid);
                                SafeLuaExecute(atkBuf);
                            }
                        } else {
                            if (state != STATE_LOOT) { state = STATE_LOOT; stateStartTime = GetTickCount(); }
                            
                            printf("[STATE] ЖДЕМ ГЕНЕРАЦИЮ ТРУПА СЕРВЕРОМ...         \n");
                            Sleep(1200); 
                            
                            printf("[STATE] ВЗАИМОДЕЙСТВИЕ И СБОР ЛУТА (LUA)...      \n");
                            char interactBuf[128];
                            sprintf_s(interactBuf, "TargetUnit('0x%llX') InteractUnit('target')", activeTargetGuid);
                            SafeLuaExecute(interactBuf);
                            
                            Sleep(1500); 
                            
                            SafeLuaExecute("if LootFrame:IsVisible() then for i=1,GetNumLootItems() do LootSlot(i) end CloseLoot() end");
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
                printf("[FSM] %-15s | LUA UNLOCKER: ACTIVE     \n", stateNames[state]);
            }
        }
        Sleep(100); // Чтобы не вешать процессор
    }

    FreeConsole(); FreeLibraryAndExitThread((HMODULE)lpParam, 0); return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,MainThread,h,0,0); }
    return TRUE;
}
