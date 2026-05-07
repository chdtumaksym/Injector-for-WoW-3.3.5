#include <windows.h>
#include <iostream>
#include <vector>

#pragma comment(lib, "user32.lib")

// Оффсеты 3.3.5а
#define PLAYER_BASE_OFFSET 0x00BD07E0
#define OBJECT_MANAGER_PTR 0x00B41414
#define FIRST_OBJECT_OFFSET 0xAC
#define NEXT_OBJECT_OFFSET 0x3C

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Imba Bot: Object Scanner Active ---\n");

    while (true) {
        // 1. Читаем координаты игрока
        DWORD playerBase = *(DWORD*)PLAYER_BASE_OFFSET;
        if (playerBase) {
            float x = *(float*)(playerBase + 0x798);
            float y = *(float*)(playerBase + 0x79C);
            printf("Player Pos: X: %.2f Y: %.2f\n", x, y);

            // 2. Сканируем объекты вокруг (Мобы, NPC)
            DWORD objMgr = *(DWORD*)OBJECT_MANAGER_PTR;
            if (objMgr) {
                DWORD currentObj = *(DWORD*)(objMgr + FIRST_OBJECT_OFFSET);
                int count = 0;
                
                while (currentObj != 0 && (currentObj & 1) == 0) {
                    count++;
                    // В следующих шагах мы будем вытягивать имена и HP этих объектов
                    currentObj = *(DWORD*)(currentObj + NEXT_OBJECT_OFFSET);
                    if(count > 100) break; // Защита от бесконечного цикла
                }
                printf("Objects found nearby: %d\n", count);
            }
        }

        printf("------------------------------\n");
        Sleep(2000); // Обновляем раз в 2 секунды
        system("cls"); // Чистим консоль
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
