/* The in-process unpacker (runtime/image.c on the shipped RCT.EXE) must give
   the same image as tools/unpack.py. Usage: test_image <RCT.EXE> <RCT.unpacked.exe> */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rct.h"

int main(int argc, char **argv)
{
    const char *packed = argc > 1 ? argv[1] : "original/RCT.EXE";
    const char *unpacked = argc > 2 ? argv[2] : "original/RCT.unpacked.exe";
    RctImage img;
    const char *err = rct_load_image(packed, &img);
    if (err) { printf("%s\n", err); return 2; }
    CHECK(img.entry == 0x58E06D, "entry %08X", img.entry);

    FILE *f = fopen(unpacked, "rb");
    if (!f) { printf("cannot open %s\n", unpacked); return 2; }
    static uint8_t want[0x850000];
    size_t n = fread(want, 1, sizeof want, f);
    fclose(f);
    /* Skip what legitimately differs: the bound IAT (0x787000, 0x364 bytes) and
       the wrapper's own section (.neolit), whose stage-2 data keeps pointers to
       its work buffers (Unicorn addresses vs heap addresses). */
    size_t diffs = 0, first = 0;
    for (size_t o = 0x1000; o < n; o++) {
        uint32_t a = 0x400000 + (uint32_t)o;
        if ((a >= 0x787000 && a < 0x787364) || (a >= 0xC46000 && a < 0xC4C000)) continue;
        if (((uint8_t *)0x400000)[o] != want[o]) { if (!diffs++) first = a; if (getenv("SHOWDIFF")) printf("%08X %02X %02X\n", a, ((uint8_t *)0x400000)[o], want[o]); }
    }
    CHECK(diffs == 0, "%u bytes differ from %s, first at %08X", (unsigned)diffs, unpacked, (unsigned)first);
    printf("%s (%d failures)\n", rct_failures ? "FAILED" : "ok", rct_failures);
    return rct_failures != 0;
}
