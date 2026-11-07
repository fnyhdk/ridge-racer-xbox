/* video_image against an independent statement of what it should produce:
 * every screen pixel is the VRAM pixel nearest-neighbour above it, its red and
 * blue swapped into X1R5G5B5. Random VRAM, every display width the console
 * offers, wrapping display areas, both screen modes. The bars must stay black.
 *
 *   sh test/host/pictest.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "windows.h"
#include "hal/video.h"

typedef uint16_t u16; typedef uint32_t u32;
int video_init(void);
void video_image(const u16 *, int, int, int, int);

static VIDEO_MODE mode;
static void *bufs[2]; static int nb;
static volatile unsigned int crtc, other;
static int no720;
VIDEO_MODE XVideoGetMode(void) { return mode; }
int XVideoSetMode(int w, int h, int bpp, int r)
{ (void)r; if (h == 720 && no720) { memset(&mode, 0, sizeof mode); return 0; }
  mode.width = w; mode.height = h; mode.bpp = bpp; return 1; }
void XVideoFlushFB(void) {}
volatile unsigned int *host_videoreg(unsigned int o) { return o == 0x00600800 ? &crtc : &other; }
void *MmAllocateContiguousMemoryEx(size_t n, unsigned long a, unsigned long b, unsigned long al, unsigned long p)
{ void *q; (void)a; (void)b; (void)p; if (posix_memalign(&q, al, n)) return 0; bufs[nb++ & 1] = q; return q; }
void Sleep(DWORD ms) { (void)ms; }
BOOL QueryPerformanceFrequency(LARGE_INTEGER *f) { f->QuadPart = 1000000; return 1; }
BOOL QueryPerformanceCounter(LARGE_INTEGER *c) { static long long t; c->QuadPart = t += 1000000; return 1; }
FILE *host_fopen(const char *p, const char *m) { return fopen(p, m); }
char mod_perf_text[64];
unsigned long long (*gpu_clock)(void);
unsigned long long gpu_draw_ticks;
double mod_hz;
static int menu;
int mods_is_open(void) { return menu; }
void mods_toggle(void) {}
void mods_move(int d) { (void)d; }
void mods_change(int d) { (void)d; }
/* The overlay: opaque pixels where the real one draws text -- the top rows
   and the bottom line -- and, with the menu open, anywhere. */
static int over_x(int i, int w) { return (i * 37) % w; }
static int over_y(int i, int h) { return i < 40 ? (i * 7) % 24 : i < 80 ? h - 1 - (i % 14) : (i * 13) % h; }
static u32 over_c(int i) { return 0xFF000000u | (u32)(i * 0x9E3779B1u >> 8); }
static int over_n(void) { return menu ? 120 : 80; }
void mods_draw(u32 *p, int pitch, int w, int h)
{
    int i;
    for (i = 0; i < over_n(); i++) p[over_y(i, h) * (pitch / 4) + over_x(i, w)] = over_c(i);
}

static u16 vram[1024 * 512];

static int check(int x, int y, int w, int h)
{
    int X, Y, bad = 0, pw = mode.height * 4 / 3, px = (mode.width - pw) / 2;
    const u16 *fb = 0;
    if (((uintptr_t)bufs[0] & 0x7FFFFFFF) == crtc) fb = bufs[0];
    if (((uintptr_t)bufs[1] & 0x7FFFFFFF) == crtc) fb = bufs[1];
    if (!fb) { printf("CRTC points nowhere\n"); return 1; }
    for (Y = 0; Y < mode.height; Y++)
        for (X = 0; X < mode.width; X++) {
            u16 got = fb[Y * mode.width + X], want = 0;
            if (X >= px && X < px + pw) {
                int sx = (X - px) * w / pw, sy = Y * h / mode.height;
                u16 c = vram[((y + sy) & 511) * 1024 + ((x + sx) & 1023)];
                int o;
                want = (u16)(((c & 31) << 10) | (c & 0x3E0) | ((c >> 10) & 31));
                for (o = 0; o < over_n(); o++)   /* the last one drawn wins */
                    if (over_x(o, w) == sx && over_y(o, h) == sy) {
                        u32 v = over_c(o);
                        want = (u16)(((v >> 9) & 0x7C00) | ((v >> 6) & 0x3E0) | ((v >> 3) & 31));
                    }
            }
            if (got != want && bad++ < 3)
                printf("  (%d,%d) got %04X want %04X\n", X, Y, got, want);
        }
    printf("%dx%d screen, menu %s, area %d,%d %dx%d: %s\n", mode.width, mode.height,
           menu ? "open" : "closed", x, y, w, h,
           bad ? "WRONG" : "exact");
    return bad != 0;
}

int main(int argc, char **argv)
{
    static const int areas[][4] = {
        {0, 0, 320, 240}, {0, 240, 320, 240}, {960, 500, 320, 240},
        {0, 0, 256, 240}, {100, 3, 368, 240}, {0, 0, 512, 480}, {700, 100, 640, 480},
        {0, 0, 321, 241}, {5, 7, 320, 30},
    };
    int i, k, fails = 0;
    no720 = argc > 1;
    srand(1);
    if (!video_init()) { printf("video_init failed\n"); return 1; }
    for (k = 0; k < 4; k++) {
        menu = k & 1;
        for (i = 0; i < 1024 * 512; i++) vram[i] = (u16)(rand() ^ (rand() << 8));
        for (i = 0; i < (int)(sizeof areas / sizeof areas[0]); i++) {
            video_image(vram, areas[i][0], areas[i][1], areas[i][2], areas[i][3]);
            fails += check(areas[i][0], areas[i][1], areas[i][2], areas[i][3]);
        }
    }
    printf(fails ? "FAILED\n" : "all exact\n");
    return fails != 0;
}
