@echo off
setlocal
if not defined YUFS_ENGINE_ROOT set "YUFS_ENGINE_ROOT=C:\Program Files\Epic Games\UE_5.7"
set "YUFS_EDITOR=%YUFS_ENGINE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe"
set "YUFS_PROJECT=%~dp0YUFS\YUFS.uproject"
set "YUFS_BUILD=%YUFS_ENGINE_ROOT%\Engine\Build\BatchFiles\Build.bat"
if not exist "%YUFS_BUILD%" (
    echo [ERROR] UE 5.7 build script not found. Set YUFS_ENGINE_ROOT to the engine folder.
    pause
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Test-YUFSCheckout.ps1" -ProjectRoot "%~dp0."
if errorlevel 1 (
    pause
    exit /b 1
)
if /I "%~1"=="--verify-only" exit /b 0
echo Building THIS checkout: %YUFS_PROJECT%
call "%YUFS_BUILD%" YUFSEditor Win64 Development "-Project=%YUFS_PROJECT%" -WaitMutex -NoHotReloadFromIDE -DisableLiveCoding
if errorlevel 1 (
    echo [ERROR] Build failed. The editor was not launched. Close this project's editor before building.
    pause
    exit /b 1
)
start "" "%YUFS_EDITOR%" "%YUFS_PROJECT%" -NoHotReloadFromIDE -NoLiveCoding -DisablePython
exit /b 0
