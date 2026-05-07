#include <windows.h>
#include <iostream>

#pragma comment(lib, "user32.lib")

// Функция поиска сигнатуры с безопасным парсингом PE-заголовка
uintptr_t FindPattern(HMODULE module, const char* pattern, const char* mask) {
    IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)module;
    IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)((uint8_t*)module + dosHeader->e_lfanew);

    // Сканируем весь образ модуля, используя VirtualQuery для защиты
    uintptr_t start = (uintptr_t)module;
    uintptr_t size = ntHeaders->OptionalHeader.SizeOfImage;
    size_t maskLen = strlen(mask);

    uintptr_t current = start;
    while (current < start + size - maskLen) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((LPCVOID)current, &mbi, sizeof(mbi))) break;

        // Проверяем, что страница памяти валидна и доступна для чтения
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
            uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            uintptr_t scanEnd = (regionEnd > start + size) ? start + size : regionEnd;

            for (uintptr_t i = current; i < scanEnd - maskLen; i++) {
                bool found = true;
                for (size_t j = 0; j < maskLen; j++) {
                    // Важно: строгий каст к uint8_t для сырых байтов, чтобы не было конфликтов знака
                    if (mask[j] != '?' && (uint8_t)pattern[j] != *(uint8_t*)(i + j)) {
                        found = false;
                        break;
                    }
                }
                if (found) return i;
            }
        }
        // Прыгаем на следующую страницу памяти
        current = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    return 0;
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    HMODULE base = GetModuleHandleA(NULL);
    printf("--- Pattern Scanner Active ---\n");
    printf("[*] Base: 0x%p. Safe scanning entire module...\n", (void*)base);

    const char* pattern = "\x8B\x15\x00\x00\x00\x00\x8B\x42\x2C\x85\xC0";
    const char* mask = "xx????xxxxx";

    uintptr_t match = FindPattern(base, pattern, mask);
    uintptr_t connectionAddr = 0;

    if (match) {
        connectionAddr = *(uintptr_t*)(match + 2);
        printf("[!] FOUND Pattern! ClientConnection Pointer: 0x%p\n", (void*)connectionAddr);
    } else {
        printf("[-] Pattern not found. Trying 3.3.5a static fallback...\n");
        // Статический адрес для 3.3.5a (Base + 0x00879CE0 = 0x00C79CE0)
        connectionAddr = (uintptr_t)base + 0x00879CE0;
        printf("[!] Using Static ClientConnection Pointer: 0x%p\n", (void*)connectionAddr);
    }

    printf("[*] Press END to unload the DLL.\n");

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConnection = 0;
        
        // Заворачиваем чтение в try/except на случай, если фолбэк адрес оказался неверным
        __try {
            clientConnection = *(uintptr_t*)connectionAddr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            clientConnection = 0;
        }
        
        // Если мы в мире и структура инициализирована
        if (clientConnection) {
            // Правильная цепочка: ClientConnection -> +0x2ED0 (ObjectManager) -> +0xAC (First Object)
            uintptr_t objMgr = *(uintptr_t*)(clientConnection + 0x2ED0);
            
            if (objMgr) {
                uintptr_t cur = *(uintptr_t*)(objMgr + 0xAC);
                int totalObjects = 0;
                int players = 0;
                int units = 0;
                int gameObjects = 0;
                
                // Дополнительная проверка указателя cur
                __try {
                    while (cur != 0 && (cur & 1) == 0 && totalObjects < 2000) {
                        totalObjects++;
                        
                        // Читаем тип объекта (смещение 0x14)
                        int objType = *(int*)(cur + 0x14);
                        
                        if (objType == 5) players++;         // Тип 5 - Игрок
                        else if (objType == 4) units++;      // Тип 4 - NPC / Моб
                        else if (objType == 6) gameObjects++;// Тип 6 - ГО (сундуки, объекты)

                        cur = *(uintptr_t*)(cur + 0x3C); // Смещение на следующий объект
                    }
                    printf("Total: %d | Players: %d | NPCs: %d | GOs: %d          \r", 
                           totalObjects, players, units, gameObjects);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    printf("Reading Error...                                      \r");
                }
            }
        } else {
            printf("Waiting for world...                                          \r");
        }
        Sleep(100);
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
