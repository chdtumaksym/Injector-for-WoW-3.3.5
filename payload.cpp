#include <windows.h>
#include <d3d9.h>
#include <stdint.h>
#include <cmath>

#pragma comment(lib, "d3d9.lib")

// Структура координат
struct Vector3 {
    float x, y, z;
};

// Прототип функции движения Click-to-Move для 3.3.5a (12340)
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, uint32_t clickType, uint64_t* targetGuid, Vector3* pos, float precision);
tClickToMove ClickToMove = (tClickToMove)0x00611130;

// Для хука EndScene
typedef HRESULT(STDMETHODCALLTYPE* tEndScene)(LPDIRECT3DDEVICE9 pDevice);
tEndScene oEndScene = nullptr;

// Безопасное получение указателя на своего персонажа
uintptr_t GetPlayerPtr() {
    uintptr_t s_curMgr = *(uintptr_t*)0x00CB2418;
    if (!s_curMgr || (s_curMgr & 1)) return 0;

    uint64_t localGuid = *(uint64_t*)(s_curMgr + 0xC0);
    uintptr_t currentObj = *(uintptr_t*)(s_curMgr + 0xAC);

    while (currentObj && (currentObj & 1) == 0) {
        if (*(uint64_t*)(currentObj + 0x30) == localGuid) return currentObj;
        currentObj = *(uintptr_t*)(currentObj + 0x3C);
    }
    return 0;
}

// Логика бота
void RunBotLogic() {
    uintptr_t player = GetPlayerPtr();
    if (!player) return;

    // Считываем GUID текущей цели (таргета)
    // Оффсет таргета в 3.3.5a обычно 0x28 или через дескрипторы
    uint64_t targetGuid = *(uint64_t*)(player + 0x28); 

    // Если есть цель — бежим к ней и взаимодействуем (Interact)
    if (targetGuid != 0) {
        Vector3 zeroPos = { 0, 0, 0 };
        // Тип 6 заставляет персонажа подбежать к цели и начать действие
        ClickToMove(player, 6, &targetGuid, &zeroPos, 2.0f);
    }
}

// Наш хук, который вызывается игрой каждый кадр
HRESULT STDMETHODCALLTYPE hkEndScene(LPDIRECT3DDEVICE9 pDevice) {
    static bool inside = false;
    if (!inside) {
        inside = true;
        RunBotLogic();
        inside = false;
    }
    return oEndScene(pDevice);
}

// Поток инициализации
DWORD WINAPI MainThread(LPVOID lpParam) {
    // Ждем загрузки d3d9.dll
    while (!GetModuleHandleA("d3d9.dll")) Sleep(500);

    // Ищем адрес устройства DirectX игры
    uintptr_t d3d9_device_ptr = 0;
    while (!d3d9_device_ptr) {
        d3d9_device_ptr = *(uintptr_t*)0x00C5DF88; 
        Sleep(500);
    }

    // Получаем VTable (таблицу функций)
    uintptr_t* vtable = *(uintptr_t**)d3d9_device_ptr;
    if (!vtable) return 0;

    // Сохраняем оригинальный EndScene (индекс 42)
    oEndScene = (tEndScene)vtable[42];

    // Устанавливаем VMT Hook (подменяем адрес функции в таблице)
    DWORD oldProtect;
    if (VirtualProtect(&vtable[42], sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        vtable[42] = (uintptr_t)hkEndScene;
        VirtualProtect(&vtable[42], sizeof(uintptr_t), oldProtect, &oldProtect);
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
