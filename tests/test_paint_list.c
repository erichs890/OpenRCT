/* Differential tests: paint_session_begin (0x42635F), paint_arrange (0x42839E)
   and paint_draw (0x42860C) vs src/paint_list.c, through their adapters.
   The sprite drawer 0x41DA8B is replaced by a recorder, so paint_draw is
   compared by the sequence of sprites it asks for. */
#include <stdio.h>
#include <string.h>
#include "rct.h"
#include "paint.h"
#include "hooks.h"

#define DATA_START 0x88A000
#define DATA_END 0xC3F000
#define SLOTS 96
#define W(p, o) (*(uint16_t *)((p) + (o)))
#define D(p, o) (*(uint32_t *)((p) + (o)))

void hook_42635F(void), hook_42839E(void), hook_42860C(void);   /* runtime/hooks.S */
static uint8_t base_data[DATA_END - DATA_START], after_orig[DATA_END - DATA_START];
static uint8_t pool[0x30 * SLOTS], pool_orig[0x30 * SLOTS];
static PaintDpi dpi;

/* 0x41DA8B stand-in: records cx, dx, ebx, edi and preserves every register */
static uint32_t calls[2][1024][4];
static int ncalls[2], side;
void test_record_sprite(uint32_t ecx, uint32_t edx, uint32_t ebx, uint32_t edi)
{
    int n = ncalls[side]++;
    if (n < 1024) {
        calls[side][n][0] = (uint16_t)ecx, calls[side][n][1] = (uint16_t)edx;
        calls[side][n][2] = ebx, calls[side][n][3] = edi;
    }
}
void record_sprite(void);
__asm__(".intel_syntax noprefix\n"
        ".globl _record_sprite\n"
        "_record_sprite:\n"
        "    pushad\n"
        "    push edi\n    push ebx\n    push edx\n    push ecx\n"
        "    call _test_record_sprite\n"
        "    add esp, 16\n"
        "    popad\n"
        "    ret\n"
        ".att_syntax\n");

static uint32_t s;
#define R(n) ((s = s * 1103515245 + 12345) >> 8) % (n)

static uint8_t *slot(int i) { return pool + 0x30 * i; }

/* random structs in buckets min..max (slots 0..n-1), the root goes to slot n */
static void setup_arrange(uint32_t seed)
{
    s = seed;
    memcpy((void *)DATA_START, base_data, sizeof base_data);
    for (size_t i = 0; i < sizeof pool; i++) pool[i] = (uint8_t)R(256);
    memset(g_paint_quadrants, 0, sizeof g_paint_quadrants);
    int n = R(60), wide = R(8) == 0;
    uint32_t min = R(250), span = R(7);
    for (int i = 0; i < n; i++) {
        uint8_t *p = slot(i);
        for (int o = 4; o <= 0x0E; o += 2) W(p, o) = wide ? (uint16_t)R(0x10000) : (uint16_t)R(48);
        uint32_t b = min + R(span + 1);
        W(p, 0x14) = R(10) ? (uint16_t)b : (uint16_t)R(0x10000);
        D(p, 0x20) = (uint32_t)(uintptr_t)g_paint_quadrants[b];
        g_paint_quadrants[b] = p;
    }
    g_paint_next = slot(n);
    g_paint_quadrant_min = R(12) ? min : 0xFFFFFFFF;
    g_paint_quadrant_max = min + span;
    g_paint_rotation = R(4);
    g_paint_8ff008 = R(0x7FFFFFFF), g_paint_strings = R(0x7FFFFFFF);
    g_paint_last_added = slot(R(SLOTS));
}

/* random draw list: top-level structs, child chains, attached lists */
static void setup_draw(uint32_t seed)
{
    s = seed;
    memcpy((void *)DATA_START, base_data, sizeof base_data);
    for (size_t i = 0; i < sizeof pool; i++) pool[i] = (uint8_t)R(256);
    int used = 1;
    uint8_t *prev = slot(0);
    g_paint_root = (uint32_t)(uintptr_t)prev;
    for (int t = R(12); t > 0 && used < SLOTS - 8; t--) {
        uint8_t *p = slot(used++);
        D(prev, 0x20) = (uint32_t)(uintptr_t)p;
        prev = p;
        for (uint8_t *c = p;;) {
            if (R(4) == 0) D(c, 0x18) = 0;
            if (R(8) < 4 && c[0x24] < 14) c[0x24] = (uint8_t)R(14);
            if (R(3) || used >= SLOTS - 8) { D(c, 0x1C) = 0; break; }
            D(c, 0x1C) = (uint32_t)(uintptr_t)slot(used);
            c = slot(used++);
        }
        uint8_t *last = p;
        while (D(last, 0x1C)) last = (uint8_t *)(uintptr_t)D(last, 0x1C);
        D(last, 0x18) = 0;
        for (int a = R(4), first = 1; a > 0 && used < SLOTS - 1; a--) {
            uint8_t *at = slot(used++);
            if (first) D(last, 0x18) = (uint32_t)(uintptr_t)at, first = 0;
            D(at, 8) = a > 1 && used < SLOTS - 1 ? (uint32_t)(uintptr_t)slot(used) : 0;
            if (!D(at, 8)) break;
        }
    }
    D(prev, 0x20) = 0;
    dpi.zoom = (int16_t)R(3);
    g_paint_dpi = &dpi;
    g_paint_session_flags = (uint16_t)R(0x10000);
}

static void diff(const char *what, int t)
{
    size_t o = 0;
    while (o < sizeof after_orig && ((uint8_t *)DATA_START)[o] == after_orig[o]) o++;
    CHECK(o == sizeof after_orig && !memcmp(pool, pool_orig, sizeof pool), "%s case %d: memory differs at %08X%s",
          what, t, (unsigned)(DATA_START + o), memcmp(pool, pool_orig, sizeof pool) ? " (pool)" : "");
}

static void save(void)
{
    memcpy(after_orig, (void *)DATA_START, sizeof after_orig);
    memcpy(pool_orig, pool, sizeof pool);
}

#define CHECK_REGS(what, t, r, m)                                                                         \
    CHECK(!memcmp(&r, &m, 7 * 4), "%s case %d: eax %08X/%08X ebx %08X/%08X ecx %08X/%08X edx %08X/%08X " \
          "esi %08X/%08X edi %08X/%08X ebp %08X/%08X", what, t, r.eax, m.eax, r.ebx, m.ebx, r.ecx, m.ecx,  \
          r.edx, m.edx, r.esi, m.esi, r.edi, m.edi, r.ebp, m.ebp)

int main(int argc, char **argv)
{
    rct_harness_init(argc, argv);
    memcpy(base_data, (void *)DATA_START, sizeof base_data);
    rct_patch_jmp(0x41DA8B, (void *)record_sprite);

    for (int t = 0; t < 2000; t++) {
        Regs in = {rnd(0xFFFFFFFF), rnd(0xFFFFFFFF), rnd(0xFFFFFFFF), rnd(0xFFFFFFFF), rnd(0xFFFFFFFF),
                   rnd(0xFFFFFFFF), (uint32_t)(uintptr_t)slot(rnd(SLOTS))};
        setup_arrange(t);
        Regs r = in;
        rct_call(0x42635F, &r);
        save();
        setup_arrange(t);
        Regs m = in;
        rct_call((uint32_t)(uintptr_t)hook_42635F, &m);
        CHECK_REGS("begin", t, r, m);
        diff("begin", t);
    }

    int moved = 0;
    for (int t = 0; t < 20000; t++) {
        Regs in = {rnd(0xFFFFFFFF), rnd(0xFFFFFFFF), rnd(0xFFFFFFFF), rnd(0xFFFFFFFF), rnd(0xFFFFFFFF),
                   rnd(0xFFFFFFFF), rnd(0xFFFFFFFF)};
        setup_arrange(t);
        Regs r = in;
        rct_call(0x42839E, &r);
        save();
        setup_arrange(t);
        Regs m = in;
        rct_call((uint32_t)(uintptr_t)hook_42839E, &m);
        CHECK_REGS("arrange", t, r, m);
        diff("arrange", t);
        moved += r.eax != in.eax;
    }

    long sprites = 0;
    for (int t = 0; t < 20000; t++) {
        Regs in = {rnd(0xFFFFFFFF), rnd(0xFFFFFFFF), rnd(0xFFFFFFFF), rnd(0xFFFFFFFF), rnd(0xFFFFFFFF),
                   rnd(0xFFFFFFFF), rnd(0xFFFFFFFF)};
        for (side = 0; side < 2; side++) {
            setup_draw(t);
            ncalls[side] = 0;
            Regs r = in;
            rct_call(side ? (uint32_t)(uintptr_t)hook_42860C : 0x42860C, &r);
            if (!side) save();
        }
        diff("draw", t);
        int n = ncalls[0] < 1024 ? ncalls[0] : 1024;
        CHECK(ncalls[0] == ncalls[1] && !memcmp(calls[0], calls[1], sizeof calls[0][0] * n),
              "draw case %d: %d / %d sprites", t, ncalls[0], ncalls[1]);
        sprites += ncalls[0];
    }
    printf("paint list: 2000 begin, 20000 arrange (%d with buckets), 20000 draw (%ld sprites)\n", moved, sprites);
    printf("%s (%d failures)\n", rct_failures ? "FAILED" : "ok", rct_failures);
    return rct_failures != 0;
}
