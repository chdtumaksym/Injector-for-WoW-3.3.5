#include <windows.h>

// Максимально отключаем все проверки, чтобы Manual Map не спотыкался
#pragma runtime_checks("", off)
#pragma check_stack(off)

void Setup() {
    // Используем стандартные флаги: MB_OK (окно) и MB_TOPMOST (поверх всех окон)
    // Четвертый аргумент ОБЯЗАТЕЛЕН.
    MessageBoxA(NULL, "Стерильный тест: DLL внутри процесса!", "Бот-Дебаг", MB_OK | MB_TOPMOST | MB_ICONINFORMATION);
}

// Кастомная точка входа, чтобы не тащить за собой CRT и ошибки инициализации кук
extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        // Создаем поток, чтобы не вешать загрузчик
        HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Setup, NULL, 0, NULL);
        if (hThread) {
            CloseHandle(hThread);
        }
    }
    return TRUE;
}
