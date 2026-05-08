#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include "MinHook.h" // Обязательно скачай и подключи

// Струкутра вектора для координат
struct Vector3 { float x, y, z; };

// Прототипы функций WoW 3.3.5a (12340)
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, uint32_t clickType, uint64_t* targetGuid, Vector3* pos, float precision);
tClickToMove ClickToMove = (tClickToMove)0x00611130;

// Указатель на игрока (Local Player)
uintptr_t GetPlayerPtr() {
    uintptr_t s_curMgr = *(uintptr_t*)0x00CB2418; // Статический адрес CurMgr
    if (!s_curMgr) return 0;
    return *(uintptr_t*)(s_curMgr + 0x34); // Оффсет локального игрока
}

// Оригинальная функция EndScene (для вызова после нашего кода)
typedef HRESULT(STDMETHODCALLTYPE* tEndScene)(LPDIRECT3DDEVICE9 pDevice);
tEndScene oEndScene = nullptr;

// Наша функция-хук, которая будет работать ВНУТРИ цикла игры
HRESULT STDMETHODCALLTYPE hkEndScene(LPDIRECT3DDEVICE9 pDevice) {
    static bool botActive = true; 

    if (botActive) {
        uintptr_t playerPtr = GetPlayerPtr();
        if (playerPtr) {
            // ПРИМЕР: Бежим в определенную точку (координаты для теста)
            // В реальном боте здесь будет выбор цели из ObjectManager
            Vector3 targetPos = { 100.0f, 150.0f, 50.0f }; 
            uint64_t targetGuid = 0;

            // Вызываем CTM (4 - движение, 6 - взаимодействие)
            // Это безопасно, так как мы в главном потоке (EndScene)
            // ClickToMove(playerPtr, 4, &targetGuid, &targetPos, 0.5f);
        }
    }

    return oEndScene(pDevice); // Обязательно возвращаем управление игре
}

// Поток инициализации хука
DWORD WINAPI InitHook(LPVOID lpParam) {
    // Ждем инициализации d3d9.dll в игре
    while (GetModuleHandleA("d3d9.dll") == NULL) Sleep(100);

    // Инициализация MinHook
    if (MH_Initialize() != MH_OK) return 1;

    // Находим адрес EndScene (через создание временного девайса или поиск паттерна)
    // Для 3.3.5а можно использовать смещение от vtable
    // Ниже — упрощенный пример получения адреса через фиктивный девайс
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return 1;

    D3DPRESENT_PARAMETERS d3dpp = { 0 };
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;

    IDirect3DDevice9* pDummyDevice = NULL;
    if (pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, GetDesktopWindow(), D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummyDevice) == S_OK) {
        uintptr_t* vtable = *(uintptr_t**)pDummyDevice;
        uintptr_t endSceneAddr = vtable[42]; // 42 - индекс EndScene в DX9

        // Ставим хук
        MH_CreateHook((LPVOID)endSceneAddr, &hkEndScene, (LPVOID*)&oEndScene);
        MH_EnableHook((LPVOID)endSceneAddr);

        pDummyDevice->Release();
    }
    pD3D->Release();

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        // Создаем поток для инициализации, чтобы не вешать DllMain
        CreateThread(NULL, 0, InitHook, NULL, 0, NULL);
    }
    return TRUE;
}
