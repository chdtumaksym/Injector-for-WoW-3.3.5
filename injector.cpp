#include <windows.h>
#include <iostream>
#include <fstream>
#include <tlhelp32.h>
#include <vector>

// Структура данных для Shellcode, который выполнится внутри WoW
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

// Shellcode: этот код копируется в WoW и настраивает DLL "изнутри"
void __stdcall ShellCode(MANUAL_MAPPING_DATA* pData) {
    if (!pData) return;
    BYTE* pBase = pData->pbase;
    auto* pOpt = &reinterpret_cast<IMAGE_NT_HEADERS*>(pBase + reinterpret_cast<IMAGE_DOS_HEADER*>(pBase)->e_lfanew)->OptionalHeader;
    auto _LoadLibraryA = pData->fnLoadLibraryA;
    auto _GetProcAddress = pData->fnGetProcAddress;

    // 1. Релокация (подстройка адресов)
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

    // 2. Импорты (подключение системных функций)
    auto* pImportDir = &pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (pImportDir->Size) {
        auto* pImportDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(pBase + pImportDir->VirtualAddress);
        while (pImportDesc->Name) {
            HMODULE hMod = _LoadLibraryA(reinterpret_cast<char*>(pBase + pImportDesc->Name));
            auto* pThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImportDesc->FirstThunk);
            auto* pIAT = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImportDesc->FirstThunk);
            if (pImportDesc->OriginalFirstThunk) pThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImportDesc->OriginalFirstThunk);
            while (pThunk->u1.AddressOfData) {
                if (IMAGE_SNAP_BY_ORDINAL(pThunk->u1.Ordinal)) pIAT->u1.Function = (DWORD)_GetProcAddress(hMod, (char*)(pThunk->u1.Ordinal & 0xFFFF));
                else pIAT->u1.Function = (DWORD)_GetProcAddress(hMod, ((IMAGE_IMPORT_BY_NAME*)(pBase + pThunk->u1.AddressOfData))->Name);
                pThunk++; pIAT++;
            }
            pImportDesc++;
        }
    }

    // 3. Запуск DllMain
    if (pOpt->AddressOfEntryPoint) {
        auto _DllMain = reinterpret_cast<BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID)>(pBase + pOpt->AddressOfEntryPoint);
        _DllMain((HINSTANCE)pBase, DLL_PROCESS_ATTACH, nullptr);
    }
}

// Заглушка для определения размера ShellCode
void __stdcall ShellCodeEnd() {}

int main() {
    const char* dllFile = "bot_payload.dll";
    const char* procName = "WoW.exe";

    DWORD pid = GetProcessId(procName);
    if (!pid) return 1;

    std::ifstream file(dllFile, std::ios::binary | std::ios::ate);
    auto fileSize = file.tellg();
    std::vector<BYTE> buffer(fileSize);
    file.seekg(0, std::ios::beg);
    file.read((char*)buffer.data(), fileSize);
    file.close();

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    auto* pOldHeader = reinterpret_cast<IMAGE_NT_HEADERS*>(buffer.data() + reinterpret_cast<IMAGE_DOS_HEADER*>(buffer.data())->e_lfanew);
    
    // Выделяем память под DLL в WoW
    BYTE* pTargetBase = (BYTE*)VirtualAllocEx(hProc, nullptr, pOldHeader->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    
    // Копируем заголовки и секции
    WriteProcessMemory(hProc, pTargetBase, buffer.data(), pOldHeader->OptionalHeader.SizeOfHeaders, nullptr);
    auto* pSectionHeader = IMAGE_FIRST_SECTION(pOldHeader);
    for (UINT i = 0; i != pOldHeader->FileHeader.NumberOfSections; ++i, ++pSectionHeader) {
        WriteProcessMemory(hProc, pTargetBase + pSectionHeader->VirtualAddress, buffer.data() + pSectionHeader->PointerToRawData, pSectionHeader->SizeOfRawData, nullptr);
    }

    // Подготовка данных для Shellcode
    MANUAL_MAPPING_DATA data;
    data.fnLoadLibraryA = LoadLibraryA;
    data.fnGetProcAddress = GetProcAddress;
    data.pbase = pTargetBase;

    BYTE* pDataLoc = (BYTE*)VirtualAllocEx(hProc, nullptr, sizeof(MANUAL_MAPPING_DATA), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(hProc, pDataLoc, &data, sizeof(MANUAL_MAPPING_DATA), nullptr);

    // Копируем сам Shellcode и запускаем его
    DWORD shellSize = (DWORD)ShellCodeEnd - (DWORD)ShellCode;
    BYTE* pCodeLoc = (BYTE*)VirtualAllocEx(hProc, nullptr, shellSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    WriteProcessMemory(hProc, pCodeLoc, ShellCode, shellSize, nullptr);

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, (LPTHREAD_START_ROUTINE)pCodeLoc, pDataLoc, 0, nullptr);
    
    if (hThread) {
        std::cout << "[+] Stealth Inject Successful!" << std::endl;
        CloseHandle(hThread);
    }

    CloseHandle(hProc);
    return 0;
}
