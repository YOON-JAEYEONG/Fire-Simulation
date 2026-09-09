$projectPath = Join-Path $PSScriptRoot 'YUFS\YUFS.uproject'
$startupPath = Join-Path $PSScriptRoot 'YUFS\Content\Python\start_extinguisher_demo.py'
$enginePath = 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe'
if (!(Test-Path -LiteralPath $projectPath) -or !(Test-Path -LiteralPath $enginePath)) {
    throw 'Unreal Engine 5.7 or YUFS project was not found.'
}
& $enginePath $projectPath '/Game/Maps/Prototype' '-YUFSBuildingInteractions' '-NoSplash' '-nop4' "-ExecutePythonScript=$startupPath"
