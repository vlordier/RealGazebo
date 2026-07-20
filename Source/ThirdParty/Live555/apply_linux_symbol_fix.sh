#!/usr/bin/env bash
#
# apply_linux_symbol_fix.sh
#
# Renames Live555's `BufferedPacket` symbols inside lib/Linux/libliveMedia.a so
# they no longer collide with Unreal's `BufferedPacket` when both are linked into
# a single binary (monolithic / packaged builds).
#
# WHY THIS IS NEEDED
#   Unreal's PacketHandler module and Live555 both declare a class named
#   `BufferedPacket` in the global namespace:
#
#     UE      Runtime/PacketHandlers/PacketHandler/Public/PacketHandler.h
#     Live555 Include/liveMedia/MultiFramedRTPSource.hh
#
#   Up to UE 5.1 the engine defined its destructor inline in the header, so it
#   was emitted as a *weak* symbol and the linker silently resolved the clash.
#   UE 5.7 moved that destructor out of line into PacketHandler.cpp and exported
#   it with PACKETHANDLER_API, making it a *strong* symbol. Two strong
#   definitions of `_ZN14BufferedPacketD{1,2}Ev` then collide:
#
#     ld.lld: error: duplicate symbol: BufferedPacket::~BufferedPacket()
#
#   Modular builds (the editor) are unaffected because each module is a separate
#   .so. Only monolithic builds put both definitions in one link.
#
# WHY LINUX ONLY
#   MSVC encodes virtual-ness in the mangled name, so the two destructors get
#   different symbols on Windows and never collide:
#
#     Live555 (virtual)     ??1BufferedPacket@@UEAA@XZ
#     UE      (non-virtual) ??1BufferedPacket@@QEAA@XZ
#
#   The Itanium ABI used by clang on Linux does not encode virtual-ness, so both
#   mangle to `_ZN14BufferedPacketD2Ev`. Windows needs no fix today. If UE ever
#   makes its destructor virtual, Windows will collide too and will need the same
#   treatment.
#
# WHY A BINARY PATCH INSTEAD OF A REBUILD
#   Live555 only publishes the newest release; live.2025.09.17.tar.gz (the
#   version vendored here) is no longer downloadable. Rebuilding therefore means
#   upgrading Live555, which changes RTSP runtime behaviour and would also
#   perturb the Windows build that currently works. Renaming the symbols leaves
#   the compiled code byte-for-byte identical. See README.md before changing this.
#
# THE MANGLED NAME
#   In the Itanium ABI a class name is prefixed with its length, so
#   `BufferedPacket` (14 chars) appears as `14BufferedPacket` and
#   `Live555BufferedPacket` (21 chars) as `21Live555BufferedPacket`. Substituting
#   the whole prefixed token is what makes this safe: sibling classes such as
#   `18LATMBufferedPacket` and `21BufferedPacketFactory` carry different length
#   prefixes and are left untouched.
#
#   The new name matches the rename that Private/RTSP/Live555Types.h already
#   applies to Live555's headers, so the archive and the headers finally agree.
#
# USAGE
#   ./apply_linux_symbol_fix.sh          # apply (no-op if already applied)
#   ./apply_linux_symbol_fix.sh --verify # report status only, change nothing
#
set -euo pipefail

readonly OLD_TOKEN="14BufferedPacket"
readonly NEW_TOKEN="21Live555BufferedPacket"

# The two symbols that actually collide with Unreal. Used to verify the result.
readonly COLLIDING=("_ZN14BufferedPacketD1Ev" "_ZN14BufferedPacketD2Ev")

readonly HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly LIB="${HERE}/lib/Linux/libliveMedia.a"

OBJCOPY="${OBJCOPY:-$(command -v objcopy || command -v llvm-objcopy || true)}"
RANLIB="${RANLIB:-$(command -v ranlib || command -v llvm-ranlib || true)}"

die() { echo "error: $*" >&2; exit 1; }

[[ -f "${LIB}" ]] || die "not found: ${LIB}"
command -v nm >/dev/null || die "nm not found (install binutils)"

# Count *global* definitions only. The archive also contains a handful of
# file-local `.localalias` symbols carrying the old name; those never take part
# in cross-object resolution and are deliberately left alone.
count_global_old() {
    nm "${LIB}" 2>/dev/null | grep -E ' [TWD] ' | grep -c "${OLD_TOKEN}" || true
}
count_new() { nm "${LIB}" 2>/dev/null | grep -c "${NEW_TOKEN}" || true; }

# Note: deliberately `grep -c`, never `grep -q`. Under `set -o pipefail` a
# `grep -q` exits on the first match, `nm` upstream dies of SIGPIPE, and the
# pipeline reports failure *even though the symbol was found* - which would turn
# the safety check below into a silent rubber stamp. `grep -c` drains its input.
is_globally_defined() {
    local sym="$1" n
    n=$(nm "${LIB}" 2>/dev/null | grep -E ' [TWD] ' | grep -c "^\S* [TWD] ${sym}$" || true)
    [[ "${n}" -gt 0 ]]
}

report() {
    echo "library : ${LIB}"
    echo "old-name global symbols : $(count_global_old)"
    echo "new-name symbols        : $(count_new)"
    for sym in "${COLLIDING[@]}"; do
        if is_globally_defined "${sym}"; then
            echo "  COLLIDES: ${sym} still globally defined"
        else
            echo "  ok      : ${sym} absent"
        fi
    done
}

if [[ "${1:-}" == "--verify" ]]; then
    report
    exit 0
fi

if [[ "$(count_new)" -gt 0 ]]; then
    echo "Already applied - nothing to do."
    report
    exit 0
fi

[[ -n "${OBJCOPY}" ]] || die "objcopy not found (install binutils, or set OBJCOPY=)"
[[ -n "${RANLIB}"  ]] || die "ranlib not found (install binutils, or set RANLIB=)"

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

# Every mangled symbol that embeds the class token, defined or referenced.
# Renaming definitions and references together keeps Live555 internally
# consistent - subclasses like LATMBufferedPacket refer to the base destructor.
nm "${LIB}" 2>/dev/null \
    | grep -oE "_Z[A-Za-z0-9_]*${OLD_TOKEN}[A-Za-z0-9_]*" \
    | sort -u > "${work}/symbols"

[[ -s "${work}/symbols" ]] || die "no ${OLD_TOKEN} symbols found - wrong or already-modified archive?"

awk -v old="${OLD_TOKEN}" -v new="${NEW_TOKEN}" \
    '{ n = $0; gsub(old, new, n); print $0, n }' \
    "${work}/symbols" > "${work}/rename.map"

echo "Renaming $(wc -l < "${work}/rename.map") symbols in $(basename "${LIB}")..."
"${OBJCOPY}" --redefine-syms="${work}/rename.map" "${LIB}"
"${RANLIB}" "${LIB}"

# Fail loudly rather than leave a half-patched archive behind.
for sym in "${COLLIDING[@]}"; do
    if is_globally_defined "${sym}"; then
        die "${sym} is still globally defined - patch did not take"
    fi
done

echo "Done."
report
