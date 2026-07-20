# Live555 (vendored)

RTSP/RTP library used by `RealGazeboStreaming` to serve H.264 video.

| | |
|---|---|
| Version | **2025.09.17** (see `Include/liveMedia/liveMedia_version.hh`) |
| Upstream | <https://download.live555.com/> |
| License | LGPL — see `LICENSE` |
| Contents | `Include/` headers, `lib/Linux/*.a`, `lib/Win64/*.lib` |

Only prebuilt libraries are vendored; the upstream sources are not in this
repository. How the current binaries were originally produced is not recorded —
compiler and flags are unknown. Anyone replacing them should write down what
they did, in this file.

> **Upstream keeps only the newest release.** `live.2025.09.17.tar.gz` is already
> gone from the server; as of 2026-07-20 the only downloads are
> `live.2026.07.08.tar.gz` and `live555-latest.tar.gz` (the same file). You
> cannot re-fetch the version vendored here. If you ever rebuild, commit the
> source tarball (~680 KB) alongside the binaries so the build stays
> reproducible.

## Linux symbol patch — REQUIRED

`lib/Linux/libliveMedia.a` is **not** a pristine upstream build. Its
`BufferedPacket` symbols have been renamed to `Live555BufferedPacket` by
`apply_linux_symbol_fix.sh`. Without this, packaged (monolithic) Linux builds
fail to link.

### The problem

Unreal and Live555 both declare a global-namespace class named `BufferedPacket`:

- `Engine/Source/Runtime/PacketHandlers/PacketHandler/Public/PacketHandler.h`
- `Include/liveMedia/MultiFramedRTPSource.hh`

Through UE 5.1 the engine defined its destructor **inline in the header**, which
emits a *weak* symbol, and the linker silently resolved the clash. **UE 5.7 moved
it out of line** into `PacketHandler.cpp` and exported it with
`PACKETHANDLER_API`, making it a *strong* symbol. Two strong definitions then
collide:

```
ld.lld: error: duplicate symbol: BufferedPacket::~BufferedPacket()
  >>> PacketHandler.cpp:93
  >>> MultiFramedRTPSource.cpp in archive .../lib/Linux/libliveMedia.a
```

Exactly two symbols collide: `_ZN14BufferedPacketD1Ev` and `_ZN14BufferedPacketD2Ev`.

This only affects **monolithic** builds. The editor is modular — each module is
its own `.so` — so the two definitions never meet, which is why this never
surfaced during editor development.

### Why Windows needs nothing

MSVC encodes virtual-ness in the mangled name, so the two destructors are
different symbols there and cannot collide:

| | mangled | |
|---|---|---|
| Live555 | `??1BufferedPacket@@`**`U`**`EAA@XZ` | `U` = public virtual |
| Unreal | `??1BufferedPacket@@`**`Q`**`EAA@XZ` | `Q` = public non-virtual |

The Itanium ABI used by clang on Linux does **not** encode virtual-ness — both
mangle to `_ZN14BufferedPacketD2Ev`. Hence the clash is Linux-only.

`lib/Win64/*.lib` is therefore untouched upstream output.

⚠️ If Unreal ever makes `~BufferedPacket` virtual, Windows will collide too and
will need the equivalent fix.

### Why patch the binary instead of rebuilding

`Private/RTSP/Live555Types.h` already does the same rename for everything *this
project* compiles:

```cpp
#define BufferedPacket Live555BufferedPacket
#include "liveMedia.hh"
// ...
#undef BufferedPacket
```

The prebuilt archive was built without that define, so the headers and the binary
disagreed. Rebuilding Live555 with `-DBufferedPacket=Live555BufferedPacket` is the
proper fix — but the vendored version is no longer downloadable, so rebuilding
means *upgrading* Live555, which changes RTSP runtime behaviour and would also
disturb the Windows build that currently works.

Renaming symbols in the archive leaves the compiled machine code **byte-for-byte
identical** and makes the binary agree with the headers. That is the smaller,
safer change for a build-only problem.

### Reproducing

```bash
./apply_linux_symbol_fix.sh          # apply (no-op if already applied)
./apply_linux_symbol_fix.sh --verify # report status, change nothing
```

Idempotent, and it aborts if the colliding symbols survive. Needs binutils
(`nm`, `objcopy`, `ranlib`); override with `OBJCOPY=` / `RANLIB=` if needed.

**Re-run this after replacing `lib/Linux/libliveMedia.a` for any reason.** A
fresh upstream archive will reintroduce the clash and the packaged Linux build
will fail to link again.

### What the patch does

Renames the length-prefixed mangled token `14BufferedPacket` →
`21Live555BufferedPacket` (Itanium ABI prefixes a class name with its length; 14
and 21 are those lengths). 42 symbols are renamed — definitions and references
together, so Live555 stays internally consistent.

Sibling classes are untouched because their length prefixes differ:
`18LATMBufferedPacket`, `26MPEG4GenericBufferedPacket`, `21BufferedPacketFactory`,
`30QTGenericBufferedPacketFactory`. None of them collide with Unreal anyway.

A few file-local `.localalias` symbols keep the old name. They are local (`t`),
never take part in cross-object resolution, and cannot collide.

### Verified

- `C_Track Linux Shipping` links clean: 0 errors, 0 duplicate, 0 undefined
- Streaming really is linked in, not silently dropped
- Both classes coexist at distinct addresses — the symbols are separated, not merged

Build/link only. Full UAT packaging (cook + stage) and RTSP runtime behaviour
were not re-tested after the patch.

## Upgrading Live555

The symbol patch is a stopgap for a version that can no longer be fetched. When
you do upgrade, do it properly instead:

1. Download the current release and **commit the tarball** — it will be
   unavailable within months.
2. Build with `-DBufferedPacket=Live555BufferedPacket` so the rename is baked in
   and `apply_linux_symbol_fix.sh` becomes unnecessary — delete it then.
3. Rebuild **both** Linux (UE's bundled clang, for ABI compatibility) and Win64
   (MSVC), so the platforms stay on one version.
4. Re-test RTSP at runtime, not just the build.
5. Update this file with the version, toolchain, and flags used.
