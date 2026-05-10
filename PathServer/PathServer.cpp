#define NOMINMAX 
#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>

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

const float TILE_SIZE = 533.33333f;
const float CENTER_POINT = 32.0f * TILE_SIZE;

// Идеальная прямая конвертация координат
void WoWToRecast(const Vector3& wow, float* recast) {
    recast[0] = CENTER_POINT - wow.x;
    recast[1] = wow.z;
    recast[2] = CENTER_POINT - wow.y;
}

void RecastToWoW(const float* recast, Vector3& wow) {
    wow.x = CENTER_POINT - recast[0];
    wow.z = recast[1];
    wow.y = CENTER_POINT - recast[2];
}

void InitNavMesh() {
    g_NavMesh = dtAllocNavMesh();
    g_NavQuery = dtAllocNavMeshQuery();
    
    dtNavMeshParams params;
    memset(&params, 0, sizeof(params));
    
    params.orig[0] = 0.0f;
    params.orig[1] = 0.0f;
    params.orig[2] = 0.0f;
    
    params.tileWidth = TILE_SIZE;
    params.tileHeight = TILE_SIZE;
    params.maxTiles = 16384;     
    params.maxPolys = 1 << 22;   
    
    g_NavMesh->init(&params);
    g_NavQuery->init(g_NavMesh, 2048);
    
    g_Filter.setIncludeFlags(0xFFFF);
    g_Filter.setExcludeFlags(0);
    std::cout << "[+] Detour NavMesh Engine Initialized (Direct Mapping)!\n";
}

void GetGridCoordinates(float x, float y, int& gridX, int& gridY) {
    gridX = (int)(32.0f - (x / TILE_SIZE));
    gridY = (int)(32.0f - (y / TILE_SIZE));
}

bool LoadTile(int mapId, int gridX, int gridY) {
    // Прямая загрузка тайла без смещений
    if (g_NavMesh->getTileAt(gridX, gridY, 0)) return true; 

    char filename[512];
    sprintf_s(filename, "E:\\Cheats\\WoW Inject\\mmaps\\%03d%02d%02d.mmtile", mapId, gridX, gridY);

    if (std::find(g_LoadedTiles.begin(), g_LoadedTiles.end(), filename) != g_LoadedTiles.end()) {
        return true;
    }

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
    
    // Огромный радиус поиска, чтобы 100% найти пол
    float extents[3] = { 50.0f, 100.0f, 50.0f };

    dtPolyRef startRef = 0, endRef = 0;
    g_NavQuery->findNearestPoly(startPos, extents, &g_Filter, &startRef, 0);
    g_NavQuery->findNearestPoly(endPos, extents, &g_Filter, &endRef, 0);

    if (!startRef || !endRef) {
        if (!startRef) std::cout << "[-] WARNING: Could not find NavMesh polygon for START.\n";
        if (!endRef) std::cout << "[-] WARNING: Could not find NavMesh polygon for END.\n";
        std::cout << "[-] Using straight line.\n";
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
    std::cout << "--- WoW NavMesh Server (Direct Mapping) ---\n";
    InitNavMesh();

    SECURITY_DESCRIPTOR sd;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = &sd;
    sa.bInheritHandle = FALSE;

    while (true) {
        HANDLE hPipe = CreateNamedPipeA(
            "\\\\.\\pipe\\WoWNavMeshPipe", PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 1024 * 16, 1024 * 16, 0, &sa);

        if (hPipe != INVALID_HANDLE_VALUE) {
            if (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
                PathRequest req;
                DWORD bytesRead;
                if (ReadFile(hPipe, &req, sizeof(PathRequest), &bytesRead, NULL)) {
                    std::cout << "[+] Request: " << req.start.x << "," << req.start.y << "," << req.start.z 
                              << " -> " << req.end.x << "," << req.end.y << "," << req.end.z << "\n";
                    
                    std::vector<Vector3> path = CalculatePath(req.start, req.end);
                    
                    DWORD bytesWritten;
                    int count = path.size();
                    WriteFile(hPipe, &count, sizeof(int), &bytesWritten, NULL);
                    WriteFile(hPipe, path.data(), count * sizeof(Vector3), &bytesWritten, NULL);
                }
            }
            FlushFileBuffers(hPipe);
            DisconnectNamedPipe(hPipe);
        } else {
            Sleep(1000);
        }
    }
    return 0;
}
