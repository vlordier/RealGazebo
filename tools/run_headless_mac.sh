#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 /path/to/Project.uproject [map]" >&2
  exit 2
fi

PROJECT="$1"
MAP="${2:-}"
UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
EDITOR="${UE_EDITOR:-$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor}"
STANAG_HOST="${REALGAZEBO_STANAG_HOST:-127.0.0.1}"
STANAG_PORT="${REALGAZEBO_STANAG_PORT:-5000}"

if [[ ! -x "$EDITOR" ]]; then
  echo "Unreal Editor executable not found: $EDITOR" >&2
  echo "Set UE_ROOT or UE_EDITOR." >&2
  exit 3
fi

ARGS=(
  "$PROJECT"
  -game
  -RenderOffscreen
  -unattended
  -nosplash
  -NoSound
  -log
  -stdout
  -FullStdOutLogOutput
  "-RealGazeboStanagHost=$STANAG_HOST"
  "-RealGazeboStanagPort=$STANAG_PORT"
)

# IMPORTANT: never add -nullrhi. SceneCapture and GPU encoding require a real RHI.
if [[ -n "$MAP" ]]; then
  ARGS+=("$MAP")
fi

exec "$EDITOR" "${ARGS[@]}"
