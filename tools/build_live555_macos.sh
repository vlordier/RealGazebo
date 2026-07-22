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
./genMakefiles macosx
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

for lib in "$DEST"/*.a; do
  echo "$(basename "$lib"): $(file "$lib")"
done

echo "LIVE555 macOS libraries installed in $DEST"
