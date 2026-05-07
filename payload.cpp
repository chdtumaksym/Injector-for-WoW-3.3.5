#include <windows.h>
#include <iostream>

#pragma comment(lib, "user32.lib")

// Функция поиска сигнатуры с безопасным парсингом PE-заголовка
uintptr_t FindPattern(HMODULE module, const char* pattern, const char* mask) {
    IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)module;
    IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)((uint8_t*)module + dosHeader->e_lfanew);

    // Сканируем только секцию кода, чтобы не словить Access Violation
    uintptr_t start = (uintptr_t)module + ntHeaders->OptionalHeader.BaseOfCode;
    uintptr_t size = ntHeaders->OptionalHeader.SizeOfCode;
    size_t maskLen = strlen(mask); // Длина вычисляется один раз!

    for (uintptr_t i = 0; i < size - maskLen; i++) {
        bool found = true;
        for (size_t j = 0; j < maskLen; j++) {
            if (mask[j] != '?' && pattern[j] != *(char*)(start + i + j)) {
                found = false;
                break;
            }
        }
        if (found) return start + i;
    }
    return 0;
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    HMODULE base = GetModuleHandleA(NULL);
    printf("--- Pattern Scanner Active ---\n");
    printf("[*] Base: 0x%p. Safe scanning .text section...\n", (void*)base);

    const char* pattern = "\x8B\x15\x00\x00\x00\x00\x8B\x42\x2C\x85\xC0";
    const char* mask = "xx????xxxxx";

    uintptr_t match = FindPattern(base, pattern, mask);

    if (match) {
        uintptr_t connectionAddr = *(uintptr_t*)(match + 2);
        printf("[!] FOUND! ClientConnection Pointer: 0x%p\n", (void*)connectionAddr);
        printf("[*] Press END to unload the DLL.\n");

        while (!GetAsyncKeyState(VK_END)) {
            uintptr_t clientConnection = *(uintptr_t*)connectionAddr;
            
            // Если мы в мире и структура инициализирована
            if (clientConnection) {
                // Правильная цепочка: ClientConnection -> +0x2ED0 (ObjectManager) -> +0xAC (First Object)
                uintptr_t objMgr = *(uintptr_t*)(clientConnection + 0x2ED0);
                
                if (objMgr) {
                    uintptr_t cur = *(uintptr_t*)(objMgr + 0xAC);
                    int count = 0;
                    
                    while (cur != 0 && (cur & 1) == 0 && count < 2000) {
                        count++;
                        cur = *(uintptr_t*)(cur + 0x3C); // Смещение на следующий объект
                    }
                    printf("Real-time Objects: %d          \r", count);
                }
            } else {
                printf("Waiting for world...           \r");
            }
            Sleep(100);
        }
    } else {
        printf("[!] ERROR: Pattern not found.\n");
        while (!GetAsyncKeyState(VK_END)) Sleep(100);
    }

    // Адекватное завершение работы
    printf("\n[*] Unloading...\n");
    fclose(f);
    FreeConsole();
    FreeLibraryAndExitThread((HMODULE)lpParam, 0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL); // Оптимизация, чтобы не вызывать DllMain на каждый новый поток
        HANDLE hThread = CreateThread(NULL, 0, MainThread, hinstDLL, 0, NULL);
        if (hThread) CloseHandle(hThread);
    }
    return TRUE;
}
