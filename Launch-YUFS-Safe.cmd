@echo off
setlocal
if not defined YUFS_ENGINE_ROOT set "YUFS_ENGINE_ROOT=C:\Program Files\Epic Games\UE_5.7"
set "YUFS_EDITOR=%YUFS_ENGINE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe"
set "YUFS_PROJECT=%~dp0YUFS\YUFS.uproject"
rem After cloning or pulling source changes, use Build-And-Launch-YUFS-Safe.cmd first.
if not exist "%YUFS_EDITOR%" (
    echo [ERROR] UE 5.7 editor not found. Set YUFS_ENGINE_ROOT to the engine folder.
    pause
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Test-YUFSCheckout.ps1" -ProjectRoot "%~dp0." -SkipHash -RequireBuild
if errorlevel 1 (
    echo Run Build-And-Launch-YUFS-Safe.cmd after downloading the required LFS files.
    pause
    exit /b 1
)
if /I "%~1"=="--verify-only" exit /b 0
start "" "%YUFS_EDITOR%" "%YUFS_PROJECT%" -NoHotReloadFromIDE -NoLiveCoding -DisablePython
exit /b 0
