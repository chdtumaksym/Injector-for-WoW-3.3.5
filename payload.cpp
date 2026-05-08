#include <windows.h>
#include <d3d9.h>
#include <vector>
#include <cmath>

// Структуры
struct Vector3 { float x, y, z; };

// Оффсеты WoW 3.3.5a (12340)
#define ADDR_CUR_MGR 0x00CB2418
#define ADDR_CLMT 0x00611130
#define OFF_MGR_LOCAL_GUID 0xC0
#define OFF_MGR_FIRST_OBJ 0xAC
#define OFF_OBJ_NEXT 0x3C
#define OFF_OBJ_GUID 0x30
#define OFF_OBJ_TYPE 0x14
#define OFF_UNIT_POS 0x7D8

// Прототипы функций игры
typedef void(__thiscall* tClickToMove)(uintptr_t playerPtr, uint32_t clickType, uint64_t* targetGuid, Vector3* pos, float precision);
tClickToMove ClickToMove = (tClickToMove)ADDR_CLMT;

// Для VMT Хука
typedef HRESULT(STDMETHODCALLTYPE* tEndScene)(LPDIRECT3DDEVICE9 pDevice);
tEndScene oEndScene = nullptr;
uintptr_t* vtable = nullptr;

// Получение указателя на игрока
uintptr_t GetPlayerPtr() {
    uintptr_t s_curMgr = *(uintptr_t*)ADDR_CUR_MGR;
    if (!s_curMgr) return 0;
    uint64_t localGuid = *(uint64_t*)(s_curMgr + OFF_MGR_LOCAL_GUID);
    uintptr_t currentObj = *(uintptr_t*)(s_curMgr + OFF_MGR_FIRST_OBJ);

    while (currentObj && (currentObj & 1) == 0) {
        if (*(uint64_t*)(currentObj + OFF_OBJ_GUID) == localGuid) return currentObj;
        currentObj = *(uintptr_t*)(currentObj + OFF_OBJ_NEXT);
    }
    return 0;
}

// Главная логика
void RunBotLogic() {
    uintptr_t playerBase = GetPlayerPtr();
    if (!playerBase) return;

    uintptr_t s_curMgr = *(uintptr_t*)ADDR_CUR_MGR;
    uintptr_t currentObj = *(uintptr_t*)(s_curMgr + OFF_MGR_FIRST_OBJ);

    uintptr_t bestTarget = 0;
    float minDist = 9999.0f;
    Vector3 myPos = *(Vector3*)(playerBase + OFF_UNIT_POS);

    while (currentObj && (currentObj & 1) == 0) {
        int type = *(int*)(currentObj + OFF_OBJ_TYPE);
        uint64_t guid = *(uint64_t*)(currentObj + OFF_OBJ_GUID);

        if (type == 3 && guid != *(uint64_t*)(playerBase + OFF_OBJ_GUID)) {
            Vector3 objPos = *(Vector3*)(currentObj + OFF_UNIT_POS);
            float dist = sqrt(pow(objPos.x - myPos.x, 2) + pow(objPos.y - myPos.y, 2));

            if (dist < minDist) {
                minDist = dist;
                bestTarget = currentObj;
            }
        }
        currentObj = *(uintptr_t*)(currentObj + OFF_OBJ_NEXT);
    }

    if (bestTarget && minDist < 40.0f) {
        uint64_t targetGuid = *(uint64_t*)(bestTarget + OFF_OBJ_GUID);
        Vector3 targetPos = *(Vector3*)(bestTarget + OFF_UNIT_POS);
        ClickToMove(playerBase, 6, &targetGuid, &targetPos, 2.0f);
    }
}

// Наш хук
HRESULT STDMETHODCALLTYPE hkEndScene(LPDIRECT3DDEVICE9 pDevice) {
    RunBotLogic();
    return oEndScene(pDevice);
}

// Инициализация VMT хука (Безопаснее для Warden)
DWORD WINAPI MainThread(LPVOID lpParam) {
    while (!GetModuleHandleA("d3d9.dll")) Sleep(100);

    // Ищем девайс в памяти игры (чтобы не создавать свой)
    // В WoW 3.3.5 адрес устройства DX9 лежит тут:
    uintptr_t d3d9_device_ptr = *(uintptr_t*)0x00C5DF88; 
    while (!d3d9_device_ptr) {
        d3d9_device_ptr = *(uintptr_t*)0x00C5DF88;
        Sleep(100);
    }

    vtable = *(uintptr_t**)d3d9_device_ptr;
    oEndScene = (tEndScene)vtable[42]; // 42 - индекс EndScene

    // Подменяем адрес в таблице (VMT Hook)
    DWORD oldProtect;
    VirtualProtect(&vtable[42], sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect);
    vtable[42] = (uintptr_t)hkEndScene;
    VirtualProtect(&vtable[42], sizeof(uintptr_t), oldProtect, &oldProtect);

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        CreateThread(0, 0, MainThread, 0, 0, 0);
    }
    return TRUE;
}
