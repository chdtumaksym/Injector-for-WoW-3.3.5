#include <windows.h>
#include <iostream>
#include <tlhelp32.h>
#include <fstream>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

// Структура для передачи данных внутрь WoW
struct MANUAL_MAPPING_DATA {
    typedef HMODULE(WINAPI* pLoadLibraryA)(LPCSTR);
    typedef FARPROC(WINAPI* pGetProcAddress)(HMODULE, LPCSTR);
    pLoadLibraryA fnLoadLibraryA;
    pGetProcAddress fnGetProcAddress;
    BYTE* pbase;
};

// Функция поиска ID процесса игры
DWORD GetProcessId(const char* procName) {
    DWORD procId = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);
        if (Process32First(hSnap, &pe)) {
            do {
                if (!_stricmp(pe.szExeFile, procName)) {
                    procId = pe.th32ProcessID;
                    break;
                }
            } while (Process32Next(hSnap, &pe));
        }
    }
    CloseHandle(hSnap);
    return procId;
}

int main() {
    const char* targetProcess = "WoW.exe";
    const char* dllFile = "bot_payload.dll";

    std::cout << "--- Professional Manual Map Injector ---" << std::endl;

    DWORD processId = GetProcessId(targetProcess);
    if (!processId) {
        std::cerr << "[-] Игра WoW.exe не запущена!" << std::endl;
        return 1;
    }

    // Читаем нашу DLL в память инжектора
    std::ifstream file(dllFile, std::ios::binary | std::ios::ate);
    if (file.fail()) {
        std::cerr << "[-] Файл " << dllFile << " не найден!" << std::endl;
        return 1;
    }
    auto fileSize = file.tellg();
    std::vector<BYTE> buffer(fileSize);
    file.seekg(0, std::ios::beg);
    file.read((char*)buffer.data(), fileSize);
    file.close();

    // Здесь должна быть логика Manual Mapping (релокации, импорты)
    // Для первого теста используем стандартный LoadLibrary, 
    // чтобы убедиться, что компиляция и запуск работают.
    
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (!hProc) {
        std::cerr << "[-] Не удалось открыть процесс WoW." << std::endl;
        return 1;
    }

    LPVOID loc = VirtualAllocEx(hProc, 0, MAX_PATH, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    char fullPath[MAX_PATH];
    GetFullPathNameA(dllFile, MAX_PATH, fullPath, NULL);
    WriteProcessMemory(hProc, loc, fullPath, strlen(fullPath) + 1, 0);
    
    HANDLE hThread = CreateRemoteThread(hProc, 0, 0, (LPTHREAD_START_ROUTINE)LoadLibraryA, loc, 0, 0);
    
    if (hThread) {
        std::cout << "[+] Успех! Бот внедрен." << std::endl;
        CloseHandle(hThread);
    }

    CloseHandle(hProc);
    return 0;
}
