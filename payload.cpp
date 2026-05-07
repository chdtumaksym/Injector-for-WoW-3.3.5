#include <windows.h>
#include <iostream>

#pragma comment(lib, "user32.lib")

// Оффсеты БЕЗ учета базы (чистые смещения)
#define OFFSET_OBJMGR 0x00B41414
#define OFFSET_PLAYER 0x00BD07E0

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    // 1. Получаем базу модуля (где начинается память WoW в этот раз)
    uintptr_t baseAddr = (uintptr_t)GetModuleHandleA(NULL);
    
    printf("--- PRO SCANNER v2.0 ---\n");
    printf("[*] Module Base: 0x%p\n", (void*)baseAddr);

    while (true) {
        // 2. Считаем реальный адрес: База + Оффсет
        // ВНИМАНИЕ: Если у тебя чистый 12340, база обычно 0x400000. 
        // Но современные ОС могут её менять.
        
        uintptr_t realObjMgrPtr = baseAddr + (OFFSET_OBJMGR - 0x400000);
        uintptr_t realPlayerPtr = baseAddr + (OFFSET_PLAYER - 0x400000);

        DWORD* objMgr = (DWORD*)realObjMgrPtr;
        DWORD* player = (DWORD*)realPlayerPtr;

        if (objMgr && !IsBadReadPtr(objMgr, sizeof(DWORD))) {
            printf("[+] Real ObjMgr Addr: 0x%p | Value: 0x%X\n", (void*)realObjMgrPtr, *objMgr);
        }
        
        if (player && !IsBadReadPtr(player, sizeof(DWORD))) {
            printf("[+] Real Player Addr: 0x%p | Value: 0x%X\n", (void*)realPlayerPtr, *player);
        }

        Sleep(1500);
        system("cls");
        printf("Base: 0x%p | Scanning for life...\n", (void*)baseAddr);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
