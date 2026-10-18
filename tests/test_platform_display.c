/* Differential tests: 0x57FAB6 vs src/platform_display.c (cdecl, called directly). */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "rct.h"
#include "platform_display.h"

typedef void(__cdecl *invalidate_fn)(int32_t, int32_t, int32_t, int32_t);

static void test_invalidate(void)
{
    static uint8_t grid_orig[GFX_GRID_SIZE];
    static const int sizes[][2] = {{640, 480}, {800, 600}, {1024, 768}, {1280, 1024}, {1280, 720}, {1000, 700}, {0, 0}};
    int marked = 0;
    for (int t = 0; t < 20000; t++) {
        int k = (int)rnd(7);
        int32_t w = k < 6 ? sizes[k][0] : (int32_t)rnd(1281), h = k < 6 ? sizes[k][1] : (int32_t)rnd(1017);
        int32_t c[4];
        for (int i = 0; i < 4; i++) {
            int32_t lim = (i & 1) ? h : w;
            c[i] = rnd(10) == 0 ? (int32_t)rnd(4000) - 2000 : (int32_t)rnd((uint32_t)lim + 200) - 100;
        }
        int32_t count0 = (int32_t)rnd(1000);
        uint8_t seed_grid = (uint8_t)rnd(2);

        g_client_width = w, g_client_height = h;
        memset(g_gfx_dirty_grid, seed_grid, GFX_GRID_SIZE);
        g_gfx_dirty_count = count0;
        ((invalidate_fn)0x57FAB6)(c[0], c[1], c[2], c[3]);
        memcpy(grid_orig, g_gfx_dirty_grid, GFX_GRID_SIZE);
        int32_t count_orig = g_gfx_dirty_count;

        memset(g_gfx_dirty_grid, seed_grid, GFX_GRID_SIZE);
        g_gfx_dirty_count = count0;
        gfx_invalidate_rect(c[0], c[1], c[2], c[3]);
        CHECK(count_orig == g_gfx_dirty_count && !memcmp(grid_orig, g_gfx_dirty_grid, GFX_GRID_SIZE),
              "invalidate case %d (%dx%d, %d,%d,%d,%d): count %d/%d", t, w, h, c[0], c[1], c[2], c[3], count_orig,
              g_gfx_dirty_count);
        marked += count_orig != count0;
    }
    printf("invalidate: 20000 cases, %d marked blocks\n", marked);

    /* the fix: a 1920x1200 client must not write past the grid */
    static uint8_t after[GFX_GRID_SIZE + 64];
    uint8_t *guard = (uint8_t *)0xC3BE60 + GFX_GRID_SIZE;   /* what follows the grid in the image */
    memcpy(after, guard, 64);
    g_client_width = 1920, g_client_height = 1200;
    gfx_invalidate_rect(0, 0, 1920, 1200);
    CHECK(!memcmp(after, guard, 64), "gfx_invalidate_rect wrote past the grid");
}

int main(int argc, char **argv)
{
    rct_harness_init(argc, argv);
    test_invalidate();
    printf("%s (%d failures)\n", rct_failures ? "FAILED" : "ok", rct_failures);
    return rct_failures != 0;
}
