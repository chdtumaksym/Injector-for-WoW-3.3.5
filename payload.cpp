#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "user32.lib")

// --- СТАТИЧНЫЕ ОФФСЕТЫ 3.3.5a (12340) ---
#define OFFSET_S_CUR_MGR         0x00879CE0
#define OFFSET_OBJECT_MANAGER    0x2ED0
#define ADDR_TARGET_GUID         0x00BD07B0 

#define ADDR_CLICK_TO_MOVE       0x00611130
#define ADDR_INTERACT            0x00609390
#define ADDR_GET_PLAYER          0x004038BE

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

// --- ФУНКЦИИ ДВИЖКА ---
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef void(__thiscall* tInteract)(uintptr_t playerPtr, uintptr_t unitPtr);
typedef uintptr_t(__cdecl* tGetActivePlayer)();

tClickToMove EngineClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;
tInteract EngineInteractUnit = (tInteract)ADDR_INTERACT;
tGetActivePlayer GetPlayer = (tGetActivePlayer)ADDR_GET_PLAYER;

struct BotCommand {
    volatile int type; // 0: None, 1: Move, 2: Interact (Attack/Loot)
    volatile uintptr_t pTarget;
    volatile uint64_t guid;
    volatile float x, y, z;
} g_Cmd;

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

// --- IAT HOOKING (ПЕРЕХВАТ ТАБЛИЦЫ ИМПОРТА) ---
typedef BOOL(WINAPI* tPeekMessageW)(LPMSG, HWND, UINT, UINT, UINT);
tPeekMessageW oPeekMessageW = nullptr;

// Эта функция теперь вызывается игрой каждый раз, когда она обращается к Windows
BOOL WINAPI HookedPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    int cmd = g_Cmd.type;
    if (cmd != 0) {
        uintptr_t pLocal = GetPlayer();
        if (pLocal && SafeRead<int>(pLocal + 0x14) == 4) {
            __try {
                if (cmd == 1) { 
                    float pos[3] = { g_Cmd.x, g_Cmd.y, g_Cmd.z };
                    EngineClickToMove(pLocal, 4, (uint64_t*)&g_Cmd.guid, pos, 0.5f);
                } 
                else if (cmd == 2 && g_Cmd.pTarget != 0) { 
                    EngineInteractUnit(pLocal, g_Cmd.pTarget); 
                }
            } __except(1) {}
        }
        g_Cmd.type = 0; // Команда выполнена в 100% безопасном контексте главного потока
    }
    return oPeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
}

bool InstallIATHook() {
    HMODULE hExe = GetModuleHandleA(NULL);
    if (!hExe) return false;

    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hExe;
    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((uintptr_t)hExe + dosHeader->e_lfanew);
    IMAGE_DATA_DIRECTORY importDirectory = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDirectory.VirtualAddress == 0) return false;

    PIMAGE_IMPORT_DESCRIPTOR importDesc = (PIMAGE_IMPORT_DESCRIPTOR)((uintptr_t)hExe + importDirectory.VirtualAddress);

    while (importDesc->Name != 0) {
        char* moduleName = (char*)((uintptr_t)hExe + importDesc->Name);
        if (_stricmp(moduleName, "USER32.dll") == 0) {
            PIMAGE_THUNK_DATA origFirstThunk = (PIMAGE_THUNK_DATA)((uintptr_t)hExe + importDesc->OriginalFirstThunk);
            PIMAGE_THUNK_DATA firstThunk = (PIMAGE_THUNK_DATA)((uintptr_t)hExe + importDesc->FirstThunk);

            while (origFirstThunk->u1.AddressOfData != 0) {
                if (!(origFirstThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                    PIMAGE_IMPORT_BY_NAME importByName = (PIMAGE_IMPORT_BY_NAME)((uintptr_t)hExe + origFirstThunk->u1.AddressOfData);
                    if (strcmp((char*)importByName->Name, "PeekMessageW") == 0) {
                        DWORD oldProtect;
                        VirtualProtect(&firstThunk->u1.Function, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect);
                        oPeekMessageW = (tPeekMessageW)firstThunk->u1.Function; // Сохраняем оригинал
                        firstThunk->u1.Function = (uintptr_t)HookedPeekMessageW; // Пишем наш хук
                        VirtualProtect(&firstThunk->u1.Function, sizeof(uintptr_t), oldProtect, &oldProtect);
                        return true;
                    }
                }
                origFirstThunk++;
                firstThunk++;
            }
        }
        importDesc++;
    }
    return false;
}

void RemoveIATHook() {
    if (!oPeekMessageW) return;
    HMODULE hExe = GetModuleHandleA(NULL);
    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hExe;
    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((uintptr_t)hExe + dosHeader->e_lfanew);
    PIMAGE_IMPORT_DESCRIPTOR importDesc = (PIMAGE_IMPORT_DESCRIPTOR)((uintptr_t)hExe + ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

    while (importDesc->Name != 0) {
        char* moduleName = (char*)((uintptr_t)hExe + importDesc->Name);
        if (_stricmp(moduleName, "USER32.dll") == 0) {
            PIMAGE_THUNK_DATA origFirstThunk = (PIMAGE_THUNK_DATA)((uintptr_t)hExe + importDesc->OriginalFirstThunk);
            PIMAGE_THUNK_DATA firstThunk = (PIMAGE_THUNK_DATA)((uintptr_t)hExe + importDesc->FirstThunk);
            while (origFirstThunk->u1.AddressOfData != 0) {
                if (!(origFirstThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                    PIMAGE_IMPORT_BY_NAME importByName = (PIMAGE_IMPORT_BY_NAME)((uintptr_t)hExe + origFirstThunk->u1.AddressOfData);
                    if (strcmp((char*)importByName->Name, "PeekMessageW") == 0) {
                        DWORD oldProtect;
                        VirtualProtect(&firstThunk->u1.Function, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect);
                        firstThunk->u1.Function = (uintptr_t)oPeekMessageW; // Восстанавливаем
                        VirtualProtect(&firstThunk->u1.Function, sizeof(uintptr_t), oldProtect, &oldProtect);
                        return;
                    }
                }
                origFirstThunk++; firstThunk++;
            }
        }
        importDesc++;
    }
}

void SendCommandAndWait(int type, uint64_t guid = 0, float x = 0, float y = 0, float z = 0, uintptr_t pTarget = 0) {
    g_Cmd.guid = guid; g_Cmd.x = x; g_Cmd.y = y; g_Cmd.z = z; g_Cmd.pTarget = pTarget; g_Cmd.type = type;
    int timeout = 0;
    while (g_Cmd.type != 0 && timeout < 50) { 
        Sleep(10);
        timeout++;
    }
    if (g_Cmd.type != 0) g_Cmd.type = 0; 
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin IAT Ghost v31.0 (Bypass Warden) ---\n");
    if (InstallIATHook()) {
        printf("[+] Таблица импорта перехвачена. Код скрыт от Warden.\n");
    } else {
        printf("[-] Ошибка перехвата IAT. Перезапусти клиент!\n");
        return 0;
    }
    printf("[!] ГАЛОЧКИ 'Перемещение по щелчку' И 'Автосбор' ДОЛЖНЫ БЫТЬ ВКЛЮЧЕНЫ!\n");
    printf("[!] БИНДЫ НЕ НУЖНЫ. КНОПКИ НЕ НАЖИМАЮТСЯ. РАБОТАЕТ В СВЕРНУТОМ ВИДЕ.\n");
    printf("--------------------------------------------------\n");

    uintptr_t connectionAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    BotState state = STATE_SEARCH;
    uint64_t activeTargetGuid = 0;
    DWORD stateStartTime = GetTickCount();
    DWORD lastActionTime = 0;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConn = SafeRead<uintptr_t>(connectionAddr);
        SetConsoleCursor(0, 7);

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

                DWORD elapsed = GetTickCount() - stateStartTime;

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
                    
                    if (activeTargetGuid != 0) { 
                        state = STATE_MOVE; 
                        stateStartTime = GetTickCount(); 
                    }
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
                                
                                SendCommandAndWait(1, activeTargetGuid, tx, ty, tz, 0);
                            } else {
                                if (state == STATE_COMBAT && elapsed > 25000) {
                                    printf("[!] БОЙ ЗАБАГАЛСЯ! Сбрасываю...               \n");
                                    activeTargetGuid = 0; state = STATE_SEARCH; stateStartTime = GetTickCount();
                                    continue;
                                }
                                if (state != STATE_COMBAT) { state = STATE_COMBAT; stateStartTime = GetTickCount(); }

                                if (GetTickCount() - lastActionTime > 2000) {
                                    SendCommandAndWait(2, activeTargetGuid, 0, 0, 0, tObj);
                                    lastActionTime = GetTickCount();
                                }
                            }
                        } else {
                            if (state != STATE_LOOT) { state = STATE_LOOT; stateStartTime = GetTickCount(); }
                            
                            printf("[STATE] ЖДЕМ ГЕНЕРАЦИЮ ТРУПА СЕРВЕРОМ...         \n");
                            Sleep(1200); 
                            
                            printf("[STATE] NATIVE ВЗАИМОДЕЙСТВИЕ (ЛУТ)...           \n");
                            SendCommandAndWait(2, activeTargetGuid, 0, 0, 0, tObj);
                            
                            Sleep(2000); 
                            
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
                printf("[FSM] %-15s | IAT GHOST: ACTIVE        \n", stateNames[state]);
            }
        }
        Sleep(100);
    }

    RemoveIATHook();
    fclose(f); FreeConsole(); FreeLibraryAndExitThread((HMODULE)lpParam, 0); return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,MainThread,h,0,0); }
    return TRUE;
}
