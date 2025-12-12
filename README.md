# RealGazebo Plugin

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.1+-blue)](https://www.unrealengine.com/)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Windows-lightgrey)](https://realgazebo.chungbuk.ac.kr/)

**Website:** [realgazebo.chungbuk.ac.kr](https://realgazebo.chungbuk.ac.kr/)
**Repository:** [github.com/SUV-Lab/RealGazebo](https://github.com/SUV-Lab/RealGazebo)

---

## Overview

RealGazebo is an integrated simulator that bridges **PX4-Gazebo physics simulation** with **Unreal Engine 5 photorealistic rendering**. It enables large-scale, high-fidelity simulation of multi-heterogeneous unmanned vehicles (UAVs, UGVs, USVs) with real-time RTSP video streaming.

Developed by **SUV Lab, Chungbuk National University** as a next-generation platform for autonomous vehicle research and development.

---

## Architecture

The RealGazebo plugin consists of **4 runtime modules** working together:

### 1. RealGazebo (Master Orchestrator)
The central coordination module that provides unified API access to all subsystems. Acts as a lightweight master coordinator that registers manager actors and forwards API calls to appropriate child subsystems.

**Key Role:** System integration and subsystem orchestration

### 2. RealGazeboBridge (Network & Vehicle Management)
Receives UDP data from PX4-Gazebo simulation and manages vehicle lifecycle. Handles packet parsing, vehicle spawning via object pooling, and real-time state updates.

**Key Role:** UDP communication (port 5005) and vehicle lifecycle management

### 3. RealGazeboUI (Visualization & Camera Control)
Provides camera control modes and vehicle UI widgets for enhanced visualization. Offers multiple camera perspectives (follow, orbit, free-fly) and heads-up display elements.

**Key Role:** Camera systems and user interface

### 4. RealGazeboStreaming (RTSP Video Streaming)
Hardware-accelerated H.264 video streaming via RTSP. Uses NVENC/AMF encoders for zero-copy GPU texture encoding with ultra-low latency (< 100ms end-to-end).

**Key Role:** Real-time video streaming (port 8554) for multi-camera vehicle feeds

### Project Structure

```
Plugins/RealGazebo/
├── Source/
│   ├── RealGazebo/              # Master orchestrator module
│   ├── RealGazeboBridge/        # Network & vehicle management
│   ├── RealGazeboUI/            # Visualization & camera control
│   └── RealGazeboStreaming/     # RTSP streaming module
├── Content/                     # Plugin content assets
└── README.md                    # This file
```

---

## Key Features

- **Multi-Vehicle Support** - Up to 256 instances per vehicle type (0-255 range). Tested with 8+ simultaneous heterogeneous vehicles (quadcopters, rovers, boats, VTOL)
- **PX4 Integration** - Full compatibility with PX4 autopilot via Gazebo bridge
- **RTSP Streaming** - Hardware-accelerated H.264 streaming at 30/60 FPS with < 100ms latency
- **Zero-Copy Pipeline** - Direct GPU texture to NVENC encoding (no CPU memory copies)
- **Dynamic Spawning** - Runtime vehicle creation/destruction via UDP protocol
- **Object Pooling** - Efficient vehicle reuse for performance optimization
- **Cross-Platform** - Linux and Windows support with platform-specific optimizations

---

## System Requirements

### Software
- **Unreal Engine:** 5.1 or later
- **Operating System:** Linux (Ubuntu 20.04/22.04) or Windows 10/11

### Hardware
- **GPU:** NVIDIA RTX 30/40/50 series (for NVENC) or AMD RX 6000/7000 (for AMF)
- **RAM:** 16 GB minimum, 32 GB recommended
- **CPU:** Multi-core processor (16+ threads recommended)

---

## Module Dependencies

```
RealGazebo (Master - UWorldSubsystem)
├── RealGazeboBridge (UGameInstanceSubsystem)
│   ├── UDP Receiver (port 5005)
│   ├── Vehicle Pool Manager
│   └── Data Stream Processor
│
├── RealGazeboUI (UGameInstanceSubsystem)
│   ├── Camera Controllers
│   └── UI Widgets
│
└── RealGazeboStreaming (UGameInstanceSubsystem)
    ├── RTSP Server (port 8554)
    ├── Hardware Encoders (NVENC/AMF)
    ├── Frame Pool & Capture
    └── Streaming Pipelines (per-camera isolation)
```

---

## RTSP URL Format

```
rtsp://<IP>:<PORT>/<vehicle_type>_<vehicle_num>/<camera_id>

Examples:
rtsp://localhost:8554/x500_1/front
rtsp://localhost:8554/lc_62_2/bottom
rtsp://192.168.1.100:8554/rover_0/rear
```

---

## UDP Protocol (PX4-Gazebo → Unreal)

**Port:** 5005

**Packet Types:**
- **MessageID 1:** Pose data (position + quaternion)
- **MessageID 2:** Motor RPM data
- **MessageID 3:** Servo state data
- **MessageID 4:** Destroy vehicle command
- **MessageID 5:** Battery + navigation state

**Packet Format:**
```
Header (3 bytes):
  - vehicle_num: uint8 (0-255)
  - vehicle_code: uint8 (vehicle type)
  - message_id: uint8 (1-5)
```

---

## License

This project is licensed under the **GNU General Public License v3.0**.
See [LICENSE](LICENSE) file for full license information.

**Copyright (c) 2024-2025 SUV Lab, Chungbuk National University**

**Authors:** Gonapinuwala Lahiru Sandaruwan, MinKyu Kim

**Supervisor:** Prof. SungTae Moon

---

## About SUV Lab

**SUV Lab (Smart Unmanned Vehicles Laboratory)** at Chungbuk National University focuses on deep learning-aided smart unmanned vehicle systems, with expertise in:

- Multi-UAV swarming flight technology
- Flight control systems and Bayesian filtering
- Real-time object detection for UAV imagery
- Deep learning-enhanced autonomous navigation

**Director:** Prof. SungTae Moon

**Location:** Chungbuk National University, Department of Intelligent System & Robotics

**Contact:** stmoon@cbnu.ac.kr | +82-43-261-3256

---

## Links

- **Homepage:** [realgazebo.chungbuk.ac.kr](https://realgazebo.chungbuk.ac.kr/)
- **SUV Lab:** [sites.google.com/view/suvlab](https://sites.google.com/view/suvlab/home)
- **PX4 Integration:** [github.com/SUV-Lab/RealGazebo-PX4](https://github.com/SUV-Lab/RealGazebo-PX4)
- **ROS2 Integration:** [github.com/SUV-Lab/RealGazebo-ROS2](https://github.com/SUV-Lab/RealGazebo-ROS2)
- **Installation Guide:** [Documentation](https://realgazebo.chungbuk.ac.kr/install)

---

## Support

For issues, questions, or contributions, please visit the [GitHub Issues](https://github.com/SUV-Lab/RealGazebo/issues) page.

**SUV Lab - Chungbuk National University**
Advancing autonomous vehicle simulation technology.
