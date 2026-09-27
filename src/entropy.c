/*
    Plugin Manager for ARK-5
    entropy.c: SHA-256 entropy pool backing getentropy() (used by mbedTLS
    through libcurl). See entropy.h for why the libc version isn't good enough.
*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <pspkernel.h>
#include <psppower.h>
#include <psprtc.h>
#include <pspwlan.h>

#include <mbedtls/sha256.h>

#include "entropy.h"

static unsigned char pool[32];
static uint64_t counter;
static SceUID lock = -1;
static int ready;

static void mix(const void *data, size_t len)
{
    mbedtls_sha256_context ctx;
    uint64_t now = sceKernelGetSystemTimeWide();
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);
    mbedtls_sha256_update_ret(&ctx, pool, sizeof(pool));
    mbedtls_sha256_update_ret(&ctx, (const unsigned char *)&now, sizeof(now));
    if (data && len) mbedtls_sha256_update_ret(&ctx, data, len);
    mbedtls_sha256_finish_ret(&ctx, pool);
    mbedtls_sha256_free(&ctx);
}

void entropy_add(const void *data, size_t len)
{
    if (!ready) return;
    sceKernelWaitSema(lock, 1, NULL);
    mix(data, len);
    sceKernelSignalSema(lock, 1);
}

void entropy_init(void)
{
    if (ready) return;
    lock = sceKernelCreateSema("pm_entropy", 0, 1, 1, NULL);

    struct {
        uint64_t wide;
        u64 tick;
        u32 low;
        int free_mem, max_mem;
        int batt_volt, batt_temp, batt_pct;
        unsigned char mac[8];
        void *heap, *stack;
        u32 jitter[64];
    } seed;
    memset(&seed, 0, sizeof(seed));

    seed.wide = sceKernelGetSystemTimeWide();
    sceRtcGetCurrentTick(&seed.tick);
    seed.free_mem = sceKernelTotalFreeMemSize();
    seed.max_mem = sceKernelMaxFreeMemSize();
    seed.batt_volt = scePowerGetBatteryVolt();
    seed.batt_temp = scePowerGetBatteryTemp();
    seed.batt_pct = scePowerGetBatteryLifePercent();
    sceWlanGetEtherAddr(seed.mac);
    seed.heap = malloc(16);
    seed.stack = &seed;
    free(seed.heap);

    /* scheduler / bus timing jitter */
    for (int i = 0; i < 64; i++) {
        u32 t0 = sceKernelGetSystemTimeLow();
        sceKernelDelayThread(i & 3);
        seed.jitter[i] = sceKernelGetSystemTimeLow() - t0;
    }
    seed.low = sceKernelGetSystemTimeLow();

    mix(&seed, sizeof(seed));
    memset(&seed, 0, sizeof(seed));
    ready = 1;
}

/* Replaces newlib's getentropy(), which mbedTLS uses as its platform source. */
int getentropy(void *buffer, size_t length)
{
    unsigned char *out = buffer;
    if (!ready) entropy_init();

    sceKernelWaitSema(lock, 1, NULL);
    while (length > 0) {
        unsigned char block[32];
        mbedtls_sha256_context ctx;
        uint64_t now = sceKernelGetSystemTimeWide();

        counter++;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts_ret(&ctx, 0);
        mbedtls_sha256_update_ret(&ctx, pool, sizeof(pool));
        mbedtls_sha256_update_ret(&ctx, (const unsigned char *)&counter, sizeof(counter));
        mbedtls_sha256_update_ret(&ctx, (const unsigned char *)&now, sizeof(now));
        mbedtls_sha256_finish_ret(&ctx, block);
        mbedtls_sha256_free(&ctx);

        size_t n = length < sizeof(block) ? length : sizeof(block);
        memcpy(out, block, n);
        out += n;
        length -= n;

        /* forward secrecy: never reuse the pool that produced an output */
        mix(block, sizeof(block));
        memset(block, 0, sizeof(block));
    }
    sceKernelSignalSema(lock, 1);
    return 0;
}
