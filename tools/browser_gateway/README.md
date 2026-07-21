# Browser gateway

RealGazebo keeps per-camera video encoded once in Unreal. The existing RTSP output is relayed to browser WebRTC without re-encoding.

Default demo stream:

- RTSP input: `rtsp://127.0.0.1:8554/x500_1/fpv`
- go2rtc API/UI: `http://127.0.0.1:1984`

Run:

```bash
go2rtc -config tools/browser_gateway/go2rtc.yaml
```

Or use:

```bash
tools/run_vertical_slice_mac.sh /path/to/Project.uproject /Game/Maps/YourMap
```

The browser relay must remain a remux/transport step. Do not configure transcoding for the normal path; the H.264 stream produced by Unreal should be forwarded unchanged.
