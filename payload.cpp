#include <windows.h>
#include <iostream>

#pragma comment(lib, "user32.lib")

// Жесткие оффсеты для 12340
#define ADDR_OBJMGR 0x00B41414
#define ADDR_PLAYER 0x00BD07E0

DWORD WINAPI MainThread(LPVOID lpParam) {
    // Принудительно открываем консоль
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- PROJECT IMBA: READY ---\n");

    while (true) {
        // Прямое чтение без лишних функций безопасности
        DWORD* objMgrPtr = (DWORD*)ADDR_OBJMGR;
        
        if (objMgrPtr) {
            DWORD objMgr = *objMgrPtr;
            if (objMgr != 0) {
                DWORD firstObj = *(DWORD*)(objMgr + 0xAC);
                printf("[+] ObjectManager: 0x%X | FirstObj: 0x%X\n", objMgr, firstObj);
            } else {
                printf("[-] ObjectManager is 0. Move your character!\n");
            }
        }

        // Чекаем GUID игрока — если тут 0, значит мы вообще не в той памяти
        DWORD playerGuid = *(DWORD*)ADDR_PLAYER;
        printf("[*] Player Pointer: 0x%X\n", playerGuid);

        Sleep(1000);
        system("cls");
        printf("--- PROJECT IMBA: SCANNING ---\n");
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        // Отключаем DEP для нашего потока (иногда помогает на Win10/11)
        SetProcessDEPPolicy(0); 
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
