#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <clocale>
#include <stdint.h>

#pragma comment(lib, "user32.lib")

// --- СТАТИЧНЫЕ ОФФСЕТЫ 3.3.5a (12340) ---
#define OFFSET_S_CUR_MGR         0x00879CE0
#define OFFSET_OBJECT_MANAGER    0x2ED0
#define ADDR_TARGET_GUID         0x00BD07B0 

#define ADDR_CLICK_TO_MOVE       0x00611130
#define ADDR_GET_PLAYER          0x004038BE
#define ADDR_INTERACT_WITH_GUID  0x005277B0 

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetActivePlayer)();
typedef int(__cdecl* tInteractWithGuid)(uint32_t guidLow, uint32_t guidHigh);

tClickToMove EngineClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;
tGetActivePlayer GetPlayer = (tGetActivePlayer)ADDR_GET_PLAYER;
tInteractWithGuid InteractWithGuid = (tInteractWithGuid)ADDR_INTERACT_WITH_GUID;

struct BotCommand {
    volatile int type; 
    volatile uintptr_t pTarget;
    volatile uint64_t guid;
    volatile float x, y, z;
} g_Cmd;

// --- УТИЛИТЫ ПАМЯТИ ---
template <typename T>
T SafeRead(uintptr_t address) {
    T buffer = T();
    if (address == 0) return buffer;
    __try { buffer = *(T*)address; } 
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return buffer;
}

template <typename T>
void SafeWrite(uintptr_t address, T value) {
    if (address == 0) return;
    __try { *(T*)address = value; } 
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

// --- IAT HOOKING (Внедрение в главный поток) ---
typedef BOOL(WINAPI* tPeekMessageA)(LPMSG, HWND, UINT, UINT, UINT);
tPeekMessageA oPeekMessageA = nullptr;

BOOL WINAPI HookedPeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    int cmd = g_Cmd.type;
    if (cmd != 0) {
        uintptr_t pLocal = GetPlayer();
        if (pLocal && SafeRead<int>(pLocal + 0x14) == 4) {
            __try {
                if (cmd == 1) { 
                    float pos[3] = { g_Cmd.x, g_Cmd.y, g_Cmd.z };
                    EngineClickToMove(pLocal, 4, (uint64_t*)&g_Cmd.guid, pos, 0.5f);
                } 
                else if (cmd == 2) { 
                    uint32_t low = (uint32_t)(g_Cmd.guid & 0xFFFFFFFF);
                    uint32_t high = (uint32_t)(g_Cmd.guid >> 32);
                    InteractWithGuid(low, high);
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        g_Cmd.type = 0; 
    }
    return oPeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
}

bool InstallIATHook() {
    HMODULE hExe = GetModuleHandleA(NULL);
    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hExe;
    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((uintptr_t)hExe + dosHeader->e_lfanew);
    PIMAGE_IMPORT_DESCRIPTOR importDesc = (PIMAGE_IMPORT_DESCRIPTOR)((uintptr_t)hExe + ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

    while (importDesc->Name != 0) {
        if (_stricmp((char*)((uintptr_t)hExe + importDesc->Name), "USER32.dll") == 0) {
            PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)((uintptr_t)hExe + importDesc->FirstThunk);
            PIMAGE_THUNK_DATA origThunk = (PIMAGE_THUNK_DATA)((uintptr_t)hExe + importDesc->OriginalFirstThunk);
            while (origThunk->u1.AddressOfData != 0) {
                if (!(origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                    PIMAGE_IMPORT_BY_NAME importName = (PIMAGE_IMPORT_BY_NAME)((uintptr_t)hExe + origThunk->u1.AddressOfData);
                    if (strcmp((char*)importName->Name, "PeekMessageA") == 0) {
                        DWORD old;
                        VirtualProtect(&thunk->u1.Function, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &old);
                        oPeekMessageA = (tPeekMessageA)thunk->u1.Function;
                        thunk->u1.Function = (uintptr_t)HookedPeekMessageA;
                        VirtualProtect(&thunk->u1.Function, sizeof(uintptr_t), old, &old);
                        return true;
                    }
                }
                thunk++; origThunk++;
            }
        }
        importDesc++;
    }
    return false;
}

void SendCommand(int type, uint64_t guid = 0, float x = 0, float y = 0, float z = 0) {
    g_Cmd.guid = guid; g_Cmd.x = x; g_Cmd.y = y; g_Cmd.z = z; g_Cmd.type = type;
    for (int i = 0; i < 100 && g_Cmd.type != 0; i++) Sleep(5);
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    setlocale(LC_ALL, "Russian");
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin Ghidra-Fixed v36.0 ---\n");
    if (InstallIATHook()) {
        printf("[+] IAT Hook активен. SafeWrite восстановлен.\n");
    } else {
        printf("[-] ОШИБКА: IAT Hook не удался.\n");
        return 0;
    }
    printf("[+] Адрес Interact: 0x%X\n", ADDR_INTERACT_WITH_GUID);
    printf("--------------------------------------------------\n");

    uintptr_t connectionAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    BotState state = STATE_SEARCH;
    uint64_t activeTargetGuid = 0;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConn = SafeRead<uintptr_t>(connectionAddr);
        SetConsoleCursor(0, 6);

        if (!clientConn) {
            printf("[!] Ждем вход в мир...                           \n");
            Sleep(500); continue;
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
                int hp = SafeRead<int>(pDesc + 0x60), maxHp = SafeRead<int>(pDesc + 0x80);
                float myX = SafeRead<float>(pLocal + 0x798), myY = SafeRead<float>(pLocal + 0x79C);

                printf("[PLAYER] HP: %d/%d | POS: %.1f, %.1f      \n", hp, maxHp, myX, myY);

                if (state == STATE_SEARCH) {
                    float bestDist = 45.0f; activeTargetGuid = 0;
                    cur = SafeRead<uintptr_t>(mgr + 0xAC);
                    while (cur != 0 && (cur & 1) == 0) {
                        if (SafeRead<int>(cur + 0x14) == 3) {
                            uintptr_t d = SafeRead<uintptr_t>(cur + 0x8);
                            if (SafeRead<int>(d + 0x60) > 0) {
                                float dx = SafeRead<float>(cur + 0x798) - myX, dy = SafeRead<float>(cur + 0x79C) - myY;
                                float dist = sqrt(dx*dx + dy*dy);
                                if (dist < bestDist) { bestDist = dist; activeTargetGuid = SafeRead<uint64_t>(cur + 0x30); }
                            }
                        }
                        cur = SafeRead<uintptr_t>(cur + 0x3C);
                    }
                    if (activeTargetGuid != 0) state = STATE_MOVE;
                }

                if (activeTargetGuid != 0) {
                    // Используем SafeWrite, который ты потерял
                    SafeWrite<uint64_t>(pDesc + 0x48, activeTargetGuid);
                    SafeWrite<uint64_t>(ADDR_TARGET_GUID, activeTargetGuid);

                    uintptr_t tObj = 0; cur = SafeRead<uintptr_t>(mgr + 0xAC);
                    while (cur != 0 && (cur & 1) == 0) { 
                        if(SafeRead<uint64_t>(cur+0x30) == activeTargetGuid) { tObj = cur; break; } 
                        cur = SafeRead<uintptr_t>(cur+0x3C); 
                    }
                    
                    if (tObj) {
                        float tx = SafeRead<float>(tObj + 0x798), ty = SafeRead<float>(tObj + 0x79C), tz = SafeRead<float>(tObj + 0x7A0);
                        float dist = sqrt(pow(tx-myX, 2) + pow(ty-myY, 2));
                        
                        if (SafeRead<int>(SafeRead<uintptr_t>(tObj+0x8) + 0x60) > 0) {
                            if (dist > 4.5f) {
                                state = STATE_MOVE;
                                SendCommand(1, activeTargetGuid, tx, ty, tz);
                            } else {
                                state = STATE_COMBAT;
                                SendCommand(2, activeTargetGuid);
                                Sleep(500);
                            }
                        } else {
                            state = STATE_LOOT;
                            SendCommand(2, activeTargetGuid);
                            Sleep(2000); activeTargetGuid = 0; state = STATE_SEARCH;
                        }
                    } else { activeTargetGuid = 0; state = STATE_SEARCH; }
                }
                printf("[FSM] ТЕКУЩЕЕ СОСТОЯНИЕ: %-15s \n", stateNames[state]);
            }
        }
        Sleep(100);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) CreateThread(0,0,MainThread,h,0,0);
    return TRUE;
}
