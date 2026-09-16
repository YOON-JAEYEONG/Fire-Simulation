@echo off
"C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0YUFS\YUFS.uproject" /Game/Maps/Prototype -YUFSBuildingInteractions "-ExecutePythonScript=%~dp0YUFS\Content\Python\preview_suppression_animation.py"
