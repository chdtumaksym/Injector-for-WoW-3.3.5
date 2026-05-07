#include <windows.h>
#include <iostream>
#include <stdio.h>

// Подключаем библиотеку для MessageBox
#pragma comment(lib, "user32.lib")

// Оффсеты для WoW 3.3.5a (Build 12340)
#define PLAYER_BASE_OFFSET 0x00BD07E0
#define CTM_BASE 0x00BD07A0

// Перечисления для Click-to-Move
enum CTM_Action {
    Face = 1,
    Stop = 2,
    Walk = 4,
    MoveTo = 5,
    InteractNPC = 6,
    InteractObject = 7
};

// Функция движения
void MoveToCoord(float x, float y, float z) {
    float* ctm_ptr = (float*)CTM_BASE;
    if (ctm_ptr) {
        *(float*)(CTM_BASE + 0x8) = x;
        *(float*)(CTM_BASE + 0xC) = y;
        *(float*)(CTM_BASE + 0x10) = z;
        *(int*)(CTM_BASE + 0x1C) = MoveTo;
    }
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    // Создаем консоль для отладки
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    printf("--- Professional Bot Debug Console ---\n");
    MessageBoxA(NULL, "Imba Bot запущен внутри WoW!", "Status", MB_OK);

    while (true) {
        // Читаем базу игрока
        DWORD* playerBasePtr = (DWORD*)PLAYER_BASE_OFFSET;
        if (playerBasePtr && *playerBasePtr) {
            DWORD pBase = *playerBasePtr;
            
            float x = *(float*)(pBase + 0x798);
            float y = *(float*)(pBase + 0x79C);
            float z = *(float*)(pBase + 0x7A0);

            printf("My Pos: X: %.2f | Y: %.2f | Z: %.2f\r", x, y, z);
            
            // Тест движения на клавишу F1
            if (GetAsyncKeyState(VK_F1) & 0x8000) {
                MoveToCoord(x + 5.0f, y, z);
            }
        }
        Sleep(100);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
