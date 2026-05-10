#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
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

// --- МАТРИЦЫ ТРАНСФОРМАЦИИ КООРДИНАТ (WoW <-> Detour) ---
void WoWToRecast(const Vector3& wow, float* recast) {
    recast[0] = 32.0f * 533.33333f - wow.y;
    recast[1] = wow.z;
    recast[2] = 32.0f * 533.33333f - wow.x;
}

void RecastToWoW(const float* recast, Vector3& wow) {
    wow.x = 32.0f * 533.33333f - recast[2];
    wow.y = 32.0f * 533.33333f - recast[0];
    wow.z = recast[1];
}

void InitNavMesh() {
    g_NavMesh = dtAllocNavMesh();
    g_NavQuery = dtAllocNavMeshQuery();
    
    dtNavMeshParams params;
    memset(&params, 0, sizeof(params));
    // Ориджин для стандартных mmaps от Trinity/AzerothCore всегда 0,0,0
    params.orig[0] = 0.0f;
    params.orig[1] = 0.0f;
    params.orig[2] = 0.0f;
    params.tileWidth = 533.33333f;
    params.tileHeight = 533.33333f;
    params.maxTiles = 256;     
    params.maxPolys = 65536;   
    
    g_NavMesh->init(&params);
    g_NavQuery->init(g_NavMesh, 2048);
    
    g_Filter.setIncludeFlags(0xFFFF);
    g_Filter.setExcludeFlags(0);
    std::cout << "[+] Detour NavMesh Engine Initialized!\n";
}

void GetGridCoordinates(float x, float y, int& gridX, int& gridY) {
    gridX = (int)(32.0f - (x / 533.33333f));
    gridY = (int)(32.0f - (y / 533.33333f));
}

bool LoadTile(int mapId, int gridX, int gridY) {
    // ВНИМАНИЕ: В Detour X и Y перевернуты относительно WoW!
    if (g_NavMesh->getTileAt(gridY, gridX, 0)) return true;

    char filename[512];
    // Желательно в будущем брать путь из конфига, а не хардкодить диск E:
    sprintf_s(filename, "E:\\Cheats\\WoW Inject\\mmaps\\%03d%02d%02d.mmtile", mapId, gridX, gridY);

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;

    MmapTileHeader header;
    file.read((char*)&header, sizeof(MmapTileHeader));

    unsigned char* data = (unsigned char*)dtAlloc(header.size, DT_ALLOC_PERM);
    if (!data) return false;

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

std::vector<Vector3> CalculatePath(Vector3 start, Vector3 end) {
    std::vector<Vector3> path;
    
    int startGridX, startGridY, endGridX, endGridY;
    GetGridCoordinates(start.x, start.y, startGridX, startGridY);
    GetGridCoordinates(end.x, end.y, endGridX, endGridY);
    
    LoadTile(0, startGridX, startGridY);
    LoadTile(0, endGridX, endGridY);

    // Конвертируем WoW -> Recast
    float startPos[3], endPos[3];
    WoWToRecast(start, startPos);
    WoWToRecast(end, endPos);
    
    // Расширяем зону поиска полигона до 10 метров (если бот стоит на камне или заборе)
    float extents[3] = { 10.0f, 10.0f, 10.0f };

    dtPolyRef startRef = 0, endRef = 0;
    g_NavQuery->findNearestPoly(startPos, extents, &g_Filter, &startRef, 0);
    g_NavQuery->findNearestPoly(endPos, extents, &g_Filter, &endRef, 0);

    if (!startRef || !endRef) {
        std::cout << "[-] WARNING: Outside of NavMesh bounds. Using straight line.\n";
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

        // Конвертируем обратно Recast -> WoW
        for (int i = 0; i < straightPathCount; ++i) {
            Vector3 pt;
            RecastToWoW(&straightPath[i*3], pt);
            path.push_back(pt);
        }
        std::cout << "[+] Path found! Waypoints: " << straightPathCount << "\n";
    } else {
        path.push_back(end);
    }

    return path;
}

int main() {
    std::cout << "--- WoW NavMesh Server (Coordinates Fixed) ---\n";
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
