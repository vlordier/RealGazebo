#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIVE555_VERSION="${LIVE555_VERSION:-2026.07.08}"
LIVE555_URL="${LIVE555_URL:-https://download.live555.com/live.${LIVE555_VERSION}.tar.gz}"
DEST="$ROOT_DIR/Source/ThirdParty/Live555/lib/Mac"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

case "$(uname -s)" in
  Darwin) ;;
  *) echo "error: macOS is required" >&2; exit 1 ;;
esac

ARCH="$(uname -m)"
if [[ "$ARCH" != "arm64" ]]; then
  echo "warning: building Live555 for host architecture $ARCH; Apple Silicon builds require arm64" >&2
fi

for tool in curl tar make clang++ ar; do
  command -v "$tool" >/dev/null 2>&1 || { echo "error: missing required tool: $tool" >&2; exit 1; }
done

echo "Downloading LIVE555 ${LIVE555_VERSION} from official upstream..."
curl --fail --location --retry 3 "$LIVE555_URL" -o "$TMP/live555.tar.gz"
tar -xzf "$TMP/live555.tar.gz" -C "$TMP"
SRC="$TMP/live"
[[ -d "$SRC" ]] || { echo "error: expected extracted directory $SRC" >&2; exit 1; }

cd "$SRC"
# LIVE555 has used several macOS config names over time. Prefer the no-OpenSSL
# variant because RealGazebo defines NO_OPENSSL=1, then fall back to recent
# named macOS releases and historical generic names.
for candidate in \
  macosx-no-openssl \
  macosx-bigsur \
  macosx-catalina \
  macos \
  macosx; do
  if [[ -f "config.${candidate}" ]]; then
    PLATFORM="$candidate"
    break
  fi
done

if [[ -z "${PLATFORM:-}" ]]; then
  echo "error: no supported LIVE555 macOS config found" >&2
  ls -1 config.* >&2 || true
  exit 1
fi

echo "Building LIVE555 using config.${PLATFORM}..."
./genMakefiles "$PLATFORM"
make -j"$(sysctl -n hw.ncpu)"

rm -rf "$DEST"
mkdir -p "$DEST/include/BasicUsageEnvironment" \
         "$DEST/include/UsageEnvironment" \
         "$DEST/include/groupsock" \
         "$DEST/include/liveMedia"

cp liveMedia/libliveMedia.a "$DEST/"
cp groupsock/libgroupsock.a "$DEST/"
cp BasicUsageEnvironment/libBasicUsageEnvironment.a "$DEST/"
cp UsageEnvironment/libUsageEnvironment.a "$DEST/"
cp liveMedia/include/* "$DEST/include/liveMedia/"
cp groupsock/include/* "$DEST/include/groupsock/"
cp BasicUsageEnvironment/include/* "$DEST/include/BasicUsageEnvironment/"
cp UsageEnvironment/include/* "$DEST/include/UsageEnvironment/"
printf '%s\n' "$LIVE555_VERSION" > "$DEST/VERSION"
printf '%s\n' "$PLATFORM" > "$DEST/CONFIG"

for lib in "$DEST"/*.a; do
  echo "$(basename "$lib"): $(file "$lib")"
  if [[ "$ARCH" == "arm64" ]] && ! lipo -info "$lib" 2>&1 | grep -q 'arm64'; then
    echo "error: $(basename "$lib") does not contain arm64" >&2
    exit 1
  fi
done

echo "LIVE555 macOS libraries installed in $DEST"
