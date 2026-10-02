#ifndef PM_TEST_PSPKERNEL_H
#define PM_TEST_PSPKERNEL_H
typedef int SceUID;
typedef unsigned int SceSize;
typedef unsigned int SceUInt;
typedef struct { SceSize size; int status; } SceKernelThreadInfo;
#define PSP_THREAD_ATTR_USER 1
#define PSP_THREAD_ATTR_VFPU 2
#define PSP_THREAD_STOPPED 16
#define PSP_THREAD_KILLED 32
SceUID sceKernelCreateSema(const char *, int, int, int, void *);
SceUID sceKernelCreateThread(const char *, int (*)(SceSize, void *), int, int, int, void *);
int sceKernelStartThread(SceUID, SceSize, void *);
int sceKernelDeleteThread(SceUID);
int sceKernelDeleteSema(SceUID);
int sceKernelWaitSema(SceUID, int, void *);
int sceKernelSignalSema(SceUID, int);
int sceKernelWaitThreadEnd(SceUID, SceUInt *);
int sceKernelReferThreadStatus(SceUID, SceKernelThreadInfo *);
int sceKernelDelayThread(unsigned int);
unsigned int sceKernelGetSystemTimeLow(void);
#endif
