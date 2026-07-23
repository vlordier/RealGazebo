#!/usr/bin/env bash
set -euo pipefail
# Import Bordeaux tiles into UE5.8

PROJECT="${1:-/Users/vincent/Work/RealGazebo-ROS2/src/realgazebo-ue5/realgazebo-ue5.uproject}"
MANIFEST="${2:-/tmp/bordeaux_processed/manifest.json}"
TILES_DIR="${3:-/tmp/bordeaux_processed/fbx}"
TARGET_MAP="${4:-/RealGazebo/Maps/RealGazebo}"

UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
EDITOR="${UE_EDITOR:-$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor}"
SCRIPT="$(cd "$(dirname "$0")" && pwd)/import_bordeaux_ue.py"

if [[ ! -f "$PROJECT" ]]; then
  echo "Project not found: $PROJECT" >&2
  exit 1
fi
if [[ ! -f "$MANIFEST" ]]; then
  echo "Manifest not found: $MANIFEST" >&2
  exit 1
fi
if [[ ! -d "$TILES_DIR" ]]; then
  echo "Tiles dir not found: $TILES_DIR" >&2
  exit 1
fi

export BORDEAUX_MANIFEST="$MANIFEST"
export BORDEAUX_TILES_DIR="$TILES_DIR"
export BORDEAUX_TARGET_MAP="$TARGET_MAP"

echo "Importing Bordeaux tiles into $TARGET_MAP..."
echo "  Manifest: $MANIFEST"
echo "  Tiles: $TILES_DIR"
echo "  Project: $PROJECT"

"$EDITOR" "$PROJECT" \
  -unattended \
  -nosplash \
  -NoSound \
  -log \
  -stdout \
  -FullStdOutLogOutput \
  "-ExecutePythonScript=$SCRIPT"

echo "Done!"