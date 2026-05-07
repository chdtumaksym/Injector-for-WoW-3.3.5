#include <windows.h>
#include <iostream>

#pragma comment(lib, "user32.lib")

// Оффсеты ОТНОСИТЕЛЬНО начала модуля WoW.exe
DWORD off_ObjectManager = 0x00B41414; 

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    // Получаем реальный адрес, куда загружена игра
    DWORD baseAddress = (DWORD)GetModuleHandleA(NULL);
    printf("--- Imba Bot: Auto-Base Mode ---\n");
    printf("[+] WoW.exe Base Address: 0x%X\n", baseAddress);

    while (true) {
        // Пробуем два варианта: статический и динамический (с учетом базы)
        DWORD objMgrAddr = off_ObjectManager; 
        DWORD* objMgrPtr = (DWORD*)objMgrAddr;

        if (objMgrPtr && *objMgrPtr) {
            DWORD objMgr = *objMgrPtr;
            DWORD currentObj = *(DWORD*)(objMgr + 0xAC);
            int count = 0;
            while (currentObj != 0 && (currentObj & 1) == 0) {
                count++;
                currentObj = *(DWORD*)(currentObj + 0x3C);
                if(count > 1000) break;
            }
            printf("[+] Status: OK! Objects found: %d\n", count);
        } else {
            printf("[!] Waiting for ObjectManager... Check your WoW version.\n");
        }

        if (GetAsyncKeyState(VK_F1) & 0x8000) {
            printf("[!] F1 Action triggered!\n");
        }

        Sleep(2000);
        system("cls");
        printf("Base: 0x%X | Searching for data...\n", baseAddress);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
