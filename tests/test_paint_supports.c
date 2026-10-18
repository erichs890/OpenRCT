/* Differential tests: metal supports 0x40C3E1 / 0x40C860 vs src/paint_supports.c.
   Each side starts from the same snapshot of the whole writable image
   (.data + DATASEG) plus a paint-struct pool, with random paint state; everything
   is compared afterwards. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "rct.h"
#include "paint.h"

#define DATA_START 0x88A000
#define DATA_END 0xC3F000
#define PAINT_NEXT 0x8CFDEC     /* next free paint struct */
#define PAINT_END 0x8CFDE4      /* end of the paint struct pool */
#define PAINT_DPI 0xC1D4B8      /* -> drawing target, used for culling */
#define POOL_SIZE (0x30 * 512)

static uint8_t base_data[DATA_END - DATA_START], after_orig[DATA_END - DATA_START];
static uint8_t pool[POOL_SIZE], pool_orig[POOL_SIZE];
static struct { uint32_t bits; int16_t x, y, w, h; } dpi;

typedef struct { uint32_t type, segment, height, flags, rot; int16_t special; } Case;

static void setup(const Case *c, uint32_t seed)
{
    memcpy((void *)DATA_START, base_data, sizeof base_data);
    memset(pool, 0, sizeof pool);
    uint32_t s = seed;
#define R(n) ((s = s * 1103515245 + 12345) >> 8) % (n)
    for (int i = 0; i < 9; i++) {
        uint32_t k = R(4);
        g_support_segments[i].height = k == 0 ? 0xFFFF : k == 1 ? 0 : (uint16_t)R(0x500);
        g_support_segments[i].slope = (uint8_t)R(256);
    }
    g_paint_support.flags = (uint8_t)(R(8) ? 1 : 0);
    g_paint_session_flags = (uint16_t)(R(10) ? 0 : 8);
    g_paint_rotation = c->rot;
    *(uint8_t **)PAINT_NEXT = pool;
    *(uint8_t **)PAINT_END = pool + (R(6) ? POOL_SIZE : 0x30 * R(4));   /* sometimes nearly full */
    *(void **)PAINT_DPI = &dpi;
    int big = R(3);
    dpi.x = big ? -16000 : (int16_t)(R(400) - 200);
    dpi.y = big ? -16000 : (int16_t)(R(400) - 200);
    dpi.w = big ? 32000 : (int16_t)R(300);
    dpi.h = big ? 32000 : (int16_t)R(300);
#undef R
}

static void test_metal(int variant)
{
    uint32_t addr = variant ? 0x40C860 : 0x40C3E1;
    int drawn = 0, blocked = 0;
    for (int t = 0; t < 3000; t++) {
        Case c = {rnd(13), rnd(9), rnd(0x500), rnd(0x20000) << 19, rnd(4), 0};
        if (rnd(2)) c.special = (int16_t)(rnd(200) - 70);

        setup(&c, t);
        Regs r = {.eax = 0x5A5A0000u | (uint16_t)c.special, .ebx = c.segment, .ecx = 0x13579BDF,
                  .edx = 0xA5A50000u | c.height, .esi = 0x2468ACE0, .edi = c.type, .ebp = c.flags};
        Regs in = r;
        rct_call(addr, &r);
        int orig_cf = r.eflags & 1;
        CHECK(r.ebx == in.ebx && r.edx == in.edx && r.esi == in.esi && r.edi == in.edi && r.ebp == in.ebp,
              "metal %d case %d: original register contract", variant, t);
        if (c.special == 0) CHECK(r.eax == in.eax, "metal %d case %d: eax not preserved", variant, t);
        memcpy(after_orig, (void *)DATA_START, sizeof after_orig);
        memcpy(pool_orig, pool, sizeof pool);
        uint32_t used = (uint32_t)(*(uint8_t **)PAINT_NEXT - pool);

        setup(&c, t);
        int mine = metal_supports_paint(variant, c.type, c.segment, c.special, (uint16_t)c.height, c.flags);
        size_t o = 0;
        while (o < sizeof after_orig && ((uint8_t *)DATA_START)[o] == after_orig[o]) o++;
        CHECK(mine == orig_cf && o == sizeof after_orig && !memcmp(pool, pool_orig, sizeof pool),
              "metal %d case %d (type %u seg %u special %d h %u rot %u): cf %d/%d, first diff %08X%s", variant, t,
              c.type, c.segment, c.special, c.height, c.rot, orig_cf, mine, (unsigned)(DATA_START + o),
              memcmp(pool, pool_orig, sizeof pool) ? ", pool differs" : "");
        drawn += used > 0;
        blocked += orig_cf;
    }
    printf("metal %d: 3000 cases, %d drew sprites, %d blocked\n", variant, drawn, blocked);
}

static void test_wooden(int variant)
{
    uint32_t addr = variant ? 0x40C038 : 0x40BC98;
    int drawn = 0, blocked = 0;
    for (int t = 0; t < 3000; t++) {
        Case c = {rnd(12), 0, rnd(0x500), rnd(0x20000) << 19, rnd(4), 0};
        if (rnd(2)) c.special = (int16_t)(1 + rnd(24));
        uint16_t ground = (uint16_t)rnd(0x480);
        uint8_t slope = (uint8_t)(rnd(3) ? rnd(0x40) : 0);
        uint16_t stop = (uint16_t)(((ground + 15) & 0xFFF0) + 0x10 * (1 + rnd(9)));
        int attach_to = (int)rnd(3);

        setup(&c, t);
        g_paint_support.height = ground;
        g_paint_support.slope = slope;
        g_paint_c2d4ec = (t & 1) ? stop : 0;
        g_paint_last_struct = attach_to ? (uint32_t)(uintptr_t)(pool + 0x30 * attach_to) : 0;
        Regs r = {.eax = 0x5A5A0000u | (uint16_t)c.special, .ebx = 0x11111111, .ecx = 0x13579BDF,
                  .edx = 0xA5A50000u | c.height, .esi = 0x2468ACE0, .edi = c.type, .ebp = c.flags};
        Regs in = r;
        rct_call(addr, &r);
        int orig = (r.eax & 0xFF) | (r.eflags & 1) << 8;
        CHECK(r.edx == in.edx && r.esi == in.esi && r.edi == in.edi && r.ebp == in.ebp,
              "wooden %d case %d: original register contract", variant, t);
        memcpy(after_orig, (void *)DATA_START, sizeof after_orig);
        memcpy(pool_orig, pool, sizeof pool);
        uint32_t used = (uint32_t)(*(uint8_t **)PAINT_NEXT - pool);

        setup(&c, t);
        g_paint_support.height = ground;
        g_paint_support.slope = slope;
        g_paint_c2d4ec = (t & 1) ? stop : 0;
        g_paint_last_struct = attach_to ? (uint32_t)(uintptr_t)(pool + 0x30 * attach_to) : 0;
        int mine = wooden_supports_paint(variant, c.type, c.special, (uint16_t)c.height, c.flags);
        size_t o = 0;
        while (o < sizeof after_orig && ((uint8_t *)DATA_START)[o] == after_orig[o]) o++;
        CHECK(mine == orig && o == sizeof after_orig && !memcmp(pool, pool_orig, sizeof pool),
              "wooden %d case %d (type %u special %d h %u ground %u slope %02X rot %u): result %03X/%03X, "
              "first diff %08X%s", variant, t, c.type, c.special, c.height, ground, slope, c.rot, orig, mine,
              (unsigned)(DATA_START + o), memcmp(pool, pool_orig, sizeof pool) ? ", pool differs" : "");
        drawn += used > 0;
        blocked += orig >> 8;
    }
    printf("wooden %d: 3000 cases, %d drew sprites, %d blocked\n", variant, drawn, blocked);
}

int main(int argc, char **argv)
{
    rct_harness_init(argc, argv);
    memcpy(base_data, (void *)DATA_START, sizeof base_data);
    test_metal(0);
    test_metal(1);
    test_wooden(0);
    test_wooden(1);
    printf("%s (%d failures)\n", rct_failures ? "FAILED" : "ok", rct_failures);
    return rct_failures != 0;
}
