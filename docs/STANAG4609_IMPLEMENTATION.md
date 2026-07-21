# STANAG 4609 output implementation

RealGazebo treats STANAG 4609 as an output/muxing concern, not a rendering or encoder concern.

Pipeline:

```
SceneCapture -> GPU texture -> one hardware H.264 encode -> encoded fanout
                                                        |-> RTSP
                                                        |-> browser transport
                                                        `-> STANAG 4609 sink
                                                             |-> MPEG-TS video PES
                                                             `-> MISB KLV metadata PES
```

The STANAG sink must never trigger a second video encode or CPU pixel readback.

Initial implementation scope:
- MPEG-2 Transport Stream packetization for H.264 elementary stream
- dedicated metadata PID carrying MISB UAS Local Set KLV
- PAT/PMT generation
- monotonic 90 kHz PTS derived from capture timestamps
- UDP output suitable for downstream STANAG/MISP validation tooling

Compliance must be validated with an independent STANAG/MISP analyzer before claiming formal interoperability.
