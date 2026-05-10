#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>

// Подключаем библиотеку Detour
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

struct Vector3 { float x, y, z; };
struct PathRequest { Vector3 start; Vector3 end; };

#pragma pack(push, 1)
struct MmapTileHeader {
    uint32_t mmapMagic;
    uint32_t dtVersion;
    uint32_t mmapVersion;
    uint32_t size;
    char usesLiquids;
    char padding[3];
};
#pragma pack(pop)

dtNavMesh* g_NavMesh = nullptr;
dtNavMeshQuery* g_NavQuery = nullptr;
dtQueryFilter g_Filter;

void InitNavMesh() {
    g_NavMesh = dtAllocNavMesh();
    g_NavQuery = dtAllocNavMeshQuery();
    
    dtNavMeshParams params;
    memset(&params, 0, sizeof(params));
    params.orig[0] = -32.0f * 533.33333f;
    params.orig[1] = 0.0f;
    params.orig[2] = -32.0f * 533.33333f;
    params.tileWidth = 533.33333f;
    params.tileHeight = 533.33333f;
    params.maxTiles = 128;     // Безопасный лимит тайлов
    params.maxPolys = 32768;   // Безопасный лимит полигонов (не вызывает краш движка)
    
    dtStatus status = g_NavMesh->init(&params);
    if (dtStatusFailed(status)) {
        std::cout << "[-] FATAL ERROR: dtNavMesh->init failed! Status: " << status << "\n";
        Sleep(5000);
        exit(1);
    }

    status = g_NavQuery->init(g_NavMesh, 2048);
    if (dtStatusFailed(status)) {
        std::cout << "[-] FATAL ERROR: dtNavMeshQuery->init failed!\n";
        Sleep(5000);
        exit(1);
    }
    
    g_Filter.setIncludeFlags(0xFFFF);
    g_Filter.setExcludeFlags(0);
    
    std::cout << "[+] Detour NavMesh Engine Initialized Safely!\n";
}

bool LoadTile(int mapId, int gridX, int gridY) {
    // Если тайл уже загружен - пропускаем
    if (g_NavMesh->getTileAt(gridX, gridY, 0)) return true;

    char filename[512];
    sprintf_s(filename, "E:\\Cheats\\WoW Inject\\mmaps\\%03d%02d%02d.mmtile", mapId, gridX, gridY);

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "[-] ERROR: Tile not found: " << filename << "\n";
        return false;
    }

    MmapTileHeader header;
    file.read((char*)&header, sizeof(MmapTileHeader));

    if (header.mmapMagic != 0x4D4D4150 && header.mmapMagic != 0x50414D4D) {
        std::cout << "[-] ERROR: Invalid Magic in " << filename << "\n";
        return false;
    }

    if (header.size == 0 || header.size > 50000000) { 
        std::cout << "[-] ERROR: Invalid tile size: " << header.size << "\n";
        return false;
    }

    // [!] ФИКС КРАША 0xc0000409: Проверяем, выделилась ли память
    unsigned char* data = (unsigned char*)dtAlloc(header.size, DT_ALLOC_PERM);
    if (!data) {
        std::cout << "[-] FATAL ERROR: Memory allocation failed for size " << header.size << "\n";
        return false;
    }

    file.read((char*)data, header.size);

    dtTileRef tileRef = 0;
    dtStatus status = g_NavMesh->addTile(data, header.size, DT_TILE_FREE_DATA, 0, &tileRef);
    
    if (dtStatusSucceed(status)) {
        std::cout << "[+] Loaded NavMesh Tile: " << gridX << "_" << gridY << " (Size: " << header.size << " bytes)\n";
        return true;
    } else {
        std::cout << "[-] ERROR: Failed to add tile to NavMesh! Status: " << status << "\n";
        dtFree(data);
        return false;
    }
}

void GetGridCoordinates(float x, float y, int& gridX, int& gridY) {
    gridX = (int)(32.0f - (x / 533.33333f));
    gridY = (int)(32.0f - (y / 533.33333f));
}

std::vector<Vector3> CalculatePath(Vector3 start, Vector3 end) {
    std::vector<Vector3> path;
    
    int startGridX, startGridY, endGridX, endGridY;
    GetGridCoordinates(start.x, start.y, startGridX, startGridY);
    GetGridCoordinates(end.x, end.y, endGridX, endGridY);
    
    LoadTile(0, startGridX, startGridY);
    LoadTile(0, endGridX, endGridY);

    // [!] ФИКС КООРДИНАТ: В Detour координаты это (Y, Z, X)
    float startPos[3] = { start.y, start.z, start.x };
    float endPos[3] = { end.y, end.z, end.x };
    float extents[3] = { 3.0f, 5.0f, 3.0f };

    dtPolyRef startRef, endRef;
    g_NavQuery->findNearestPoly(startPos, extents, &g_Filter, &startRef, 0);
    g_NavQuery->findNearestPoly(endPos, extents, &g_Filter, &endRef, 0);

    if (!startRef || !endRef) {
        std::cout << "[-] WARNING: Could not find NavMesh polygon near start/end. Using straight line.\n";
        path.push_back(end); 
        return path;
    }

    dtPolyRef polys[256];
    int polyCount = 0;
    g_NavQuery->findPath(startRef, endRef, startPos, endPos, &g_Filter, polys, &polyCount, 256);

    if (polyCount > 0) {
        float straightPath[256 * 3];
        unsigned char straightPathFlags[256];
        dtPolyRef straightPathPolys[256];
        int straightPathCount = 0;

        g_NavQuery->findStraightPath(startPos, endPos, polys, polyCount, straightPath, straightPathFlags, straightPathPolys, &straightPathCount, 256, 0);

        // Возвращаем из Detour (Y, Z, X) обратно в WoW (X, Y, Z)
        for (int i = 0; i < straightPathCount; ++i) {
            path.push_back({ straightPath[i*3+2], straightPath[i*3], straightPath[i*3+1] });
        }
        std::cout << "[+] Path calculated! Waypoints: " << straightPathCount << "\n";
    } else {
        std::cout << "[-] WARNING: Path calculation failed. Using straight line.\n";
        path.push_back(end);
    }

    return path;
}

int main() {
    std::cout << "--- WoW NavMesh Server (Phase 3: Detour Active) ---\n";
    InitNavMesh();
    std::cout << "[+] Waiting for Bot DLL to connect...\n";

    while (true) {
        HANDLE hPipe = CreateNamedPipeA(
            "\\\\.\\pipe\\WoWNavMeshPipe",
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 1024 * 16, 1024 * 16, 0, NULL);

        if (hPipe != INVALID_HANDLE_VALUE) {
            bool connected = ConnectNamedPipe(hPipe, NULL) ? true : (GetLastError() == ERROR_PIPE_CONNECTED);
            
            if (connected) {
                PathRequest req;
                DWORD bytesRead;
                if (ReadFile(hPipe, &req, sizeof(PathRequest), &bytesRead, NULL)) {
                    std::vector<Vector3> path = CalculatePath(req.start, req.end);
                    
                    DWORD bytesWritten;
                    int count = path.size();
                    WriteFile(hPipe, &count, sizeof(int), &bytesWritten, NULL);
                    WriteFile(hPipe, path.data(), count * sizeof(Vector3), &bytesWritten, NULL);
                }
            }
            FlushFileBuffers(hPipe);
            DisconnectNamedPipe(hPipe);
        }
        CloseHandle(hPipe);
    }
    return 0;
}
