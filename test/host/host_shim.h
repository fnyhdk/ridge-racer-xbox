/* Force-included into the Xbox files when they are built for the host. */
#include <stdio.h>
FILE *host_fopen(const char *path, const char *mode);
#define fopen host_fopen
