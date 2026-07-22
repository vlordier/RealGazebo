#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 /path/to/Project.uproject [map]" >&2
  exit 2
fi

PROJECT="$1"
MAP="${2:-/RealGazebo/Maps/RealGazebo}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
EDITOR="${UE_EDITOR:-$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor}"
PYTHON_SCRIPT="$ROOT/tools/world_validation/audit_world.py"
PROJECT_DIR="$(cd "$(dirname "$PROJECT")" && pwd)"
REPORT="${REALGAZEBO_WORLD_REPORT:-$PROJECT_DIR/Saved/WorldValidation/world_audit.json}"
SCREENSHOT="${REALGAZEBO_WORLD_SCREENSHOT:-$PROJECT_DIR/Saved/WorldValidation/world.png}"

if [[ ! -x "$EDITOR" ]]; then
  echo "UnrealEditor not executable: $EDITOR" >&2
  exit 3
fi
if [[ ! -f "$PROJECT" ]]; then
  echo "uproject not found: $PROJECT" >&2
  exit 4
fi

mkdir -p "$(dirname "$REPORT")" "$(dirname "$SCREENSHOT")"
rm -f "$REPORT" "$SCREENSHOT"

export REALGAZEBO_WORLD_MAP="$MAP"
export REALGAZEBO_WORLD_REPORT="$REPORT"
export REALGAZEBO_WORLD_SCREENSHOT="$SCREENSHOT"

"$EDITOR" "$PROJECT" \
  -unattended \
  -nosplash \
  -NoSound \
  -log \
  -stdout \
  -FullStdOutLogOutput \
  "-ExecutePythonScript=$PYTHON_SCRIPT"

if [[ ! -f "$REPORT" ]]; then
  echo "world audit did not produce report: $REPORT" >&2
  exit 5
fi

python3 "$ROOT/tools/world_validation/validate_world_report.py" "$REPORT" --require-screenshot

echo "world report: $REPORT"
echo "world screenshot: $SCREENSHOT"
