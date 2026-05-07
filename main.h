DWORD WINAPI MainThread(LPVOID lpParam) {
    // Создаем консоль прямо внутри игры для отладки
    AllocConsole();
    freopen("CONOUT$", "w", stdout);

    std::cout << "--- Professional Bot Debug Console ---" << std::endl;

    while (true) {
        DWORD playerBase = *(DWORD*)0x00BD07E0; // База игрока
        if (playerBase) {
            float x = *(float*)(playerBase + 0x798);
            float y = *(float*)(playerBase + 0x79C);
            float z = *(float*)(playerBase + 0x7A0);

            // Выводим твои координаты в консоль в реальном времени
            printf("My Pos: X: %.2f | Y: %.2f | Z: %.2f\r", x, y, z);
            
            // ТЕСТ ДВИЖЕНИЯ: Если нажмешь клавишу F1, персонаж побежит вперед
            if (GetAsyncKeyState(VK_F1) & 0x8000) {
                std::cout << "\n[!] Moving to test coordinate..." << std::endl;
                MoveToCoord(x + 5.0f, y, z); // Пробежать 5 метров по X
            }
        }
        Sleep(100);
    }
    return 0;
}
