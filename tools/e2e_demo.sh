#!/usr/bin/env bash
# E2E Demo: Launch UE editor, capture screenshot + RTSP video
set -euo pipefail

PROJECT="/Users/vincent/Work/RealGazebo-ROS2/src/realgazebo-ue5/realgazebo-ue5.uproject"
UE="/Users/Shared/Epic Games/UE_5.8"
EDITOR="$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
MAP="/RealGazebo/Maps/RealGazebo"
OUTDIR="${1:-/tmp/realgazebo_e2e_demo}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

mkdir -p "$OUTDIR"

echo "=== RealGazebo E2E Demo ==="
echo "Output: $OUTDIR"

# Write a Python script that will be executed by the editor
cat > /tmp/e2e_demo.py << 'PYEOF'
import os, sys, time, json, unreal

output_dir = os.environ.get("E2E_DEMO_OUTPUT", "/tmp/realgazebo_e2e_demo")
os.makedirs(output_dir, exist_ok=True)

unreal.log("=== E2E DEMO: Starting ===")
unreal.log("Loading map...")

# Load the map
level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level_subsystem.load_level("/RealGazebo/Maps/RealGazebo"):
    unreal.log_error("E2E DEMO: Failed to load map")
    sys.exit(1)

unreal.log("E2E DEMO: Map loaded")

# Wait for level to settle
time.sleep(2.0)

# Count actors
actors = unreal.EditorLevelLibrary.get_all_level_actors()
categories = {}
for actor in actors:
    cls = actor.get_class().get_name()
    categories[cls] = categories.get(cls, 0) + 1

unreal.log("E2E DEMO: Actors: {}".format(len(actors)))
for cls, count in sorted(categories.items(), key=lambda x: -x[1])[:10]:
    unreal.log("  {}: {}".format(cls, count))

# Find validation camera
camera = None
for actor in actors:
    if actor.get_actor_label() == "ValidationCamera":
        camera = actor
        break

if camera:
    loc = camera.get_actor_location()
    rot = camera.get_actor_rotation()
    unreal.log("E2E DEMO: ValidationCamera at ({:.1f}, {:.1f}, {:.1f})".format(loc.x, loc.y, loc.z))
    
    # Set viewport and capture screenshot
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    editor_subsystem.set_level_viewport_camera_info(loc, rot)
    time.sleep(0.5)
    
    # Try HighResShot console command
    world = unreal.EditorLevelLibrary.get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, "HighResShot 1920x1080")
    time.sleep(2.0)
    
    # Check for screenshot
    import glob
    screenshots = glob.glob(os.path.expanduser("~/Library/Application Support/Epic/UnrealEditor/realgazebo-ue5/Saved/Screenshots/*.png"))
    if screenshots:
        newest = max(screenshots, key=os.path.getctime)
        dest = os.path.join(output_dir, "screenshot.png")
        import shutil
        shutil.copy(newest, dest)
        unreal.log("E2E DEMO: Screenshot saved to {}".format(dest))
    else:
        unreal.log_warning("E2E DEMO: No screenshot found")
else:
    unreal.log_warning("E2E DEMO: ValidationCamera not found")

# Save report
report = {
    "actor_count": len(actors),
    "actor_categories": {k: v for k, v in sorted(categories.items(), key=lambda x: -x[1])[:15]},
    "camera_found": camera is not None,
}
with open(os.path.join(output_dir, "e2e_report.json"), "w") as f:
    json.dump(report, f, indent=2)

unreal.log("=== E2E DEMO: Complete ===")
sys.exit(0)
PYEOF

export E2E_DEMO_OUTPUT="$OUTDIR"

echo ""
echo "=== Launching UE Editor ==="
"$EDITOR" "$PROJECT" \
  "$MAP" \
  -unattended \
  -nosplash \
  -NoSound \
  -log \
  -stdout \
  -FullStdOutLogOutput \
  -ResX=1920 \
  -ResY=1080 \
  -Windowed \
  "-ExecutePythonScript=/tmp/e2e_demo.py" 2>&1 | grep -E "E2E DEMO:|Error:|Warning:" | head -20

echo ""
echo "=== Results ==="
if [ -f "$OUTDIR/screenshot.png" ]; then
    echo "SCREENSHOT: $OUTDIR/screenshot.png ($(stat -f%z "$OUTDIR/screenshot.png") bytes)"
else
    echo "SCREENSHOT: NOT FOUND"
fi

if [ -f "$OUTDIR/e2e_report.json" ]; then
    echo "REPORT:"
    cat "$OUTDIR/e2e_report.json"
fi

echo ""
echo "=== E2E Demo Complete ==="