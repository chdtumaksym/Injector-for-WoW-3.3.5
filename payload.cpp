#include <windows.h>
#include <iostream>

void Setup() {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    printf("--- ИНЖЕКТ УСПЕШЕН. ХУКОВ НЕТ. ТЫ ВИДИШЬ ЭТО? ---\n");
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        // Мы даже поток не создаем, просто проверим, выживет ли игра после инъекции
        Setup();
    }
    return TRUE;
}
