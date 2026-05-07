#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>

// Структура для передачи данных в Shellcode внутри WoW
struct MANUAL_MAPPING_DATA {
    typedef HMODULE(WINAPI* pLoadLibraryA)(LPCSTR);
    typedef FARPROC(WINAPI* pGetProcAddress)(HMODULE, LPCSTR);

    pLoadLibraryA fnLoadLibraryA;
    pGetProcAddress fnGetProcAddress;
    BYTE* pbase;
};

// Этот код будет выполнен ПРЯМО В ПАМЯТИ WoW. 
// Здесь нельзя использовать глобальные переменные или прямые вызовы функций API.
void __stdcall LibraryLoader(MANUAL_MAPPING_DATA* pData) {
    if (!pData) return;

    BYTE* pBase = pData->pbase;
    auto* pOpt = &reinterpret_cast<IMAGE_NT_HEADERS*>(pBase + reinterpret_cast<IMAGE_DOS_HEADER*>(pBase)->e_lfanew)->OptionalHeader;

    auto _LoadLibraryA = pData->fnLoadLibraryA;
    auto _GetProcAddress = pData->fnGetProcAddress;

    // Релокация (подстройка адресов)
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

    // Импорты (подключение системных DLL)
    auto* pImportDir = &pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (pImportDir->Size) {
        auto* pImportDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(pBase + pImportDir->VirtualAddress);
        while (pImportDesc->Name) {
            HMODULE hMod = _LoadLibraryA(reinterpret_cast<char*>(pBase + pImportDesc->Name));
            auto* pThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImportDesc->FirstThunk);
            auto* pIAT = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImportDesc->FirstThunk);
            if (pImportDesc->OriginalFirstThunk)
                pThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImportDesc->OriginalFirstThunk);

            while (pThunk->u1.AddressOfData) {
                if (IMAGE_SNAP_BY_ORDINAL(pThunk->u1.Ordinal))
                    pIAT->u1.Function = reinterpret_cast<DWORD>(_GetProcAddress(hMod, reinterpret_cast<char*>(pThunk->u1.Ordinal & 0xFFFF)));
                else {
                    auto* pImportData = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(pBase + pThunk->u1.AddressOfData);
                    pIAT->u1.Function = reinterpret_cast<DWORD>(_GetProcAddress(hMod, pImportData->Name));
                }
                ++pThunk;
                ++pIAT;
            }
            ++pImportDesc;
        }
    }

    // Запуск DllMain
    if (pOpt->AddressOfEntryPoint) {
        auto _DllMain = reinterpret_cast<BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID)>(pBase + pOpt->AddressOfEntryPoint);
        _DllMain(reinterpret_cast<HINSTANCE>(pBase), DLL_PROCESS_ATTACH, nullptr);
    }
}
