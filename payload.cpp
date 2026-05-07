#include <windows.h>
#include <iostream>

#define PLAYER_BASE_OFFSET 0x00BD07E0
#define OBJECT_MANAGER_PTR 0x00B41414

// Типы объектов в WoW 3.3.5
enum WowObjType {
    OT_ITEM = 1,
    OT_CONTAINER = 2,
    OT_UNIT = 3,      // Это мобы и NPC
    OT_PLAYER = 4,    // Другие игроки
    OT_GAMEOBJECT = 5 // Руда, трава, сундуки
};

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Professional Object Radar ---\n");

    while (true) {
        DWORD objMgr = *(DWORD*)OBJECT_MANAGER_PTR;
        if (objMgr) {
            // Начало списка объектов
            DWORD currentObj = *(DWORD*)(objMgr + 0xAC); 
            
            int units = 0, players = 0, items = 0;

            while (currentObj != 0 && (currentObj & 1) == 0) {
                // Читаем тип объекта (находится по смещению 0x14)
                int type = *(int*)(currentObj + 0x14);

                if (type == OT_UNIT) units++;
                if (type == OT_PLAYER) players++;
                if (type == OT_GAMEOBJECT) items++;

                // Переход к следующему объекту
                currentObj = *(DWORD*)(currentObj + 0x3C);
            }
            
            printf("Found: [Mobs: %d] [Players: %d] [Objects: %d]\n", units, players, items);
        }
        Sleep(1000);
        system("cls");
        printf("--- Professional Object Radar ---\n");
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
