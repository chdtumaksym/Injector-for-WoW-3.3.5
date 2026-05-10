#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>

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

// Инициализация движка Detour
void InitNavMesh() {
    g_NavMesh = dtAllocNavMesh();
    g_NavQuery = dtAllocNavMeshQuery();
    
    std::cout << "[+] Detour NavMesh Engine Initialized!\n";
    std::cout << "[!] Note: Map loading logic will be added in Phase 2.\n";
}

// Функция расчета пути (Пока что возвращает прямую линию, так как карты еще не загружены)
std::vector<Vector3> CalculatePath(Vector3 start, Vector3 end) {
    std::vector<Vector3> path;
    
    // В следующем этапе здесь будет вызов g_NavQuery->findPath(...)
    // А пока просто отдаем прямую линию, чтобы не сломать бота
    Vector3 mid1 = { start.x + (end.x - start.x) * 0.33f, start.y + (end.y - start.y) * 0.33f, start.z + (end.z - start.z) * 0.33f };
    Vector3 mid2 = { start.x + (end.x - start.x) * 0.66f, start.y + (end.y - start.y) * 0.66f, start.z + (end.z - start.z) * 0.66f };
    
    path.push_back(mid1);
    path.push_back(mid2);
    path.push_back(end);

    return path;
}

int main() {
    std::cout << "--- WoW NavMesh Server (Phase 2: Detour Integration) ---\n";
    
    InitNavMesh(); // Запускаем движок навигации

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
