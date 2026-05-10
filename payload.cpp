#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdarg>

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

struct Vector3 { float x, y, z; };

std::vector<Vector3> RequestPathFromServer(Vector3 start, Vector3 end) {
    std::vector<Vector3> path;
    if (!WaitNamedPipeA("\\\\.\\pipe\\WoWNavMeshPipe", 100)) return path;

    HANDLE hPipe = CreateFileA("\\\\.\\pipe\\WoWNavMeshPipe", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPipe != INVALID_HANDLE_VALUE) {
        struct { Vector3 s; Vector3 e; } req = { start, end };
        DWORD bytesWritten, bytesRead;
        WriteFile(hPipe, &req, sizeof(req), &bytesWritten, NULL);
        
        int count = 0;
        if (ReadFile(hPipe, &count, sizeof(int), &bytesRead, NULL) && count > 0) {
            path.resize(count);
            ReadFile(hPipe, path.data(), count * sizeof(Vector3), &bytesRead, NULL);
        }
        CloseHandle(hPipe);
    }
    return path;
}

std::vector<Vector3> g_CurrentPath;
int g_PathIndex = 0;
uint64_t g_PathTargetGuid = 0;

void Log(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    static std::string lastMsg = "";
    std::string newMsg(buffer);
    if (newMsg == lastMsg) return; 
    lastMsg = newMsg;

    CreateDirectoryA("C:\\WoWBot", NULL);
    std::ofstream logFile("C:\\WoWBot\\bot_log.txt", std::ios_base::app);
    if (logFile.is_open()) {
        logFile << newMsg << "\n";
        logFile.close();
    }
}

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
        Log("[-] ERROR: Could not open profile %s", filename.c_str());
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
    Log("[+] Profile loaded! Total tasks: %d", g_Profile.size());
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
    static DWORD lastTime = 0;

    float diff = sqrt(pow(x - lastX, 2) + pow(y - lastY, 2) + pow(z - lastZ, 2));

    if (guid != lastGuid || type != lastType || diff > 1.5f || GetTickCount() - lastTime > 1500) {
        uint64_t ctmGuid = guid;
        float ctmPos[3] = { x, y, z };
        ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, 0, type, &ctmGuid, ctmPos, 0.5f);
        
        lastGuid = guid; lastType = type;
        lastX = x; lastY = y; lastZ = z;
        lastTime = GetTickCount();
    }
}

uint64_t FindNpcGuidById(int npcId, float& outX, float& outY, float& outZ) {
    uintptr_t conn = *(uintptr_t*)ADDR_S_CUR_MGR;
    uintptr_t mgr = conn ? *(uintptr_t*)(conn + OFFSET_OBJECT_MANAGER) : 0;
    if (!mgr) return 0;

    uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
    while (cur && (cur & 1) == 0) {
        int type = *(int*)(cur + 0x14);
        if (type == 3) { 
            uintptr_t desc = *(uintptr_t*)(cur + 0x8);
            if (desc) {
                int entryId = *(int*)(desc + 0xC); 
                if (entryId == npcId) {
                    outX = *(float*)(cur + 0x798);
                    outY = *(float*)(cur + 0x79C);
                    outZ = *(float*)(cur + 0x7A0);
                    return *(uint64_t*)(cur + 0x30); 
                }
            }
        }
        cur = *(uintptr_t*)(cur + 0x3C);
    }
    return 0;
}

// [!] ЧТЕНИЕ ЖУРНАЛА КВЕСТОВ ИЗ ПАМЯТИ [!]
bool HasQuest(uintptr_t pLocalDesc, int questId) {
    // В 3.3.5a журнал квестов начинается со смещения 0x1394 в дескрипторе игрока
    // Всего 25 слотов, каждый слот занимает 16 байт (4 uint32)
    for (int i = 0; i < 25; i++) {
        int qId = *(int*)(pLocalDesc + 0x1394 + (i * 16));
        if (qId == questId) return true;
    }
    return false;
}

void CompleteKill() {
    g_Blacklist.push_back(g_BotTarget);
    g_BotTarget = 0; 
    g_CurrentPath.clear(); 
    ExecuteLua("ClearTarget(); CloseLoot();"); 

    if (!g_Profile.empty() && g_CurrentTaskIndex < g_Profile.size()) {
        if (g_Profile[g_CurrentTaskIndex].type == TASK_GRIND) {
            g_Profile[g_CurrentTaskIndex].killsDone++;
            Log("[GRIND] Kill %d / %d", g_Profile[g_CurrentTaskIndex].killsDone, g_Profile[g_CurrentTaskIndex].count);
            if (g_Profile[g_CurrentTaskIndex].killsDone >= g_Profile[g_CurrentTaskIndex].count) {
                g_CurrentTaskIndex++;
                Log("[TASK] Grind complete! Moving to next task.");
            }
        }
    }
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
    
    uint32_t myFlags = *(uint32_t*)(pLocalDesc + 0xEC); 
    bool inCombat = (myFlags & 0x80000) != 0; 
    int myFaction = *(int*)(pLocalDesc + 0xDC);
    
    int myLevel = *(int*)(pLocalDesc + 0xD8);
    static int lastLevel = 0;

    if (myLevel != lastLevel) {
        if (lastLevel != 0) Log("\n[LEVEL UP] Congratulations! You are now level %d!\n", myLevel);
        lastLevel = myLevel;
    }

    if (myHpPercent < 40.0f) {
        ExecuteLua("MoveForwardStop(); MoveBackwardStop();"); 
        Log("[SURVIVAL] HP %.1f%%! Pausing tasks to heal...", myHpPercent);
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
        if (!hasTarget) { g_BotTarget = 0; g_CurrentPath.clear(); }
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
            
            if (dist > 5.0f) {
                if (g_PathTargetGuid != g_BotTarget || g_CurrentPath.empty()) {
                    g_CurrentPath = RequestPathFromServer({myX, myY, myZ}, {tX, tY, tZ});
                    g_PathIndex = 0;
                    g_PathTargetGuid = g_BotTarget;
                }
                
                if (!g_CurrentPath.empty() && g_PathIndex < g_CurrentPath.size()) {
                    Vector3 nextPt = g_CurrentPath[g_PathIndex];
                    float distToPt = GetDistance3D(myX, myY, myZ, nextPt.x, nextPt.y, nextPt.z);
                    
                    if (distToPt < 1.5f) g_PathIndex++;
                    else ActionCTM(pLocal, CTM_MOVE, 0, nextPt.x, nextPt.y, nextPt.z);
                    
                    Log("[COMBAT] Navigating to target... Dist: %.1f", dist);
                } else {
                    ActionCTM(pLocal, CTM_ATTACK, g_BotTarget, tX, tY, tZ); 
                }
            } else {
                ActionCTM(pLocal, CTM_ATTACK, g_BotTarget, tX, tY, tZ);
                Log("[COMBAT] Attacking... Dist: %.1f", dist);
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
                    if (g_PathTargetGuid != g_BotTarget || g_CurrentPath.empty()) {
                        g_CurrentPath = RequestPathFromServer({myX, myY, myZ}, {tX, tY, tZ});
                        g_PathIndex = 0;
                        g_PathTargetGuid = g_BotTarget;
                    }
                    
                    if (!g_CurrentPath.empty() && g_PathIndex < g_CurrentPath.size()) {
                        Vector3 nextPt = g_CurrentPath[g_PathIndex];
                        if (GetDistance3D(myX, myY, myZ, nextPt.x, nextPt.y, nextPt.z) < 1.5f) g_PathIndex++;
                        else ActionCTM(pLocal, CTM_MOVE, 0, nextPt.x, nextPt.y, nextPt.z);
                    } else {
                        ActionCTM(pLocal, CTM_MOVE, g_BotTarget, tX, tY, tZ);
                    }
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
                        CompleteKill(); 
                        isLooting = false;
                    }
                }
            } else {
                if (isLooting) {
                    CompleteKill(); 
                    isLooting = false;
                } else {
                    if (deathTime == 0) deathTime = GetTickCount();
                    if (GetTickCount() - deathTime > 1500) {
                        CompleteKill(); 
                        deathTime = 0;
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
        g_CurrentPath.clear(); 
        ProgrammaticTarget(g_BotTarget);
        return;
    }

    if (g_Profile.empty() || g_CurrentTaskIndex >= g_Profile.size()) {
        Log("[IDLE] Profile finished or not loaded. Waiting...");
        return;
    }

    BotTask& task = g_Profile[g_CurrentTaskIndex];
    static DWORD taskTimer = 0; 

    if (task.type == TASK_GOTO) {
        float distToWaypoint = GetDistance3D(myX, myY, myZ, task.x, task.y, task.z);
        
        static float lastDist = 0;
        static DWORD stuckTimer = 0;
        
        if (abs(distToWaypoint - lastDist) < 0.5f) {
            if (GetTickCount() - stuckTimer > 3000) {
                Log("[!] STUCK ON WALL! Jumping...");
                ExecuteLua("JumpOrAscendStart();");
                stuckTimer = GetTickCount(); 
            }
        } else {
            lastDist = distToWaypoint;
            stuckTimer = GetTickCount();
        }

        if (g_CurrentPath.empty()) {
            g_CurrentPath = RequestPathFromServer({myX, myY, myZ}, {task.x, task.y, task.z});
            g_PathIndex = 0;
        }

        if (distToWaypoint < 2.0f) {
            Log("[TASK] Reached waypoint %d!", g_CurrentTaskIndex);
            g_CurrentTaskIndex++; 
            g_CurrentPath.clear();
        } else {
            if (!g_CurrentPath.empty() && g_PathIndex < g_CurrentPath.size()) {
                Vector3 nextPt = g_CurrentPath[g_PathIndex];
                if (GetDistance3D(myX, myY, myZ, nextPt.x, nextPt.y, nextPt.z) < 1.5f) g_PathIndex++;
                else ActionCTM(pLocal, CTM_MOVE, 0, nextPt.x, nextPt.y, nextPt.z);
                Log("[TASK] Navigating to Waypoint %d... Dist: %.1f", g_CurrentTaskIndex, distToWaypoint);
            } else {
                ActionCTM(pLocal, CTM_MOVE, 0, task.x, task.y, task.z); 
            }
        }
    }
    else if (task.type == TASK_ACCEPT_QUEST) {
        // [!] УМНАЯ ПРОВЕРКА: Если квест уже есть в журнале - пропускаем задачу!
        if (HasQuest(pLocalDesc, task.questId)) {
            Log("[TASK] Quest %d is already in log! Skipping...", task.questId);
            g_CurrentTaskIndex++;
            return;
        }

        float npcX = 0, npcY = 0, npcZ = 0;
        uint64_t npcGuid = FindNpcGuidById(task.npcId, npcX, npcY, npcZ);
        
        if (!npcGuid) {
            Log("[TASK] Looking for NPC %d...", task.npcId);
            return;
        }

        ProgrammaticTarget(npcGuid);
        float distToNpc = GetDistance3D(myX, myY, myZ, npcX, npcY, npcZ);
        
        if (distToNpc > 4.5f) {
            if (g_PathTargetGuid != npcGuid || g_CurrentPath.empty()) {
                g_CurrentPath = RequestPathFromServer({myX, myY, myZ}, {npcX, npcY, npcZ});
                g_PathIndex = 0;
                g_PathTargetGuid = npcGuid;
            }
            
            if (!g_CurrentPath.empty() && g_PathIndex < g_CurrentPath.size()) {
                Vector3 nextPt = g_CurrentPath[g_PathIndex];
                if (GetDistance3D(myX, myY, myZ, nextPt.x, nextPt.y, nextPt.z) < 1.5f) g_PathIndex++;
                else ActionCTM(pLocal, CTM_MOVE, 0, nextPt.x, nextPt.y, nextPt.z);
            } else {
                ActionCTM(pLocal, CTM_MOVE, npcGuid, npcX, npcY, npcZ);
            }
            Log("[TASK] Approaching NPC %d... Dist: %.1f", task.npcId, distToNpc);
            taskTimer = 0;
        } else {
            if (taskTimer == 0) {
                ExecuteLua("MoveForwardStop();");
                *(uint64_t*)ADDR_MOUSEOVER_GUID = npcGuid;
                ExecuteLua("InteractUnit('mouseover')"); 
                taskTimer = GetTickCount();
                Log("[TASK] Opening Dialog with NPC...");
            }
            else if (GetTickCount() - taskTimer > 1500) {
                ExecuteLua("SelectGossipAvailableQuest(1); SelectAvailableQuest(1); AcceptQuest();");
                Log("[TASK] Quest Accepted!");
                g_CurrentTaskIndex++;
                g_CurrentPath.clear();
                taskTimer = 0;
            }
        }
    }
    else if (task.type == TASK_TURN_IN_QUEST) {
        float npcX = 0, npcY = 0, npcZ = 0;
        uint64_t npcGuid = FindNpcGuidById(task.npcId, npcX, npcY, npcZ);
        
        if (!npcGuid) {
            Log("[TASK] Looking for NPC %d...", task.npcId);
            return;
        }

        ProgrammaticTarget(npcGuid);
        float distToNpc = GetDistance3D(myX, myY, myZ, npcX, npcY, npcZ);
        
        if (distToNpc > 4.5f) {
            if (g_PathTargetGuid != npcGuid || g_CurrentPath.empty()) {
                g_CurrentPath = RequestPathFromServer({myX, myY, myZ}, {npcX, npcY, npcZ});
                g_PathIndex = 0;
                g_PathTargetGuid = npcGuid;
            }
            
            if (!g_CurrentPath.empty() && g_PathIndex < g_CurrentPath.size()) {
                Vector3 nextPt = g_CurrentPath[g_PathIndex];
                if (GetDistance3D(myX, myY, myZ, nextPt.x, nextPt.y, nextPt.z) < 1.5f) g_PathIndex++;
                else ActionCTM(pLocal, CTM_MOVE, 0, nextPt.x, nextPt.y, nextPt.z);
            } else {
                ActionCTM(pLocal, CTM_MOVE, npcGuid, npcX, npcY, npcZ);
            }
            Log("[TASK] Approaching NPC %d... Dist: %.1f", task.npcId, distToNpc);
            taskTimer = 0;
        } else {
            if (taskTimer == 0) {
                ExecuteLua("MoveForwardStop();");
                *(uint64_t*)ADDR_MOUSEOVER_GUID = npcGuid;
                ExecuteLua("InteractUnit('mouseover')"); 
                taskTimer = GetTickCount();
                Log("[TASK] Opening Dialog with NPC...");
            }
            else if (GetTickCount() - taskTimer > 1500) {
                ExecuteLua("SelectGossipActiveQuest(1); SelectActiveQuest(1); CompleteQuest(); GetQuestReward(1);");
                Log("[TASK] Quest Turned In!");
                g_CurrentTaskIndex++;
                g_CurrentPath.clear();
                taskTimer = 0;
            }
        }
    }
    else if (task.type == TASK_GRIND) {
        Log("[TASK] Grinding mode active. Looking for mob ID %d... (%d/%d)", task.npcId, task.killsDone, task.count);
    }
    else if (task.type == TASK_LOAD_PROFILE) {
        Log("[SYSTEM] Loading next profile: %s", task.nextProfile);
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
    FreeLibraryAndExitThread(g_hModule, 0); 
    return 0;
}

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_TIMER && wParam == 1337) {
        if (GetAsyncKeyState(VK_END) & 0x8000) {
            g_Active = false;
            Log("[!] EJECTING BOT...");
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
                            Log("[+] Waypoint Saved: %.2f %.2f %.2f", x, y, z);
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
                Log("[!] BOT STATUS: %s", g_Active ? "ACTIVE" : "PAUSED");
                
                if (g_Active) {
                    std::string profileToLoad = GetProfilePathFromGUI();
                    if (!profileToLoad.empty() && (g_CurrentProfileName != profileToLoad || g_Profile.empty())) {
                        LoadProfile(profileToLoad);
                    }
                } else { 
                    g_BotTarget = 0; 
                    g_CurrentPath.clear();
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
    CreateDirectoryA("C:\\WoWBot", NULL);
    std::ofstream logFile("C:\\WoWBot\\bot_log.txt", std::ios_base::trunc);
    logFile.close();

    Log("--- Bot Core Loaded ---");
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
