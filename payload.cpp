#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>
#include <vector>
#include <algorithm>

// ==========================================
// --- АДРЕСА ПАМЯТИ WoW 3.3.5a (12340) ---
// ==========================================
#define ADDR_S_CUR_MGR          0x00C79CE0
#define OFFSET_OBJECT_MANAGER   0x2ED0
#define ADDR_CLICK_TO_MOVE      0x00727400 
#define ADDR_TARGET_GUID        0x00BD07B8 
#define ADDR_MOUSEOVER_GUID     0x00BD07A0 
#define ADDR_LUA_EXECUTE        0x00819210 

#define CTM_MOVE                4 
#define CTM_LOOT                6
#define CTM_ATTACK              11 

#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

typedef void(__fastcall* tClickToMove)(uintptr_t ecx, uintptr_t edx, int type, uint64_t* guid, float* pos, float prec);
typedef void(__cdecl* tLuaExecute)(const char* code, const char* fileName, int state);

bool g_Active = false;
WNDPROC oWndProc = nullptr;
HWND g_WoWHwnd = NULL;
HINSTANCE g_hModule = NULL; 

std::vector<uint64_t> g_Blacklist; 
uint64_t g_BotTarget = 0; 

// --- ТАЙМЕРЫ ПАЛАДИНА ---
DWORD g_GCD = 0; 
DWORD g_LastHeal = 0;
DWORD g_LastDevotion = 0;
DWORD g_LastSeal = 0;
DWORD g_LastBlessing = 0;
DWORD g_LastJudgement = 0;

float GetDistance3D(float x1, float y1, float z1, float x2, float y2, float z2) {
    return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2) + pow(z2 - z1, 2));
}

void ExecuteLua(const char* command) {
    ((tLuaExecute)ADDR_LUA_EXECUTE)(command, "bot_core", 0);
}

void ProgrammaticTarget(uint64_t guid) {
    if (*(uint64_t*)ADDR_TARGET_GUID != guid) {
        *(uint64_t*)ADDR_MOUSEOVER_GUID = guid;
        ExecuteLua("TargetUnit('mouseover')");
    }
}

void ActionCTM(uintptr_t pLocal, int type, uint64_t guid, float x, float y, float z) {
    static uint64_t lastGuid = 0;
    static int lastType = 0;
    static float lastX = 0, lastY = 0, lastZ = 0;

    float diff = sqrt(pow(x - lastX, 2) + pow(y - lastY, 2) + pow(z - lastZ, 2));

    if (guid != lastGuid || type != lastType || diff > 1.5f) {
        uint64_t ctmGuid = guid;
        float ctmPos[3] = { x, y, z };
        ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, 0, type, &ctmGuid, ctmPos, 0.5f);
        
        lastGuid = guid; lastType = type;
        lastX = x; lastY = y; lastZ = z;
    }
}

bool TryCast(int vkKey, DWORD& lastCastTime, DWORD cooldown, DWORD castTime = 1500) {
    if (GetTickCount() < g_GCD) return false; 
    if (GetTickCount() - lastCastTime < cooldown) return false; 

    UINT scanCode = MapVirtualKey(vkKey, MAPVK_VK_TO_VSC);
    LPARAM lParamDown = (scanCode << 16) | 1;
    LPARAM lParamUp = (scanCode << 16) | (1 << 30) | (1 << 31) | 1;

    PostMessage(g_WoWHwnd, WM_KEYDOWN, vkKey, lParamDown);
    PostMessage(g_WoWHwnd, WM_KEYUP, vkKey, lParamUp);

    lastCastTime = GetTickCount();
    g_GCD = GetTickCount() + castTime; 
    return true;
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

    uintptr_t pLocalDesc = *(uintptr_t*)(pLocal + 0x8);
    if (!pLocalDesc) return;
    
    int myHp = *(int*)(pLocalDesc + 0x60);
    // [!] ИСПРАВЛЕНО: 0x80 - это Макс ХП. (0x70 была Мана!)
    int myMaxHp = *(int*)(pLocalDesc + 0x80); 
    float myHpPercent = ((float)myHp / (float)myMaxHp) * 100.0f;
    
    uint32_t myFlags = *(uint32_t*)(pLocalDesc + 0x114);
    bool inCombat = (myFlags & 0x80000) != 0; 
    
    int myFaction = *(int*)(pLocalDesc + 0x8C);

    // --- 1. СИСТЕМА ВЫЖИВАНИЯ ---
    if (myHpPercent < 40.0f) {
        if (GetTickCount() >= g_GCD) {
            ExecuteLua("MoveForwardStop(); MoveBackwardStop();"); 
            if (TryCast('1', g_LastHeal, 0, 3000)) { 
                printf("[SURVIVAL] HP %.1f%%! Casting Holy Light...\n", myHpPercent);
            }
        }
        return; // Блокируем поиск мобов, пока не вылечимся
    }

    bool hasTarget = false;
    int targetHp = 0;
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

    // --- 2. СИСТЕМА БАФОВ ---
    if (!inCombat && !hasTarget) {
        if (TryCast('2', g_LastDevotion, 3600000)) { printf("[BUFF] Casting Devotion Aura\n"); return; }
        if (TryCast('3', g_LastSeal, 1680000)) { printf("[BUFF] Casting Seal of Righteousness\n"); return; }
        if (TryCast('4', g_LastBlessing, 540000)) { printf("[BUFF] Casting Blessing of Might\n"); return; }
    }

    static bool isLooting = false;
    static DWORD lootTimer = 0;

    // --- 3. БОЙ И ЛУТ ---
    if (hasTarget) {
        ProgrammaticTarget(g_BotTarget);
        float dist = GetDistance3D(myX, myY, myZ, tX, tY, tZ);

        if (targetHp > 0) {
            isLooting = false; 

            ActionCTM(pLocal, CTM_ATTACK, g_BotTarget, tX, tY, tZ);
            printf("Chasing/Attacking... Dist: %.1f      \r", dist);
            
            if (dist < 5.0f) {
                static DWORD lastAtk = 0;
                if (GetTickCount() - lastAtk > 1500) {
                    ExecuteLua("InteractUnit('mouseover'); StartAttack();");
                    lastAtk = GetTickCount();
                }

                if (TryCast('5', g_LastJudgement, 10500)) {
                    printf("\n[COMBAT] Casting Judgement!\n");
                }
            }
        } 
        else {
            if (dist > 4.5f && !isLooting) {
                ActionCTM(pLocal, CTM_MOVE, g_BotTarget, tX, tY, tZ);
                printf("Running to Corpse... Dist: %.1f        \r", dist);
            } else {
                if (!isLooting) {
                    uint64_t ctmGuid = g_BotTarget;
                    float ctmPos[3] = { tX, tY, tZ };
                    ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, 0, CTM_LOOT, &ctmGuid, ctmPos, 0.5f);
                    
                    isLooting = true;
                    lootTimer = GetTickCount();
                    printf("Looting Corpse... Waiting 3 sec.       \r");
                }

                static DWORD lastLoot = 0;
                if (GetTickCount() - lastLoot > 300) {
                    *(uint64_t*)ADDR_MOUSEOVER_GUID = g_BotTarget;
                    ExecuteLua("InteractUnit('mouseover'); if LootFrame:IsVisible() then for i=1, GetNumLootItems() do LootSlot(i) end end");
                    lastLoot = GetTickCount();
                }

                if (GetTickCount() - lootTimer > 3000) {
                    g_Blacklist.push_back(g_BotTarget);
                    g_BotTarget = 0; 
                    isLooting = false;
                    ExecuteLua("ClearTarget(); CloseLoot();"); 
                    printf("\n[+] Corpse processed. Moving on.\n");
                }
            }
        }
    } 
    else {
        // --- 4. УМНЫЙ РАДАР ---
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
                        
                        int mobFaction = *(int*)(desc + 0x8C);
                        uint32_t mobDynFlags = *(uint32_t*)(desc + 0x114);
                        uint32_t mobFlags = *(uint32_t*)(desc + 0xD4);

                        bool isSameFaction = (myFaction == mobFaction); 
                        bool isTapped = (mobDynFlags & 0x4) != 0;       
                        bool isUnattackable = (mobFlags & 0x8) != 0;    

                        if (hp > 0 && maxHp > 1 && !isSameFaction && !isTapped && !isUnattackable) {
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
            ProgrammaticTarget(g_BotTarget);
            printf("\nFound new HOSTILE target! Dist: %.1f        \n", bestDist);
        } else {
            if (GetTickCount() > g_GCD) g_GCD = 0;
            printf("Scanning for enemies...             \r");
        }
    }
}

DWORD WINAPI EjectThread(LPVOID) {
    SetWindowLongA(g_WoWHwnd, GWL_WNDPROC, (LONG)oWndProc); 
    KillTimer(g_WoWHwnd, 1337);
    Sleep(100);
    FreeConsole();
    FreeLibraryAndExitThread(g_hModule, 0); 
    return 0;
}

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_TIMER && wParam == 1337) {
        
        if (GetAsyncKeyState(VK_END) & 0x8000) {
            g_Active = false;
            printf("\n\n[!] EJECTING BOT... You can close this console.\n");
            CreateThread(0, 0, EjectThread, 0, 0, 0);
            return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
        }

        static bool isPressed = false;
        if (GetAsyncKeyState(VK_INSERT) & 0x8000) {
            if (!isPressed) {
                isPressed = true;
                g_Active = !g_Active;
                Beep(g_Active ? 800 : 400, 100);
                printf("\n\n[!] BOT STATUS: %s\n", g_Active ? "ACTIVE" : "PAUSED");
                
                if (!g_Active) { 
                    g_BotTarget = 0; 
                    ExecuteLua("ClearTarget(); CloseLoot();");
                    g_Blacklist.clear(); 
                }
            }
        } else {
            isPressed = false;
        }

        static DWORD lastTick = 0;
        static bool isPulsing = false; 

        if (g_Active && !isPulsing && (GetTickCount() - lastTick > 150)) {
            isPulsing = true;
            __try {
                BotPulse();
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
            lastTick = GetTickCount();
            isPulsing = false;
        }
    }
    return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
}

DWORD WINAPI Setup(LPVOID) {
    AllocConsole(); 
    freopen("CONOUT$", "w", stdout);
    printf("--- Bot v155: The Healthy Paladin ---\n");

    g_WoWHwnd = FindWindowA(NULL, "World of Warcraft");
    if (!g_WoWHwnd) return 0;

    oWndProc = (WNDPROC)SetWindowLongA(g_WoWHwnd, GWL_WNDPROC, (LONG)HookedWndProc);
    SetTimer(g_WoWHwnd, 1337, 50, NULL); 

    printf("[+] HP Offset Fixed (0x80).\n");
    printf("[+] Paladin Brain (Heal, Buffs, Judgement): INTEGRATED.\n");
    printf("[!] Action Bar Setup: 1=Heal, 2=Aura, 3=Seal, 4=Might, 5=Judgement\n");
    printf("[!] Press [INSERT] to Start/Pause.\n");
    printf("[!] Press [END] to Unload the Bot.\n\n");
    return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        g_hModule = h; 
        DisableThreadLibraryCalls(h);
        CreateThread(0, 0, Setup, 0, 0, 0);
    }
    return TRUE;
}
