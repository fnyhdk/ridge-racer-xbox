#!/bin/sh
# Builds Ridge Racer for the original Xbox.
#
#   ./build.sh                      bin/ and RidgeRacer.iso
#   ./build.sh CUE=/path/disc.cue   the same, with the disc's music
#
# Needs Homebrew's llvm and lld (Apple's clang has no lld), cmake for
# extract-xiso, and python3. Any other make variable can be passed through:
# PSX_EXE=..., DISC_ISO=..., NXDK_DIR=..., DECOMP=...
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
# DECOMP and NXDK_DIR are needed here as well as by make, so they are taken
# from the arguments as make would take them.
for a in "$@"; do
    case "$a" in
    DECOMP=*) DECOMP="${a#DECOMP=}" ;;
    NXDK_DIR=*) NXDK_DIR="${a#NXDK_DIR=}" ;;
    esac
done
export NXDK_DIR="${NXDK_DIR:-$HERE/../nxdk-master}"
for llvm in /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin; do
    [ -d "$llvm" ] && PATH="$llvm:$PATH"
done
export PATH="$NXDK_DIR/bin:$PATH"
if ! command -v lld >/dev/null 2>&1; then
    echo "lld is not on the path: brew install llvm lld cmake" >&2
    exit 1
fi
if [ ! -f "$NXDK_DIR/lib/pdclib/include/stdio.h" ]; then
    echo "nxdk's submodules are missing from $NXDK_DIR (see README.md)" >&2
    exit 1
fi
DECOMP="${DECOMP:-$HERE/../rr-decomp-0.1.0}"
if [ ! -f "$DECOMP/tools/m0/build.py" ]; then
    echo "the decompilation is not at $DECOMP (see README.md)" >&2
    exit 1
fi
# The runtime changes the Xbox build needs, applied once to the decomp.
if ! patch -d "$DECOMP" -p1 -R -s -f --dry-run < "$HERE/patches/m0-xbox.patch" >/dev/null 2>&1; then
    echo "applying patches/m0-xbox.patch to $DECOMP"
    patch -d "$DECOMP" -p1 -N -s < "$HERE/patches/m0-xbox.patch" || {
        echo "patches/m0-xbox.patch does not apply to $DECOMP: is it version 0.1.0?" >&2
        exit 1
    }
fi
export DECOMP
cd "$HERE"
exec make -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" "$@"
