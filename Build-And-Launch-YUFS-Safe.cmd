@echo off
setlocal

set "YUFS_BUILD=C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat"
set "YUFS_EDITOR=C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"
set "YUFS_LINK=C:\CodexWork\FireSimulationWorkspace"
set "YUFS_TARGET=%~dp0."
set "YUFS_PROJECT=%YUFS_LINK%\YUFS\YUFS.uproject"

tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>NUL | find /I "UnrealEditor.exe" >NUL
if not errorlevel 1 (
    echo [ERROR] Unreal Editor is already running.
    echo Close it before rebuilding the project.
    pause
    exit /b 1
)

if not exist "%YUFS_BUILD%" (
    echo [ERROR] Unreal Engine 5.7 build script was not found.
    echo %YUFS_BUILD%
    pause
    exit /b 1
)

if not exist "C:\CodexWork" mkdir "C:\CodexWork"

if not exist "%YUFS_PROJECT%" (
    if exist "%YUFS_LINK%" (
        echo [ERROR] %YUFS_LINK% already exists but is not the YUFS project junction.
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

call "%YUFS_BUILD%" YUFSEditor Win64 Development "%YUFS_PROJECT%" -WaitMutex -NoHotReload -NoUBA
if errorlevel 1 (
    echo.
    echo [ERROR] YUFSEditor build failed. The editor was not launched.
    pause
    exit /b 1
)

start "" "%YUFS_EDITOR%" "%YUFS_PROJECT%" -NoCompile -NoHotReloadFromIDE -NoLiveCoding -DisablePython
exit /b 0
