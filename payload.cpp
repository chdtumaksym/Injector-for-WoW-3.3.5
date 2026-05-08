// Добавь эти проверки, чтобы не вылетало
void RunBotLogic() {
    uintptr_t s_curMgr = *(uintptr_t*)ADDR_CUR_MGR;
    if (!s_curMgr || (s_curMgr & 1)) return; // Если менеджера нет - выходим

    uintptr_t localGuid = *(uintptr_t*)(s_curMgr + OFF_MGR_LOCAL_GUID);
    uintptr_t currentObj = *(uintptr_t*)(s_curMgr + OFF_MGR_FIRST_OBJ);
    
    uintptr_t playerBase = 0;
    // Сначала ищем себя, чтобы знать свои координаты
    uintptr_t tempObj = currentObj;
    while (tempObj && (tempObj & 1) == 0) {
        if (*(uint64_t*)(tempObj + OFF_OBJ_GUID) == localGuid) {
            playerBase = tempObj;
            break;
        }
        tempObj = *(uintptr_t*)(tempObj + OFF_OBJ_NEXT);
    }

    if (!playerBase) return;

    Vector3 myPos = *(Vector3*)(playerBase + OFF_UNIT_POS);
    uintptr_t bestTarget = 0;
    float minDist = 100.0f;

    // Снова перебор - ищем ближайшего моба
    while (currentObj && (currentObj & 1) == 0) {
        uint32_t type = *(uint32_t*)(currentObj + OFF_OBJ_TYPE);
        
        // Тип 3 - мобы, тип 4 - игроки
        if (type == 3) {
            Vector3 objPos = *(Vector3*)(currentObj + OFF_UNIT_POS);
            float dist = sqrt(pow(objPos.x - myPos.x, 2) + pow(objPos.y - myPos.y, 2));

            if (dist < minDist && dist > 0.1f) {
                minDist = dist;
                bestTarget = currentObj;
            }
        }
        currentObj = *(uintptr_t*)(currentObj + OFF_OBJ_NEXT);
    }

    if (bestTarget) {
        uint64_t targetGuid = *(uint64_t*)(bestTarget + OFF_OBJ_GUID);
        Vector3 targetPos = *(Vector3*)(bestTarget + OFF_UNIT_POS);
        // Используем 6 (Interact), чтобы он и бежал и бил
        ClickToMove(playerBase, 6, &targetGuid, &targetPos, 2.0f);
    }
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    // Ждем, пока прогрузится мир (адрес устройства заполняется не сразу)
    uintptr_t d3d9_ptr = 0;
    while (!d3d9_ptr) {
        d3d9_ptr = *(uintptr_t*)0x00C5DF88; 
        Sleep(500);
    }

    uintptr_t* vtable_ptr = *(uintptr_t**)d3d9_ptr;
    oEndScene = (tEndScene)vtable_ptr[42]; // Берем оригинальный EndScene

    DWORD oldProtect;
    VirtualProtect(&vtable_ptr[42], sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect);
    vtable_ptr[42] = (uintptr_t)hkEndScene; // Подменяем
    VirtualProtect(&vtable_ptr[42], sizeof(uintptr_t), oldProtect, &oldProtect);

    return 0;
}
