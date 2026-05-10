#include <windows.h>
#include <iostream>
#include <vector>
#include <string>

// [!] ПОДКЛЮЧАЕМ БИБЛИОТЕКУ DETOUR [!]
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

struct Vector3 { float x, y, z; };

struct PathRequest {
    Vector3 start;
    Vector3 end;
};

// Глобальные объекты навигации
dtNavMesh* g_NavMesh = nullptr;
dtNavMeshQuery* g_NavQuery = nullptr;
dtQueryFilter g_Filter;

// Инициализация движка Detour
void InitNavMesh() {
    g_NavMesh = dtAllocNavMesh();
    g_NavQuery = dtAllocNavMeshQuery();
    
    // Настраиваем фильтры (какие поверхности бот может пересекать)
    g_Filter.setIncludeFlags(0xFFFF); // Разрешаем ходить везде, где есть сетка
    g_Filter.setExcludeFlags(0);
    
    std::cout << "[+] Detour NavMesh Engine Initialized!\n";
    std::cout << "[!] Waiting for map binary parser...\n";
}

// Функция расчета пути
std::vector<Vector3> CalculatePath(Vector3 start, Vector3 end) {
    std::vector<Vector3> path;
    
    // TODO: Здесь будет реальный поиск пути через g_NavQuery->findPath()
    // Как только мы напишем загрузчик .mmap файлов!
    
    // Пока отдаем прямую линию, чтобы бот не стоял на месте
    Vector3 mid1 = { start.x + (end.x - start.x) * 0.33f, start.y + (end.y - start.y) * 0.33f, start.z + (end.z - start.z) * 0.33f };
    Vector3 mid2 = { start.x + (end.x - start.x) * 0.66f, start.y + (end.y - start.y) * 0.66f, start.z + (end.z - start.z) * 0.66f };
    
    path.push_back(mid1);
    path.push_back(mid2);
    path.push_back(end);

    return path;
}

int main() {
    std::cout << "--- WoW NavMesh Server (Phase 2: Detour Integration) ---\n";
    
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
                    std::cout << "[*] Path requested to: " << req.end.x << ", " << req.end.y << "\n";
                    
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
