# ArduPilot / MAVLink swarm bridge

This tooling connects ArduPilot SITL vehicles to RealGazebo's existing UDP pose protocol.

## Data flow

```text
Gazebo dynamics (optional external simulator)
        <-> ArduPilot SITL
              | MAVLink
              | LOCAL_POSITION_NED + ATTITUDE
              v
ardupilot_to_realgazebo.py
              | RealGazebo UDP MessageID=1, port 5005
              v
Unreal vehicle actors -> FPV cameras -> hardware H.264 encoder
                                  |-> browser/WebRTC relay
                                  `-> STANAG 4609-style MPEG-TS/KLV UDP
```

RealGazebo already converts Gazebo ENU coordinates into Unreal coordinates. The bridge therefore converts MAVLink NED to Gazebo ENU and emits the existing 31-byte pose packet; it does not send Unreal centimetres.

## Dependencies

```bash
python -m pip install pymavlink
```

## Existing SITL instances

For four instances using the conventional ArduPilot TCP port spacing:

```bash
COUNT=4 tools/ardupilot/run_swarm_tools.sh
```

Defaults:

- instance 0: `tcp:127.0.0.1:5760`
- instance 1: `tcp:127.0.0.1:5770`
- instance 2: `tcp:127.0.0.1:5780`
- instance 3: `tcp:127.0.0.1:5790`
- RealGazebo UDP destination: `127.0.0.1:5005`

The controller does **not** switch to GUIDED or arm by default. For a disposable SITL demo:

```bash
COUNT=4 GUIDED=1 ARM=1 tools/ardupilot/run_swarm_tools.sh
```

## Gazebo-backed ArduPilot

Use the official ArduPilot Gazebo plugin / external simulator configuration. The standard single-vehicle pattern is:

```bash
gz sim -v4 -r iris_runway.sdf
sim_vehicle.py -v ArduCopter -f gazebo-iris --model JSON --no-mavproxy
```

For multi-vehicle worlds, each Gazebo model and SITL instance needs distinct simulation/MAVLink ports. Once the SITL TCP endpoints are available, `run_swarm_tools.sh` is agnostic to whether the physics source is Gazebo or ArduPilot's built-in SITL model.

## Demo trajectory

`lissajous_boids.py` sends `SET_POSITION_TARGET_LOCAL_NED` setpoints with:

- phase-shifted 3D Lissajous references,
- short-range separation repulsion,
- optional weak cohesion,
- target smoothing,
- yaw aligned with path motion.

This is a deterministic demo behavior only. It is intentionally separate from production collision avoidance / CBF / ORCA / task allocation logic.

## Protocol tests

```bash
cd tools/ardupilot
python -m unittest -v test_realgazebo_udp.py
```
