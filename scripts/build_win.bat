@echo off
REM Build script – Windows
REM Requires: cmake, Visual Studio, libjpeg-turbo, sdl2, sdl2-ttf
REM
REM   vcpkg install libjpeg-turbo sdl2 sdl2-ttf
REM   cmake -S . -B build ^
REM         -DCMAKE_BUILD_TYPE=Release ^
REM         -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
REM
REM d3d11.lib + dxgi.lib ship with Windows SDK (part of Visual Studio).

setlocal enabledelayedexpansion
set BUILD_DIR=%~dp0..\build

echo ==^> Configuring ...
cmake -S %~dp0.. -B %BUILD_DIR% -DCMAKE_BUILD_TYPE=Release

echo ==^> Building ...
cmake --build %BUILD_DIR% --config Release --parallel

echo ==^> Done: %BUILD_DIR%\Release\romer.exe
dir %BUILD_DIR%\Release\romer.exe 2>nul
