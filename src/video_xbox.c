/* The Xbox's screen and controllers -- the Xbox side of video.c.
 *
 * It provides the same seven names as video.c and nothing else: the rest of
 * the runtime does not know which of the two it was linked with.
 *
 * The picture
 * -----------
 * The rasteriser is the runtime's, unchanged: it draws into the 1024 by 512
 * video memory of the PlayStation, in software. What happens here is only the
 * last step -- the display area the GPU designates, converted and scaled to
 * the screen the Xbox puts out.
 *
 * The screen is 15 bits a pixel, the PlayStation's own depth: nothing is lost,
 * and half the bytes cross the processor's bus as at 32. At 720p the picture
 * is 960 by 720, and that bus -- 64 bits at 133 MHz, shared with nothing
 * faster -- was a large part of the time spent putting it on screen.
 *
 * No SDL renderer in between. nxdk's SDL can draw into a window surface, but
 * the window surface is itself a copy of the framebuffer; writing the
 * framebuffer directly saves a full-screen copy per frame, which on a 733 MHz
 * processor is not nothing.
 *
 * Two framebuffers, and the CRTC is pointed at the finished one. It latches
 * its start address at the vertical blank, so a frame is never shown half
 * drawn, and the one being drawn is the one nobody is looking at.
 *
 * The controllers
 * ---------------
 * Read through SDL's game-controller layer, which nxdk maps from the Xbox
 * pad: A, B, X, Y by position, the analogue triggers as axes, White and Black
 * as the shoulders. The layout is the desktop's:
 *
 *   A           cross    -- accelerate        B      circle   -- brake
 *   X           square                        Y      triangle
 *   White       L1                            Black  R1
 *   right trigger  neGcon I  (throttle, analogue)
 *   left trigger   neGcon II (brake, analogue)
 *   left stick  twist (steering, analogue); d-pad digital directions
 *   Start       start                         Back   select
 *
 *   both sticks clicked together   open or close the settings menu
 *   Back + Start + both triggers   return to the dashboard
 */
#include "rt.h"
#include <hal/video.h>
#include <windows.h>
#include <SDL.h>
#include <stdio.h>
#include <string.h>

int mods_is_open(void);
void mods_toggle(void);
void mods_move(int);
void mods_change(int);
void mods_draw(u32 *, int, int, int);

#define PCRTC_START 0x00600800

/* The screen, and the 4:3 picture inside it.
 *
 * 720p when the dashboard allows it -- an HDTV AV pack, 720p ticked, and an
 * NTSC console: nxdk offers no HD mode in PAL. The PlayStation's 240 lines
 * triple exactly into 720, and its 4:3 picture is 960 wide, between two black
 * bars. Otherwise 640 by 480, which the console sends as 480p or 480i by the
 * same settings. */
static int screen_w, screen_h;
static int pic_x, pic_w, pic_h;

/* Thirty presented frames per second, not sixty.
 *
 * The game is frame-locked: every state calls VSync(0) once per frame, after
 * polling VSync(1) until a fixed time has passed -- a limiter that holds the
 * console at two vertical blanks per frame. Its logic advances by one
 * thirtieth of a second per frame, so the race clock counts down two seconds
 * per second when frames are shown at sixty. Measured, not assumed: at 60 the
 * TIME display fell 29 units in 14 s; at 30 it falls one per second. The
 * settings menu can still change it. */
double mod_hz = 30.0;
#define hz mod_hz

static u32 *screens[2];   /* two 15-bit pixels to a word */
static int back;
/* The display area converted to 32 bits, before scaling. The menu is drawn
   into it at the console's resolution, as on the desktop. */
static u32 frame[640 * 480];
static int closed;
static u32 buttons = 0xFFFF;
static SDL_GameController *pad;

static void perf_init(void);
static void conv_init(void);

static void pad_open(int index)
{
    if (pad) return;
    pad = SDL_GameControllerOpen(index);
    if (pad) printf("controller: %s\n", SDL_GameControllerName(pad));
}

static void pad_close(void)
{
    if (pad) { SDL_GameControllerClose(pad); pad = 0; }
}

int video_init(void)
{
    int i;
    conv_init();
    if (XVideoSetMode(1280, 720, 15, REFRESH_DEFAULT)) {
        screen_w = 1280; screen_h = 720;
    } else {
        VIDEO_MODE vm = XVideoGetMode();
        if (vm.width != 640 || vm.height != 480 || vm.bpp != 15)
            if (!XVideoSetMode(640, 480, 15, REFRESH_DEFAULT)) return 0;
        screen_w = 640; screen_h = 480;
    }
    pic_h = screen_h;
    pic_w = screen_h * 4 / 3;
    pic_x = (screen_w - pic_w) / 2;
    printf("video: %dx%d, picture %dx%d\n", screen_w, screen_h, pic_w, pic_h);
    /* The bars are cleared here once; only the picture is written after. */
    for (i = 0; i < 2; i++) {
        screens[i] = MmAllocateContiguousMemoryEx(screen_w * screen_h * 2,
                                                  0, 0x7FFFFFFF, 0x1000,
                                                  PAGE_READWRITE | PAGE_WRITECOMBINE);
        if (!screens[i]) return 0;
        memset(screens[i], 0, screen_w * screen_h * 2);
    }
    VIDEOREG(PCRTC_START) = (unsigned int)screens[0] & 0x7FFFFFFF;
    back = 1;
    perf_init();

    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL : %s\n", SDL_GetError());
        return 0;
    }
    /* A pad already plugged in raises no event: look for it once. */
    for (i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) { pad_open(i); break; }
    if (!pad) printf("controller: none yet\n");
    return 1;
}

/* --- pacing --------------------------------------------------------------
 *
 * As on the desktop: the game blocks on its own buffer swap, so waiting out
 * the rest of the sixtieth of a second here paces everything else. Lateness
 * does not accumulate. The kernel's performance counter is the timer; Sleep
 * gives the processor back to the audio and USB threads for most of the wait,
 * and the last millisecond is spun, Sleep being too coarse for it. */
static LARGE_INTEGER next_frame, freq;

static void pace(void)
{
    LARGE_INTEGER now;
    if (hz <= 0.0) return;
    if (!next_frame.QuadPart) QueryPerformanceCounter(&next_frame);
    next_frame.QuadPart += (LONGLONG)((double)freq.QuadPart / hz);
    QueryPerformanceCounter(&now);
    if (now.QuadPart > next_frame.QuadPart) { next_frame = now; return; }
    {
        double left = (double)(next_frame.QuadPart - now.QuadPart) / (double)freq.QuadPart;
        if (left > 0.002) Sleep((DWORD)((left - 0.001) * 1000.0));
        do QueryPerformanceCounter(&now); while (now.QuadPart < next_frame.QuadPart);
    }
}

/* --- where the time goes -------------------------------------------------
 *
 * The game advances one step per frame shown, so a frame that takes longer
 * than a thirtieth of a second slows the game itself. PERFORMANCE in the
 * settings shows, averaged over each second: frames per second, then the
 * milliseconds per frame spent running the game, drawing its polygons,
 * putting the picture on screen, reading the controller, and waiting for the
 * next frame. */
extern char mod_perf_text[64];
extern unsigned long long (*gpu_clock)(void);
extern unsigned long long gpu_draw_ticks;

static unsigned long long perf_now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (unsigned long long)t.QuadPart;
}

static void perf_init(void)
{
    QueryPerformanceFrequency(&freq);
    gpu_clock = perf_now;
}

static unsigned long long perf_resumed, perf_window, perf_draw0;
static unsigned long long perf_run, perf_out, perf_in, perf_idle;
static unsigned perf_frames;

static void perf_frame(unsigned long long start, unsigned long long flipped,
                       unsigned long long shown, unsigned long long end)
{
    double ms;
    unsigned long long draw;
    if (perf_resumed) perf_run += start - perf_resumed;
    perf_out += flipped - start;
    perf_in += shown - flipped;
    perf_idle += end - shown;
    perf_resumed = end;
    perf_frames++;
    if (!perf_window) { perf_window = end; perf_draw0 = gpu_draw_ticks; return; }
    if (end - perf_window < (unsigned long long)freq.QuadPart) return;

    ms = 1000.0 / (double)freq.QuadPart / perf_frames;
    draw = gpu_draw_ticks - perf_draw0;
    sprintf(mod_perf_text, "FPS %d  GAME %d  DRAW %d  OUT %d  IN %d  IDLE %d",
            (int)(perf_frames * (double)freq.QuadPart / (double)(end - perf_window) + 0.5),
            (int)((perf_run > draw ? perf_run - draw : 0) * ms + 0.5),
            (int)(draw * ms + 0.5), (int)(perf_out * ms + 0.5),
            (int)(perf_in * ms + 0.5), (int)(perf_idle * ms + 0.5));
    perf_window = end;
    perf_draw0 = gpu_draw_ticks;
    perf_run = perf_out = perf_in = perf_idle = 0;
    perf_frames = 0;
}

/* --- the controller --------------------------------------------------- */

static u32 pad_read(void)
{
    u32 m = 0xFFFF;
    int x, y;
    const int DEADZONE = 12000;
    if (!pad) return m;
    SDL_GameControllerUpdate();
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP))    m &= ~0x1000u;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) m &= ~0x2000u;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  m &= ~0x4000u;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  m &= ~0x8000u;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A))     m &= ~0x0040u;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B))     m &= ~0x0020u;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X))     m &= ~0x0080u;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y))     m &= ~0x0010u;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  m &= ~0x0004u;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) m &= ~0x0008u;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START)) m &= ~0x0800u;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK))  m &= ~0x0100u;
    if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  > 8000) m &= ~0x0001u;
    if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000) m &= ~0x0002u;
    x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (x < -DEADZONE) m &= ~0x8000u;
    if (x >  DEADZONE) m &= ~0x2000u;
    if (y < -DEADZONE) m &= ~0x1000u;
    if (y >  DEADZONE) m &= ~0x4000u;
    return m;
}

/* The neGcon's four analogue bytes, exactly as video.c builds them: see the
   comment there for the game's thresholds (0x6A for a full press). */
int video_negcon(unsigned char *q)
{
    const int FULL = 0x6A;
    const int DEADZONE = 3000;
    int x, t, i, ii, l;
    if (!pad) return 0;

    x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    if (x > -DEADZONE && x < DEADZONE) x = 0;
    else x -= (x > 0 ? DEADZONE : -DEADZONE);
    t = 0x80 + x * 127 / (32767 - DEADZONE);
    if (t < 0) t = 0;
    if (t > 255) t = 255;

    i  = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) * FULL / 32767;
    ii = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  * FULL / 32767;
    l  = 0;
    if (i < 0) i = 0;
    if (ii < 0) ii = 0;

    if (!(buttons & 0x8000u)) t = 0x00;
    if (!(buttons & 0x2000u)) t = 0xFF;
    if (!(buttons & 0x0040u)) i = FULL;
    if (!(buttons & 0x0020u)) ii = FULL;
    if (!(buttons & 0x0004u)) l = FULL;

    q[0] = (unsigned char)t;
    q[1] = (unsigned char)i;
    q[2] = (unsigned char)ii;
    q[3] = (unsigned char)l;
    return 1;
}

/* There is no F1 and no Escape: the two chords below stand in for them. Both
   are combinations no one presses while driving. */
static void poll_input(void)
{
    SDL_Event e;
    static int chord_before;
    int chord;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_CONTROLLERDEVICEADDED) { pad_open(e.cdevice.which); continue; }
        if (e.type == SDL_CONTROLLERDEVICEREMOVED) { pad_close(); continue; }
    }
    if (!pad) {
        /* Not every pad announces itself with an event on nxdk: keep
           looking while there is none. */
        int i;
        for (i = 0; i < SDL_NumJoysticks(); i++)
            if (SDL_IsGameController(i)) { pad_open(i); break; }
        buttons = 0xFFFF;
        return;
    }
    SDL_GameControllerUpdate();
    chord = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSTICK)
         && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    if (chord && !chord_before) mods_toggle();
    chord_before = chord;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK)
        && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START)
        && SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000
        && SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000)
        closed = 1;

    if (mods_is_open()) {
        static u32 before = 0xFFFF;
        u32 now = pad_read();
        u32 pressed = before & ~now;
        if (pressed & 0x1000u) mods_move(-1);
        if (pressed & 0x4000u) mods_move(+1);
        if (pressed & 0x8000u) mods_change(-1);
        if (pressed & 0x2000u) mods_change(+1);
        if (pressed & 0x0100u) mods_toggle();   /* Back closes it too */
        before = now;
        buttons = 0xFFFF;
        return;
    }
    buttons = pad_read();
}

/* --- the picture -------------------------------------------------------- */

/* VRAM's colour (red in the low five bits) to 32 bits, for the menu to draw
 * on, through one table per byte of the source: two kilobytes that stay in
 * the first-level cache, where the arithmetic cost a dozen instructions a
 * pixel. Green straddles the two bytes, but its expansion (g << 3) + (g >> 2)
 * splits exactly into a part from each, so the two entries are added. */
static u32 conv_lo[256], conv_hi[256];

static void conv_init(void)
{
    u32 i;
    for (i = 0; i < 256; i++) {
        u32 r = i & 31, gl = i >> 5, gh = i & 3, b = (i >> 2) & 31;
        conv_lo[i] = (((r << 3) | (r >> 2)) << 16) | (((gl << 3) + (gl >> 2)) << 8);
        conv_hi[i] = 0xFF000000u | (((gh << 6) + (gh << 1)) << 8) | ((b << 3) | (b >> 2));
    }
}

/* And back down to the screen's 15 bits, red now in the high five. The top
   five bits of each expanded component are the original five. */
static INLINE u32 pack15(u32 c)
{
    return ((c >> 9) & 0x7C00) | ((c >> 6) & 0x03E0) | ((c >> 3) & 0x001F);
}

/* VRAM's colour straight to the screen's: the same five bits a component,
   red and blue exchanged. */
static INLINE u32 swap15(u32 c)
{
    return ((c & 31) << 10) | (c & 0x03E0) | ((c >> 10) & 31);
}

/* With the menu closed, the overlay writes text in the top rows and on the
   bottom line, and nothing else: only those bands go through the 32-bit
   frame. Something drawn outside them would simply not be shown. */
#define BAND_TOP 24
#define BAND_BOTTOM 14

void video_image(const u16 *vram, int x, int y, int w, int h)
{
    static int xmap[960];
    static int mapped_w;
    /* One source line at the screen's depth, and one scaled line of the
       picture, two pixels to a word: both stay in the first-level cache. */
    static u32 line15[640];
    static u32 row[480];
    u32 *dst;
    int i, j, k, last, words, open;
    unsigned long long start = perf_now(), flipped, shown;
    if (!screens[0]) return;
    if (w > 640) w = 640;
    if (h > 480) h = 480;
    if (w <= 0 || h <= 0) return;
    x &= 1023;

    /* The menu draws on a 32-bit picture, so with it open the whole display
       area is converted first. Otherwise only the bands are, and they are
       left clear: the overlay's colours are opaque, so 0 means "not drawn
       here". */
    open = mods_is_open();
    if (open) {
        for (j = 0; j < h; j++) {
            u32 *f = frame + j * w;
            const u16 *src = vram + (((y + j) & 511) << 10);
            for (i = 0; i < w; i++) {
                u32 c = src[(x + i) & 1023];
                f[i] = conv_lo[c & 0xFF] + conv_hi[c >> 8];
            }
        }
    } else {
        memset(frame, 0, sizeof(u32) * w * (h < BAND_TOP ? h : BAND_TOP));
        if (h > BAND_TOP) {
            int b0 = h - BAND_BOTTOM < BAND_TOP ? BAND_TOP : h - BAND_BOTTOM;
            memset(frame + b0 * w, 0, sizeof(u32) * w * (h - b0));
        }
    }
    mods_draw(frame, w * 4, w, h);

    /* Nearest-neighbour into the picture: 320 by 240 doubles or triples
       exactly, the other widths the console offers (256, 368, 512) stretch
       to the full width as a television would.
     *
     * Each source line is read once and scaled once, into buffers that stay
     * in the cache, and copied to the screen as many times as it repeats.
     *
     * Memory, not arithmetic, is the cost here. The Pentium III does not
     * prefetch: a line of video memory not in the cache is a full trip to
     * RAM, waited for. So the next source line is asked for while this one
     * is worked on, and there is no intermediate picture, which used to be
     * written out and read back from RAM at twice the size of the source.
     * The framebuffer is write-combined: written in order it is fast, read
     * back it is uncached and very slow, so the copies come from the buffer,
     * never from the line above. */
    if (mapped_w != w) {
        for (i = 0; i < pic_w; i++) xmap[i] = i * w / pic_w;
        mapped_w = w;
    }
    words = pic_w / 2;
    dst = screens[back] + pic_x / 2;
    last = -1;
    for (j = 0; j < pic_h; j++) {
        u32 *out = dst + j * (screen_w / 2);
        k = j * h / pic_h;
        if (k != last) {
            const u16 *src = vram + (((y + k) & 511) << 10);
            const u32 *f = frame + k * w;
            last = k;
            if (!open && k + 1 < h) {
                const u16 *next = vram + (((y + k + 1) & 511) << 10);
                for (i = 0; i < w; i += 16) __builtin_prefetch(next + ((x + i) & 1023), 0, 0);
            }
            if (open) {
                for (i = 0; i < w; i++) line15[i] = pack15(f[i]);
            } else if (k < BAND_TOP || k >= h - BAND_BOTTOM) {
                for (i = 0; i < w; i++)
                    line15[i] = f[i] ? pack15(f[i]) : swap15(src[(x + i) & 1023]);
            } else if (x + w <= 1024) {
                src += x;
                for (i = 0; i < w; i++) line15[i] = swap15(src[i]);
            } else {
                for (i = 0; i < w; i++) line15[i] = swap15(src[(x + i) & 1023]);
            }
            if (pic_w == 3 * w) {
                for (i = 0; i < w; i += 2) {
                    u32 a = line15[i], b = line15[i + 1];
                    u32 *o = row + i / 2 * 3;
                    o[0] = a | (a << 16); o[1] = a | (b << 16); o[2] = b | (b << 16);
                }
            } else if (pic_w == 2 * w) {
                for (i = 0; i < w; i++) { u32 a = line15[i]; row[i] = a | (a << 16); }
            } else {
                for (i = 0; i < words; i++)
                    row[i] = line15[xmap[2 * i]] | (line15[xmap[2 * i + 1]] << 16);
            }
        }
        for (i = 0; i + 8 <= words; i += 8) {   /* a whole 32-byte line at a time */
            out[i] = row[i];         out[i + 1] = row[i + 1];
            out[i + 2] = row[i + 2]; out[i + 3] = row[i + 3];
            out[i + 4] = row[i + 4]; out[i + 5] = row[i + 5];
            out[i + 6] = row[i + 6]; out[i + 7] = row[i + 7];
        }
        for (; i < words; i++) out[i] = row[i];
    }
    XVideoFlushFB();   /* drain the write-combining buffers before the flip */
    VIDEOREG(PCRTC_START) = (unsigned int)screens[back] & 0x7FFFFFFF;
    back ^= 1;
    flipped = perf_now();

    poll_input();
    shown = perf_now();
    pace();
    perf_frame(start, flipped, shown, perf_now());
}

u32 video_pad(void) { return buttons; }
int video_closed(void) { return closed; }
void video_quit(void) { SDL_Quit(); }
