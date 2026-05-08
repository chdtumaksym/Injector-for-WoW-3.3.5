#include <windows.h>
#include <iostream>
#include <stdint.h>

template <typename T>
T SafeRead(uintptr_t address) {
    T buffer = T();
    if (!address) return buffer;
    __try { buffer = *(T*)address; } __except(1) {}
    return buffer;
}

template <typename T>
void SafeWrite(uintptr_t address, T value) {
    if (!address) return;
    __try { *(T*)address = value; } __except(1) {}
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    std::cout << "--- Skeleton Payload Loaded ---" << std::endl;

    while (!GetAsyncKeyState(VK_END)) {
        // Пример чтения координат игрока
        uintptr_t playerBase = SafeRead<uintptr_t>(0x00BD07E0); // проверять актуальный оффсет для билда
        if (playerBase) {
            float x = SafeRead<float>(playerBase + 0x798);
            float y = SafeRead<float>(playerBase + 0x79C);
            float z = SafeRead<float>(playerBase + 0x7A0);
            std::cout << "Player pos: X=" << x << " Y=" << y << " Z=" << z << "\r";
        }
        Sleep(500);
    }

    fclose(f);
    FreeConsole();
    FreeLibraryAndExitThread((HMODULE)lpParam, 0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        CreateThread(nullptr, 0, MainThread, hinstDLL, 0, nullptr);
    }
    return TRUE;
}
