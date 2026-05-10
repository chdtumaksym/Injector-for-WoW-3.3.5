#define NOMINMAX // [!] ФИКС ОШИБКИ КОМПИЛЯЦИИ C2589
#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>

// Подключаем правильный Detour
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

std::vector<std::string> g_LoadedTiles;

// --- ИСТИННАЯ ТРАНСФОРМАЦИЯ КООРДИНАТ TRINITYCORE ---
void WoWToRecast(const Vector3& wow, float* recast) {
    recast[0] = wow.y;
    recast[1] = wow.z;
    recast[2] = wow.x;
}

void RecastToWoW(const float* recast, Vector3& wow) {
    wow.x = recast[2];
    wow.y = recast[0];
    wow.z = recast[1];
}

void InitNavMesh() {
    g_NavMesh = dtAllocNavMesh();
    g_NavQuery = dtAllocNavMeshQuery();
    
    dtNavMeshParams params;
    memset(&params, 0, sizeof(params));
    params.orig[0] = 0.0f;
    params.orig[1] = 0.0f;
    params.orig[2] = 0.0f;
    params.tileWidth = 533.33333f;
    params.tileHeight = 533.33333f;
    params.maxTiles = 1024;     
    params.maxPolys = 1 << 22;   
    
    g_NavMesh->init(&params);
    g_NavQuery->init(g_NavMesh, 2048);
    
    g_Filter.setIncludeFlags(0xFFFF);
    g_Filter.setExcludeFlags(0);
    std::cout << "[+] Detour NavMesh Engine Initialized (True Coordinates)!\n";
}

void GetGridCoordinates(float x, float y, int& gridX, int& gridY) {
    gridX = (int)(32.0f - (x / 533.33333f));
    gridY = (int)(32.0f - (y / 533.33333f));
}

bool LoadTile(int mapId, int gridX, int gridY) {
    // В Detour координаты тайлов высчитываются по формуле TrinityCore
    int navX = 32 - gridY;
    int navY = 32 - gridX;
    
    if (g_NavMesh->getTileAt(navX, navY, 0)) return true; // Уже загружен

    char filename[512];
    sprintf_s(filename, "E:\\Cheats\\WoW Inject\\mmaps\\%03d%02d%02d.mmtile", mapId, gridX, gridY);

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;

    MmapTileHeader header;
    file.read((char*)&header, sizeof(MmapTileHeader));

    if (header.mmapMagic != 0x4D4D4150 && header.mmapMagic != 0x50414D4D) return false;

    unsigned char* data = (unsigned char*)dtAlloc(header.size, DT_ALLOC_PERM);
    if (!data) return false;

    file.read((char*)data, header.size);

    dtTileRef tileRef = 0;
    dtStatus status = g_NavMesh->addTile(data, header.size, DT_TILE_FREE_DATA, 0, &tileRef);
    
    if (dtStatusSucceed(status)) {
        g_LoadedTiles.push_back(std::string(filename));
        std::cout << "[+] Loaded NavMesh Tile: " << gridX << "_" << gridY << "\n";
        return true;
    } else {
        dtFree(data);
        return false;
    }
}

std::vector<Vector3> CalculatePath(Vector3 start, Vector3 end) {
    std::vector<Vector3> path;
    
    int startGridX, startGridY, endGridX, endGridY;
    GetGridCoordinates(start.x, start.y, startGridX, startGridY);
    GetGridCoordinates(end.x, end.y, endGridX, endGridY);
    
    //[!] УМНАЯ ЗАГРУЗКА КАРТ [!]
    int minX = std::min(startGridX, endGridX) - 1;
    int maxX = std::max(startGridX, endGridX) + 1;
    int minY = std::min(startGridY, endGridY) - 1;
    int maxY = std::max(startGridY, endGridY) + 1;
    
    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            LoadTile(0, x, y);
        }
    }

    float startPos[3], endPos[3];
    WoWToRecast(start, startPos);
    WoWToRecast(end, endPos);
    
    float extents[3] = { 50.0f, 50.0f, 50.0f };

    dtPolyRef startRef = 0, endRef = 0;
    g_NavQuery->findNearestPoly(startPos, extents, &g_Filter, &startRef, 0);
    g_NavQuery->findNearestPoly(endPos, extents, &g_Filter, &endRef, 0);

    if (!startRef || !endRef) {
        std::cout << "[-] WARNING: Could not find NavMesh polygon. Using straight line.\n";
        path.push_back(end); 
        return path;
    }

    dtPolyRef polys[512];
    int polyCount = 0;
    g_NavQuery->findPath(startRef, endRef, startPos, endPos, &g_Filter, polys, &polyCount, 512);

    if (polyCount > 0) {
        float straightPath[512 * 3];
        unsigned char straightPathFlags[512];
        dtPolyRef straightPathPolys[512];
        int straightPathCount = 0;

        g_NavQuery->findStraightPath(startPos, endPos, polys, polyCount, straightPath, straightPathFlags, straightPathPolys, &straightPathCount, 512, 0);

        for (int i = 0; i < straightPathCount; ++i) {
            Vector3 pt;
            RecastToWoW(&straightPath[i*3], pt);
            path.push_back(pt);
        }
        std::cout << "[+] Path found! Waypoints: " << straightPathCount << "\n";
    } else {
        std::cout << "[-] WARNING: Path calculation failed. Using straight line.\n";
        path.push_back(end);
    }

    return path;
}

int main() {
    std::cout << "--- WoW NavMesh Server (True Coordinates) ---\n";
    InitNavMesh();

    while (true) {
        HANDLE hPipe = CreateNamedPipeA(
            "\\\\.\\pipe\\WoWNavMeshPipe", PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 1024 * 16, 1024 * 16, 0, NULL);

        if (hPipe != INVALID_HANDLE_VALUE) {
            if (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
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
