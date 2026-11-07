#!/bin/sh
# Builds the Xbox platform layer against the host stand-ins (see shim.c).
set -e
cd "$(dirname "$0")/../.."
H=test/host
SDLC="$(sdl2-config --cflags) -I$(sdl2-config --prefix)/include"
CF="-O2 -w -fcommon -fno-strict-aliasing -DWITH_SDL -I$H -I../rr-decomp-0.1.0/tools/m0 -include $H/host_shim.h $SDLC"
mkdir -p $H/obj
clang $CF -Dmain=m0_main -c gen/main.c -o $H/obj/main.o
clang $CF -o $H/xbox_host $H/obj/main.o gen/hw.c gen/gpu.c gen/gte.c gen/spu.c gen/mods.c \
    gen/audio.c gen/game.c gen/table.c gen/cdfiles.c src/video_xbox.c src/xbox_main.c \
    $H/shim.c $(sdl2-config --libs)
echo "=> $H/xbox_host"
