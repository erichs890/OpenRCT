/* Differential tests: the SV4/SC4 stream (0x419CB5..0x41A32F) vs src/sawyer_io.c. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rct.h"
#include "sawyer_io.h"
#include "platform_file.h"

#define ORIG_READ_BYTE 0x419F53
#define ORIG_RLE_READ_BYTE 0x419FF6
#define ORIG_RLE_READ 0x419FEB

#define open_with rct_temp_file

/* Same starting state for both sides, including stale buffer contents. */
static void reset_io(HANDLE h, uint32_t seed)
{
    uint8_t *p = (uint8_t *)&g_sawyer_io;
    for (size_t i = 0; i < sizeof g_sawyer_io; i++)
        p[i] = (uint8_t)((seed = seed * 1103515245 + 12345) >> 16);
    g_sawyer_io.file = h;
    g_sawyer_io.buf_count = 0;
    g_sawyer_io.rle_state = 0;
    g_sawyer_io.rle_count = 0;
    g_io_busy = 0;
}

typedef struct {
    SawyerIO io;
    uint32_t busy, pos;
} After;

static void snapshot(After *a)
{
    memset(a, 0, sizeof *a);   /* padding after the packed SawyerIO */
    a->io = g_sawyer_io;
    a->io.file = NULL;
    a->busy = g_io_busy;
    a->pos = SetFilePointer(g_sawyer_io.file, 0, NULL, FILE_CURRENT);
}


/* Random RLE stream; with `junk`, also arbitrary bytes (any byte is a valid token). */
static size_t make_stream(uint8_t *out, size_t cap, size_t *decoded, int junk)
{
    size_t o = 0, d = 0;
    while (o + 130 < cap) {
        if (junk) { out[o++] = (uint8_t)rnd(256); d++; continue; }
        if (rnd(2)) {
            int n = (int)rnd(128);                 /* literal: n+1 bytes */
            out[o++] = (uint8_t)n;
            for (int i = 0; i <= n; i++) out[o++] = (uint8_t)rnd(256);
            d += n + 1;
        } else {
            int n = -1 - (int)rnd(128);            /* run: 1-n repeats, n = -128 included */
            out[o++] = (uint8_t)n;
            out[o++] = (uint8_t)rnd(256);
            d += 1 - n;
        }
        if (rnd(64) == 0) break;
    }
    *decoded = d;
    return o;
}

static void test_read_byte(void)
{
    static uint8_t data[3000];
    for (int t = 0; t < 100; t++) {
        size_t len = rnd(sizeof data);
        for (size_t i = 0; i < len; i++) data[i] = (uint8_t)rnd(256);
        uint32_t n = rnd(3500) + 1;
        HANDLE h = open_with(data, len);
        uint8_t got[3600], want[3600];
        After A, B;
        reset_io(h, t);
        Regs r = {0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00, 0x12345678, 0x9ABCDEF0, 0x0BADF00D, 0};
        for (uint32_t i = 0; i < n; i++) {
            Regs in = r;
            rct_call(ORIG_READ_BYTE, &r);
            want[i] = (uint8_t)r.eax;
            /* register contract: only al changes */
            CHECK((r.eax & ~0xFFu) == (in.eax & ~0xFFu) && r.ebx == in.ebx && r.ecx == in.ecx && r.edx == in.edx &&
                  r.esi == in.esi && r.edi == in.edi && r.ebp == in.ebp, "read_byte clobbered registers");
        }
        snapshot(&A);
        SetFilePointer(h, 0, NULL, FILE_BEGIN);
        reset_io(h, t);
        for (uint32_t i = 0; i < n; i++) got[i] = sawyer_read_byte();
        snapshot(&B);
        CloseHandle(h);
        CHECK(!memcmp(got, want, n) && !memcmp(&A, &B, sizeof A), "read_byte case %d (len %u, n %u)", t, (unsigned)len, n);
    }
}

static void test_rle(int junk)
{
    static uint8_t stream[20000], want[40000], got[40000];
    for (int t = 0; t < 300; t++) {
        size_t decoded, len = make_stream(stream, rnd(2) ? sizeof stream : 300, &decoded, junk);
        /* sometimes read past the end of the stream (stale-buffer path) */
        uint32_t n = 1 + rnd((uint32_t)(rnd(8) ? (decoded ? decoded : 1) : decoded + 3000));
        if (n > sizeof want) n = sizeof want;
        HANDLE h = open_with(stream, len);
        After A, B;

        reset_io(h, t + 1000);
        memset(want, 0, sizeof want);
        Regs r = {0, 0, n, 0, (uint32_t)(uintptr_t)want, 0, 0, 0};
        rct_call(ORIG_RLE_READ, &r);
        CHECK(r.ecx == 0 && r.esi == (uint32_t)(uintptr_t)(want + n), "rle_read register contract");
        snapshot(&A);

        SetFilePointer(h, 0, NULL, FILE_BEGIN);
        reset_io(h, t + 1000);
        memset(got, 0, sizeof got);
        sawyer_rle_read(got, n);
        snapshot(&B);
        CloseHandle(h);
        CHECK(!memcmp(got, want, sizeof got) && !memcmp(&A, &B, sizeof A), "rle_read %s case %d (n %u)",
              junk ? "junk" : "stream", t, n);

        /* the single-byte entry point, interleaved with arbitrary resumes */
        h = open_with(stream, len);
        reset_io(h, t);
        uint32_t m = n < 500 ? n : 500;
        for (uint32_t i = 0; i < m; i++) { Regs q = {.eax = 0xFFFFFF00}; rct_call(ORIG_RLE_READ_BYTE, &q); want[i] = (uint8_t)q.eax;
            CHECK((q.eax >> 8) == 0xFFFFFF, "rle_read_byte touched eax[31:8]"); }
        snapshot(&A);
        SetFilePointer(h, 0, NULL, FILE_BEGIN);
        reset_io(h, t);
        for (uint32_t i = 0; i < m; i++) got[i] = sawyer_rle_read_byte();
        snapshot(&B);
        CloseHandle(h);
        CHECK(!memcmp(got, want, m) && !memcmp(&A, &B, sizeof A), "rle_read_byte case %d", t);
    }
}

/* ---- writer ---- */

#define ORIG_WRITE_BEGIN 0x419CC0
#define ORIG_WRITE_BYTE 0x419CEC
#define ORIG_RLE_WRITE_BYTE 0x419DB1
#define ORIG_RLE_FLUSH 0x419EDE
#define ORIG_WRITE_END 0x419D6B
#define ORIG_RLE_WRITE 0x419CB5
#define ORIG_READ_REWIND 0x419FB6
#define ORIG_WRITE_CK_SV4 0x41A05F
#define ORIG_WRITE_CK_SC4 0x41A0AB
#define ORIG_CHECK_SV4 0x41A0E4
#define ORIG_CHECK_SC4 0x41A215

enum { EAX = 1, EBX = 2, ECX = 4, EDX = 8, ESI = 16, EDI = 32, EBP = 64 };

/* Call the original with sentinel registers; `keep` = registers it must preserve. */
static Regs call_checked(uint32_t addr, Regs in, unsigned keep, const char *what)
{
    static const Regs sentinel = {0xA1A1A1A1, 0xB2B2B2B2, 0xC3C3C3C3, 0xD4D4D4D4, 0xE5E5E5E5, 0xF6F6F6F6, 0x07070707, 0};
    Regs r = sentinel;
    if (in.eax) r.eax = in.eax;
    if (in.ecx) r.ecx = in.ecx;
    if (in.esi) r.esi = in.esi;
    Regs before = r;
    rct_call(addr, &r);
    const uint32_t *a = &before.eax, *b = &r.eax;
    for (int i = 0; i < 7; i++)
        if ((keep & (1u << i)) && a[i] != b[i]) {
            CHECK(0, "%s clobbered register %d (%08X -> %08X)", what, i, a[i], b[i]);
            break;
        }
    return r;
}

enum { OP_RLE_BLOCK, OP_RAW_BYTE, OP_RLE_BYTE, OP_FLUSH };
typedef struct { int kind; uint32_t off, len; } Op;

static uint8_t wdata[20000];
static Op ops[400];
static int nops;

/* Data with long runs, short runs and noise; ops cut it into random pieces. */
static void make_writer_case(int allow_raw)
{
    uint32_t n = 1 + rnd(rnd(4) ? 3000 : sizeof wdata - 1), alpha = 1 + rnd(rnd(2) ? 3 : 256);
    for (uint32_t i = 0; i < n;) {
        if (rnd(5) == 0) {
            uint32_t run = 1 + rnd(rnd(3) ? 130 : 400);
            uint8_t b = (uint8_t)rnd(alpha);
            while (run-- && i < n) wdata[i++] = b;
        } else {
            wdata[i++] = (uint8_t)rnd(alpha);
        }
    }
    nops = 0;
    for (uint32_t off = 0; off < n && nops < (int)(sizeof ops / sizeof *ops) - 1;) {
        Op o = {OP_RLE_BLOCK, off, 1 + rnd(n - off < 600 ? n - off : 600)};
        uint32_t k = rnd(20);
        if (k == 0 && allow_raw) o.kind = OP_RAW_BYTE, o.len = 1;
        else if (k < 3) o.kind = OP_RLE_BYTE, o.len = 1;
        else if (k == 3) o.kind = OP_FLUSH, o.len = 0;
        ops[nops++] = o;
        off += o.len;
    }
}

static void run_writer(int orig, int sc4)
{
    if (orig) call_checked(ORIG_WRITE_BEGIN, (Regs){0}, ~0u, "write_begin");
    else sawyer_write_begin();
    for (int i = 0; i < nops; i++) {
        Op o = ops[i];
        uint8_t *p = wdata + o.off;
        if (!orig) {
            switch (o.kind) {
            case OP_RLE_BLOCK: sawyer_rle_write(p, o.len); break;
            case OP_RAW_BYTE: sawyer_write_byte(*p); break;
            case OP_RLE_BYTE: sawyer_rle_write_byte(*p); break;
            case OP_FLUSH: sawyer_rle_flush(); break;
            }
            continue;
        }
        switch (o.kind) {
        case OP_RLE_BLOCK: {
            Regs r = call_checked(ORIG_RLE_WRITE, (Regs){.ecx = o.len, .esi = (uint32_t)(uintptr_t)p},
                                  EBX | EDX | EDI | EBP, "rle_write");
            CHECK(r.ecx == 0 && r.esi == (uint32_t)(uintptr_t)(p + o.len), "rle_write ecx/esi out");
            break;
        }
        case OP_RAW_BYTE:
            call_checked(ORIG_WRITE_BYTE, (Regs){.eax = 0x12345600u | *p}, ~0u, "write_byte");
            break;
        case OP_RLE_BYTE:
            call_checked(ORIG_RLE_WRITE_BYTE, (Regs){.eax = 0x12345600u | *p}, EBX | ECX | EDX | ESI | EDI | EBP,
                         "rle_write_byte");
            break;
        case OP_FLUSH:
            call_checked(ORIG_RLE_FLUSH, (Regs){0}, EBX | ECX | EDX | ESI | EDI | EBP, "rle_flush");
            break;
        }
    }
    if (orig) {
        call_checked(ORIG_WRITE_END, (Regs){0}, EBX | EDX | ESI | EDI | EBP, "write_end");
        call_checked(sc4 ? ORIG_WRITE_CK_SC4 : ORIG_WRITE_CK_SV4, (Regs){0}, EBX | ESI | EDI | EBP, "write_checksum");
    } else {
        sawyer_write_end();
        if (sc4) sawyer_write_checksum_sc4(); else sawyer_write_checksum_sv4();
    }
}

static void test_writer(void)
{
    static uint8_t fa[60000], fb[60000], back[20000];
    for (int t = 0; t < 400; t++) {
        int raw = t % 4 == 3, sc4 = t % 2;
        g_checksum_negate = (uint8_t)(t % 3 == 0);
        make_writer_case(raw);
        After A, B;
        HANDLE h = rct_temp_rw();
        reset_io(h, t);
        run_writer(1, sc4);
        snapshot(&A);
        size_t na = rct_file_contents(h, fa, sizeof fa);
        CloseHandle(h);

        h = rct_temp_rw();
        reset_io(h, t);
        run_writer(0, sc4);
        snapshot(&B);
        size_t nb = rct_file_contents(h, fb, sizeof fb);
        CHECK(na == nb && !memcmp(fa, fb, na) && !memcmp(&A, &B, sizeof A), "writer case %d (file %u/%u bytes)",
              t, (unsigned)na, (unsigned)nb);

        /* properties: the stream decodes back (rle ops only), and the checksum validates */
        uint32_t total = 0;
        for (int i = 0; i < nops; i++) total += ops[i].len;
        if (!raw && total) {
            sawyer_read_rewind();
            sawyer_rle_read(back, total);
            CHECK(!memcmp(back, wdata, total), "writer case %d does not round-trip", t);
        }
        if (sc4) {
            CHECK(nb < 14 || (sawyer_check_sc4() && g_sawyer_io.checksum_ok), "writer case %d: written SC4 checksum rejected (%u bytes)", t, (unsigned)nb);
        } else {
            CHECK(nb < 8 || sawyer_check_sv4(), "writer case %d: written SV4 checksum rejected (%u bytes)", t, (unsigned)nb);
        }
        CloseHandle(h);
    }
    g_checksum_negate = 0;
}

/* ---- checksum validation ---- */

static void check_one(HANDLE h, const char *name, int *valid_sv4, int *valid_sc4)
{
    for (int sc4 = 0; sc4 < 2; sc4++)
        for (int neg = 0; neg < 2; neg++) {
            After A, B;
            g_checksum_negate = (uint8_t)neg;
            reset_io(h, sc4 * 2 + neg);
            Regs r = call_checked(sc4 ? ORIG_CHECK_SC4 : ORIG_CHECK_SV4, (Regs){0}, ESI | EBP, "check");
            int orig_ok = !(r.eflags & 1);
            snapshot(&A);
            reset_io(h, sc4 * 2 + neg);
            int mine = sc4 ? sawyer_check_sc4() : sawyer_check_sv4();
            snapshot(&B);
            CHECK(orig_ok == mine && !memcmp(&A, &B, sizeof A), "check_%s neg=%d %s: orig %d mine %d",
                  sc4 ? "sc4" : "sv4", neg, name, orig_ok, mine);
            if (!neg && orig_ok) ++*(sc4 ? valid_sc4 : valid_sv4);

            /* rewind from whatever state the check left */
            reset_io(h, 7);
            call_checked(ORIG_READ_REWIND, (Regs){0}, EBX | ESI | EDI | EBP, "read_rewind");
            snapshot(&A);
            reset_io(h, 7);
            sawyer_read_rewind();
            snapshot(&B);
            CHECK(!memcmp(&A, &B, sizeof A), "read_rewind");
        }
    g_checksum_negate = 0;
}

static void test_checksums(const char *game_dir)
{
    int files = 0, sv4 = 0, sc4 = 0, d1, d2;
    static const char *const patterns[] = {"\\Saved Games\\*.SV4", "\\Scenarios\\*.SC4"};
    for (int k = 0; k < 2; k++) {
        char pat[MAX_PATH], path[2 * MAX_PATH];
        WIN32_FIND_DATAA fd;
        snprintf(pat, sizeof pat, "%s%s", game_dir, patterns[k]);
        HANDLE f = FindFirstFileA(pat, &fd);
        if (f == INVALID_HANDLE_VALUE) continue;
        do {
            snprintf(path, sizeof path, "%s%.*s%s", game_dir, (int)(strrchr(patterns[k], '\\') - patterns[k] + 1),
                     patterns[k], fd.cFileName);
            HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            check_one(h, fd.cFileName, &sv4, &sc4);
            CloseHandle(h);
            files++;
        } while (FindNextFileA(f, &fd));
        FindClose(f);
    }
    printf("real files: %d (valid as SV4: %d, as SC4: %d)\n", files, sv4, sc4);
    CHECK(files > 0, "no SV4/SC4 files found under %s (set RCT_DIR)", game_dir);

    /* synthetic: tiny files around the size limits, random tails, and every valid offset */
    static const uint32_t offs[] = {120001u, 108156u, 110001u, 108112u};
    static uint8_t data[3000];
    for (int t = 0; t < 300; t++) {
        uint32_t n = t < 20 ? (uint32_t)t : 4 + rnd(sizeof data - 4);
        for (uint32_t i = 0; i < n; i++) data[i] = (uint8_t)rnd(256);
        if (n >= 4 && t % 3) {
            uint32_t s = 0;
            for (uint32_t i = 0; i < n - 4; i++) { s = (s & ~0xFFu) | (uint8_t)(s + data[i]); s = s << 3 | s >> 29; }
            uint32_t o = offs[rnd(4)];
            uint32_t v = rnd(2) ? s + o : s - o;
            memcpy(data + n - 4, &v, 4);
        }
        HANDLE h = open_with(data, n);
        check_one(h, "synthetic", &d1, &d2);
        CloseHandle(h);
    }
}

int main(int argc, char **argv)
{
    rct_harness_init(argc, argv);
    const char *game = getenv("RCT_DIR") ? getenv("RCT_DIR")
                                         : "C:\\Program Files (x86)\\Steam\\steamapps\\common\\RollerCoaster Tycoon Deluxe";
    test_read_byte();
    test_rle(0);
    test_rle(1);
    test_writer();
    test_checksums(game);
    printf("%s (%d failures)\n", rct_failures ? "FAILED" : "ok", rct_failures);
    return rct_failures != 0;
}
