$previewProject = Join-Path $PSScriptRoot 'YUFS/YUFS.uproject'
$previewScript = Join-Path $PSScriptRoot 'YUFS/Content/Python/start_extinguisher_demo.py'
& 'C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe' $previewProject '/Game/Maps/Prototype' '-YUFSBuildingInteractions' '-YUFSInteractionPreview' '-NoSplash' '-nop4' "-ExecutePythonScript=$previewScript"
