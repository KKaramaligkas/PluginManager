/*
    Plugin Manager for ARK-5
    clock.c: the C library's clocks, from the RTC and the system timer.

    The PSP SDK gets the date for time(), gettimeofday() and clock_gettime()
    from the kernel's sceKernelLibcGettimeofday(). On a real PSP that call
    doesn't return the date: with the clock set right, the Plugin Manager saw
    2 Jan 1970, 00:47. PPSSPP returns the full date, so the emulator didn't
    show it. mbedTLS checks certificate dates with time(), so every
    certificate looked "not valid yet".

    These replace the SDK's versions (the linker takes them before libcglue's):
    the date comes from the RTC, which counts UTC microseconds since
    1 Jan 0001 (the XMB adds the time zone), and the other clocks from the
    system timer, which counts microseconds since the PSP started.
*/

#include <errno.h>
#include <sys/time.h>
#include <time.h>

#include <pspkernel.h>
#include <psprtc.h>

/* 1 Jan 1970 in RTC ticks */
#define RTC_UNIX_EPOCH  62135596800000000ULL

/* newlib's time() and gettimeofday() call this */
int _gettimeofday(struct timeval *tp, void *tz)
{
    u64 tick;
    if (sceRtcGetCurrentTick(&tick) < 0 || tick < RTC_UNIX_EPOCH) {
        errno = EIO;
        return -1;
    }
    tick -= RTC_UNIX_EPOCH;
    if (tp) {
        tp->tv_sec = (time_t)(tick / 1000000);
        tp->tv_usec = (suseconds_t)(tick % 1000000);
    }
    if (tz) {
        struct timezone *z = tz;
        z->tz_minuteswest = 0;
        z->tz_dsttime = 0;
    }
    return 0;
}

/* curl measures its timeouts with CLOCK_MONOTONIC */
int clock_gettime(clockid_t clock_id, struct timespec *tp)
{
    if (!tp) {
        errno = EINVAL;
        return -1;
    }
    if (clock_id == CLOCK_REALTIME) {
        struct timeval tv;
        if (_gettimeofday(&tv, NULL) < 0) return -1;
        tp->tv_sec = tv.tv_sec;
        tp->tv_nsec = (long)tv.tv_usec * 1000;
        return 0;
    }
    SceInt64 us = sceKernelGetSystemTimeWide();
    tp->tv_sec = (time_t)(us / 1000000);
    tp->tv_nsec = (long)(us % 1000000) * 1000;
    return 0;
}
