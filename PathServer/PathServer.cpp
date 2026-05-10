#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <iomanip>

struct Vector3 { float x, y, z; };

struct PathRequest {
    Vector3 start;
    Vector3 end;
};

void GetGridCoordinates(float x, float y, int& gridX, int& gridY) {
    const float TILE_SIZE = 533.33333f;
    gridX = (int)(32.0f - (x / TILE_SIZE));
    gridY = (int)(32.0f - (y / TILE_SIZE));
}

bool LoadTileHeader(int mapId, int gridX, int gridY) {
    char filename[512];
    sprintf_s(filename, "E:\\Cheats\\WoW Inject\\mmaps\\%03d%02d%02d.mmtile", mapId, gridX, gridY);

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "[-] ERROR: Tile not found: " << filename << "\n";
        return false;
    }

    uint32_t magic = 0;
    file.read((char*)&magic, sizeof(uint32_t));

    std::cout << "[*] Checking Tile " << gridX << "_" << gridY << " | Magic: 0x" << std::hex << magic << std::dec << "\n";

    if (magic == 0x4D4D4150 || magic == 0x50414D4D) {
        std::cout << "[+] SUCCESS: Detected TrinityCore/AzerothCore Format!\n";
        return true;
    } else if (magic == 0x444E4156 || magic == 0x56414E44) {
        std::cout << "[+] SUCCESS: Detected RAW Detour Format!\n";
        return true;
    } else {
        std::cout << "[-] ERROR: Unknown Tile Format! Please send this Magic code to the developer.\n";
        return false;
    }
}

std::vector<Vector3> CalculatePath(Vector3 start, Vector3 end) {
    std::vector<Vector3> path;
    int gridX, gridY;
    GetGridCoordinates(start.x, start.y, gridX, gridY);
    LoadTileHeader(0, gridX, gridY);

    Vector3 mid1 = { start.x + (end.x - start.x) * 0.33f, start.y + (end.y - start.y) * 0.33f, start.z + (end.z - start.z) * 0.33f };
    Vector3 mid2 = { start.x + (end.x - start.x) * 0.66f, start.y + (end.y - start.y) * 0.66f, start.z + (end.z - start.z) * 0.66f };
    
    path.push_back(mid1);
    path.push_back(mid2);
    path.push_back(end);
    return path;
}

int main() {
    std::cout << "--- WoW NavMesh Server (Phase 2: Magic Detector) ---\n";
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
