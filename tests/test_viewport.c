/* Differential tests: 0x425E26 viewport_render, 0x4288AF/0x4288E9 zoom vs src/viewport.c.
   The callees 0x425EE3 (render strip) and 0x57C28A (invalidate window) are
   replaced by recorders, so both sides are compared on the exact calls they make. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "rct.h"
#include "hooks.h"
#include "viewport.h"

typedef struct { uint32_t eax, ebx, edx, ebp, esi, edi; } Call;
static Call calls[2][64];
static int ncalls[2], side;

void __cdecl record(const uint32_t *f)   /* pushad frame: edi esi ebp esp ebx edx ecx eax */
{
    if (ncalls[side] < 64) calls[side][ncalls[side]++] = (Call){f[7], f[4], f[5], f[2], f[1], f[0]};
}
/* render strip stand-in: records, then clobbers what the real one clobbers (eax..ebp, not esi/edi) */
__asm__(".intel_syntax noprefix\n"
        ".globl _rec_strip\n_rec_strip:\n"
        "  pushad\n  push esp\n  call _record\n  add esp, 4\n  popad\n"
        "  mov eax, 0xDEAD0001\n  mov ebx, 0xDEAD0002\n  mov ecx, 0xDEAD0003\n  mov edx, 0xDEAD0004\n"
        "  mov ebp, 0xDEAD0005\n  ret\n"
        ".globl _rec_invalidate\n_rec_invalidate:\n"
        "  pushad\n  push esp\n  call _record\n  add esp, 4\n  popad\n  ret\n"
        ".att_syntax\n");
void rec_strip(void), rec_invalidate(void);

static int16_t r16(int lo, int hi) { return (int16_t)(lo + (int)rnd((uint32_t)(hi - lo + 1))); }

static void random_viewport(Viewport *vp)
{
    vp->width = r16(0, 1400), vp->height = r16(0, 1100);
    vp->x = r16(-100, 1000), vp->y = r16(-100, 800);
    vp->view_x = r16(-30000, 30000), vp->view_y = r16(-30000, 30000);
    vp->zoom = (uint8_t)rnd(3);
    vp->view_width = (int16_t)(vp->width << vp->zoom), vp->view_height = (int16_t)(vp->height << vp->zoom);
    vp->flags = (uint16_t)rnd(0x10000);
}

static void test_render(void)
{
    int called = 0;
    for (int t = 0; t < 20000; t++) {
        Viewport vp;
        DrawPixelInfo dpi = {(uint8_t *)0x12340000, r16(-50, 900), r16(-50, 700), r16(0, 1300), r16(0, 1000),
                             r16(0, 64), 0};
        random_viewport(&vp);
        int16_t x1 = r16(-200, 1500), y1 = r16(-200, 1200), x2 = r16(-200, 1500), y2 = r16(-200, 1200);
        if (rnd(3) == 0) x1 = dpi.x, y1 = dpi.y, x2 = (int16_t)(dpi.x + dpi.width), y2 = (int16_t)(dpi.y + dpi.height);

        side = 0, ncalls[0] = 0;
        Regs r = {.eax = 0x11110000u | (uint16_t)x1, .ebx = 0x22220000u | (uint16_t)y1, .ecx = 0x33333333,
                  .edx = 0x44440000u | (uint16_t)x2, .ebp = 0x55550000u | (uint16_t)y2,
                  .esi = (uint32_t)(uintptr_t)&vp, .edi = (uint32_t)(uintptr_t)&dpi};
        Regs in = r;
        rct_call(0x425E26, &r);
        CHECK(!memcmp(&r, &in, 7 * 4), "render case %d: original did not preserve registers", t);
        side = 1, ncalls[1] = 0;
        viewport_render(&vp, &dpi, x1, y1, x2, y2);
        int same = ncalls[0] == ncalls[1];
        for (int i = 0; same && i < ncalls[0]; i++)
            same = calls[0][i].esi == calls[1][i].esi && calls[0][i].edi == calls[1][i].edi &&
                   (uint16_t)calls[0][i].eax == (uint16_t)calls[1][i].eax &&
                   (uint16_t)calls[0][i].ebx == (uint16_t)calls[1][i].ebx &&
                   (uint16_t)calls[0][i].edx == (uint16_t)calls[1][i].edx &&
                   (uint16_t)calls[0][i].ebp == (uint16_t)calls[1][i].ebp;
        CHECK(same, "render case %d: %d/%d strip calls differ", t, ncalls[0], ncalls[1]);
        called += ncalls[0] > 0;
    }
    printf("viewport_render: 20000 cases, %d drew strips\n", called);
}

static void test_zoom(void)
{
    static uint8_t win[2][0x188];
    for (int t = 0; t < 20000; t++) {
        Viewport vp0, vp1;
        random_viewport(&vp0);
        vp1 = vp0;
        int none = rnd(20) == 0;
        for (int s = 0; s < 2; s++) {
            for (int i = 0; i < 0x188; i++) win[s][i] = (uint8_t)(t * 7 + i);
            *(Viewport **)(win[s] + 8) = none ? NULL : (s ? &vp1 : &vp0);
        }
        int in = t & 1;
        side = 0, ncalls[0] = 0;
        Regs r = {.esi = (uint32_t)(uintptr_t)win[0]};
        rct_call(in ? 0x4288AF : 0x4288E9, &r);
        side = 1, ncalls[1] = 0;
        if (in) window_zoom_in(win[1]); else window_zoom_out(win[1]);
        uint32_t p0 = *(uint32_t *)(win[0] + 8), p1 = *(uint32_t *)(win[1] + 8);
        CHECK(!memcmp(win[0] + 12, win[1] + 12, 0x188 - 12) && !memcmp(win[0], win[1], 8) &&
              (p0 == 0) == (p1 == 0) && !memcmp(&vp0, &vp1, sizeof vp0) && ncalls[0] == ncalls[1] &&
              (!ncalls[0] || calls[1][0].esi == (uint32_t)(uintptr_t)win[1]),
              "zoom %s case %d (zoom %d)", in ? "in" : "out", t, vp0.zoom);
    }
    printf("zoom in/out: 20000 cases\n");
}

int main(int argc, char **argv)
{
    rct_harness_init(argc, argv);
    rct_patch_jmp(0x425EE3, (void *)rec_strip);
    rct_patch_jmp(0x57C28A, (void *)rec_invalidate);
    test_render();
    test_zoom();
    printf("%s (%d failures)\n", rct_failures ? "FAILED" : "ok", rct_failures);
    return rct_failures != 0;
}
