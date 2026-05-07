#include <windows.h>
#include <iostream>

#pragma comment(lib, "user32.lib")

// Самые стабильные оффсеты для 12340
#define ADDR_OBJMGR 0x00B41414
#define ADDR_PLAYER_GUID 0x00BD07E0

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Professional WoW 3.3.5a Scanner ---\n");

    while (true) {
        // Пробуем прочитать указатель на менеджер объектов
        DWORD* objMgrPtr = (DWORD*)ADDR_OBJMGR;
        
        if (objMgrPtr && !IsBadReadPtr(objMgrPtr, sizeof(DWORD)) && *objMgrPtr != 0) {
            DWORD objMgr = *objMgrPtr;
            
            // Если нашли менеджер, ищем первый объект
            DWORD currentObj = *(DWORD*)(objMgr + 0xAC);
            int count = 0;
            
            while (currentObj != 0 && (currentObj & 1) == 0) {
                count++;
                currentObj = *(DWORD*)(currentObj + 0x3C);
                if (count > 2000) break;
            }
            printf("[SUCCESS] Found %d objects in memory!\n", count);
        } else {
            // Если менеджер пуст, пробуем прочитать хотя бы GUID игрока
            DWORD* playerGuidPtr = (DWORD*)ADDR_PLAYER_GUID;
            if (playerGuidPtr && *playerGuidPtr != 0) {
                printf("[DEBUG] ObjMgr is NULL, but PlayerGUID found: 0x%X\n", *playerGuidPtr);
            } else {
                printf("[WAITING] Memory is locked or world not loaded...\n");
            }
        }

        Sleep(2000);
        system("cls");
        printf("--- Professional WoW 3.3.5a Scanner ---\n");
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
