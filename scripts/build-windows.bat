@echo off
setlocal

set "VCVARSALL=D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"

if not exist "%VCVARSALL%" (
    echo ERROR: vcvarsall.bat not found at "%VCVARSALL%"
    echo Please update the VCVARSALL path in this script to match your VS installation.
    exit /b 1
)

call "%VCVARSALL%" x64 >nul

if "%~1"=="" (
    echo Usage:
    echo   scripts\build-windows.bat windows-debug [extra cmake args...]
    echo   scripts\build-windows.bat windows-release [extra cmake args...]
    echo.
    echo This script activates the MSVC x64 environment then runs:
    echo   cmake --preset ^<preset^> [args...]
    exit /b 0
)

set "PRESET=%~1"
shift

echo [kd39] Configuring with preset: %PRESET%
cmake --preset %PRESET% %1 %2 %3 %4 %5 %6 %7 %8 %9
if errorlevel 1 exit /b %errorlevel%

echo.
echo [kd39] Building with preset: build-%PRESET%
cmake --build --preset build-%PRESET%
if errorlevel 1 exit /b %errorlevel%

echo.
echo [kd39] Build succeeded. compile_commands.json is at:
echo   build\%PRESET%\compile_commands.json
