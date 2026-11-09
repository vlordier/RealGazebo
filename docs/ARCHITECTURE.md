# RealGazebo — UE5.8 macOS Streaming Plugin

## What We're Building

A **real-time drone FPV video streaming plugin** for Unreal Engine 5.8 on macOS, forming the visualization/rendering layer of a larger Gazebo + ArduPilot + MAVLink simulation stack.

## Architecture

```
Gazebo + ArduPilot SITL + MAVLink
        ↓
RealGazebo / UE5.8
        ↓
Drone transforms driven by simulation
        ↓
Multiple FPV cameras (SceneCapture2D)
        ↓
Metal GPU texture pipeline
        ↓
VideoToolbox H.264 encoding
        ↓
Encoded video fanout
        ├── RTSP (Live555)
        ├── STANAG 4609 MPEG-TS + MISB KLV metadata
        └── Browser/WebRTC path
```

## Current State

### ✅ Working
- **UE5.8 Editor builds cleanly** (0 errors, 0 warnings after deprecation fixes)
- **Bordeaux-Mérignac airport environment** imported as 81 tiled static meshes with 9 semantic materials
- **World validation**: latest UE5.8 audit reports 499 actors (488 static meshes plus lighting, atmosphere/fog, post-process, manager, camera and 3 synthetic swarm actors); the report is stored in `world_audit.json`
- **SyntheticDroneSwarm**: C++ actor that spawns 5 drones with deterministic Lissajous trajectories
- **RTSP server**: Starts on port 8554, serves H.264 streams via Live555
- **E2E demo**: Map loads, camera captures 1920×1080 screenshot, RTSP server responds
- **Python linting**: `ruff check` — all checks passed
- **Shell linting**: `shellcheck` — all checks passed
- **Unit tests**: 74/74 passing

### 🔴 Known Issue — VideoToolbox Encoding
The VideoToolbox H.264 encoder fails at runtime with `LogAVCodecs: Error: Error`. Root cause: UE5.8 ships with **empty/stub AVCodecs/VTCodecs plugin directories** — the Metal video resource type (`FVideoResourceMetal`) is missing entirely. The `USimpleVideoEncoder` from `AVCodecsCoreRHI` cannot encode frames because the underlying VTCodecs Metal implementation doesn't exist.

**Fix in progress**: Implementing a direct VideoToolbox encoder via `dlopen`/`dlsym` in a separate `.mm` (Objective-C++) file to avoid the `FVector` type conflict between CarbonCore and UE.

### 📋 Remaining Work
1. **Finish and verify the Bordeaux scene checkpoint**: keep the semantic materials and airport-centered transform, run the Unreal world/render audit, and confirm the current 499-actor scene is visually correct.
2. **Keep the automated checks green**: run the focused Python tests and lint checks; Unreal-dependent tests must run inside the UE Python environment because the standalone interpreter has no `unreal` module.
3. **Re-run the vertical-slice acceptance test**: verify one authoritative vehicle pose, FPV capture, GPU-resident hardware encoding, RTSP output, and the saved render evidence after the scene changes.
4. **Complete the transport architecture**: replace RTSP-only capture demand with aggregate encoded-sink demand, fan out encoded access units to RTSP/browser/STANAG sinks, and attach synchronized telemetry metadata.
5. **Commit the verified scene and documentation changes**, excluding disposable local screenshots, PPMs, and editor logs unless they are intentionally selected as evidence.

The current checkout contains the RealGazebo plugin rather than the host Unreal
`.uproject`.  The repair script is ready at `tools/bordeaux/import_bordeaux_ue.py`,
but the repair/audit/render commands must be run from the host project that mounts
this plugin.

## Key Files

| File | Purpose |
|------|---------|
| `Source/RealGazeboStreaming/Private/Encoder/VTEncoder.mm` | Direct VideoToolbox encoder via dlopen (Objective-C++) |
| `Source/RealGazeboStreaming/Public/Encoder/HardwareEncoderWrapper.h` | Encoder wrapper header |
| `Source/RealGazeboStreaming/Private/Encoder/HardwareEncoderWrapper.cpp` | Encoder wrapper implementation |
| `Source/RealGazeboStreaming/Public/Synthetic/SyntheticDroneSwarm.h` | Synthetic drone swarm actor |
| `tools/bordeaux/preprocess_bordeaux.py` | OBJ → UE tiling pipeline |
| `tools/bordeaux/import_bordeaux_ue.py` | UE Python import script |
| `tools/world_validation/audit_world.py` | World validation script |
| `tools/world_validation/render_test.py` | Render test script |
| `tools/e2e_demo.sh` | End-to-end demo launcher |
