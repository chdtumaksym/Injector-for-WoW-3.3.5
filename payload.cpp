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
                uint64_t localGuid = *(uint64_t*)(objMgr + 0xC0); // Получаем GUID нашего персонажа
                
                __try {
                    uintptr_t localPlayerObj = 0;
                    uint64_t targetGuid = 0;
                    uintptr_t targetObj = 0;

                    // Первый проход: ищем себя и узнаем GUID нашей цели
                    while (cur != 0 && (cur & 1) == 0) {
                        uint64_t objGuid = *(uint64_t*)(cur + 0x30);
                        if (objGuid == localGuid && localGuid != 0) {
                            localPlayerObj = cur;
                            uintptr_t descriptor = *(uintptr_t*)(cur + 0x8);
                            // 0x48 (index 0x12) - UNIT_FIELD_TARGET (uint64_t)
                            targetGuid = *(uint64_t*)(descriptor + 0x48);
                            break; // Нашли себя, дальше этот цикл крутить нет смысла
                        }
                        cur = *(uintptr_t*)(cur + 0x3C);
                    }

                    // Второй проход: если у нас есть цель, ищем её объект в памяти
                    if (targetGuid != 0) {
                        cur = *(uintptr_t*)(objMgr + 0xAC); // Сбрасываем указатель на начало списка
                        while (cur != 0 && (cur & 1) == 0) {
                            uint64_t objGuid = *(uint64_t*)(cur + 0x30);
                            if (objGuid == targetGuid) {
                                targetObj = cur;
                                break;
                            }
                            cur = *(uintptr_t*)(cur + 0x3C);
                        }
                    }

                    // Вывод информации
                    if (localPlayerObj) {
                        uintptr_t localDesc = *(uintptr_t*)(localPlayerObj + 0x8);
                        int hp = *(int*)(localDesc + 0x60);
                        int maxHp = *(int*)(localDesc + 0x80);
                        
                        if (targetObj) {
                            uintptr_t targetDesc = *(uintptr_t*)(targetObj + 0x8);
                            int tHp = *(int*)(targetDesc + 0x60);
                            int tMaxHp = *(int*)(targetDesc + 0x80);
                            printf("ME: %d/%d HP | TARGET: %d/%d HP                          \r", hp, maxHp, tHp, tMaxHp);
                        } else {
                            printf("ME: %d/%d HP | TARGET: NONE                              \r", hp, maxHp);
                        }
                    } else {
                        printf("Local player not found in ObjectManager...                          \r");
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    printf("Reading Error...                                                    \r");
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
