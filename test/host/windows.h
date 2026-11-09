/* Host stand-in for the few Win32/kernel names the Xbox files use. */
#ifndef HOST_WINDOWS_H
#define HOST_WINDOWS_H
#include <stddef.h>
typedef unsigned int DWORD;
typedef int BOOL;
typedef void *HANDLE, *LPVOID;
typedef long long LONGLONG;
typedef union { LONGLONG QuadPart; } LARGE_INTEGER;
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);
#define WINAPI
#define INFINITE 0xFFFFFFFFu
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOMBINE 0x400
HANDLE CreateThread(void *, size_t, LPTHREAD_START_ROUTINE, LPVOID, DWORD, DWORD *);
DWORD WaitForSingleObject(HANDLE, DWORD);
void Sleep(DWORD);
BOOL QueryPerformanceCounter(LARGE_INTEGER *);
BOOL QueryPerformanceFrequency(LARGE_INTEGER *);
void *MmAllocateContiguousMemoryEx(size_t, unsigned long, unsigned long, unsigned long, unsigned long);
void MmFreeContiguousMemory(void *);
#endif
