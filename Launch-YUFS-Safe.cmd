@echo off
setlocal

set "YUFS_EDITOR=C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"
set "YUFS_LINK=C:\CodexWork\FireSimulationWorkspace"
set "YUFS_TARGET=%~dp0."
set "YUFS_PROJECT=%YUFS_LINK%\YUFS\YUFS.uproject"

if not exist "%YUFS_EDITOR%" (
    echo [ERROR] Unreal Engine 5.7 editor was not found.
    echo %YUFS_EDITOR%
    pause
    exit /b 1
)

if not exist "C:\CodexWork" mkdir "C:\CodexWork"

if not exist "%YUFS_PROJECT%" (
    if exist "%YUFS_LINK%" (
        echo [ERROR] %YUFS_LINK% already exists but is not the YUFS project junction.
        echo Remove or rename that path, then run this file again.
        pause
        exit /b 1
    )

    mklink /J "%YUFS_LINK%" "%YUFS_TARGET%"
    if errorlevel 1 (
        echo [ERROR] Failed to create the ASCII project junction.
        pause
        exit /b 1
    )
)

start "" "%YUFS_EDITOR%" "%YUFS_PROJECT%" -NoCompile -NoHotReloadFromIDE -NoLiveCoding -DisablePython
exit /b 0
