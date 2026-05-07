#include "main.h"

// Наша главная функция внутри WoW
DWORD WINAPI MainThread(LPVOID lpParam) {
    // В будущем тут будет Object Manager и логика
    MessageBoxA(NULL, "Инжект успешен! Warden нас не видит.", "Imba Bot", MB_OK);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
