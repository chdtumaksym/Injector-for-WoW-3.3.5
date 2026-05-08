#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <clocale>
#include <stdint.h>
#include <tlhelp32.h>

#pragma comment(lib, "user32.lib")

#define OFFSET_S_CUR_MGR         0x00879CE0
#define OFFSET_OBJECT_MANAGER    0x2ED0
#define ADDR_TARGET_GUID         0x00BD07B0 
#define ADDR_CLICK_TO_MOVE       0x00611130
#define ADDR_GET_PLAYER          0x004038BE
#define ADDR_PEEK_MESSAGE        0x008A20E0 // Примерный адрес функции, которая часто вызывается в Main Thread (или используем импорт)

typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, int clickType, uint64_t* interactGuid, float* pos, float precision);
typedef uintptr_t(__cdecl* tGetActivePlayer)();

tClickToMove EngineClickToMove = (tClickToMove)ADDR_CLICK_TO_MOVE;
tGetActivePlayer GetPlayer = (tGetActivePlayer)ADDR_GET_PLAYER;

struct BotCommand {
    volatile int actionType; // 4: Бег, 11: Атака, 6: Лут
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

// Глобальные переменные для HWBP
PVOID g_VehHandle = nullptr;
DWORD g_MainThreadId = 0;
uintptr_t g_HookAddress = 0;

// Обработчик исключений (VEH)
LONG WINAPI ExceptionHandler(EXCEPTION_POINTERS* ExceptionInfo) {
    // Проверяем, что это исключение от нашей аппаратной точки останова
    if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
        if ((uintptr_t)ExceptionInfo->ExceptionRecord->ExceptionAddress == g_HookAddress) {
            
            int currentAction = g_Cmd.actionType;
            if (currentAction != 0) {
                uintptr_t pLocal = GetPlayer();
                if (pLocal && SafeRead<int>(pLocal + 0x14) == 4) {
                    __try {
                        float pos[3] = { g_Cmd.x, g_Cmd.y, g_Cmd.z };
                        EngineClickToMove(pLocal, currentAction, (uint64_t*)&g_Cmd.guid, pos, 0.5f);
                    } __except(1) {}
                }
                g_Cmd.actionType = 0;
            }

            // Возобновляем выполнение, сбрасывая флаг Resume
            ExceptionInfo->ContextRecord->EFlags |= (1 << 16); 
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

DWORD GetMainThreadId(DWORD processId) {
    HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hThreadSnap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te32;
    te32.dwSize = sizeof(THREADENTRY32);
    if (!Thread32First(hThreadSnap, &te32)) { CloseHandle(hThreadSnap); return 0; }
    DWORD mainThreadId = 0;
    ULONGLONG minCreateTime = 0xFFFFFFFFFFFFFFFF;
    do {
        if (te32.th32OwnerProcessID == processId) {
            HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te32.th32ThreadID);
            if (hThread) {
                FILETIME creationTime, exitTime, kernelTime, userTime;
                if (GetThreadTimes(hThread, &creationTime, &exitTime, &kernelTime, &userTime)) {
                    ULONGLONG currentTime = ((ULONGLONG)creationTime.dwHighDateTime << 32) | creationTime.dwLowDateTime;
                    if (currentTime < minCreateTime) {
                        minCreateTime = currentTime;
                        mainThreadId = te32.th32ThreadID;
                    }
                }
                CloseHandle(hThread);
            }
        }
    } while (Thread32Next(hThreadSnap, &te32));
    CloseHandle(hThreadSnap);
    return mainThreadId;
}

bool InstallHWBP() {
    HMODULE hUser32 = GetModuleHandleA("USER32.dll");
    if (!hUser32) return false;
    g_HookAddress = (uintptr_t)GetProcAddress(hUser32, "PeekMessageA");
    if (!g_HookAddress) return false;

    g_VehHandle = AddVectoredExceptionHandler(1, ExceptionHandler);
    if (!g_VehHandle) return false;

    g_MainThreadId = GetMainThreadId(GetCurrentProcessId());
    if (!g_MainThreadId) return false;

    HANDLE hThread = OpenThread(THREAD_SET_CONTEXT | THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, g_MainThreadId);
    if (!hThread) return false;

    SuspendThread(hThread);
    CONTEXT ctx = { 0 };
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    GetThreadContext(hThread, &ctx);
    
    // Записываем адрес в DR0 и активируем его на выполнение (DR7)
    ctx.Dr0 = g_HookAddress;
    ctx.Dr7 |= 1; // Локальное включение DR0
    ctx.Dr7 &= ~(0xF0000); // Очистка условий для DR0 (Выполнение)
    
    SetThreadContext(hThread, &ctx);
    ResumeThread(hThread);
    CloseHandle(hThread);
    return true;
}

void SendCTMCommand(int actionType, uint64_t guid, float x, float y, float z) {
    g_Cmd.guid = guid; g_Cmd.x = x; g_Cmd.y = y; g_Cmd.z = z; g_Cmd.actionType = actionType;
    for (int i = 0; i < 50 && g_Cmd.actionType != 0; i++) Sleep(5);
}

DWORD WINAPI BotLogicThread(LPVOID lpParam) {
    setlocale(LC_ALL, "Russian");
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Paladin HWBP Stealth Internal v44.0 ---\n");
    if (InstallHWBP()) printf("[+] Аппаратный хук (HWBP) установлен на DR0. Память чиста.\n");
    else { printf("[-] ОШИБКА: Не удалось установить дебаг-регистры.\n"); return 0; }
    printf("--------------------------------------------------\n");

    uintptr_t connAddr = (uintptr_t)GetModuleHandleA(NULL) + OFFSET_S_CUR_MGR;
    int state = 0; 
    uint64_t activeGuid = 0;
    DWORD lastActionTime = 0;

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t conn = SafeRead<uintptr_t>(connAddr);
        SetConsoleCursor(0, 4);
        if (!conn) { printf("[!] Ожидание подключения к миру... \n"); Sleep(500); continue; }

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
                int hp = SafeRead<int>(pDesc + 0x60), maxHp = SafeRead<int>(pDesc + 0x80);
                float myX = SafeRead<float>(pLocal + 0x798), myY = SafeRead<float>(pLocal + 0x79C);

                printf("[ИГРОК] HP: %d/%d | Координаты: %.1f, %.1f      \n", hp, maxHp, myX, myY);

                if (state == 0) {
                    float bestDist = 45.0f; activeGuid = 0;
                    cur = SafeRead<uintptr_t>(mgr + 0xAC);
                    while (cur && (cur & 1) == 0) {
                        if (SafeRead<int>(cur + 0x14) == 3) {
                            uintptr_t d = SafeRead<uintptr_t>(cur + 0x8);
                            if (SafeRead<int>(d + 0x60) > 0 && SafeRead<uint64_t>(d + 0x38) == 0) {
                                float dx = SafeRead<float>(cur + 0x798) - myX, dy = SafeRead<float>(cur + 0x79C) - myY;
                                float dist = sqrt(dx*dx + dy*dy);
                                if (dist < bestDist) { bestDist = dist; activeGuid = SafeRead<uint64_t>(cur + 0x30); }
                            }
                        }
                        cur = SafeRead<uintptr_t>(cur + 0x3C);
                    }
                    if (activeGuid) state = 1;
                }

                if (activeGuid) {
                    SafeWrite<uint64_t>(pDesc + 0x48, activeGuid);
                    SafeWrite<uint64_t>(ADDR_TARGET_GUID, activeGuid);

                    uintptr_t tObj = 0; cur = SafeRead<uintptr_t>(mgr + 0xAC);
                    while (cur && (cur & 1) == 0) { if(SafeRead<uint64_t>(cur+0x30)==activeGuid){tObj=cur;break;} cur=SafeRead<uintptr_t>(cur+0x3C); }

                    if (tObj) {
                        int thp = SafeRead<int>(SafeRead<uintptr_t>(tObj + 0x8) + 0x60);
                        float tx = SafeRead<float>(tObj + 0x798), ty = SafeRead<float>(tObj + 0x79C), tz = SafeRead<float>(tObj + 0x7A0);
                        float dist = sqrt(pow(tx-myX, 2) + pow(ty-myY, 2));

                        if (thp > 0) {
                            if (dist > 4.5f) { 
                                state = 1; 
                                SendCTMCommand(4, activeGuid, tx, ty, tz); 
                            } else { 
                                state = 2; 
                                if (GetTickCount() - lastActionTime > 1500) {
                                    SendCTMCommand(11, activeGuid, tx, ty, tz); 
                                    lastActionTime = GetTickCount();
                                }
                            }
                        } else {
                            state = 3; 
                            printf("[ЛОГ] Отправка CTM команды на лут из VEH... \n");
                            Sleep(1000);
                            SendCTMCommand(6, activeGuid, tx, ty, tz); 
                            Sleep(2000); 
                            activeGuid = 0; state = 0;
                        }
                    } else { activeGuid = 0; state = 0; }
                }
                const char* sNames[] = { "ПОИСК", "ПРОГРАММНЫЙ БЕГ", "ПРОГРАММНАЯ АТАКА", "ПРОГРАММНЫЙ ЛУТ" };
                printf("[СТАТУС] Текущее действие: %-15s \n", sNames[state]);
            }
        }
        Sleep(100);
    }

    if (g_MainThreadId) {
        HANDLE hThread = OpenThread(THREAD_SET_CONTEXT | THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, g_MainThreadId);
        if (hThread) {
            SuspendThread(hThread);
            CONTEXT ctx = { 0 }; ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            GetThreadContext(hThread, &ctx);
            ctx.Dr0 = 0; ctx.Dr7 &= ~1;
            SetThreadContext(hThread, &ctx);
            ResumeThread(hThread); CloseHandle(hThread);
        }
    }
    if (g_VehHandle) RemoveVectoredExceptionHandler(g_VehHandle);
    fclose(f); FreeConsole(); FreeLibraryAndExitThread((HMODULE)lpParam, 0); return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0,0,BotLogicThread,h,0,0); }
    return TRUE;
}
