/* Differential tests: original 0x584F20..0x585000 vs src/platform_file.c. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "rct.h"
#include "platform_file.h"

#define ORIG_FILE_READ 0x584F65
#define open_with rct_temp_file

static void test_file_read(void)
{
    typedef uint32_t (*fn)(void *, void *, uint32_t);
    fn orig = (fn)ORIG_FILE_READ;
    static uint8_t data[5000], a[6000], b[6000];
    for (int t = 0; t < 200; t++) {
        size_t len = rnd(sizeof data);
        for (size_t i = 0; i < len; i++) data[i] = (uint8_t)rnd(256);
        uint32_t skip = len ? rnd((uint32_t)len) : 0;
        uint32_t req = rnd(4) == 0 ? 0xFFFFFFFF : rnd(5500);
        memset(a, 0xAA, sizeof a); memset(b, 0xAA, sizeof b);
        HANDLE h = open_with(data, len);
        SetFilePointer(h, skip, NULL, FILE_BEGIN);
        uint32_t ra = orig(h, a, req), pa = SetFilePointer(h, 0, NULL, FILE_CURRENT);
        SetFilePointer(h, skip, NULL, FILE_BEGIN);
        uint32_t rb = file_read(h, b, req), pb = SetFilePointer(h, 0, NULL, FILE_CURRENT);
        CloseHandle(h);
        CHECK(ra == rb && pa == pb && !memcmp(a, b, sizeof a), "file_read case %d: ret %u/%u pos %u/%u", t, ra, rb, pa, pb);
    }
    /* invalid handle: both report 0xFFFFFFFF */
    CHECK(orig(INVALID_HANDLE_VALUE, a, 4) == file_read(INVALID_HANDLE_VALUE, b, 4), "file_read invalid handle");
}

static void test_write_seek(void)
{
    typedef uint32_t (*wfn)(void *, const void *, uint32_t);
    typedef uint32_t (*sfn)(void *, int32_t);
    static const struct { uint32_t addr; sfn mine; } seeks[] = {
        {0x584F20, file_seek_set}, {0x584F37, file_seek_cur}, {0x584F4E, file_seek_end}};
    static uint8_t data[5000], fa[5000], fb[5000];
    for (int t = 0; t < 200; t++) {
        uint32_t len = rnd(sizeof data);
        for (uint32_t i = 0; i < len; i++) data[i] = (uint8_t)rnd(256);
        HANDLE a = rct_temp_rw(), b = rct_temp_rw();
        uint32_t ra = ((wfn)0x585000)(a, data, len), rb = file_write(b, data, len);
        CHECK(ra == rb, "file_write ret %u/%u", ra, rb);
        for (int k = 0; k < 20; k++) {
            int s = (int)rnd(3);
            int32_t off = (int32_t)rnd(len + 100) - (s ? (int32_t)(len / 2) : 0);
            uint32_t pa = ((sfn)seeks[s].addr)(a, off), pb = seeks[s].mine(b, off);
            CHECK(pa == pb, "seek %d(%d): %u/%u", s, off, pa, pb);
        }
        size_t na = rct_file_contents(a, fa, sizeof fa), nb = rct_file_contents(b, fb, sizeof fb);
        CHECK(na == nb && !memcmp(fa, fb, na), "file_write contents");
        CloseHandle(a);
        CloseHandle(b);
    }
    CHECK(((wfn)0x585000)(INVALID_HANDLE_VALUE, data, 4) == file_write(INVALID_HANDLE_VALUE, data, 4),
          "file_write invalid handle");
}

int main(int argc, char **argv)
{
    rct_harness_init(argc, argv);
    test_file_read();
    test_write_seek();
    printf("%s (%d failures)\n", rct_failures ? "FAILED" : "ok", rct_failures);
    return rct_failures != 0;
}
