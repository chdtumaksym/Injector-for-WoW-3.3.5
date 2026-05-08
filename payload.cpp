#include <windows.h>
#include <d3d9.h>
#include <cmath>

#pragma comment(lib, "d3d9.lib")

struct Vector3 { float x, y, z; };

typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, uint32_t clickType, uint64_t* targetGuid, Vector3* pos, float precision);
tClickToMove ClickToMove = (tClickToMove)0x00611130;

typedef HRESULT(STDMETHODCALLTYPE* tEndScene)(LPDIRECT3DDEVICE9 pDevice);
tEndScene oEndScene = nullptr;

// Безопасный поиск игрока
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

void RunBotLogic() {
    uintptr_t player = GetPlayerPtr();
    if (!player) return;

    // Пока просто тест: если мы в мире, пусть персонаж бежит в координаты (0,0,0) 
    // или к таргету, если нажмешь на него.
    uint64_t targetGuid = *(uint64_t*)(player + 0x28); // Оффсет текущей цели
    if (targetGuid != 0) {
        Vector3 zeroPos = { 0, 0, 0 };
        ClickToMove(player, 6, &targetGuid, &zeroPos, 2.0f);
    }
}

HRESULT STDMETHODCALLTYPE hkEndScene(LPDIRECT3DDEVICE9 pDevice) {
    RunBotLogic();
    return oEndScene(pDevice);
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    // 1. Создаем фейковый девайс, чтобы найти адрес реальной таблицы EndScene
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return 0;

    D3DPRESENT_PARAMETERS d3dpp = { 0 };
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = GetForegroundWindow();

    IDirect3DDevice9* pDummyDevice = nullptr;
    if (FAILED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummyDevice))) {
        pD3D->Release();
        return 0;
    }

    uintptr_t* vtable = *(uintptr_t**)pDummyDevice;
    oEndScene = (tEndScene)vtable[42]; // 42 - EndScene

    // 2. Хукаем через VMT (подменяем указатель в таблице)
    // Внимание: мы подменяем указатель в таблице КОНКРЕТНОГО девайса.
    // Но так как у игры он один, мы найдем его через адрес в памяти.
    uintptr_t realDevicePtr = *(uintptr_t*)0x00C5DF88; 
    if (realDevicePtr) {
        uintptr_t* realVtable = *(uintptr_t**)realDevicePtr;
        DWORD oldProtect;
        VirtualProtect(&realVtable[42], sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect);
        realVtable[42] = (uintptr_t)hkEndScene;
        VirtualProtect(&realVtable[42], sizeof(uintptr_t), oldProtect, &oldProtect);
    }

    pDummyDevice->Release();
    pD3D->Release();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID res) {
    if (r == DLL_PROCESS_ATTACH) CreateThread(0, 0, MainThread, 0, 0, 0);
    return TRUE;
}
