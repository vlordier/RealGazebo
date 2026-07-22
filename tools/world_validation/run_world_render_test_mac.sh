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
PROJECT_DIR="$(cd "$(dirname "$PROJECT")" && pwd)"
OUTPUT="${REALGAZEBO_WORLD_SCREENSHOT:-$PROJECT_DIR/Saved/WorldValidation/world.png}"
AUTOMATION_REPORT="${REALGAZEBO_AUTOMATION_REPORT:-$PROJECT_DIR/Saved/WorldValidation/AutomationReport}"
TEST_NAME="${REALGAZEBO_WORLD_TEST:-Editor.Python.RealGazebo.world_validation.test_world_render}"

if [[ ! -x "$EDITOR" ]]; then
  echo "UnrealEditor not executable: $EDITOR" >&2
  exit 3
fi
if [[ ! -f "$PROJECT" ]]; then
  echo "uproject not found: $PROJECT" >&2
  exit 4
fi

mkdir -p "$(dirname "$OUTPUT")" "$AUTOMATION_REPORT"
rm -f "$OUTPUT"

export REALGAZEBO_WORLD_MAP="$MAP"
export REALGAZEBO_WORLD_SCREENSHOT="$OUTPUT"

"$EDITOR" "$PROJECT" \
  -unattended \
  -nosplash \
  -NoSound \
  -log \
  -stdout \
  -FullStdOutLogOutput \
  "-ExecCmds=Automation RunTest $TEST_NAME;Quit" \
  "-ReportExportPath=$AUTOMATION_REPORT"

if [[ ! -f "$OUTPUT" ]]; then
  echo "render automation did not produce screenshot: $OUTPUT" >&2
  echo "Ensure the built-in PythonAutomationTest plugin is enabled for the host project." >&2
  exit 5
fi
if [[ $(stat -f%z "$OUTPUT") -lt 1024 ]]; then
  echo "render screenshot is unexpectedly small: $OUTPUT" >&2
  exit 6
fi

echo "render screenshot: $OUTPUT"
echo "automation report: $AUTOMATION_REPORT"
