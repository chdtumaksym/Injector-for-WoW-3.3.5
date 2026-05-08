#include <windows.h>
#include <d3d9.h>
#include <stdint.h>
#include <cmath>

#pragma comment(lib, "d3d9.lib")

struct Vector3 { float x, y, z; };

// Прототипы функций 3.3.5a (12340)
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, uint32_t clickType, uint64_t* targetGuid, Vector3* pos, float precision);
tClickToMove ClickToMove = (tClickToMove)0x00611130;

typedef HRESULT(STDMETHODCALLTYPE* tEndScene)(LPDIRECT3DDEVICE9 pDevice);
tEndScene oEndScene = nullptr;

// Безопасное получение указателя на игрока
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

// Наш хук
HRESULT STDMETHODCALLTYPE hkEndScene(LPDIRECT3DDEVICE9 pDevice) {
    uintptr_t player = GetPlayerPtr();
    
    // Бот работает только если персонаж в мире
    if (player) {
        uint64_t targetGuid = *(uint64_t*)(player + 0x28); 
        if (targetGuid != 0) {
            Vector3 zeroPos = { 0, 0, 0 };
            ClickToMove(player, 6, &targetGuid, &zeroPos, 2.0f);
        }
    }
    return oEndScene(pDevice);
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    // Ждем, пока устройство DX9 в игре проинициализируется
    uintptr_t device_ptr = 0;
    while (!device_ptr) {
        device_ptr = *(uintptr_t*)0x00C5DF88; // Статический адрес IDirect3DDevice9* для 3.3.5a
        Sleep(500);
    }

    uintptr_t* vtable = *(uintptr_t**)device_ptr;
    oEndScene = (tEndScene)vtable[42]; // Сохраняем оригинал EndScene

    // МЕНЯЕМ ПРАВА ДОСТУПА К ПАМЯТИ (чтобы не было ACCESS_VIOLATION при записи)
    DWORD oldProtect;
    if (VirtualProtect(&vtable[42], sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        vtable[42] = (uintptr_t)hkEndScene; // Подменяем указатель
        VirtualProtect(&vtable[42], sizeof(uintptr_t), oldProtect, &oldProtect);
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) CreateThread(0, 0, MainThread, 0, 0, 0);
    return TRUE;
}
