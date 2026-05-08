#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <clocale>
#include <stdint.h>

#pragma comment(lib, "user32.lib")

// --- СТАТИЧНЫЕ ОФФСЕТЫ 3.3.5a ---
#define OFFSET_S_CUR_MGR         0x00879CE0
#define OFFSET_OBJECT_MANAGER    0x2ED0
#define ADDR_TARGET_GUID         0x00BD07B0 

#define ADDR_CLICK_TO_MOVE       0x00611130
#define ADDR_GET_PLAYER          0x004038BE
#define ADDR_LUA_EXECUTE         0x00819210 // FrameScript_Execute

enum BotState { STATE_SEARCH, STATE_MOVE, STATE_COMBAT, STATE_LOOT };
const char* stateNames[] = { "SEARCHING", "MOVING", "COMBAT", "LOOTING" };

// Сигнатуры функций
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetActivePlayer)();
// FrameScript_Execute(const char* script, const char* name, void* state)
typedef void(__cdecl* tFrameScript_Execute)(const char* script, const char* name, void* state);

tClickToMove EngineClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;
tGetActivePlayer GetPlayer = (tGetActivePlayer)ADDR_GET_PLAYER;
tFrameScript_Execute LuaExecute = (tFrameScript_Execute)ADDR_LUA_EXECUTE;

struct BotCommand {
    volatile int type; // 1: Move, 2: Lua Interact
    volatile uint64_t guid;
    volatile float x, y, z;
} g_Cmd;

template <typename T>
T SafeRead(uintptr_t address) {
    T buffer = T();
    if (!address) return buffer;
    __try { buffer = *(T*)address; } __except(1) {}
    return buffer;
}

template <typename T>
void SafeWrite(uintptr_t address, T value) {
    if (!address) return;
    __try { *(T*)address = value; } __except(1) {}
}

void SetConsoleCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

// --- IAT HOOK (Безопасный контекст Main Thread) ---
typedef BOOL(WINAPI* tPeekMessageA)(LPMSG, HWND, UINT, UINT, UINT);
tPeekMessageA oPeekMessageA = nullptr;

BOOL WINAPI HookedPeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    if (g_Cmd.type != 0) {
        uintptr_t pLocal = GetPlayer();
        if (pLocal && SafeRead<int>(pLocal + 0x14) == 4) {
            __try {
                if (g_Cmd.type == 1) { 
                    float pos[3] = { g_Cmd.x, g_Cmd.y, g_Cmd.z };
                    EngineClickToMove(pLocal, 4, (uint64_t*)&g_Cmd.guid, pos, 0.5f);
                } 
                else if (g_Cmd.type == 2) { 
                    // ВМЕСТО ВЫЗОВА C++ ФУНКЦИЙ, ГОВОРИМ ИГРЕ ВЫПОЛНИТЬ LUA
                    // Это автоматически обработает таргет, дистанцию и лут
                    LuaExecute("InteractUnit('target')", "InjectedScript", NULL);
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        g_Cmd.type = 0; 
    }
    return oPeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
}

bool InstallIATHook() {
    HMODULE hExe = GetModuleHandleA(NULL);
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)hExe;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((uintptr_t)hExe + dos->e_lfanew);
    PIMAGE_IMPORT_DESCRIPTOR imp = (PIMAGE_IMPORT_DESCRIPTOR)((uintptr_t)hExe + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

    while (imp->Name) {
        if (_stricmp((char*)((uintptr_t)hExe + imp->Name), "USER32.dll") == 0) {
            PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)((uintptr_t)hExe + imp->FirstThunk);
            PIMAGE_THUNK_DATA oThunk = (PIMAGE_THUNK_DATA)((uintptr_t)hExe + imp->OriginalFirstThunk);
            while (oThunk->u1.AddressOfData) {
                if (!(oThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                    PIMAGE_IMPORT_BY_NAME name = (PIMAGE_IMPORT_BY_NAME)((uintptr_t)hExe + oThunk->u1.AddressOfData);
                    if (strcmp((char*)name->Name, "PeekMessageA") == 0) {
                        DWORD old;
                        VirtualProtect(&thunk->u1.Function, 4, PAGE_EXECUTE_READWRITE, &old);
                        oPeekMessageA = (tPeekMessageA)thunk->u1.Function;
                        thunk->u1.Function = (uintptr_t)HookedPeekMessageA;
                        VirtualProtect(&thunk->u1.Function, 4, old, &old);
                        return true;
                    }
                }
                thunk++; oThunk++;
            }
        }
        imp++;
    }
    return false;
}

void SendCommand(int type, uint64_t guid = 0, float x = 0, float y = 0, float z = 0) {
    g_Cmd.guid = guid; g_Cmd.x = x; g_Cmd.y = y; g_Cmd.z = z; g_Cmd.type = type;
    for (int i = 0; i < 50 && g_Cmd.type != 0; i++) Sleep(5);
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    setlocale(LC_ALL, "Russian");
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin Lua Injector v37.0 ---\n");
    if (InstallIATHook()) printf("[+] IAT Hook: OK\n");
    else { printf("[-] IAT Error\n"); return 0; }

    uintptr_t connAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    BotState state = STATE_SEARCH;
    uint64_t activeGuid = 0;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t conn = SafeRead<uintptr_t>(connAddr);
        SetConsoleCursor(0, 6);
        if (!conn) { printf("[!] Ждем мир...\n"); Sleep(500); continue; }

        uintptr_t mgr = SafeRead<uintptr_t>(conn + OFFSET_OBJECT_MANAGER);
        if (mgr) {
            uintptr_t cur = SafeRead<uintptr_t>(mgr + 0xAC);
            uint64_t myGuid = SafeRead<uint64_t>(mgr + 0xC0);
            uintptr_t pLocal = 0;
            while (cur && (cur & 1) == 0) {
                if (SafeRead<uint64_t>(cur + 0x30) == myGuid) { pLocal = cur; break; }
                cur = SafeRead<uintptr_t>(cur + 0x3C);
            }

            if (pLocal) {
                uintptr_t pDesc = SafeRead<uintptr_t>(pLocal + 0x8);
                float myX = SafeRead<float>(pLocal + 0x798), myY = SafeRead<float>(pLocal + 0x79C);
                
                if (state == STATE_SEARCH) {
                    float best = 45.0f; activeGuid = 0;
                    cur = SafeRead<uintptr_t>(mgr + 0xAC);
                    while (cur && (cur & 1) == 0) {
                        if (SafeRead<int>(cur + 0x14) == 3) {
                            float dist = sqrt(pow(SafeRead<float>(cur + 0x798)-myX,2)+pow(SafeRead<float>(cur + 0x79C)-myY,2));
                            if (dist < best && SafeRead<int>(SafeRead<uintptr_t>(cur+0x8)+0x60) > 0) {
                                best = dist; activeGuid = SafeRead<uint64_t>(cur + 0x30);
                            }
                        }
                        cur = SafeRead<uintptr_t>(cur + 0x3C);
                    }
                    if (activeGuid) state = STATE_MOVE;
                }

                if (activeGuid) {
                    SafeWrite<uint64_t>(pDesc + 0x48, activeGuid);
                    SafeWrite<uint64_t>(ADDR_TARGET_GUID, activeGuid);

                    uintptr_t tObj = 0; cur = SafeRead<uintptr_t>(mgr + 0xAC);
                    while (cur && (cur & 1) == 0) { if(SafeRead<uint64_t>(cur+0x30)==activeGuid){tObj=cur;break;} cur=SafeRead<uintptr_t>(cur+0x3C); }

                    if (tObj) {
                        float tx=SafeRead<float>(tObj+0x798), ty=SafeRead<float>(tObj+0x79C), tz=SafeRead<float>(tObj+0x7A0);
                        float dist = sqrt(pow(tx-myX,2)+pow(ty-myY,2));
                        if (SafeRead<int>(SafeRead<uintptr_t>(tObj+0x8)+0x60) > 0) {
                            if (dist > 4.5f) { state = STATE_MOVE; SendCommand(1, activeGuid, tx, ty, tz); }
                            else { state = STATE_COMBAT; SendCommand(2); Sleep(1000); }
                        } else {
                            state = STATE_LOOT; printf("[LOG] Looting... \n");
                            SendCommand(2); Sleep(2000); activeGuid = 0; state = STATE_SEARCH;
                        }
                    } else { activeGuid = 0; state = STATE_SEARCH; }
                }
                printf("[FSM] State: %-15s \n", stateNames[state]);
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
