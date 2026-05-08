#include <windows.h>

// Отключаем проверку стека на уровне кода, чтобы Manual Map не крашился
#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

// Функция-заглушка, чтобы проверить жизнь
void Setup() {
    // MessageBoxA — самая стабильная штука. Если она не вылезет, значит DLL даже не запустилась.
    MessageBoxA(NULL, "Стерильный тест: DLL внутри процесса!", "Бот-Дебаг", MB_OK | MB_ICONTOPMOST);
}

// Кастомный EntryPoint без CRT (C Runtime), чтобы не зависеть от инициализации кук безопасности
extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        // Не используем DisableThreadLibraryCalls, чтобы не трогать лишний раз импорты в момент загрузки
        
        // Создаем поток через нативный WinAPI
        HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Setup, NULL, 0, NULL);
        if (hThread) CloseHandle(hThread);
    }
    return TRUE;
}
