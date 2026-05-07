#include <windows.h>

// Подключаем библиотеку для работы с окнами
#pragma comment(lib, "user32.lib")

DWORD WINAPI MainThread(LPVOID lpParam) {
    // Сюда мы позже впишем ObjectManager и логику автобота
    MessageBoxA(NULL, "Imba Bot запущен внутри WoW!", "Status", MB_OK | MB_ICONINFORMATION);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        // Создаем поток, чтобы не вешать игру своей логикой
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
