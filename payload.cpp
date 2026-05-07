#include <windows.h>
#include <iostream>
#include <vector>

#pragma comment(lib, "user32.lib")

// Функция поиска сигнатуры (байтов) в памяти
uintptr_t FindPattern(uintptr_t start, uintptr_t size, const char* pattern, const char* mask) {
    for (uintptr_t i = 0; i < size - strlen(mask); i++) {
        bool found = true;
        for (uintptr_t j = 0; mask[j] != '\0'; j++) {
            if (mask[j] != '?' && (char)pattern[j] != *(char*)(start + i + j)) {
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

    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    printf("--- Pattern Scanner Active ---\n");
    printf("[*] Base: 0x%p. Scanning...\n", (void*)base);

    // Сигнатура функции GetObjectManager для WoW 3.3.5a
    // 8B 15 ? ? ? ? 8B 42 2C 85 C0
    const char* pattern = "\x8B\x15\x00\x00\x00\x00\x8B\x42\x2C\x85\xC0";
    const char* mask = "xx????xxxxx";

    uintptr_t match = FindPattern(base, 0x1000000, pattern, mask);

    if (match) {
        // Вытягиваем адрес из инструкции MOV EDX, [ADDR]
        uintptr_t objMgrAddr = *(uintptr_t*)(match + 2);
        printf("[!] FOUND! Real ObjMgr Address: 0x%p\n", (void*)objMgrAddr);

        while (true) {
            uintptr_t objMgr = *(uintptr_t*)objMgrAddr;
            if (objMgr) {
                uintptr_t cur = *(uintptr_t*)(objMgr + 0xAC);
                int count = 0;
                while (cur != 0 && (cur & 1) == 0) {
                    count++;
                    cur = *(uintptr_t*)(cur + 0x3C);
                    if (count > 2000) break;
                }
                printf("Real-time Objects: %d\r", count);
            } else {
                printf("ObjMgr is 0. Waiting for world...\r");
            }
            Sleep(500);
        }
    } else {
        printf("[!] ERROR: Pattern not found. Your client is unique.\n");
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
