#include <windows.h>
#include <iostream>
#include <fstream>
#include <tlhelp32.h>
#include <vector>

// Структура данных для шеллкода
struct MANUAL_MAPPING_DATA {
    typedef HMODULE(WINAPI* pLoadLibraryA)(LPCSTR);
    typedef FARPROC(WINAPI* pGetProcAddress)(HMODULE, LPCSTR);
    pLoadLibraryA fnLoadLibraryA;
    pGetProcAddress fnGetProcAddress;
    BYTE* pbase;
};

// Функция поиска ID процесса
DWORD GetProcessId(const char* procName) {
    DWORD procId = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
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

// Запрещаем оптимизацию для шеллкода, чтобы функции не перемешивались
#pragma optimize("", off)
void __stdcall ShellCode(MANUAL_MAPPING_DATA* pData) {
    if (!pData) return;
    BYTE* pBase = pData->pbase;
    auto* pOldHeader = reinterpret_cast<IMAGE_NT_HEADERS*>(pBase + reinterpret_cast<IMAGE_DOS_HEADER*>(pBase)->e_lfanew);
    auto* pOpt = &pOldHeader->OptionalHeader;

    auto _LoadLibraryA = pData->fnLoadLibraryA;
    auto _GetProcAddress = pData->fnGetProcAddress;

    // 1. Релокации
    auto* pRelocDir = &pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (pRelocDir->Size) {
        auto* pReloc = reinterpret_cast<IMAGE_BASE_RELOCATION*>(pBase + pRelocDir->VirtualAddress);
        while (pReloc->VirtualAddress) {
            UINT AmountOfEntries = (pReloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            WORD* pRelativeInfo = reinterpret_cast<WORD*>(pReloc + 1);
            for (UINT i = 0; i != AmountOfEntries; ++i, ++pRelativeInfo) {
                if ((*pRelativeInfo >> 12) == IMAGE_REL_BASED_HIGHLOW) {
                    DWORD* pPatch = reinterpret_cast<DWORD*>(pBase + pReloc->VirtualAddress + ((*pRelativeInfo) & 0xFFF));
                    *pPatch += reinterpret_cast<DWORD>(pBase) - pOpt->ImageBase;
                }
            }
            pReloc = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<BYTE*>(pReloc) + pReloc->SizeOfBlock);
        }
    }

    // 2. Импорты
    auto* pImportDir = &pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (pImportDir->Size) {
        auto* pImportDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(pBase + pImportDir->VirtualAddress);
        while (pImportDesc->Name) {
            char* szMod = reinterpret_cast<char*>(pBase + pImportDesc->Name);
            HMODULE hMod = _LoadLibraryA(szMod);
            auto* pThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImportDesc->FirstThunk);
            auto* pIAT = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImportDesc->FirstThunk);
            if (pImportDesc->OriginalFirstThunk) pThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImportDesc->OriginalFirstThunk);

            while (pThunk->u1.AddressOfData) {
                if (IMAGE_SNAP_BY_ORDINAL(pThunk->u1.Ordinal)) {
                    pIAT->u1.Function = (DWORD)_GetProcAddress(hMod, (char*)(pThunk->u1.Ordinal & 0xFFFF));
                } else {
                    auto* pImportName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(pBase + pThunk->u1.AddressOfData);
                    pIAT->u1.Function = (DWORD)_GetProcAddress(hMod, pImportName->Name);
                }
                pThunk++; pIAT++;
            }
            pImportDesc++;
        }
    }

    // 3. Вызов EntryPoint
    if (pOpt->AddressOfEntryPoint) {
        auto _DllMain = reinterpret_cast<BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID)>(pBase + pOpt->AddressOfEntryPoint);
        _DllMain((HINSTANCE)pBase, DLL_PROCESS_ATTACH, nullptr);
    }
}
void __stdcall ShellCodeEnd() {} 
#pragma optimize("", on)

int main() {
    const char* dllPath = "bot_payload.dll";
    const char* procName = "WoW.exe";

    std::cout << "[*] Ищем процесс " << procName << "..." << std::endl;
    DWORD pid = GetProcessId(procName);
    if (!pid) { std::cout << "[-] Процесс не найден!" << std::endl; return 1; }

    std::ifstream file(dllPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) { std::cout << "[-] Не удалось открыть DLL!" << std::endl; return 1; }
    
    std::streamsize size = file.tellg();
    std::vector<BYTE> buffer(size);
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    file.close();

    auto* pDos = reinterpret_cast<IMAGE_DOS_HEADER*>(buffer.data());
    auto* pNt = reinterpret_cast<IMAGE_NT_HEADERS*>(buffer.data() + pDos->e_lfanew);
    
    if (pNt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
        std::cout << "[-] Ошибка: DLL должна быть x86 (32-бит)!" << std::endl;
        return 1;
    }

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) { std::cout << "[-] Ошибка OpenProcess: " << GetLastError() << std::endl; return 1; }

    // Выделяем память под саму DLL
    BYTE* pTargetBase = (BYTE*)VirtualAllocEx(hProc, nullptr, pNt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!pTargetBase) { std::cout << "[-] Ошибка VirtualAllocEx (DLL)" << std::endl; CloseHandle(hProc); return 1; }

    // Копируем заголовки и секции
    WriteProcessMemory(hProc, pTargetBase, buffer.data(), pNt->OptionalHeader.SizeOfHeaders, nullptr);
    auto* pSection = IMAGE_FIRST_SECTION(pNt);
    for (int i = 0; i < pNt->FileHeader.NumberOfSections; i++, pSection++) {
        WriteProcessMemory(hProc, pTargetBase + pSection->VirtualAddress, buffer.data() + pSection->PointerToRawData, pSection->SizeOfRawData, nullptr);
    }

    // Подготовка данных
    MANUAL_MAPPING_DATA data;
    data.fnLoadLibraryA = LoadLibraryA;
    data.fnGetProcAddress = GetProcAddress;
    data.pbase = pTargetBase;

    BYTE* pRemoteData = (BYTE*)VirtualAllocEx(hProc, nullptr, sizeof(MANUAL_MAPPING_DATA), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(hProc, pRemoteData, &data, sizeof(MANUAL_MAPPING_DATA), nullptr);

    // Копируем шеллкод (берем с запасом 1КБ, чтобы точно не обрезать из-за оптимизаций)
    DWORD shellSize = 1024; 
    BYTE* pRemoteCode = (BYTE*)VirtualAllocEx(hProc, nullptr, shellSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    WriteProcessMemory(hProc, pRemoteCode, ShellCode, shellSize, nullptr);

    std::cout << "[*] Запуск удаленного потока..." << std::endl;
    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, (LPTHREAD_START_ROUTINE)pRemoteCode, pRemoteData, 0, nullptr);
    
    if (hThread) {
        std::cout << "[+] Инъекция завершена. Если игра упала — проверь логику DLL." << std::endl;
        CloseHandle(hThread);
    } else {
        std::cout << "[-] Ошибка CreateRemoteThread: " << GetLastError() << std::endl;
    }

    CloseHandle(hProc);
    return 0;
}
