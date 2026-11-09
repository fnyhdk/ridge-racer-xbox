/* Host stand-in for nxdk's hal/video.h: the CRTC registers are memory. */
#ifndef HOST_HAL_VIDEO_H
#define HOST_HAL_VIDEO_H
typedef struct { int width, height, bpp, refresh; } VIDEO_MODE;
#define REFRESH_DEFAULT 0
#define VIDEO_MODE_480P 0x80000
VIDEO_MODE XVideoGetMode(void);
int XVideoSetMode(int, int, int, int);
int XVideoListModes(VIDEO_MODE *, int, int, void **);
unsigned int XVideoGetEncoderSettings(void);
void XVideoFlushFB(void);
volatile unsigned int *host_videoreg(unsigned int offset);
#define VIDEOREG(x) (*host_videoreg(x))
#endif
