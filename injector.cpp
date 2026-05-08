#include <windows.h>
#include <iostream>
#include <fstream>
#include <tlhelp32.h>
#include <vector>

struct MANUAL_MAPPING_DATA {
    typedef HMODULE(WINAPI* pLoadLibraryA)(LPCSTR);
    typedef FARPROC(WINAPI* pGetProcAddress)(HMODULE, LPCSTR);
    pLoadLibraryA fnLoadLibraryA;
    pGetProcAddress fnGetProcAddress;
    BYTE* pbase;
};

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

#pragma optimize("", off)
void __stdcall ShellCode(MANUAL_MAPPING_DATA* pData) {
    if (!pData) return;
    BYTE* pBase = pData->pbase;
    auto* pDos = reinterpret_cast<IMAGE_DOS_HEADER*>(pBase);
    auto* pNt = reinterpret_cast<IMAGE_NT_HEADERS*>(pBase + pDos->e_lfanew);
    auto* pOpt = &pNt->OptionalHeader;

    auto _LoadLibraryA = pData->fnLoadLibraryA;
    auto _GetProcAddress = pData->fnGetProcAddress;

    // Релокации
    auto* pRelocDir = &pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (pRelocDir->Size) {
        auto* pReloc = reinterpret_cast<IMAGE_BASE_RELOCATION*>(pBase + pRelocDir->VirtualAddress);
        while (pReloc->VirtualAddress) {
            UINT entries = (pReloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            WORD* pInfo = reinterpret_cast<WORD*>(pReloc + 1);
            for (UINT i = 0; i < entries; ++i, ++pInfo) {
                if ((*pInfo >> 12) == IMAGE_REL_BASED_HIGHLOW) {
                    DWORD* pPatch = reinterpret_cast<DWORD*>(pBase + pReloc->VirtualAddress + ((*pInfo) & 0xFFF));
                    *pPatch += reinterpret_cast<DWORD>(pBase) - pOpt->ImageBase;
                }
            }
            pReloc = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<BYTE*>(pReloc) + pReloc->SizeOfBlock);
        }
    }

    // Импорты
    auto* pImportDir = &pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (pImportDir->Size) {
        auto* pImportDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(pBase + pImportDir->VirtualAddress);
        while (pImportDesc->Name) {
            HMODULE hMod = _LoadLibraryA(reinterpret_cast<char*>(pBase + pImportDesc->Name));
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

    if (pOpt->AddressOfEntryPoint) {
        auto _DllMain = reinterpret_cast<BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID)>(pBase + pOpt->AddressOfEntryPoint);
        _DllMain((HINSTANCE)pBase, DLL_PROCESS_ATTACH, nullptr);
    }
}
void __stdcall ShellCodeEnd() {}
#pragma optimize("", on)

int main() {
    const char* dllName = "bot_payload.dll";
    DWORD pid = GetProcessId("WoW.exe");
    if (!pid) return 1;

    std::ifstream f(dllName, std::ios::binary | std::ios::ate);
    auto sz = f.tellg();
    std::vector<BYTE> buf(sz);
    f.seekg(0, std::ios::beg);
    f.read((char*)buf.data(), sz);
    f.close();

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    auto* pNt = reinterpret_cast<IMAGE_NT_HEADERS*>(buf.data() + reinterpret_cast<IMAGE_DOS_HEADER*>(buf.data())->e_lfanew);
    BYTE* pTarget = (BYTE*)VirtualAllocEx(hProc, nullptr, pNt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    WriteProcessMemory(hProc, pTarget, buf.data(), pNt->OptionalHeader.SizeOfHeaders, nullptr);
    auto* pSec = IMAGE_FIRST_SECTION(pNt);
    for (int i = 0; i < pNt->FileHeader.NumberOfSections; i++, pSec++) {
        WriteProcessMemory(hProc, pTarget + pSec->VirtualAddress, buf.data() + pSec->PointerToRawData, pSec->SizeOfRawData, nullptr);
    }

    MANUAL_MAPPING_DATA data;
    // Берем адреса из нашей памяти, так как kernel32 мапится по одним адресам почти всегда, 
    // но для 100% надежности в Manual Map лучше резолвить их через перебор модулей процесса.
    data.fnLoadLibraryA = LoadLibraryA;
    data.fnGetProcAddress = GetProcAddress;
    data.pbase = pTarget;

    BYTE* pRemData = (BYTE*)VirtualAllocEx(hProc, nullptr, sizeof(data), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(hProc, pRemData, &data, sizeof(data), nullptr);

    // Копируем 2КБ шеллкода для гарантии
    BYTE* pRemCode = (BYTE*)VirtualAllocEx(hProc, nullptr, 2048, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    WriteProcessMemory(hProc, pRemCode, ShellCode, 2048, nullptr);

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, (LPTHREAD_START_ROUTINE)pRemCode, pRemData, 0, nullptr);
    if (hThread) {
        std::cout << "[+] Инжект выполнен. Жди MessageBox в игре." << std::endl;
        CloseHandle(hThread);
    }

    CloseHandle(hProc);
    return 0;
}
