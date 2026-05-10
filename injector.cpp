#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <windows.h>
#include <tlhelp32.h>
#include <commdlg.h> 
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>

#define WM_TOGGLE_BOT (WM_APP + 1337)
#define WM_EJECT_BOT  (WM_APP + 1338)

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
                if (!_stricmp(pe.szExeFile, procName)) { procId = pe.th32ProcessID; break; }
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
    auto* pNt = reinterpret_cast<IMAGE_NT_HEADERS*>(pBase + reinterpret_cast<IMAGE_DOS_HEADER*>(pBase)->e_lfanew);
    auto* pOpt = &pNt->OptionalHeader;

    auto* pRelocDir = &pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (pRelocDir->Size) {
        auto* pReloc = reinterpret_cast<IMAGE_BASE_RELOCATION*>(pBase + pRelocDir->VirtualAddress);
        while (pReloc->VirtualAddress) {
            UINT entries = (pReloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            WORD* pInfo = reinterpret_cast<WORD*>(pReloc + 1);
            for (UINT i = 0; i < entries; ++i, ++pInfo) {
                if ((*pInfo >> 12) == IMAGE_REL_BASED_HIGHLOW) {
                    DWORD* pPatch = reinterpret_cast<DWORD*>(pBase + pReloc->VirtualAddress + ((*pInfo) & 0xFFF));
                    *pPatch += (DWORD)pBase - pOpt->ImageBase;
                }
            }
            pReloc = reinterpret_cast<IMAGE_BASE_RELOCATION*>((BYTE*)pReloc + pReloc->SizeOfBlock);
        }
    }

    auto* pImportDir = &pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (pImportDir->Size) {
        auto* pImportDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(pBase + pImportDir->VirtualAddress);
        while (pImportDesc->Name) {
            HMODULE hMod = pData->fnLoadLibraryA((char*)(pBase + pImportDesc->Name));
            auto* pThunk = (IMAGE_THUNK_DATA*)(pBase + pImportDesc->FirstThunk);
            auto* pIAT = (IMAGE_THUNK_DATA*)(pBase + pImportDesc->FirstThunk);
            if (pImportDesc->OriginalFirstThunk) pThunk = (IMAGE_THUNK_DATA*)(pBase + pImportDesc->OriginalFirstThunk);
            while (pThunk->u1.AddressOfData) {
                if (IMAGE_SNAP_BY_ORDINAL(pThunk->u1.Ordinal)) pIAT->u1.Function = (DWORD)pData->fnGetProcAddress(hMod, (char*)(pThunk->u1.Ordinal & 0xFFFF));
                else pIAT->u1.Function = (DWORD)pData->fnGetProcAddress(hMod, ((IMAGE_IMPORT_BY_NAME*)(pBase + pThunk->u1.AddressOfData))->Name);
                pThunk++; pIAT++;
            }
            pImportDesc++;
        }
    }

    if (pOpt->AddressOfEntryPoint) {
        auto _DllMain = (BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID))(pBase + pOpt->AddressOfEntryPoint);
        _DllMain((HINSTANCE)pBase, DLL_PROCESS_ATTACH, nullptr);
    }
}
void __stdcall ShellCodeEnd() {}
#pragma optimize("", on)

bool ManualMapInject(DWORD pid, const char* dllPath, std::string& guiLog) {
    std::ifstream f(dllPath, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        guiLog += "[-] Error: bot_payload.dll not found in folder!\n";
        return false;
    }
    auto sz = f.tellg();
    std::vector<BYTE> buf(sz);
    f.seekg(0, std::ios::beg);
    f.read((char*)buf.data(), sz);
    f.close();

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) return false;

    std::remove("C:\\WoWBot\\status.txt");

    auto* pNt = reinterpret_cast<IMAGE_NT_HEADERS*>(buf.data() + reinterpret_cast<IMAGE_DOS_HEADER*>(buf.data())->e_lfanew);
    BYTE* pTarget = (BYTE*)VirtualAllocEx(hProc, nullptr, pNt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (!pTarget) { CloseHandle(hProc); return false; }

    WriteProcessMemory(hProc, pTarget, buf.data(), pNt->OptionalHeader.SizeOfHeaders, nullptr);
    auto* pSec = IMAGE_FIRST_SECTION(pNt);
    for (int i = 0; i < pNt->FileHeader.NumberOfSections; i++, pSec++) {
        WriteProcessMemory(hProc, pTarget + pSec->VirtualAddress, buf.data() + pSec->PointerToRawData, pSec->SizeOfRawData, nullptr);
    }

    MANUAL_MAPPING_DATA data;
    data.fnLoadLibraryA = LoadLibraryA;
    data.fnGetProcAddress = GetProcAddress;
    data.pbase = pTarget;

    BYTE* pRemData = (BYTE*)VirtualAllocEx(hProc, nullptr, sizeof(data), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(hProc, pRemData, &data, sizeof(data), nullptr);

    BYTE* pRemCode = (BYTE*)VirtualAllocEx(hProc, nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    WriteProcessMemory(hProc, pRemCode, ShellCode, 4096, nullptr);

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, (LPTHREAD_START_ROUTINE)pRemCode, pRemData, 0, nullptr);
    if (hThread) {
        guiLog += "[+] Bot Successfully Injected!\n";
        CloseHandle(hThread);
        CloseHandle(hProc);
        return true;
    }
    
    CloseHandle(hProc);
    return false;
}

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;

    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
    return true;
}

void CleanupDeviceD3D() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            ID3D11Texture2D* pBackBuffer;
            g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
            g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
            pBackBuffer->Release();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void SetupCS2Style() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.24f, 0.26f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.85f, 0.50f, 0.10f, 1.00f); 
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.95f, 0.60f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.75f, 0.40f, 0.05f, 1.00f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.85f, 0.50f, 0.10f, 0.50f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.85f, 0.50f, 0.10f, 0.80f);
}

std::string ReadBotLog() {
    std::ifstream logFile("C:\\WoWBot\\bot_log.txt");
    if (!logFile.is_open()) return "Waiting for bot to start...\n";
    std::string content((std::istreambuf_iterator<char>(logFile)), std::istreambuf_iterator<char>());
    return content;
}

void SaveLogToFile(HWND hwndOwner, const std::string& logData) {
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwndOwner;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = "txt";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameA(&ofn) == TRUE) {
        std::ofstream out(ofn.lpstrFile);
        if (out.is_open()) out << logData;
    }
}

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"WoWBotClass", nullptr };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"WoW 3.3.5 Bot Launcher", WS_OVERLAPPEDWINDOW, 100, 100, 700, 550, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); return 1; }
    ShowWindow(hwnd, SW_SHOWDEFAULT); UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    SetupCS2Style();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    std::vector<std::string> profiles;
    int selectedProfile = 0;
    std::string guiLog = "";

    char currentDir[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, currentDir);
    std::string profilesDir = std::string(currentDir) + "\\Profiles";
    
    if (std::filesystem::exists(profilesDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(profilesDir)) {
            if (entry.path().extension() == ".txt") profiles.push_back(entry.path().filename().string());
        }
    }

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // Читаем статус через Named Mutex (Истинная проверка наличия DLL в памяти)
        int botStatus = -1; // -1: Not injected, 0: Paused, 1: Active
        HWND hwndWow = FindWindowA(NULL, "World of Warcraft");
        if (hwndWow) {
            HANDLE hMutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, "WoWBot_Active_Mutex");
            if (hMutex) {
                CloseHandle(hMutex);
                std::ifstream stat("C:\\WoWBot\\status.txt");
                if (stat.is_open()) {
                    stat >> botStatus;
                    stat.close();
                }
            } else {
                std::remove("C:\\WoWBot\\status.txt");
            }
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

        ImGui::TextColored(ImVec4(0.85f, 0.50f, 0.10f, 1.0f), "WoW 3.3.5 Bot - CS2 Edition");
        ImGui::SameLine(ImGui::GetWindowWidth() - 150);
        
        ImGui::Text("Status: "); ImGui::SameLine();
        if (botStatus == 1) ImGui::TextColored(ImVec4(0, 1, 0, 1), "ACTIVE");
        else if (botStatus == 0) ImGui::TextColored(ImVec4(1, 1, 0, 1), "PAUSED");
        else ImGui::TextColored(ImVec4(1, 0, 0, 1), "NOT INJECTED");

        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Select Profile:");
        if (ImGui::BeginCombo("##profilecombo", profiles.empty() ? "No profiles found" : profiles[selectedProfile].c_str())) {
            for (int i = 0; i < profiles.size(); i++) {
                bool is_selected = (selectedProfile == i);
                if (ImGui::Selectable(profiles[i].c_str(), is_selected)) selectedProfile = i;
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing(); ImGui::Spacing();

        if (botStatus == -1) {
            if (ImGui::Button("INJECT BOT", ImVec2(-1, 40))) {
                if (!profiles.empty()) {
                    CreateDirectoryA("C:\\WoWBot", NULL);
                    std::string fullPath = profilesDir + "\\" + profiles[selectedProfile];
                    std::ofstream settingsFile("C:\\WoWBot\\settings.ini");
                    settingsFile << fullPath;
                    settingsFile.close();

                    DWORD procId = GetProcessId("Wow.exe");
                    if (!procId) guiLog += "[-] Error: Wow.exe not found!\n";
                    else {
                        std::string dllPath = std::string(currentDir) + "\\bot_payload.dll";
                        ManualMapInject(procId, dllPath.c_str(), guiLog);
                    }
                }
            }
        } else {
            if (ImGui::Button(botStatus == 1 ? "PAUSE BOT (INSERT)" : "START BOT (INSERT)", ImVec2(ImGui::GetWindowWidth() / 2 - 12, 40))) {
                PostMessageA(hwndWow, WM_TOGGLE_BOT, 0, 0);
            }
            ImGui::SameLine();
            if (ImGui::Button("EJECT BOT (END)", ImVec2(ImGui::GetWindowWidth() / 2 - 12, 40))) {
                PostMessageA(hwndWow, WM_EJECT_BOT, 0, 0);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();

        std::string liveLog = ReadBotLog();

        if (ImGui::Button("Copy to Clipboard")) ImGui::SetClipboardText(liveLog.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Save to File...")) SaveLogToFile(hwnd, liveLog);
        ImGui::SameLine();
        if (ImGui::Button("Clear Progress")) {
            std::remove("C:\\WoWBot\\progress.txt");
            guiLog += "[!] Progress file cleared.\n";
        }

        ImGui::Spacing();
        
        ImGui::BeginChild("LogRegion", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        ImGui::TextUnformatted(liveLog.c_str());
        
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::End();
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
