name: Build-Imba-Bot
on: [push]

jobs:
  build:
    runs-on: windows-latest
    steps:
      - name: Checkout code
        uses: actions/checkout@v4

      - name: Initialize MSVC x86
        uses: ilammy/msvc-dev-cmd@v1
        with:
          arch: x86

      - name: Download ImGui
        run: |
          git clone --depth 1 -b docking https://github.com/ocornut/imgui.git imgui_src

      # [!] КАЧАЕМ ПРАВИЛЬНЫЙ 64-БИТНЫЙ DETOUR ОТ TRINITYCORE 3.3.5 [!]
      - name: Download TrinityCore Detour
        run: |
          git clone --depth 1 -b 3.3.5 https://github.com/TrinityCore/TrinityCore.git tc_src

      - name: Compile Injector, Payload and PathServer
        run: |
          cl.exe /std:c++17 /O2 /LD /EHsc payload.cpp /link /OUT:bot_payload.dll user32.lib
          
          cl.exe /std:c++17 /O2 /EHsc injector.cpp imgui_src/imgui.cpp imgui_src/imgui_draw.cpp imgui_src/imgui_tables.cpp imgui_src/imgui_widgets.cpp imgui_src/backends/imgui_impl_win32.cpp imgui_src/backends/imgui_impl_dx11.cpp /I imgui_src /I imgui_src/backends /link /OUT:injector.exe d3d11.lib d3dcompiler.lib user32.lib advapi32.lib shell32.lib comdlg32.lib
          
          # Компилируем сервер с правильными исходниками
          cl.exe /std:c++17 /O2 /EHsc PathServer/PathServer.cpp tc_src/dep/recastnavigation/Detour/Source/*.cpp /I tc_src/dep/recastnavigation/Detour/Include /link /OUT:PathServer.exe

      - name: Upload Finished Bot Suite
        uses: actions/upload-artifact@v4
        with:
          name: Imba-Bot-Release
          path: |
            injector.exe
            bot_payload.dll
            PathServer.exe
            Profiles/
