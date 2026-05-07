#include <windows.h>
#include <iostream>

#pragma comment(lib, "user32.lib")

// Самые надежные оффсеты для 3.3.5a (12340)
#define ADDR_PLAYER_BASE 0x00BD07E0
#define ADDR_OBJMGR_PTR 0x00B41414

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Imba Bot: Safe Mode --- \n");

    while (true) {
        // Проверяем, можно ли читать по адресу игрока
        DWORD* playerPtr = (DWORD*)ADDR_PLAYER_BASE;
        
        if (!IsBadReadPtr(playerPtr, sizeof(DWORD)) && *playerPtr != 0) {
            DWORD pBase = *playerPtr;
            
            // Пробуем прочитать координаты (X находится по смещению 0x798)
            float x = *(float*)(pBase + 0x798);
            float y = *(float*)(pBase + 0x79C);
            float z = *(float*)(pBase + 0x7A0);

            printf("I SEE YOU! Pos: X:%.2f Y:%.2f Z:%.2f\r", x, y, z);
        } else {
            printf("Searching for player data...\r");
        }

        // Если нажмешь F1, бот попробует прочитать количество объектов
        if (GetAsyncKeyState(VK_F1) & 0x8000) {
            DWORD* objMgrPtr = (DWORD*)ADDR_OBJMGR_PTR;
            if (!IsBadReadPtr(objMgrPtr, sizeof(DWORD)) && *objMgrPtr != 0) {
                printf("\n[!] ObjectManager found! Scanning objects...\n");
            }
        }

        Sleep(200);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
