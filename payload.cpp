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
#define ADDR_CLICK_TO_MOVE      0x00727400 // Настоящий адрес высокоуровневой функции
#define ADDR_TARGET_GUID        0x00BD07B8 // UI-Таргет
#define ADDR_MOUSEOVER_GUID     0x00BD07A0 // UI-Маусовер (для легального таргетинга)
#define ADDR_LUA_EXECUTE        0x00819210 // Встроенный интерпретатор макросов

// Боевые типы Click-To-Move
#define CTM_LOOT                6 
#define CTM_ATTACK              11 

#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

// Сигнатуры нативных функций
typedef void(__fastcall* tClickToMove)(uintptr_t ecx, uintptr_t edx, int type, uint64_t* guid, float* pos, float prec);
typedef void(__cdecl* tLuaExecute)(const char* code, const char* fileName, int state);

// ==========================================
// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ---
// ==========================================
bool g_Active = false;
WNDPROC oWndProc = nullptr;
std::vector<uint64_t> g_Blacklist; 
uint64_t g_BotTarget = 0; 

// ==========================================
// --- УТИЛИТЫ И БЕЗОПАСНЫЕ ОБЕРТКИ ---
// ==========================================

float GetDistance3D(float x1, float y1, float z1, float x2, float y2, float z2) {
    return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2) + pow(z2 - z1, 2));
}

// 100% безопасный программный вызов макросов
void ExecuteLua(const char* command) {
    ((tLuaExecute)ADDR_LUA_EXECUTE)(command, "bot_core", 0);
}

// Программный таргетинг. Заставляет сервер понять, кого мы выделили.
void ProgrammaticTarget(uint64_t guid) {
    if (*(uint64_t*)ADDR_TARGET_GUID != guid) {
        *(uint64_t*)ADDR_MOUSEOVER_GUID = guid;
        ExecuteLua("TargetUnit('mouseover')");
    }
}

// Умный приказ на движение (Anti-Dance Logic)
void ActionCTM(uintptr_t pLocal, int type, uint64_t guid, float x, float y, float z) {
    static uint64_t lastGuid = 0;
    static int lastType = 0;
    static float lastX = 0, lastY = 0, lastZ = 0;

    float diff = sqrt(pow(x - lastX, 2) + pow(y - lastY, 2) + pow(z - lastZ, 2));

    // Обновляем приказ ТОЛЬКО если сменилась цель, действие, или моб отбежал
    if (guid != lastGuid || type != lastType || diff > 1.5f) {
        uint64_t ctmGuid = guid;
        float ctmPos[3] = { x, y, z };
        
        ((tClickToMove)ADDR_CLICK_TO_MOVE)(pLocal, 0, type, &ctmGuid, ctmPos, 0.5f);
        
        lastGuid = guid;
        lastType = type;
        lastX = x; lastY = y; lastZ = z;
    }
}

// ==========================================
// --- ЯДРО БОТА (ВЫЗЫВАЕТСЯ КАЖДЫЙ ТИК) ---
// ==========================================
void BotPulse() {
    uintptr_t conn = *(uintptr_t*)ADDR_S_CUR_MGR;
    uintptr_t mgr = conn ? *(uintptr_t*)(conn + OFFSET_OBJECT_MANAGER) : 0;
    if (!mgr) return;

    uint64_t localGuid = *(uint64_t*)(mgr + 0xC0);
    uintptr_t pLocal = 0;
    float myX = 0, myY = 0, myZ = 0;

    // 1. Ищем координаты своего персонажа
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

    bool hasTarget = false;
    int targetHp = 0;
    uint32_t targetFlags = 0;
    float tX = 0, tY = 0, tZ = 0;

    // 2. Читаем состояние текущей цели
    if (g_BotTarget != 0) {
        cur = *(uintptr_t*)(mgr + 0xAC);
        while (cur && (cur & 1) == 0) {
            if (*(uint64_t*)(cur + 0x30) == g_BotTarget) {
                int objType = *(int*)(cur + 0x14);
                if (objType == 3 || objType == 4) { 
                    uintptr_t desc = *(uintptr_t*)(cur + 0x8);
                    if (desc) {
                        targetHp = *(int*)(desc + 0x60); 
                        targetFlags = *(uint32_t*)(desc + 0x114);
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

    // 3. Выполнение действий (Бой / Лут)
    if (hasTarget) {
        ProgrammaticTarget(g_BotTarget);
        float dist = GetDistance3D(myX, myY, myZ, tX, tY, tZ);

        if (targetHp > 0) {
            // Бежим к врагу
            ActionCTM(pLocal, CTM_ATTACK, g_BotTarget, tX, tY, tZ);
            printf("Chasing Target... Dist: %.1f      \r", dist);
            
            // Если в мили-зоне - принудительно стартуем автоатаку через сервер
            if (dist < 5.0f) {
                static DWORD lastAtk = 0;
                if (GetTickCount() - lastAtk > 1500) {
                    ExecuteLua("StartAttack()");
                    lastAtk = GetTickCount();
                }
            }
        } 
        else if (targetHp <= 0 && (targetFlags & 1)) {
            // Бежим лутать
            ActionCTM(pLocal, CTM_LOOT, g_BotTarget, tX, tY, tZ);
            printf("Looting Corpse... Dist: %.1f        \r", dist);
            
            // Если стоим на трупе - собираем вещи
            if (dist < 5.0f) {
                static DWORD lastLoot = 0;
                if (GetTickCount() - lastLoot > 1500) {
                    ExecuteLua("InteractUnit('target')");
                    lastLoot = GetTickCount();
                }
            }
        } 
        else {
            // Труп пустой - сбрасываем цель
            g_Blacklist.push_back(g_BotTarget);
            g_BotTarget = 0; 
            ExecuteLua("ClearTarget()"); 
            printf("Target Empty. Blacklisted.          \n");
        }
    } 
    // 4. Поиск новой цели
    else {
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
                        
                        // Игнорируем мертвых или багнутых мобов (MaxHP 0/1)
                        if (hp > 0 && maxHp > 1) {
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
            printf("\nFound new target! Dist: %.1f        \n", bestDist);
        } else {
            printf("Scanning for enemies...             \r");
        }
    }
}

// ==========================================
// --- ОБРАБОТЧИК ОКОННЫХ СООБЩЕНИЙ ---
// ==========================================
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_TIMER && wParam == 1337) {
        
        // Обработка кнопки INSERT (ВКЛ/ВЫКЛ)
        static bool isPressed = false;
        if (GetAsyncKeyState(VK_INSERT) & 0x8000) {
            if (!isPressed) {
                isPressed = true;
                g_Active = !g_Active;
                Beep(g_Active ? 800 : 400, 100);
                printf("\n[!] BOT STATUS: %s                                \n", g_Active ? "ACTIVE" : "PAUSED");
                
                // При выключении сбрасываем состояние
                if (!g_Active) { 
                    g_BotTarget = 0; 
                    ExecuteLua("ClearTarget()");
                    g_Blacklist.clear(); 
                }
            }
        } else {
            isPressed = false;
        }

        // Вызов логики (Тикает раз в 400мс, чтобы не вешать клиент)
        static DWORD lastTick = 0;
        static bool isPulsing = false; 

        if (g_Active && !isPulsing && (GetTickCount() - lastTick > 400)) {
            isPulsing = true;
            __try {
                BotPulse();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                // Блокируем любые ошибки доступа к памяти, защищая процесс от падения
            }
            lastTick = GetTickCount();
            isPulsing = false;
        }
    }
    
    // Передаем управление оригинальной игре
    return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
}

// ==========================================
// --- ТОЧКА ИНЖЕКТА ---
// ==========================================
DWORD WINAPI Setup(LPVOID) {
    AllocConsole(); 
    freopen("CONOUT$", "w", stdout);
    printf("--- Bot v129: Flawless Logic ---\n");

    HWND hwnd = FindWindowA(NULL, "World of Warcraft");
    if (!hwnd) {
        printf("[-] ERROR: WoW window not found!\n");
        return 0;
    }

    // Хукаем главное окно игры для безопасной синхронизации потоков
    oWndProc = (WNDPROC)SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)HookedWndProc);
    SetTimer(hwnd, 1337, 50, NULL); 

    printf("[+] Anti-Dance Logic: INTEGRATED.\n");
    printf("[+] Safe WndProc Main Thread: INTEGRATED.\n");
    printf("[+] Native Lua Server Sync: INTEGRATED.\n");
    printf("[!] Focus WoW window and press [INSERT] to start.\n");
    return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(0, 0, Setup, 0, 0, 0);
    }
    return TRUE;
}
