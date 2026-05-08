#include <windows.h>
#include <cmath>
#include <stdint.h>
#include <iostream>
#include <d3d9.h>

#pragma comment(lib, "d3d9.lib")

// --- АДРЕСА 3.3.5a (12340) ---
#define ADDR_PLAYER_BASE        0x00BD07E0 // Подтвержденный адрес игрока
#define ADDR_S_CUR_MGR          0x00C79CE0 // Connection / CurMgr
#define OFFSET_OBJECT_MANAGER   0x2ED0
#define ADDR_D3D9_DEVICE        0x00C5DF88

#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

typedef HRESULT(__stdcall* tEndScene)(LPDIRECT3DDEVICE9 pDevice);
tEndScene oEndScene = nullptr;

HRESULT __stdcall HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    static bool keyState = false;
    
    if (GetAsyncKeyState(VK_END) & 0x8000) {
        if (!keyState) {
            keyState = true;
            Beep(500, 100);
            printf("\n--- СКАНИРОВАНИЕ ПАМЯТИ ---\n");

            uintptr_t pLocal = *(uintptr_t*)ADDR_PLAYER_BASE;
            if (!pLocal) {
                printf("[-] ОШИБКА: Игрок не найден (pLocal == 0)\n");
                return oEndScene(pDevice);
            }

            uintptr_t conn = *(uintptr_t*)ADDR_S_CUR_MGR;
            if (!conn) {
                printf("[-] ОШИБКА: Соединение не найдено (conn == 0)\n");
                return oEndScene(pDevice);
            }

            uintptr_t mgr = *(uintptr_t*)(conn + OFFSET_OBJECT_MANAGER);
            if (!mgr) {
                printf("[-] ОШИБКА: Object Manager не найден (mgr == 0)\n");
                return oEndScene(pDevice);
            }

            float myX = *(float*)(pLocal + 0x798);
            float myY = *(float*)(pLocal + 0x79C);
            float myZ = *(float*)(pLocal + 0x7A0);
            printf("[+] Твои координаты: X:%.1f, Y:%.1f, Z:%.1f\n", myX, myY, myZ);

            int count = 0;
            uintptr_t cur = *(uintptr_t*)(mgr + 0xAC);
            
            // Проходимся по списку объектов, выводим максимум 15 штук, чтобы не спамить
            while (cur && (cur & 1) == 0 && count < 15) {
                int type = *(int*)(cur + 0x14);
                if (type == 3) { // Тип 3 = Unit (Мобы/НПЦ)
                    uintptr_t desc = *(uintptr_t*)(cur + 0x8);
                    if (desc) {
                        int hp = *(int*)(desc + 0x60);
                        uint64_t guid = *(uint64_t*)(cur + 0x30);
                        float tX = *(float*)(cur + 0x798);
                        float tY = *(float*)(cur + 0x79C);
                        
                        // Вычисляем дистанцию
                        float dist = sqrt(pow(tX - myX, 2) + pow(tY - myY, 2));

                        // Показываем только тех, кто в радиусе 50 метров
                        if (dist < 50.0f) {
                            printf("-> Юнит | GUID: %llu | HP: %d | Дистанция: %.1f\n", guid, hp, dist);
                            count++;
                        }
                    }
                }
                cur = *(uintptr_t*)(cur + 0x3C);
            }
            printf("--- СКАНИРОВАНИЕ ЗАВЕРШЕНО (Найдено %d юнитов) ---\n", count);
        }
    } else {
        keyState = false;
    }

    return oEndScene(pDevice);
}

DWORD WINAPI Setup(LPVOID) {
    AllocConsole(); 
    freopen("CONOUT$", "w", stdout);
    printf("--- Bot v113: Memory Diagnostics Edition ---\n");
    printf("[!] ЗАЙДИ В МИР И НАЖМИ 'END' ДЛЯ ЧТЕНИЯ КОБОЛЬДОВ\n");

    uintptr_t deviceAddr = 0;
    while (!(deviceAddr = *(uintptr_t*)ADDR_D3D9_DEVICE)) Sleep(100);

    uintptr_t* vTable = *(uintptr_t**)deviceAddr;
    if (vTable) {
        DWORD old;
        VirtualProtect(&vTable[42], 4, PAGE_EXECUTE_READWRITE, &old);
        oEndScene = (tEndScene)vTable[42];
        vTable[42] = (uintptr_t)HookedEndScene;
        VirtualProtect(&vTable[42], 4, old, &old);
        printf("[+] Хук графики установлен. Жду нажатия END.\n");
    }
    return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) CreateThread(0, 0, Setup, 0, 0, 0);
    return TRUE;
}
