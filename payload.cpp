#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

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
DWORD g_GCD = 0; 

// ==========================================
// --- СИСТЕМА ПРОФИЛЕЙ (ВНЕШНИЕ ФАЙЛЫ) ---
// ==========================================
enum TaskType { TASK_GOTO, TASK_ACCEPT_QUEST, TASK_TURN_IN_QUEST, TASK_GRIND };

struct BotTask {
    TaskType type;
    float x, y, z;
    int npcId;
    int questId;
    int count;
};

std::vector<BotTask> g_Profile;
int g_CurrentTaskIndex = 0;
std::string g_CurrentProfileName = "";

void LoadProfile(const std::string& filename) {
    g_Profile.clear();
    g_CurrentTaskIndex = 0;
    g_CurrentProfileName = filename;
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        printf("[-] ERROR: Could not open profile %s\n", filename.c_str());
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '/') continue; 

        std::istringstream iss(line);
        std::string command;
        iss >> command;

        BotTask task = {};
        if (command == "GOTO") {
            task.type = TASK_GOTO;
            iss >> task.x >> task.y >> task.z;
            g_Profile.push_back(task);
        } 
        else if (command == "GRIND") {
            task.type = TASK_GRIND;
            iss >> task.count;
            g_Profile.push_back(task);
        }
    }
    printf("[+] Profile '%s' loaded! Total tasks: %d\n", filename.c_str(), g_Profile.size());
}

// ==========================================
// --- БАЗОВЫЕ ФУНКЦИИ ---
// ==========================================
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

// ==========================================
// --- ЯДРО БОТА ---
// ==========================================
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
    int myMaxHp = *(int*)(pLocalDesc + 0x80); 
    if (myMaxHp <= 0) myMaxHp = 1; 
    float myHpPercent = ((float)myHp / (float)myMaxHp) * 100.0f;
    
    uint32_t myFlags = *(uint32_t*)(pLocalDesc + 0xEC); 
    bool inCombat = (myFlags & 0x80000) != 0; 
    int myFaction = *(int*)(pLocalDesc + 0xDC);
    
    int myLevel = *(int*)(pLocalDesc + 0xD8);
    static int lastLevel = 0;

    // --- АВТО-СМЕНА ПРОФИЛЕЙ ПО УРОВНЮ ---
    if (myLevel != lastLevel) {
        if (lastLevel != 0) printf("\n[LEVEL UP] Congratulations! You are now level %d!\n", myLevel);
        lastLevel = myLevel;

        if (myLevel >= 6 && g_CurrentProfileName != "C:\\Elwynn_6_10.txt") {
            printf("[SYSTEM] Level 6 reached. Leaving Northshire...\n");
            LoadProfile("C:\\Elwynn_6_10.txt");
        }
    }

    // --- 1. ВЫЖИВАНИЕ ---
    if (myHpPercent < 40.0f) {
        ExecuteLua("MoveForwardStop(); MoveBackwardStop();"); 
        printf("[SURVIVAL] HP %.1f%%! Pausing tasks to heal...\n", myHpPercent);
        return; 
    }

    bool hasTarget = false;
    int targetHp = 0;
    uint32_t targetDynFlags = 0;
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
                        targetDynFlags = *(uint32_t*)(desc + 0x13C); 
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

    static bool isLooting = false;
    static DWORD lootTimer = 0;
    static DWORD deathTime = 0;

    // ==========================================
    // РЕЖИМ БОЯ (ПРИОРИТЕТ)
    // ==========================================
    if (hasTarget) {
        ProgrammaticTarget(g_BotTarget);
        float dist = GetDistance3D(myX, myY, myZ, tX, tY, tZ);

        if (targetHp > 0) {
            isLooting = false; 
            deathTime = 0;

            ActionCTM(pLocal, CTM_ATTACK, g_BotTarget, tX, tY, tZ);
            printf("[COMBAT] Attacking... Dist: %.1f      \r", dist);
            
            if (dist < 5.0f) {
                static DWORD lastAtk = 0;
                if (GetTickCount() - lastAtk > 1500) {
                    ExecuteLua("InteractUnit('mouseover'); StartAttack();");
                    lastAtk = GetTickCount();
                }
            }
        } 
        else {
            // ЛУТ
            if (targetDynFlags & 1) { 
                if (dist > 4.5f && !isLooting) {
                    ActionCTM(pLocal, CTM_MOVE, g_BotTarget, tX, tY, tZ);
                    printf("[LOOT] Running to Corpse... Dist: %.1f        \r", dist);
                } else {
                    if (!isLooting) {
                        uint64_t ctmGuid = g_BotTarget;
                        float ctmPos[3] = { tX, tY, tZ };
                        ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, 0, CTM_LOOT, &ctmGuid, ctmPos, 0.5f);
                        isLooting = true;
                        lootTimer = GetTickCount();
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
                        printf("\n[+] Corpse processed.\n");
                    }
                }
            } else {
                if (isLooting) {
                    g_Blacklist.push_back(g_BotTarget);
                    g_BotTarget = 0; 
                    isLooting = false;
                    ExecuteLua("ClearTarget(); CloseLoot();"); 
                } else {
                    if (deathTime == 0) deathTime = GetTickCount();
                    if (GetTickCount() - deathTime > 1500) {
                        g_Blacklist.push_back(g_BotTarget);
                        g_BotTarget = 0; 
                        deathTime = 0;
                        ExecuteLua("ClearTarget();"); 
                    }
                }
            }
        }
        return; 
    } 

    // ==========================================
    // РЕЖИМ ЗАДАЧ (КВЕСТИНГ / НАВИГАЦИЯ)
    // ==========================================
    
    uint64_t bestGuid = 0;
    float bestDist = 25.0f; 

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
                    int mobFaction = *(int*)(desc + 0xDC);
                    uint32_t mobFlags = *(uint32_t*)(desc + 0xEC);
                    uint32_t mobDynFlags = *(uint32_t*)(desc + 0x13C);

                    bool isSameFaction = (myFaction == mobFaction); 
                    bool isTapped = (mobDynFlags & 0x4) != 0;       
                    bool isUnattackable = (mobFlags & 0x102) != 0; 

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
        printf("\n[!] Enemy detected! Interrupting task to fight.\n");
        return;
    }

    if (g_Profile.empty() || g_CurrentTaskIndex >= g_Profile.size()) {
        printf("[IDLE] Profile finished or not loaded. Waiting...      \r");
        return;
    }

    BotTask& task = g_Profile[g_CurrentTaskIndex];
    static DWORD taskTimer = 0; 

    if (task.type == TASK_GOTO) {
        float distToWaypoint = GetDistance3D(myX, myY, myZ, task.x, task.y, task.z);
        if (distToWaypoint < 2.0f) {
            printf("\n[TASK] Reached waypoint %d!\n", g_CurrentTaskIndex);
            g_CurrentTaskIndex++; 
        } else {
            ActionCTM(pLocal, CTM_MOVE, 0, task.x, task.y, task.z);
            printf("[TASK] Moving to Waypoint %d... Dist: %.1f      \r", g_CurrentTaskIndex, distToWaypoint);
        }
    }
    else if (task.type == TASK_ACCEPT_QUEST) {
        if (taskTimer == 0) {
            printf("\n[TASK] Accepting Quest %d from NPC %d...\n", task.questId, task.npcId);
            taskTimer = GetTickCount();
        }
        if (GetTickCount() - taskTimer > 2000) {
            g_CurrentTaskIndex++;
            taskTimer = 0;
        }
    }
    else if (task.type == TASK_TURN_IN_QUEST) {
        if (taskTimer == 0) {
            printf("\n[TASK] Turning in Quest %d to NPC %d...\n", task.questId, task.npcId);
            taskTimer = GetTickCount();
        }
        if (GetTickCount() - taskTimer > 2000) {
            g_CurrentTaskIndex++;
            taskTimer = 0;
        }
    }
    else if (task.type == TASK_GRIND) {
        printf("[TASK] Grinding mode active. Looking for mobs...      \r");
    }
}

// [!] БЕЗОПАСНАЯ ОБЕРТКА ДЛЯ ИСКЛЮЧЕНИЙ (ФИКС ОШИБКИ C2712) [!]
void SafeBotPulse() {
    __try {
        BotPulse();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Игнорируем ошибки чтения памяти
    }
}

// ==========================================
// --- ПОТОК ВЫГРУЗКИ И КЛАВИАТУРЫ ---
// ==========================================
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

        static bool f9Pressed = false;
        if (GetAsyncKeyState(VK_F9) & 0x8000) {
            if (!f9Pressed) {
                f9Pressed = true;
                
                uintptr_t conn = *(uintptr_t*)ADDR_S_CUR_MGR;
                uintptr_t mgr = conn ? *(uintptr_t*)(conn + OFFSET_OBJECT_MANAGER) : 0;
                if (mgr) {
                    uint64_t localGuid = *(uint64_t*)(mgr + 0xC0);
                    uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
                    while (cur && (cur & 1) == 0) {
                        if (*(uint64_t*)(cur + 0x30) == localGuid) {
                            float x = *(float*)(cur + 0x798);
                            float y = *(float*)(cur + 0x79C);
                            float z = *(float*)(cur + 0x7A0);
                            
                            std::ofstream outfile;
                            outfile.open("C:\\RecordedProfile.txt", std::ios_base::app);
                            outfile << "GOTO " << x << " " << y << " " << z << "\n";
                            outfile.close();
                            
                            Beep(1000, 100);
                            printf("\n[+] Waypoint Saved: %.2f %.2f %.2f\n", x, y, z);
                            break;
                        }
                        cur = *(uintptr_t*)(cur + 0x3C);
                    }
                }
            }
        } else {
            f9Pressed = false;
        }

        static bool isPressed = false;
        if (GetAsyncKeyState(VK_INSERT) & 0x8000) {
            if (!isPressed) {
                isPressed = true;
                g_Active = !g_Active;
                Beep(g_Active ? 800 : 400, 100);
                printf("\n\n[!] BOT STATUS: %s\n", g_Active ? "ACTIVE" : "PAUSED");
                
                if (g_Active) {
                    // Укажи здесь правильный путь к файлу профиля!
                    LoadProfile("C:\\Northshire_1_6.txt");
                } else { 
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
            SafeBotPulse(); // Вызываем безопасную обертку
            lastTick = GetTickCount();
            isPulsing = false;
        }
    }
    return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
}

DWORD WINAPI Setup(LPVOID) {
    AllocConsole(); 
    freopen("CONOUT$", "w", stdout);
    printf("--- Bot v161: The Compiler Fix ---\n");

    g_WoWHwnd = FindWindowA(NULL, "World of Warcraft");
    if (!g_WoWHwnd) return 0;

    oWndProc = (WNDPROC)SetWindowLongA(g_WoWHwnd, GWL_WNDPROC, (LONG)HookedWndProc);
    SetTimer(g_WoWHwnd, 1337, 50, NULL); 

    printf("[+] External Profile Engine: INTEGRATED.\n");
    printf("[+] Auto-Level Profile Switcher: INTEGRATED.\n");
    printf("[+] Waypoint Recorder (F9): INTEGRATED.\n");
    printf("[!] Press[F9] in-game to save your current position to C:\\RecordedProfile.txt\n");
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
