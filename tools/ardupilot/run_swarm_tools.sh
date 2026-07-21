#!/usr/bin/env bash
set -euo pipefail

# Connect an already-running ArduPilot SITL/Gazebo swarm to RealGazebo and
# optionally drive it with the Lissajous/boids controller.
#
# One MAVLink client per SITL instance is used. In controller mode the controller
# also forwards vehicle state to RealGazebo; bridge-only mode runs the dedicated
# state bridge instead.

COUNT="${COUNT:-4}"
BASE_TCP_PORT="${BASE_TCP_PORT:-5760}"
REALGAZEBO_HOST="${REALGAZEBO_HOST:-127.0.0.1}"
REALGAZEBO_PORT="${REALGAZEBO_PORT:-5005}"
VEHICLE_TYPE="${VEHICLE_TYPE:-0}"
RUN_CONTROLLER="${RUN_CONTROLLER:-1}"
GUIDED="${GUIDED:-0}"
ARM="${ARM:-0}"
PYTHON="${PYTHON:-python3}"
ROOT="$(cd "$(dirname "$0")" && pwd)"

bridge_args=()
controller_args=()
for ((i=0; i<COUNT; ++i)); do
  port=$((BASE_TCP_PORT + i * 10))
  endpoint="tcp:127.0.0.1:${port}"
  bridge_args+=(--vehicle "${i}:${VEHICLE_TYPE}=${endpoint}")
  controller_args+=(--vehicle "${i}=${endpoint}")
done

cleanup() {
  [[ -n "${CHILD_PID:-}" ]] && kill "$CHILD_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

if [[ "$RUN_CONTROLLER" == "1" ]]; then
  extra=()
  [[ "$GUIDED" == "1" ]] && extra+=(--guided)
  [[ "$ARM" == "1" ]] && extra+=(--arm)
  "$PYTHON" "$ROOT/lissajous_boids.py" \
    "${controller_args[@]}" \
    --vehicle-type "$VEHICLE_TYPE" \
    --realgazebo-host "$REALGAZEBO_HOST" \
    --realgazebo-port "$REALGAZEBO_PORT" \
    "${extra[@]}" &
  CHILD_PID=$!
  echo "Unified swarm controller + RealGazebo bridge PID=$CHILD_PID"
else
  "$PYTHON" "$ROOT/ardupilot_to_realgazebo.py" \
    "${bridge_args[@]}" \
    --realgazebo-host "$REALGAZEBO_HOST" \
    --realgazebo-port "$REALGAZEBO_PORT" &
  CHILD_PID=$!
  echo "MAVLink -> RealGazebo bridge PID=$CHILD_PID"
fi

wait "$CHILD_PID"
