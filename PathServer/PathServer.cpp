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
    params.orig[0] = -32.0f * 533.33333f;
    params.orig[1] = 0.0f;
    params.orig[2] = -32.0f * 533.33333f;
    params.tileWidth = 533.33333f;
    params.tileHeight = 533.33333f;
    params.maxTiles = 64 * 64;
    params.maxPolys = 1 << 22;
    
    g_NavMesh->init(&params);
    g_NavQuery->init(g_NavMesh, 2048);
    
    g_Filter.setIncludeFlags(0xFFFF);
    g_Filter.setExcludeFlags(0);
    
    std::cout << "[+] Detour NavMesh Engine Initialized!\n";
}

bool LoadTile(int mapId, int gridX, int gridY) {
    // Если тайл уже загружен в память - пропускаем
    if (g_NavMesh->getTileAt(gridX, gridY, 0)) return true;

    char filename[512];
    sprintf_s(filename, "E:\\Cheats\\WoW Inject\\mmaps\\%03d%02d%02d.mmtile", mapId, gridX, gridY);

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;

    MmapTileHeader header;
    file.read((char*)&header, sizeof(MmapTileHeader));

    if (header.mmapMagic != 0x4D4D4150 && header.mmapMagic != 0x50414D4D) return false;

    unsigned char* data = (unsigned char*)dtAlloc(header.size, DT_ALLOC_PERM);
    file.read((char*)data, header.size);

    dtTileRef tileRef = 0;
    dtStatus status = g_NavMesh->addTile(data, header.size, DT_TILE_FREE_DATA, 0, &tileRef);
    
    if (dtStatusSucceed(status)) {
        std::cout << "[+] Loaded NavMesh Tile: " << gridX << "_" << gridY << "\n";
        return true;
    } else {
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
    
    // Загружаем квадраты карты, на которых стоим мы и цель
    LoadTile(0, startGridX, startGridY);
    LoadTile(0, endGridX, endGridY);

    // Переводим координаты WoW (X, Y, Z) в координаты Detour (X, Z, Y)
    float startPos[3] = { start.x, start.z, start.y };
    float endPos[3] = { end.x, end.z, end.y };
    float extents[3] = { 3.0f, 5.0f, 3.0f };

    dtPolyRef startRef, endRef;
    g_NavQuery->findNearestPoly(startPos, extents, &g_Filter, &startRef, 0);
    g_NavQuery->findNearestPoly(endPos, extents, &g_Filter, &endRef, 0);

    if (!startRef || !endRef) {
        path.push_back(end); // Если не нашли сетку - бежим по прямой (Failsafe)
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

        // Переводим координаты Detour обратно в WoW
        for (int i = 0; i < straightPathCount; ++i) {
            path.push_back({ straightPath[i*3], straightPath[i*3+2], straightPath[i*3+1] });
        }
    } else {
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
