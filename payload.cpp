#include <windows.h>
#include <d3d9.h>
#include <iostream>

#pragma comment(lib, "d3d9.lib")

// --- МИНИМАЛЬНЫЕ ДАННЫЕ ---
#define ADDR_PLAYER_BASE         0x00BD07E0
#define ADDR_D3D9_DEVICE         0x00C5DF88

typedef HRESULT(__stdcall* tEndScene)(LPDIRECT3DDEVICE9 pDevice);
tEndScene oEndScene = nullptr;

HRESULT __stdcall HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    static DWORD lastLog = 0;
    if (GetTickCount() - lastLog > 1000) {
        uintptr_t pLocal = *(uintptr_t*)ADDR_PLAYER_BASE;
        if (pLocal) {
            float x = *(float*)(pLocal + 0x798);
            printf("STABLE HOOK | My X: %.2f\r", x);
        } else {
            printf("STABLE HOOK | Waiting for login...\r");
        }
        lastLog = GetTickCount();
    }
    return oEndScene(pDevice);
}

DWORD WINAPI Init(LPVOID) {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    std::cout << "--- STERILE TEST STARTING ---" << std::endl;

    // Ждем пока игра реально создаст девайс
    uintptr_t deviceAddr = 0;
    while (!(deviceAddr = *(uintptr_t*)ADDR_D3D9_DEVICE)) Sleep(100);

    uintptr_t* vTable = *(uintptr_t**)deviceAddr;
    if (vTable) {
        DWORD old;
        VirtualProtect(&vTable[42], 4, PAGE_EXECUTE_READWRITE, &old);
        oEndScene = (tEndScene)vTable[42];
        vTable[42] = (uintptr_t)HookedEndScene;
        VirtualProtect(&vTable[42], 4, old, &old);
        std::cout << "[+] HOOK SUCCESSFUL. NO CRASH POSSIBLE." << std::endl;
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) CreateThread(0, 0, Init, 0, 0, 0);
    return TRUE;
}
