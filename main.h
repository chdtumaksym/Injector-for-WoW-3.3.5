#pragma once
#include <windows.h>

struct LOADER_DATA {
    typedef HMODULE(WINAPI* tLoadLibraryA)(LPCSTR);
    typedef FARPROC(WINAPI* tGetProcAddress)(HMODULE, LPCSTR);

    tLoadLibraryA fnLoadLibraryA;
    tGetProcAddress fnGetProcAddress;
    BYTE* pBase;
};
