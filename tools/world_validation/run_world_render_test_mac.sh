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
METRICS="${REALGAZEBO_RENDER_METRICS:-$PROJECT_DIR/Saved/WorldValidation/render_metrics.json}"
BASELINE="${REALGAZEBO_RENDER_BASELINE:-}"
TEST_NAME="${REALGAZEBO_WORLD_TEST:-Editor.Python.RealGazebo.world_validation.test_world_render}"

if [[ ! -x "$EDITOR" ]]; then
  echo "UnrealEditor not executable: $EDITOR" >&2
  exit 3
fi
if [[ ! -f "$PROJECT" ]]; then
  echo "uproject not found: $PROJECT" >&2
  exit 4
fi
if ! python3 -c 'import PIL' >/dev/null 2>&1; then
  echo "Pillow is required for quantitative render validation: python3 -m pip install Pillow" >&2
  exit 7
fi

mkdir -p "$(dirname "$OUTPUT")" "$AUTOMATION_REPORT"
rm -f "$OUTPUT" "$METRICS"

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

VALIDATE=(python3 "$ROOT/tools/world_validation/validate_render_image.py" "$OUTPUT" --json "$METRICS")
if [[ -n "$BASELINE" ]]; then
  VALIDATE+=(--baseline "$BASELINE")
fi
"${VALIDATE[@]}"

echo "render screenshot: $OUTPUT"
echo "render metrics: $METRICS"
echo "automation report: $AUTOMATION_REPORT"
