#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 /path/to/Project.uproject [map]" >&2
  exit 2
fi

PROJECT="$1"
MAP="${2:-}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PYTHON="${PYTHON:-python3}"
GO2RTC_BIN="${GO2RTC_BIN:-$(command -v go2rtc || true)}"
COUNT="${COUNT:-4}"
VEHICLE_TYPE_NAME="${VEHICLE_TYPE_NAME:-x500}"
FPV_CAMERA="${FPV_CAMERA:-fpv}"

export REALGAZEBO_STANAG_HOST="${REALGAZEBO_STANAG_HOST:-127.0.0.1}"
export REALGAZEBO_STANAG_PORT="${REALGAZEBO_STANAG_PORT:-5000}"
export REALGAZEBO_RTSP_PORT="${REALGAZEBO_RTSP_PORT:-8554}"

TMP_GO2RTC=""
cleanup() {
  if [[ -n "${GO2RTC_PID:-}" ]]; then
    kill "$GO2RTC_PID" 2>/dev/null || true
  fi
  [[ -n "$TMP_GO2RTC" ]] && rm -f "$TMP_GO2RTC"
}
trap cleanup EXIT INT TERM

if [[ -n "$GO2RTC_BIN" ]]; then
  if [[ -n "${GO2RTC_CONFIG:-}" ]]; then
    CONFIG="$GO2RTC_CONFIG"
  else
    TMP_GO2RTC="$(mktemp -t realgazebo-go2rtc.XXXXXX.yaml)"
    "$PYTHON" "$ROOT/tools/browser_gateway/generate_go2rtc_config.py" \
      --count "$COUNT" \
      --vehicle-type-name "$VEHICLE_TYPE_NAME" \
      --camera "$FPV_CAMERA" \
      --rtsp-port "$REALGAZEBO_RTSP_PORT" \
      --output "$TMP_GO2RTC"
    CONFIG="$TMP_GO2RTC"
  fi
  "$GO2RTC_BIN" -config "$CONFIG" &
  GO2RTC_PID=$!
  echo "browser gateway: http://127.0.0.1:1984 (${COUNT} ${VEHICLE_TYPE_NAME}/${FPV_CAMERA} streams)"
else
  echo "warning: go2rtc not found; RTSP/STANAG still run, browser WebRTC relay disabled" >&2
fi

UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
EDITOR="${UE_EDITOR:-$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor}"
if [[ ! -x "$EDITOR" ]]; then
  echo "Unreal executable not found: $EDITOR" >&2
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
  "-RealGazeboRTSPPort=$REALGAZEBO_RTSP_PORT"
  "-RealGazeboStanagHost=$REALGAZEBO_STANAG_HOST"
  "-RealGazeboStanagPort=$REALGAZEBO_STANAG_PORT"
  "-RealGazeboStanagCamera=$FPV_CAMERA"
)

# Optional geographic origin used after the renderer normalizes its local world
# pose back to semantic ENU (East/North/Up) metadata.
[[ -n "${REALGAZEBO_GEO_ORIGIN_LAT:-}" ]] && ARGS+=("-RealGazeboGeoOriginLat=$REALGAZEBO_GEO_ORIGIN_LAT")
[[ -n "${REALGAZEBO_GEO_ORIGIN_LON:-}" ]] && ARGS+=("-RealGazeboGeoOriginLon=$REALGAZEBO_GEO_ORIGIN_LON")
[[ -n "${REALGAZEBO_GEO_ORIGIN_ALT:-}" ]] && ARGS+=("-RealGazeboGeoOriginAlt=$REALGAZEBO_GEO_ORIGIN_ALT")
[[ -n "$MAP" ]] && ARGS+=("$MAP")

# Never use -nullrhi: SceneCapture and hardware encoding require the real Metal RHI.
exec "$EDITOR" "${ARGS[@]}"
