#include <windows.h>
#include <iostream>
#include <cmath>

#pragma comment(lib, "user32.lib")

void SetConsoleCursor(int x, int y) {
    COORD coord;
    coord.X = x;
    coord.Y = y;
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}

uintptr_t FindPattern(HMODULE module, const char* pattern, const char* mask) {
    IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)module;
    IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)((uint8_t*)module + dosHeader->e_lfanew);

    uintptr_t start = (uintptr_t)module;
    uintptr_t size = ntHeaders->OptionalHeader.SizeOfImage;
    size_t maskLen = strlen(mask);

    uintptr_t current = start;
    while (current < start + size - maskLen) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((LPCVOID)current, &mbi, sizeof(mbi))) break;

        if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
            uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            uintptr_t scanEnd = (regionEnd > start + size) ? start + size : regionEnd;

            for (uintptr_t i = current; i < scanEnd - maskLen; i++) {
                bool found = true;
                for (size_t j = 0; j < maskLen; j++) {
                    if (mask[j] != '?' && (uint8_t)pattern[j] != *(uint8_t*)(i + j)) {
                        found = false;
                        break;
                    }
                }
                if (found) return i;
            }
        }
        current = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    return 0;
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    HMODULE base = GetModuleHandleA(NULL);
    printf("--- Autonomous Bot Radar Active ---\n");
    printf("[*] Base: 0x%p. Scanning...\n", (void*)base);

    const char* pattern = "\x8B\x15\x00\x00\x00\x00\x8B\x42\x2C\x85\xC0";
    const char* mask = "xx????xxxxx";

    uintptr_t match = FindPattern(base, pattern, mask);
    uintptr_t connectionAddr = match ? *(uintptr_t*)(match + 2) : ((uintptr_t)base + 0x00879CE0);

    if (match) printf("[!] FOUND Pattern! Pointer: 0x%p\n", (void*)connectionAddr);
    else printf("[-] Using Static Pointer: 0x%p\n", (void*)connectionAddr);

    printf("[*] Press END to unload the DLL.\n");
    printf("--------------------------------------------------\n");
    
    int hudStartY = 6; 

    while (!GetAsyncKeyState(VK_END)) {
        uintptr_t clientConnection = 0;
        
        __try {
            clientConnection = *(uintptr_t*)connectionAddr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            clientConnection = 0;
        }
        
        SetConsoleCursor(0, hudStartY);

        if (clientConnection) {
            uintptr_t objMgr = *(uintptr_t*)(clientConnection + 0x2ED0);
            
            if (objMgr) {
                uintptr_t cur = *(uintptr_t*)(objMgr + 0xAC);
                uint64_t localGuid = *(uint64_t*)(objMgr + 0xC0);
                
                __try {
                    uintptr_t localPlayerObj = 0;

                    // 1. Ищем только себя, чтобы получить свои координаты для расчетов
                    while (cur != 0 && (cur & 1) == 0) {
                        uint64_t objGuid = *(uint64_t*)(cur + 0x30);
                        if (objGuid == localGuid && localGuid != 0) {
                            localPlayerObj = cur;
                            break; 
                        }
                        cur = *(uintptr_t*)(cur + 0x3C);
                    }

                    if (localPlayerObj) {
                        uintptr_t localDesc = *(uintptr_t*)(localPlayerObj + 0x8);
                        int hp = *(int*)(localDesc + 0x60);
                        int maxHp = *(int*)(localDesc + 0x80);
                        
                        float myX = *(float*)(localPlayerObj + 0x798);
                        float myY = *(float*)(localPlayerObj + 0x79C);
                        float myZ = *(float*)(localPlayerObj + 0x7A0);
                        
                        printf("[BOT STATUS]                                     \n");
                        printf("HP: %d/%d                                        \n", hp, maxHp);
                        printf("POS: X:%.2f | Y:%.2f | Z:%.2f                    \n", myX, myY, myZ);
                        printf("--------------------------------------------------\n");

                        // 2. Ищем ближайшего живого моба
                        float closestDist = 999999.0f;
                        uint64_t closestGuid = 0;
                        int closestHp = 0, closestMaxHp = 0;
                        float cX = 0, cY = 0, cZ = 0;

                        cur = *(uintptr_t*)(objMgr + 0xAC); // Сброс указателя на начало
                        while (cur != 0 && (cur & 1) == 0) {
                            int objType = *(int*)(cur + 0x14);
                            
                            // Нас интересуют только NPC/Мобы (тип 4)
                            if (objType == 4) {
                                uintptr_t desc = *(uintptr_t*)(cur + 0x8);
                                int npcHp = *(int*)(desc + 0x60);
                                
                                // Мертвецов не трогаем
                                if (npcHp > 0) {
                                    float x = *(float*)(cur + 0x798);
                                    float y = *(float*)(cur + 0x79C);
                                    float z = *(float*)(cur + 0x7A0);
                                    
                                    float dx = x - myX;
                                    float dy = y - myY;
                                    float dz = z - myZ;
                                    float dist = sqrt(dx*dx + dy*dy + dz*dz);
                                    
                                    // Находим минимальную дистанцию
                                    if (dist < closestDist) {
                                        closestDist = dist;
                                        closestGuid = *(uint64_t*)(cur + 0x30);
                                        closestHp = npcHp;
                                        closestMaxHp = *(int*)(desc + 0x80);
                                        cX = x; cY = y; cZ = z;
                                    }
                                }
                            }
                            cur = *(uintptr_t*)(cur + 0x3C);
                        }

                        if (closestGuid != 0) {
                            printf("[AUTOTARGET: CLOSEST ALIVE NPC]                  \n");
                            printf("HP: %d/%d                                        \n", closestHp, closestMaxHp);
                            printf("POS: X:%.2f | Y:%.2f | Z:%.2f                    \n", cX, cY, cZ);
                            printf("Distance: %.2f yards                             \n", closestDist);
                        } else {
                            printf("[AUTOTARGET] NO ALIVE MOBS AROUND                \n");
                            printf("                                                 \n");
                            printf("                                                 \n");
                            printf("                                                 \n");
                        }
                    } else {
                        printf("Local player not found in ObjectManager...       \n");
                        for(int i=0; i<8; i++) printf("                                                 \n");
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    printf("Reading Error...                                     \n");
                    for(int i=0; i<8; i++) printf("                                                 \n");
                }
            }
        } else {
            printf("Waiting for world...                                     \n");
            for(int i=0; i<8; i++) printf("                                                 \n");
        }
        Sleep(50); 
    }

    printf("\n[*] Unloading...\n");
    fclose(f);
    FreeConsole();
    FreeLibraryAndExitThread((HMODULE)lpParam, 0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        HANDLE hThread = CreateThread(NULL, 0, MainThread, hinstDLL, 0, NULL);
        if (hThread) CloseHandle(hThread);
    }
    return TRUE;
}
