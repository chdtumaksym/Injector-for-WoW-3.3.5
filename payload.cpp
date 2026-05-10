#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

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

enum TaskType { TASK_GOTO, TASK_ACCEPT_QUEST, TASK_TURN_IN_QUEST, TASK_GRIND, TASK_LOAD_PROFILE };

struct BotTask {
    TaskType type;
    float x, y, z;
    int npcId;
    int questId;
    int count;
    int killsDone;
    char nextProfile[256]; 
};

std::vector<BotTask> g_Profile;
int g_CurrentTaskIndex = 0;
std::string g_CurrentProfileName = "";

std::string GetProfilePathFromGUI() {
    std::ifstream config("C:\\WoWBot\\settings.ini");
    std::string profilePath;
    if (config.is_open()) {
        std::getline(config, profilePath);
        config.close();
        return profilePath;
    }
    return "";
}

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
        task.killsDone = 0;
        if (command == "GOTO") {
            task.type = TASK_GOTO;
            iss >> task.x >> task.y >> task.z;
            g_Profile.push_back(task);
        } 
        else if (command == "GRIND") {
            task.type = TASK_GRIND;
            iss >> task.count >> task.npcId; 
            g_Profile.push_back(task);
        }
        else if (command == "ACCEPT_QUEST") {
            task.type = TASK_ACCEPT_QUEST;
            iss >> task.npcId >> task.questId;
            g_Profile.push_back(task);
        }
        else if (command == "TURN_IN_QUEST") {
            task.type = TASK_TURN_IN_QUEST;
            iss >> task.npcId >> task.questId;
            g_Profile.push_back(task);
        }
        else if (command == "LOAD_PROFILE") {
            task.type = TASK_LOAD_PROFILE;
            iss >> task.nextProfile;
            g_Profile.push_back(task);
        }
    }
    printf("[+] Profile loaded! Total tasks: %d\n", g_Profile.size());
}

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

// Функция для поиска GUID нужного NPC по его ID
uint64_t FindNpcGuidById(int npcId) {
    uintptr_t conn = *(uintptr_t*)ADDR_S_CUR_MGR;
    uintptr_t mgr = conn ? *(uintptr_t*)(conn + OFFSET_OBJECT_MANAGER) : 0;
    if (!mgr) return 0;

    uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
    while (cur && (cur & 1) == 0) {
        int type = *(int*)(cur + 0x14);
        if (type == 3) { // Если это NPC
            uintptr_t desc = *(uintptr_t*)(cur + 0x8);
            if (desc) {
                int entryId = *(int*)(desc + 0xC); // Читаем ID
                if (entryId == npcId) {
                    return *(uint64_t*)(cur + 0x30); // Возвращаем его GUID
                }
            }
        }
        cur = *(uintptr_t*)(cur + 0x3C);
    }
    return 0;
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
    int myMaxHp = *(int*)(pLocalDesc + 0x80); 
    if (myMaxHp <= 0) myMaxHp = 1; 
    float myHpPercent = ((float)myHp / (float)myMaxHp) * 100.0f;

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
            if (targetDynFlags & 1) { 
                if (dist > 4.5f && !isLooting) {
                    ActionCTM(pLocal, CTM_MOVE, g_BotTarget, tX, tY, tZ);
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
                        
                        if (!g_Profile.empty() && g_CurrentTaskIndex < g_Profile.size()) {
                            if (g_Profile[g_CurrentTaskIndex].type == TASK_GRIND) {
                                g_Profile[g_CurrentTaskIndex].killsDone++;
                                printf("\n[GRIND] Kill %d / %d\n", g_Profile[g_CurrentTaskIndex].killsDone, g_Profile[g_CurrentTaskIndex].count);
                                if (g_Profile[g_CurrentTaskIndex].killsDone >= g_Profile[g_CurrentTaskIndex].count) {
                                    g_CurrentTaskIndex++;
                                    printf("[TASK] Grind complete! Moving to next task.\n");
                                }
                            }
                        }

                        g_BotTarget = 0; 
                        isLooting = false;
                        ExecuteLua("ClearTarget(); CloseLoot();"); 
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
    
    uint64_t bestGuid = 0;
    float bestDist = 25.0f; 

    bool isGrindTask = (!g_Profile.empty() && g_CurrentTaskIndex < g_Profile.size() && g_Profile[g_CurrentTaskIndex].type == TASK_GRIND);
    int targetNpcId = isGrindTask ? g_Profile[g_CurrentTaskIndex].npcId : 0;

    cur = *(uintptr_t*)(mgr + 0xAC);
    while (cur && (cur & 1) == 0) {
        int type = *(int*)(cur + 0x14);
        if (type == 3) { 
            uint64_t guid = *(uint64_t*)(cur + 0x30);
            if (std::find(g_Blacklist.begin(), g_Blacklist.end(), guid) == g_Blacklist.end()) {
                uintptr_t desc = *(uintptr_t*)(cur + 0x8);
                if (desc) {
                    int hp = *(int*)(desc + 0x60); 
                    uint32_t mobDynFlags = *(uint32_t*)(desc + 0x13C);
                    int entryId = *(int*)(desc + 0xC); 

                    bool isTapped = (mobDynFlags & 0x4) != 0;       

                    if (isGrindTask && entryId == targetNpcId && hp > 0 && !isTapped) {
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
            
            uint64_t npcGuid = FindNpcGuidById(task.npcId);
            if (npcGuid) {
                *(uint64_t*)ADDR_MOUSEOVER_GUID = npcGuid;
                ExecuteLua("InteractUnit('mouseover'); SelectGossipAvailableQuest(1); SelectAvailableQuest(1); AcceptQuest();");
            } else {
                printf("[-] NPC %d not found nearby!\n", task.npcId);
            }
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
            
            uint64_t npcGuid = FindNpcGuidById(task.npcId);
            if (npcGuid) {
                *(uint64_t*)ADDR_MOUSEOVER_GUID = npcGuid;
                ExecuteLua("InteractUnit('mouseover'); SelectGossipActiveQuest(1); SelectActiveQuest(1); CompleteQuest(); GetQuestReward(1);");
            } else {
                printf("[-] NPC %d not found nearby!\n", task.npcId);
            }
            taskTimer = GetTickCount();
        }
        if (GetTickCount() - taskTimer > 2000) {
            g_CurrentTaskIndex++;
            taskTimer = 0;
        }
    }
    else if (task.type == TASK_GRIND) {
        printf("[TASK] Grinding mode active. Looking for mob ID %d... (%d/%d)      \r", task.npcId, task.killsDone, task.count);
    }
    else if (task.type == TASK_LOAD_PROFILE) {
        printf("\n[SYSTEM] Loading next profile: %s\n", task.nextProfile);
        std::string currentPath = GetProfilePathFromGUI();
        size_t lastSlash = currentPath.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            std::string newPath = currentPath.substr(0, lastSlash + 1) + task.nextProfile;
            LoadProfile(newPath);
        }
    }
}

void SafeBotPulse() {
    __try { BotPulse(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
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
                            
                            CreateDirectoryA("C:\\WoWBot", NULL);
                            std::ofstream outfile("C:\\WoWBot\\RecordedProfile.txt", std::ios_base::app);
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
                    std::string profileToLoad = GetProfilePathFromGUI();
                    if (!profileToLoad.empty()) {
                        LoadProfile(profileToLoad);
                    } else {
                        printf("[-] No profile selected in GUI!\n");
                    }
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
            SafeBotPulse(); 
            lastTick = GetTickCount();
            isPulsing = false;
        }
    }
    return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
}

DWORD WINAPI Setup(LPVOID) {
    AllocConsole(); 
    freopen("CONOUT$", "w", stdout);
    printf("--- Bot Core Loaded ---\n");
    g_WoWHwnd = FindWindowA(NULL, "World of Warcraft");
    if (!g_WoWHwnd) return 0;
    oWndProc = (WNDPROC)SetWindowLongA(g_WoWHwnd, GWL_WNDPROC, (LONG)HookedWndProc);
    SetTimer(g_WoWHwnd, 1337, 50, NULL); 
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
