#!/bin/zsh
set -eu
ROOT="$(cd "$(dirname "$0")" && pwd)"
ENGINE="/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
exec "$ENGINE" "$ROOT/YUFS/YUFS.uproject" /Game/Maps/Prototype \
  -YUFSBuildingInteractions \
  "-ExecutePythonScript=$ROOT/YUFS/Content/Python/preview_suppression_animation.py"
