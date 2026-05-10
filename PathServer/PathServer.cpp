#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <iomanip>

// Подключаем Detour
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

struct Vector3 { float x, y, z; };

struct PathRequest {
    Vector3 start;
    Vector3 end;
};

// Заголовок файла .mmtile (Формат TrinityCore/AzerothCore)
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

// Функция перевода координат WoW в координаты сетки (Grid)
void GetGridCoordinates(float x, float y, int& gridX, int& gridY) {
    const float TILE_SIZE = 533.33333f;
    gridX = (int)(32.0f - (x / TILE_SIZE));
    gridY = (int)(32.0f - (y / TILE_SIZE));
}

// Функция загрузки тайла с жесткого диска
bool LoadTileHeader(int mapId, int gridX, int gridY) {
    char filename[256];
    // Формируем имя файла, например: C:\WoWBot\mmaps\0004832.mmtile
    sprintf_s(filename, "C:\\WoWBot\\mmaps\\%03d%02d%02d.mmtile", mapId, gridX, gridY);

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "[-] ERROR: Tile not found: " << filename << "\n";
        return false;
    }

    MmapTileHeader header;
    file.read((char*)&header, sizeof(MmapTileHeader));

    // Проверяем магическое число 'MMAP' (в hex это 0x4D41504D)
    if (header.mmapMagic == 0x4D41504D || header.mmapMagic == 0x50414D4D) {
        std::cout << "[+] SUCCESS: Loaded Tile " << gridX << "_" << gridY << " | Detour Version: " << header.dtVersion << " | Size: " << header.size << " bytes\n";
        return true;
    } else {
        std::cout << "[-] ERROR: Invalid Tile Magic in " << filename << "\n";
        return false;
    }
}

std::vector<Vector3> CalculatePath(Vector3 start, Vector3 end) {
    std::vector<Vector3> path;
    
    // 1. Высчитываем, в каком квадрате мы находимся
    int gridX, gridY;
    GetGridCoordinates(start.x, start.y, gridX, gridY);
    
    // 2. Пытаемся прочитать файл карты для этого квадрата (MapID 0 = Восточные Королевства)
    LoadTileHeader(0, gridX, gridY);

    // Пока отдаем прямую линию, чтобы бот продолжал бегать
    Vector3 mid1 = { start.x + (end.x - start.x) * 0.33f, start.y + (end.y - start.y) * 0.33f, start.z + (end.z - start.z) * 0.33f };
    Vector3 mid2 = { start.x + (end.x - start.x) * 0.66f, start.y + (end.y - start.y) * 0.66f, start.z + (end.z - start.z) * 0.66f };
    
    path.push_back(mid1);
    path.push_back(mid2);
    path.push_back(end);

    return path;
}

int main() {
    std::cout << "--- WoW NavMesh Server (Phase 2: Map Parser) ---\n";
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
                    std::cout << "\n[*] Path requested from: " << req.start.x << ", " << req.start.y << "\n";
                    
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
