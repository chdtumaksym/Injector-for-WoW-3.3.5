#include <windows.h>
#include <iostream>
#include <vector>

struct Vector3 { float x, y, z; };

struct PathRequest {
    Vector3 start;
    Vector3 end;
};

// Заглушка. Позже здесь будет реальный DetourNavMeshQuery
std::vector<Vector3> CalculatePath(Vector3 start, Vector3 end) {
    std::vector<Vector3> path;
    
    // Разбиваем прямую линию на 3 точки, чтобы проверить связь с DLL
    Vector3 mid1 = { start.x + (end.x - start.x) * 0.33f, start.y + (end.y - start.y) * 0.33f, start.z + (end.z - start.z) * 0.33f };
    Vector3 mid2 = { start.x + (end.x - start.x) * 0.66f, start.y + (end.y - start.y) * 0.66f, start.z + (end.z - start.z) * 0.66f };
    
    path.push_back(mid1);
    path.push_back(mid2);
    path.push_back(end);

    return path;
}

int main() {
    std::cout << "--- WoW NavMesh Server (Phase 1: Bridge) ---\n";
    std::cout << "[+] Waiting for Bot DLL to connect...\n";

    while (true) {
        HANDLE hPipe = CreateNamedPipeA(
            "\\\\.\\pipe\\WoWNavMeshPipe",
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, 1024 * 16, 1024 * 16, 0, NULL);

        if (hPipe != INVALID_HANDLE_VALUE) {
            if (ConnectNamedPipe(hPipe, NULL) != FALSE) {
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
            DisconnectNamedPipe(hPipe);
        }
        CloseHandle(hPipe);
    }
    return 0;
}
