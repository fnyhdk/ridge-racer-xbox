#!/bin/sh
# Builds and runs pictest.c at 720p and at 480p.
set -e
cd "$(dirname "$0")/../.."
H=test/host
SDLC="$(sdl2-config --cflags) -I$(sdl2-config --prefix)/include"
clang -O2 -w -fno-strict-aliasing -DWITH_SDL -I$H -I../rr-decomp-0.1.0/tools/m0 -include $H/host_shim.h $SDLC \
    -o $H/pictest $H/pictest.c src/video_xbox.c $(sdl2-config --libs)
$H/pictest && $H/pictest no720
