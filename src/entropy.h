/*
    Plugin Manager for ARK-5
    entropy.h: randomness for TLS.

    The PSP C runtime implements getentropy() with a Mersenne Twister seeded
    from time(), which makes TLS session keys guessable. We provide our own
    getentropy() backed by a SHA-256 pool fed with high resolution timers,
    hardware identifiers and the timing of every button press.
*/

#ifndef PM_ENTROPY_H
#define PM_ENTROPY_H

#include <stddef.h>

void entropy_init(void);
void entropy_add(const void *data, size_t len);

#endif
