#include <windows.h>
#include <iostream>

// Эта DLL не делает НИЧЕГО, кроме открытия консоли.
// Если игра упадет с этой либой — значит твой инжектор детектится или ломает память.
void Setup() {
    // Используем MessageBox вместо консоли для теста — это еще надежнее
    MessageBoxA(NULL, "Инжект прошел успешно! Игра жива.", "Debug", MB_OK | MB_ICONINFORMATION);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        // Создаем поток, чтобы не вешать DllMain
        CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(Setup), nullptr, 0, nullptr);
    }
    return TRUE;
}
