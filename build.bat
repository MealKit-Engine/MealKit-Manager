@echo off
rem Builds MealKit Manager (Release, Ninja) into build\windows\ and runs the tests.
rem Usage: build.bat            build + tests
rem        build.bat install    also copy to %LOCALAPPDATA%\MealKit\manager (the editor starts it when opened without a project)
setlocal
if defined VCINSTALLDIR goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS=%%i"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

:build
cd /d "%~dp0"
if not exist build\windows\build.ninja cmake -S . -B build\windows -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build\windows || exit /b 1
ctest --test-dir build\windows --output-on-failure || exit /b 1
if /i "%~1"=="install" cmake --install build\windows --prefix "%LOCALAPPDATA%\MealKit\manager" || exit /b 1
rem Start menu and desktop shortcuts, the same as a package install (scripts\install.ps1 shortcuts)
if /i "%~1"=="install" powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install.ps1" shortcuts -To "%LOCALAPPDATA%\MealKit\manager" || exit /b 1
echo [MealKit Manager] Build OK: %~dp0build\windows\bin\MealKitManager.exe
