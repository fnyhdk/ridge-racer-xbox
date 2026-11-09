/* The Xbox platform layer, run on the host.
 *
 * xbox_main.c and video_xbox.c are compiled unchanged against stand-ins for
 * the nxdk calls they make. The CRTC start register is a variable; the main
 * thread, which on the Xbox only waits for the game, here saves the buffer
 * that register designates -- what the television would be showing -- and
 * counts the flips. D:\ is mapped to the XBE's directory, as nxdk does.
 *
 *   HOST_D=../../bin HOST_OUT=out HOST_SECONDS=60 ./xbox_host
 *
 * HOST_NO_720P=1 plays a dashboard with 720p off, HOST_480I=1 one with 480p
 * off, and HOST_RES="20:1 30:0" moves RESOLUTION at 20 s and 30 s, as the
 * settings menu would. HOST_MENU=25 opens the settings menu at 25 s, with
 * the second item chosen.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "windows.h"
#include "hal/video.h"
#undef fopen

static VIDEO_MODE mode;
static void *buffers[4];
static int nbuffers;
static volatile unsigned int crtc_start, other_reg;
static volatile unsigned long crtc_writes;

FILE *host_fopen(const char *path, const char *m)
{
    char p[1024];
    const char *d = getenv("HOST_D");
    if ((path[0] == 'D' || path[0] == 'd') && path[1] == ':' && path[2] == '\\') {
        size_t i, n;
        snprintf(p, sizeof p, "%s/%s", d ? d : ".", path + 3);
        for (i = 0, n = strlen(p); i < n; i++) if (p[i] == '\\') p[i] = '/';
        return fopen(p, m);
    }
    return fopen(path, m);
}

VIDEO_MODE XVideoGetMode(void) { return mode; }
int XVideoSetMode(int w, int h, int bpp, int r)
{
    (void)r;
    /* HOST_NO_720P=1 plays a console whose dashboard has 720p switched off:
       nxdk refuses the mode and forgets the current one. */
    if (h == 720 && getenv("HOST_NO_720P")) { memset(&mode, 0, sizeof mode); return 0; }
    mode.width = w; mode.height = h; mode.bpp = bpp; mode.refresh = 60;
    return 1;
}

/* The modes nxdk would list: 480 always, 720p unless switched off. */
int XVideoListModes(VIDEO_MODE *vm, int bpp, int r, void **p)
{
    int i = (int)(intptr_t)*p;
    (void)r;
    if (i == 1 && getenv("HOST_NO_720P")) i++;
    if (i > 1) return 0;
    vm->width = i ? 1280 : 640; vm->height = i ? 720 : 480;
    vm->bpp = bpp; vm->refresh = 60;
    *p = (void *)(intptr_t)(i + 1);
    return 1;
}

unsigned int XVideoGetEncoderSettings(void)
{
    return getenv("HOST_480I") ? 0 : VIDEO_MODE_480P;
}

void XVideoFlushFB(void) {}

volatile unsigned int *host_videoreg(unsigned int off)
{
    if (off == 0x00600800) { crtc_writes++; return &crtc_start; }
    return &other_reg;
}

void *MmAllocateContiguousMemoryEx(size_t n, unsigned long lo, unsigned long hi,
                                   unsigned long align, unsigned long prot)
{
    void *p;
    (void)lo; (void)hi; (void)prot;
    if (posix_memalign(&p, align ? align : 16, n)) return 0;
    if (nbuffers < 4) buffers[nbuffers++] = p;
    return p;
}

void MmFreeContiguousMemory(void *p)
{
    int i;
    for (i = 0; i < nbuffers; i++)
        if (buffers[i] == p) { buffers[i] = buffers[--nbuffers]; break; }
    free(p);
}

struct thr { pthread_t t; LPTHREAD_START_ROUTINE f; LPVOID a; };
static void *trampoline(void *p) { struct thr *t = p; t->f(t->a); return 0; }

HANDLE CreateThread(void *sec, size_t stack, LPTHREAD_START_ROUTINE f, LPVOID a,
                    DWORD flags, DWORD *id)
{
    struct thr *t = calloc(1, sizeof *t);
    pthread_attr_t at;
    (void)sec; (void)flags; (void)id;
    t->f = f; t->a = a;
    pthread_attr_init(&at);
    /* The same stack the Xbox thread gets: a translation that needs more
       would overflow here first. */
    if (stack) pthread_attr_setstacksize(&at, stack);
    if (pthread_create(&t->t, &at, trampoline, t)) return 0;
    return t;
}

void Sleep(DWORD ms) { usleep((useconds_t)ms * 1000); }

BOOL QueryPerformanceFrequency(LARGE_INTEGER *f) { f->QuadPart = 1000000000LL; return 1; }
BOOL QueryPerformanceCounter(LARGE_INTEGER *c)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    c->QuadPart = (LONGLONG)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return 1;
}

/* What the CRTC points at, as a picture. */
static void capture(const char *dir, int n)
{
    int i, x, y;
    char path[1024];
    FILE *f;
    const unsigned int *fb = 0;
    for (i = 0; i < nbuffers; i++)
        if (((unsigned int)(uintptr_t)buffers[i] & 0x7FFFFFFF) == crtc_start)
            fb = buffers[i];
    if (!fb) { printf("host: the CRTC points at no buffer we allocated (%08X)\n", crtc_start); return; }
    snprintf(path, sizeof path, "%s/x%03d.ppm", dir, n);
    f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", mode.width, mode.height);
    for (y = 0; y < mode.height; y++)
        for (x = 0; x < mode.width; x++) {
            unsigned char px[3];
            if (mode.bpp == 15) {   /* X1R5G5B5, widened as a DAC would */
                unsigned int c = ((const unsigned short *)fb)[y * mode.width + x];
                unsigned int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
                px[0] = (unsigned char)((r << 3) | (r >> 2));
                px[1] = (unsigned char)((g << 3) | (g >> 2));
                px[2] = (unsigned char)((b << 3) | (b >> 2));
            } else {
                unsigned int c = fb[y * mode.width + x];
                px[0] = (unsigned char)(c >> 16); px[1] = (unsigned char)(c >> 8); px[2] = (unsigned char)c;
            }
            fwrite(px, 1, 3, f);
        }
    fclose(f);
}

DWORD WaitForSingleObject(HANDLE h, DWORD ms)
{
    const char *o = getenv("HOST_OUT"), *s = getenv("HOST_SECONDS");
    const char *res = getenv("HOST_RES");
    int seconds = s ? atoi(s) : 30, n = 0, t;
    unsigned long before = 0;
    (void)h; (void)ms;
    if (o) mkdir(o, 0755);
    for (t = 0; t < seconds * 2; t++) {
        usleep(500000);
        if (t % 2 == 1) {
            extern char mod_perf_text[64];
            printf("host: %lu flips in the last second  [%s]\n", crtc_writes - before, mod_perf_text);
            fflush(stdout);
            before = crtc_writes;
        }
        if (getenv("HOST_MENU") && t == 2 * atoi(getenv("HOST_MENU"))) {
            void mods_toggle(void), mods_move(int);
            mods_toggle();
            mods_move(+1);
        }
        if (res && t % 2 == 0) {
            /* RESOLUTION, moved as the menu moves it. The frame after is
               not captured: the buffers may be changing under it. */
            extern int mod_res;
            const char *q = res;
            int at, v, moved = 0;
            while (sscanf(q, "%d:%d", &at, &v) == 2) {
                if (at == t / 2) { mod_res = v; moved = 1; printf("host: RESOLUTION %d\n", v); }
                q = strchr(q, ' ');
                if (!q) break;
                q++;
            }
            if (moved) { n++; continue; }
        }
        if (o) capture(o, n++);
    }
    printf("host: %lu flips in %d s\n", crtc_writes, seconds);
    fflush(stdout);
    _exit(0);
    return 0;
}
