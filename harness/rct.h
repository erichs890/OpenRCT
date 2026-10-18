/* Differential harness: load the original image and call its functions. */
#ifndef RCT_HARNESS_H
#define RCT_HARNESS_H
#include <stddef.h>
#include <stdint.h>

#include "image.h"

/* Load RCT (argv: [path] [--hook N|all]; default original/RCT.unpacked.exe, the
   shipped RCT.EXE works too). Neither the CRT nor WinMain is run. With --hook,
   the "original" side calls go through runtime/hooks.c, so the same tests
   check every register adapter inside real asm call chains. Exits on failure. */
void rct_harness_init(int argc, char **argv);

/* Deterministic PRNG for test inputs: uniform in [0, n). */
uint32_t rnd(uint32_t n);

/* Write data to a scratch file and return a fresh read handle (HANDLE) on it. */
void *rct_temp_file(const void *data, size_t len);

/* An empty scratch file opened read/write (deleted on close). */
void *rct_temp_rw(void);

/* Whole contents of an open file into buf (at most cap bytes); returns the size.
   The file position is restored. */
size_t rct_file_contents(void *handle, void *buf, size_t cap);

/* Tiny check helpers. */
extern int rct_failures;
#define CHECK(cond, ...) do { if (!(cond)) { rct_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#endif
