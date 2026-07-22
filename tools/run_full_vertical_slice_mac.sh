#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 /path/to/Project.uproject [map]" >&2
  echo "Optional env: SIM_START_CMD, COUNT, BASE_TCP_PORT, GUIDED, ARM, VEHICLE_TYPE_NAME" >&2
  exit 2
fi

PROJECT="$1"
MAP="${2:-}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PYTHON="${PYTHON:-python3}"
COUNT="${COUNT:-4}"
VEHICLE_TYPE_NAME="${VEHICLE_TYPE_NAME:-x500}"
REALGAZEBO_RTSP_PORT="${REALGAZEBO_RTSP_PORT:-8554}"
REALGAZEBO_STANAG_PORT="${REALGAZEBO_STANAG_PORT:-5000}"
export REALGAZEBO_RTSP_PORT REALGAZEBO_STANAG_PORT

pids=()
cleanup() {
  for pid in "${pids[@]:-}"; do
    kill "$pid" 2>/dev/null || true
  done
}
trap cleanup EXIT INT TERM

if [[ -n "${SIM_START_CMD:-}" ]]; then
  echo "starting simulation: $SIM_START_CMD"
  bash -lc "$SIM_START_CMD" &
  SIM_PID=$!
  pids+=("$SIM_PID")
  sleep "${SIM_BOOT_DELAY:-3}"
fi

echo "starting MAVLink swarm bridge/controller for COUNT=$COUNT"
COUNT="$COUNT" \
BASE_TCP_PORT="${BASE_TCP_PORT:-5760}" \
REALGAZEBO_HOST="${REALGAZEBO_HOST:-127.0.0.1}" \
REALGAZEBO_PORT="${REALGAZEBO_PORT:-5005}" \
VEHICLE_TYPE="${VEHICLE_TYPE:-0}" \
RUN_CONTROLLER="${RUN_CONTROLLER:-1}" \
GUIDED="${GUIDED:-0}" \
ARM="${ARM:-0}" \
"$ROOT/tools/ardupilot/run_swarm_demo.sh" &
SWARM_PID=$!
pids+=("$SWARM_PID")

echo "starting headless Unreal vertical slice"
COUNT="$COUNT" \
VEHICLE_TYPE_NAME="$VEHICLE_TYPE_NAME" \
FPV_CAMERA="${FPV_CAMERA:-fpv}" \
"$ROOT/tools/run_vertical_slice_mac.sh" "$PROJECT" "$MAP" &
UE_PID=$!
pids+=("$UE_PID")

sleep "${HEALTH_DELAY:-8}"
health_args=(
  --rtsp-port "$REALGAZEBO_RTSP_PORT"
  --stanag-port "$REALGAZEBO_STANAG_PORT"
  --timeout "${HEALTH_TIMEOUT:-10}"
)
if ! command -v go2rtc >/dev/null 2>&1; then
  health_args+=(--no-browser)
fi

if "$PYTHON" "$ROOT/tools/vertical_slice_health.py" "${health_args[@]}"; then
  echo "vertical slice healthy"
else
  echo "vertical slice health check failed" >&2
  exit 1
fi

while true; do
  if ! kill -0 "$UE_PID" 2>/dev/null; then
    echo "Unreal process exited" >&2
    wait "$UE_PID" || true
    exit 1
  fi
  if ! kill -0 "$SWARM_PID" 2>/dev/null; then
    echo "MAVLink swarm bridge/controller exited" >&2
    wait "$SWARM_PID" || true
    exit 1
  fi
  sleep 2
done
