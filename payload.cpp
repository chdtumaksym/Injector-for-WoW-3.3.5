#include <windows.h>

// Полностью отключаем все проверки компилятора, чтобы Manual Map не крашился
#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

void Setup() {
    // MessageBoxA: 1. Хэндл окна, 2. Текст, 3. Заголовок, 4. Флаги
    // Используем MB_TOPMOST, чтобы окно вылезло ПОВЕРХ игры.
    MessageBoxA(NULL, "DLL заинжекчена успешно! Manual Map работает.", "Debug Success", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
}

// Кастомный EntryPoint без CRT, чтобы избежать проблем с куками безопасности (/GS)
extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        // Создаем поток, чтобы не блокировать загрузчик игры
        HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Setup, NULL, 0, NULL);
        if (hThread) {
            CloseHandle(hThread);
        }
    }
    return TRUE;
}
