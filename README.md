# Ridge Racer for the original Xbox

## Transparency

This is my first port and I'm still learning. I used
[Claude Code](https://claude.com/claude-code) to help write it, and I reviewed
and tested the work myself throughout.

## About

A port of Ridge Racer (PlayStation, Namco 1994, Japanese release SLPS-00001) to
the original Xbox, built with [nxdk](https://github.com/XboxDev/nxdk).

The port is built on the native runtime of the
[Ridge Racer decompilation](https://github.com/kazuyette/rr-decomp). That runtime is a static
recompilation: every MIPS function of `PSX.EXE` is translated to C and runs
against a software model of the PlayStation's GPU, GTE, SPU and CD drive. This
repository compiles that runtime for the Xbox's Pentium III and adds the Xbox
side: video output, pads, audio and file access.

**No game data is included, and none is distributed.** You need your own copy
of the game. The build reads your `PSX.EXE` and disc image and places them
next to the XBE. The built XBE contains the game's code in translated form, so
please do not share builds.

## Requirements

- Your own copy of Ridge Racer (SLPS-00001):
  - `PSX.EXE`, taken from the disc;
  - an image of the data track as a 2048-byte-sector `.iso`;
  - optionally, a raw BIN/CUE rip for the music (see
    [Sound and music](#sound-and-music)).
- The [Ridge Racer decompilation](https://github.com/kazuyette/rr-decomp),
  version 0.1.0.
- [nxdk](https://github.com/XboxDev/nxdk), cloned with its submodules.
- macOS with Homebrew's LLVM, CMake and Python 3. The build has so far only
  been used on macOS. Linux (or WSL on Windows) should need little more than
  installing the same tools with its package manager.

## Building

Place the three folders side by side:

```
rr-decomp-0.1.0/   the decompilation, with PSX.EXE copied into it
nxdk-master/       nxdk
ridge-racer-xbox/  this repository (the folder name does not matter)
```

Use version 0.1.0 of the decompilation, which the patch is made against, and
name its folder `rr-decomp-0.1.0`, or point the build at it with
`DECOMP=...`. If the patch does not apply to the copy you have, `build.sh`
stops and says so.

Clone nxdk with `--recursive`. A zip download from GitHub leaves out its
submodules, and the build needs the exact commits nxdk pins:

```sh
git clone --recursive https://github.com/XboxDev/nxdk.git nxdk-master
```

Install the tools and build:

```sh
brew install llvm lld cmake
cd ridge-racer-xbox
./build.sh DISC_ISO="/path/to/SLPS-00001.iso"
```

Apple's own clang has no `lld`, which is why Homebrew's LLVM is needed. On its
first run, `build.sh` applies `patches/m0-xbox.patch` to the decompilation's
runtime. `PSX_EXE` defaults to `rr-decomp-0.1.0/PSX.EXE` and `DISC_ISO` to
`rr-decomp-0.1.0/SLPS-00001.iso`. Other locations can be given with
`PSX_EXE=...`, `DISC_ISO=...`, `DECOMP=...` and `NXDK_DIR=...`.

The build produces:

| | |
|---|---|
| `bin/default.xbe` | the program |
| `bin/PSX.EXE`, `bin/disc.iso` | your game files, read from `D:\` |
| `bin/tracks/` | the music, when built with `CUE=` |
| `RidgeRacer.iso` | all of the above as an Xbox disc image (XISO) |

`./build.sh clean-game` removes everything the build produced.

### Making the data-track ISO from a BIN/CUE rip

If you only have a BIN/CUE rip, convert its data track (track 1) to an ISO
with 2048-byte sectors. Most disc image tools can do this, for example
`bchunk`, PowerISO or AnyToISO. Then pass the result to the build as
`DISC_ISO=...`.

## Running

- **On a modded Xbox:** copy the contents of `bin/` to a folder such as
  `E:\Apps\RidgeRacer\` and launch `default.xbe`.
- **From disc:** burn `RidgeRacer.iso` to a DVD-R.
- **In xemu:** load `RidgeRacer.iso` as the disc.

If `PSX.EXE` or `disc.iso` is missing, the XBE says so on screen and returns
to the dashboard after 15 seconds.

## Controls

The PlayStation buttons follow position: A is cross, B is circle.

| Xbox pad | PlayStation | in the race |
|---|---|---|
| left stick | neGcon twist | analogue steering |
| right trigger | neGcon I | analogue throttle |
| left trigger | neGcon II | analogue brake |
| d-pad | d-pad | digital steering |
| A / B | cross / circle | accelerate / brake (digital) |
| X / Y | square / triangle | |
| White / Black | L1 / R1 | |
| Start / Back | start / select | |
| **click both sticks** | — | open or close the settings menu |
| **Back + Start + both triggers** | — | return to the dashboard |

Ridge Racer was designed alongside Namco's neGcon controller and supports it
natively. The port presents the Xbox pad as a neGcon, so steering and pedals
are analogue. `CONTROLLER` in the settings menu switches to a standard digital
pad.

The settings menu (d-pad to move and change values, Back to close) sets the
frame rate, audio latency, draw-cost model, music and effects muting, and
controller type, and turns on the performance display.

## Sound and music

Engine, tyre and voice effects come from the emulated SPU and work with the
data track alone. **The music is CD audio**, stored as separate audio tracks
that an ISO cannot hold. For music, build with a raw rip of the disc and its
CUE sheet:

```sh
./build.sh DISC_ISO=... CUE="/path/to/Ridge Racer (Japan).cue"
```

Both common layouts work:

- one `.bin` per track, as Redump-style rips are made;
- the whole disc in a single `.bin` or `.img`, as ImgBurn and CDRWIN make it.
  The build splits it into tracks.

The rip must be raw: the CUE sheet should list the data track as `MODE2/2352`.
A 2048-byte "cooked" `.img` has no audio, and the build stops with a message.
CloneCD sets (`.img`/`.ccd`/`.sub`) have no CUE sheet; convert them first.
Tools that make a suitable rip include ImgBurn ("Create image file from
disc"), redumper and DiscImageCreator.

The audio tracks are copied to `bin/tracks/` and included in
`RidgeRacer.iso`. Building again without `CUE=` removes them.

## Video and frame rate

The game renders at the PlayStation's 320×240 and is scaled up. The output
mode follows the Xbox's AV cable and dashboard settings:

| Cable and dashboard | Output | Picture |
|---|---|---|
| HDTV pack, 720p enabled, NTSC console | 720p | 960×720, pillarboxed |
| HDTV pack, 480p enabled | 480p | 640×480 |
| composite, S-Video, SCART | 480i (576i on PAL) | 640×480 |

720p and 480p are enabled under **Settings → Video** in the Xbox dashboard.
nxdk has no HD modes for PAL consoles.

The game runs at 30 frames per second, as on the PlayStation. Ridge Racer's
logic is locked to the frame rate, so the game speed depends on it: at 60 the
whole game would run twice as fast. The display refreshes at 60 Hz and shows
each frame twice.

## Performance

Because the game is frame-locked, a frame that takes longer than 33 ms slows
the game down rather than dropping frames. The patch to the decompilation's
runtime speeds up the parts the Pentium III finds expensive without changing
a single pixel of output: the software rasteriser steps textures in fixed
point, set up once per triangle, and the GTE avoids 64-bit arithmetic and
function calls where the hardware's results allow. The picture is converted
and scaled straight from the PlayStation's video memory into a 15-bit screen.

On a stock console the game runs at 30 frames per second for most of a race,
and in the high twenties in the heaviest scenes.

To check the timing on your console, turn on **PERFORMANCE** in the settings
menu. A line at the top of the screen then shows, averaged over each second,
frames per second and then milliseconds per frame, in this form:

    FPS nn  GAME nn  DRAW nn  OUT nn  IN nn  IDLE nn

A frame has 33 ms at 30 fps; the five times add up to about that.

| Field | What it measures |
|---|---|
| `FPS` | frames per second; 30 is full speed |
| `GAME` | game code, geometry and sound, in ms per frame |
| `DRAW` | the GPU's polygons, rectangles and fills |
| `OUT` | converting and scaling the picture to the screen |
| `IN` | reading the controller |
| `IDLE` | time left over; 0 means the frame overran |

## Development

| | |
|---|---|
| `Makefile` | the nxdk build: translates `PSX.EXE`, compiles the runtime, packs the XISO |
| `gen.py` | runs the decompilation's translator with the Xbox's file paths, and splits CUE images |
| `src/xbox_main.c` | entry point: checks the files, runs the game on a 1 MB stack |
| `src/video_xbox.c` | video modes, scaling, frame pacing, pads, performance display |
| `src/compat/` | small compatibility headers for nxdk's C library |
| `patches/m0-xbox.patch` | changes to the decompilation's runtime, applied by `build.sh` |
| `test/host/` | runs the Xbox code on macOS against stand-ins for nxdk |
| `test/host/pictest.sh` | checks every pixel the scaler puts on screen against an independent formula |

The host test builds `src/` against stand-ins for the nxdk calls it uses and
plays the game headless, saving the frames the Xbox would display. It needs
SDL2 (`brew install sdl2`) and a completed build:

```sh
test/host/build.sh
HOST_D=bin HOST_OUT=test/host/out HOST_SECONDS=60 \
PAD_SCRIPT="1200:start 1260: 1600:start 1660: 2200:cross" test/host/xbox_host
```

## License

MIT; see [LICENSE](LICENSE). Ridge Racer itself is the property of Bandai
Namco and is not covered.
