/* Differential tests: paint_add_parent (0x4263B8..) and paint_add_child (0x426A04..), all
   rotations, vs src/paint_add.c. Same snapshot scheme as test_paint_supports.c. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "rct.h"
#include "paint.h"

#define DATA_START 0x88A000
#define DATA_END 0xC3F000
#define POOL_SIZE (0x30 * 64)

static const uint32_t entry[3][4] = {{0x4263B8, 0x42653B, 0x4266CC, 0x42685F},
                                     {0x426A04, 0x426BA8, 0x426D5D, 0x426F17},
                                     {0x4270E0, 0x42723E, 0x4273A8, 0x427514}};
void hook_4263B8(void), hook_42653B(void), hook_4266CC(void), hook_42685F(void);   /* runtime/hooks.S */
void hook_426A04(void), hook_426BA8(void), hook_426D5D(void), hook_426F17(void);
void hook_4270E0(void), hook_42723E(void), hook_4273A8(void), hook_427514(void);
static void (*const adapter[3][4])(void) = {{hook_4263B8, hook_42653B, hook_4266CC, hook_42685F},
                                            {hook_426A04, hook_426BA8, hook_426D5D, hook_426F17},
                                            {hook_4270E0, hook_42723E, hook_4273A8, hook_427514}};
static int (*const plain[3])(int, uint32_t, int8_t, int8_t, uint16_t, uint16_t, int8_t, uint16_t, uint8_t **) = {
    paint_add_parent, paint_add_child, paint_add_attached};
static uint8_t base_data[DATA_END - DATA_START], after_orig[DATA_END - DATA_START];
static uint8_t pool[POOL_SIZE], pool_orig[POOL_SIZE];
static PaintDpi dpi;

typedef struct { uint32_t image; int8_t x, y; uint16_t bx, by; int8_t bz; uint16_t z; } Args;

static uint32_t s;
#define R(n) ((s = s * 1103515245 + 12345) >> 8) % (n)

static void setup(uint32_t seed, const Args *a)
{
    s = seed;
    memcpy((void *)DATA_START, base_data, sizeof base_data);
    memset(pool, 0xCD, sizeof pool);
    g_paint_next = pool + 0x30 * R(8);
    g_paint_end = R(8) ? pool + POOL_SIZE : g_paint_next + 0x30 * R(2);
    g_paint_dpi = &dpi;
    int big = R(3);
    dpi.x = big ? -20000 : (int16_t)(R(2000) - 1000);
    dpi.y = big ? -20000 : (int16_t)(R(2000) - 1000);
    dpi.width = big ? 30000 : (int16_t)R(800);
    dpi.height = big ? 30000 : (int16_t)R(600);
    g_paint_map_x = (int16_t)(R(8192) - 1024);
    g_paint_map_y = (int16_t)(R(8192) - 1024);
    g_paint_element_type = (uint16_t)R(0x10000);
    g_paint_element = R(0x7FFFFFFF);
    g_paint_element2 = R(0x7FFFFFFF);
    G1Element *g = &g_g1_elements[a->image & 0x1FFFF];
    g->width = (int16_t)R(200), g->height = (int16_t)R(200);
    g->x_offset = (int16_t)(R(300) - 150), g->y_offset = (int16_t)(R(300) - 150);
    for (int i = 0; i < 256; i++) g_paint_quadrants[i] = R(4) ? NULL : pool + 0x30 * R(64);
    g_paint_quadrant_min = R(300);
    g_paint_quadrant_max = R(300);
    for (int i = 0; i < 3; i++) g_paint_bound_offset[i] = (int16_t)(R(400) - 100);
}

int main(int argc, char **argv)
{
    rct_harness_init(argc, argv);
    memcpy(base_data, (void *)DATA_START, sizeof base_data);
    int added[3] = {0, 0, 0};
    for (int t = 0; t < 24000; t++) {   /* 2000 per kind and rotation; each case diffs the whole 3.8 MB image */
        int rot = t & 3, kind = (t >> 2) % 3;
        Args a = {rnd(0x8000) | rnd(0x2000) << 19, (int8_t)rnd(256), (int8_t)rnd(256), (uint16_t)rnd(64),
                  (uint16_t)rnd(64), (int8_t)rnd(256), (uint16_t)rnd(0x800)};
        if (rnd(10) == 0) a.bx = (uint16_t)rnd(0x10000), a.by = (uint16_t)rnd(0x10000), a.z = (uint16_t)rnd(0x10000);

        setup(t, &a);
        Regs r = {.eax = 0xA5000000u | (uint8_t)a.bz << 8 | (uint8_t)a.x, .ebx = a.image,
                  .ecx = 0xB6B60000u | (uint8_t)a.y, .edx = 0xC7C70000u | a.z, .esi = 0xD8D80000u | a.by,
                  .edi = 0xE9E90000u | a.bx, .ebp = 0x12345678};
        Regs in = r;
        rct_call(entry[kind][rot], &r);
        int orig_ok = !(r.eflags & 1);
        memcpy(after_orig, (void *)DATA_START, sizeof after_orig);
        memcpy(pool_orig, pool, sizeof pool);

        /* the reconstruction through its register adapter: every register and CF/ZF must match */
        setup(t, &a);
        Regs m = in;
        rct_call((uint32_t)(uintptr_t)adapter[kind][rot], &m);
        size_t o = 0;
        while (o < sizeof after_orig && ((uint8_t *)DATA_START)[o] == after_orig[o]) o++;
        const uint32_t flags = 0x8C1;   /* CF, ZF, SF, OF */
        CHECK(!memcmp(&r, &m, 7 * 4) && (r.eflags & flags) == (m.eflags & flags) && o == sizeof after_orig &&
                  !memcmp(pool, pool_orig, sizeof pool),
              "case %d kind %d rot %d: eax %08X/%08X ebx %08X/%08X ecx %08X/%08X edx %08X/%08X esi %08X/%08X "
              "edi %08X/%08X ebp %08X/%08X flags %X/%X first diff %08X%s",
              t, kind, rot, r.eax, m.eax, r.ebx, m.ebx, r.ecx, m.ecx, r.edx, m.edx, r.esi, m.esi, r.edi, m.edi, r.ebp,
              m.ebp, r.eflags & flags, m.eflags & flags, (unsigned)(DATA_START + o),
              memcmp(pool, pool_orig, sizeof pool) ? " pool differs" : "");

        /* and the plain C entry agrees on the result */
        setup(t, &a);
        uint8_t *ps;
        CHECK(plain[kind](rot, a.image, a.x, a.y, a.bx, a.by, a.bz, a.z, &ps) ==
                      orig_ok && (uint32_t)(uintptr_t)ps == r.ebp,
              "case %d kind %d rot %d: C entry differs", t, kind, rot);
        added[kind] += orig_ok;
    }
    printf("paint_add parent/child/attached: 24000 cases (4 rotations each), %d / %d / %d added\n", added[0],
           added[1], added[2]);
    printf("%s (%d failures)\n", rct_failures ? "FAILED" : "ok", rct_failures);
    return rct_failures != 0;
}
