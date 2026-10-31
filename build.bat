@echo off
rem Build TypeWave with MinGW-w64 g++ (no CMake needed).
rem Get g++ from MSYS2 (https://www.msys2.org):  pacman -S mingw-w64-ucrt-x86_64-gcc
rem then run this from the "MSYS2 UCRT64" shell or add C:\msys64\ucrt64\bin to PATH.
cd /d "%~dp0"
windres res\typewave.rc -O coff -o typewave_res.o || goto :error
g++ -std=c++17 -O2 -DUNICODE -D_UNICODE -municode -mwindows -static -s ^
    src\main_win.cpp src\keys.cpp src\synth.cpp src\engine.cpp src\settings.cpp src\audio.cpp typewave_res.o ^
    -o typewave.exe -luser32 -lshell32 -lole32 -ladvapi32 -luuid || goto :error
del typewave_res.o
echo.
echo Built typewave.exe. Double-click it, then look for the icon in the system tray.
exit /b 0
:error
echo.
echo Build failed. Is MinGW-w64 g++ installed and on PATH?  (g++ --version)
exit /b 1
