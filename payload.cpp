// Оффсеты для управления движением (3.3.5a)
DWORD CTM_Base = 0x00BD07A0; 
DWORD CTM_Push = 0x00860A90; // Адрес функции в коде игры для выполнения клика

enum CTM_Action {
    Face = 1,
    Stop = 2,
    Walk = 4, // Обычный клик по земле
    MoveTo = 5,
    InteractNPC = 6,
    InteractObject = 7
};

void MoveToCoord(float x, float y, float z) {
    // Записываем координаты в память CTM
    *(float*)(CTM_Base + 0x8) = x;
    *(float*)(CTM_Base + 0xC) = y;
    *(float*)(CTM_Base + 0x10) = z;
    
    // Вызываем действие "Бежать" (MoveTo)
    *(DWORD*)(CTM_Base + 0x1C) = MoveTo;
}
