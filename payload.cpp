#include <windows.h>
#include <iostream>

#pragma comment(lib, "user32.lib")

// Оффсеты 3.3.5а (12340)
#define PLAYER_BASE_OFFSET 0x00BD07E0
#define OBJECT_MANAGER_PTR 0x00B41414
#define CTM_PUSH 0x00860A90 // Функция клика

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Debug Mode Started ---\n");

    while (true) {
        // Проверка №1: Видим ли мы Object Manager?
        DWORD* objMgrPtr = (DWORD*)OBJECT_MANAGER_PTR;
        if (IsBadReadPtr(objMgrPtr, sizeof(DWORD))) {
            printf("[!] Error: Cannot read ObjectManager at 0x%X\n", OBJECT_MANAGER_PTR);
        } else {
            DWORD objMgr = *objMgrPtr;
            if (!objMgr) {
                printf("[?] ObjectManager is NULL. Are you in world?\n");
            } else {
                printf("[+] ObjectManager Found at: 0x%X\n", objMgr);
                // Если нашелся, считаем объекты
                DWORD currentObj = *(DWORD*)(objMgr + 0xAC);
                int count = 0;
                while (currentObj != 0 && (currentObj & 1) == 0) {
                    count++;
                    currentObj = *(DWORD*)(currentObj + 0x3C);
                    if(count > 1000) break;
                }
                printf("[+] Nearby Objects: %d\n", count);
            }
        }

        // Проверка №2: Кнопка F1
        if (GetAsyncKeyState(VK_F1) & 0x8000) {
            printf("[!] F1 Pressed! Testing Click-to-Move...\n");
            // Пробуем просто заставить персонажа "стопнуть" через CTM
            DWORD ctm_base = 0x00BD07A0;
            *(int*)(ctm_base + 0x1C) = 2; // Action: Stop
        }

        Sleep(1000);
        system("cls");
        printf("--- Debug Mode Active ---\n");
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
