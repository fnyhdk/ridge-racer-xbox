/* Force-included into every file of the Xbox build (-include).
 *
 * The runtime is written for Unix and Windows. nxdk's clang targets
 * i386-pc-win32, so it already takes the Windows branches -- <windows.h>, a
 * thread for the time limit -- and only the few names pdclib lacks are
 * supplied here. Keeping them in one header leaves tools/m0 untouched: the
 * Xbox build compiles the same files as the desktop.
 */
#ifndef XBOX_COMPAT_H
#define XBOX_COMPAT_H
#include <stdlib.h>

/* The runtime leaves through _exit() when the window closes or the time limit
   runs out. On the Xbox exit() hands control back to the dashboard, which is
   what leaving means there. */
#define _exit(status) exit(status)

#endif
