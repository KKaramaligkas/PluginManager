/* Minimal test helpers for the host unit tests. */

#ifndef PM_TEST_H
#define PM_TEST_H

#include <stdio.h>
#include <string.h>

extern int test_failures;
extern int test_checks;

#define CHECK(cond) do { \
    test_checks++; \
    if (!(cond)) { \
        test_failures++; \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_STR(a, b) do { \
    const char *_a = (a), *_b = (b); \
    test_checks++; \
    if (!_a || !_b || strcmp(_a, _b) != 0) { \
        test_failures++; \
        fprintf(stderr, "%s:%d: expected \"%s\" got \"%s\"\n", __FILE__, __LINE__, _b ? _b : "(null)", _a ? _a : "(null)"); \
    } \
} while (0)

#define CHECK_INT(a, b) do { \
    long long _a = (a), _b = (b); \
    test_checks++; \
    if (_a != _b) { \
        test_failures++; \
        fprintf(stderr, "%s:%d: %s: expected %lld got %lld\n", __FILE__, __LINE__, #a, _b, _a); \
    } \
} while (0)

#endif
