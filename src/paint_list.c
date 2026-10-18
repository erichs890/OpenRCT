/* The paint list after the primitives (src/paint_add.c) have filled it:
     0x42635F paint_session_begin  reset the buckets
     0x42839E paint_arrange        one list, depth-sorted bucket by bucket
     0x42860C paint_draw           draw every struct, its children and attached sprites
   Paint struct fields used here: +4/+6 box x/y, +8 z | z_top << 16,
   +0C/+0E box end x/y, +10/+12 screen x/y, +14 bucket, +17 sort flags,
   +18 attached list, +1C child, +20 next, +24 element type. */
#include <string.h>
#include "paint.h"
#include "image.h"

#define ORIG_DRAW_SPRITE 0x41DA8B   /* cx/dx screen x/y, ebx image, edi drawing target */

#define NEXT(p) (*(uint32_t *)(uintptr_t)((p) + 0x20))
#define F17(p) (*(uint8_t *)(uintptr_t)((p) + 0x17))
#define W(p, o) (*(uint16_t *)(uintptr_t)((p) + (o)))
#define LO16(r, v) ((r) = ((r) & 0xFFFF0000u) | (uint16_t)(v))
#define LO8(r, v) ((r) = ((r) & 0xFFFFFF00u) | (uint8_t)(v))

/* 0x42635F: in ebp = the pool to fill. */
void paint_session_begin(uint8_t *pool)
{
    g_paint_next = pool;
    g_paint_last_added = 0;
    memset(g_paint_quadrants, 0, sizeof g_paint_quadrants);
    g_paint_quadrant_min = 0xFFFFFFFF;
    g_paint_quadrant_max = 0;
    g_paint_8ff008 = 0;
    g_paint_strings = 0;
}

/* 0x42839E. Concatenates the buckets min..max after a new root struct, then,
   for each bucket b from min to max - 1, walks the structs of b and b + 1
   and moves every struct that must be drawn before the current one (its
   box relation, a 6-bit key + rotation, looked up in k_paint_sort_table) in
   front of it. Written over registers (r: eax ebx ecx edx esi edi ebp) because
   the callers see what it leaves in them (docs/functions/42839E_paint_arrange.md). */
static void arrange_pass(uint32_t *r, int first);

void paint_arrange_regs(uint32_t *r)
{
    enum { EAX, EBX, ECX, EDX, ESI, EDI, EBP };
    uint32_t root = (uint32_t)(uintptr_t)g_paint_next;
    g_paint_next += 0x30;
    g_paint_root = root;
    NEXT(root) = 0;
    r[EBP] = root;
    r[EDI] = g_paint_quadrant_min;
    if (r[EDI] == 0xFFFFFFFF)
        return;
    do {   /* append bucket edi */
        r[ESI] = (uint32_t)(uintptr_t)g_paint_quadrants[r[EDI]];
        if (r[ESI]) {
            NEXT(r[EBP]) = r[ESI];
            do {
                r[EBP] = r[ESI];
                r[ESI] = NEXT(r[EBP]);
            } while (r[ESI]);
        }
        r[EDI]++;
    } while (r[EDI] <= g_paint_quadrant_max);

    r[EAX] = g_paint_quadrant_min;
    arrange_pass(r, 1);
    r[EAX] = g_paint_quadrant_min;
    for (;;) {
        r[EAX]++;
        if (r[EAX] >= g_paint_quadrant_max)
            return;
        uint32_t saved = r[EAX];
        arrange_pass(r, 0);
        r[EAX] = saved;
    }
}

static void arrange_pass(uint32_t *r, int first)
{
    enum { EAX, EBX, ECX, EDX, ESI, EDI, EBP };
    /* find the first struct of bucket ax; edi = the one before it */
    r[EBP] = g_paint_root;
    for (;;) {
        r[EDI] = r[EBP];
        r[EBP] = NEXT(r[EDI]);
        if (!r[EBP])
            return;
        if (!((uint16_t)r[EAX] > W(r[EBP], 0x14)))
            break;
    }
    g_paint_sort_start = r[EDI];
    r[EBP] = r[EDI];
    r[EBX] = r[EAX] + 1;
    /* flags: bit 0 = to be placed, bit 1 = may be passed over, 0x80 = past bucket ax + 1 */
    for (;;) {
        r[EBP] = NEXT(r[EBP]);
        if (!r[EBP])
            break;
        LO16(r[ECX], W(r[EBP], 0x14));
        uint8_t dl = 0;
        if ((uint16_t)r[ECX] > (uint16_t)r[EBX]) {
            LO8(r[EDX], dl);
            F17(r[EBP]) = 0x80;
            break;
        }
        if ((uint16_t)r[ECX] == (uint16_t)r[EBX]) dl |= 3;
        if ((uint16_t)r[ECX] == (uint16_t)r[EAX]) dl |= first ? 3 : 1;
        LO8(r[EDX], dl);
        F17(r[EBP]) = dl;
    }
    r[EDI] = g_paint_sort_start;
    for (;;) {   /* 0x42843E: next struct to place */
        r[EBP] = NEXT(r[EDI]);
        if (!r[EBP] || (F17(r[EBP]) & 0x80))
            return;
        if (!(F17(r[EBP]) & 0x81)) {
            r[EDI] = r[EBP];
            continue;
        }
        F17(r[EBP]) &= 0xFE;
        g_paint_sort_prev = r[EDI];
        LO16(r[EAX], W(r[EBP], 4));
        LO16(r[EBX], W(r[EBP], 6));
        r[ECX] = *(uint32_t *)(uintptr_t)(r[EBP] + 8);
        LO16(r[EDX], W(r[EBP], 0x0C));
        LO16(r[ESI], W(r[EBP], 0x0E));
        for (;;) {   /* 0x428476: the structs after it */
            r[EDI] = r[EBP];
            r[EBP] = NEXT(r[EBP]);
            if (!r[EBP] || (F17(r[EBP]) & 0x80))
                break;
            if (!(F17(r[EBP]) & 0x82))
                continue;
            uint32_t b = r[EBP];
            uint32_t key = g_paint_rotation;
            key = (key << 1 | ((uint16_t)r[EAX] < W(b, 0x0C))) & 0xFFFF;
            key = (key << 1 | ((uint16_t)r[EBX] < W(b, 0x0E))) & 0xFFFF;
            key = (key << 1 | ((uint16_t)r[ECX] < W(b, 0x0A))) & 0xFFFF;
            key = (key << 1 | ((uint16_t)r[EDX] < W(b, 0x04))) & 0xFFFF;
            key = (key << 1 | ((uint16_t)r[ESI] < W(b, 0x06))) & 0xFFFF;
            key = (key << 1 | ((uint16_t)(r[ECX] >> 16) < W(b, 0x08))) & 0xFFFF;
            if (!k_paint_sort_table[(g_paint_rotation & 0xFFFF0000u) | key])
                continue;
            /* move b to just after sort_prev, i.e. before the struct being placed */
            NEXT(r[EDI]) = NEXT(b);
            uint32_t old = NEXT(g_paint_sort_prev);
            NEXT(g_paint_sort_prev) = b;
            NEXT(b) = old;
            r[EBP] = r[EDI];
        }
        r[EDI] = g_paint_sort_prev;
    }
}

/* 0x41DA8B, not reconstructed yet. */
static void draw_sprite(uint32_t image, int16_t x, int16_t y, const PaintDpi *dpi)
{
    Regs r = {0};
    r.ecx = (uint16_t)x;
    r.edx = (uint16_t)y;
    r.ebx = image;
    r.edi = (uint32_t)(uintptr_t)dpi;
    rct_call(ORIG_DRAW_SPRITE, &r);
}

/* See-through options (session flags 2 and 4) draw some element types as a
   ghost: the image keeps its sprite and gets the 0x405E0000 palette flags. */
static uint32_t see_through(uint32_t image, uint8_t type, int attached)
{
    int ghost = (g_paint_session_flags & 2) && type == 3;
    if (g_paint_session_flags & 4)
        ghost |= type == 5 || (!attached && (type == 10 || type == 12 || type == 9));
    if (ghost && !(image & 0x40000000))
        image = (image & 0x1FFFF) | 0x405E0000;
    return image;
}

/* 0x42860C. For each struct after the root: draw it; if it has a child,
   continue with the child chain; the attached sprites of the last struct in
   the chain are drawn at offsets from it. At zoom > 0, sprites of type 2 are
   placed on even pixels. */
void paint_draw(void)
{
    const PaintDpi *dpi = g_paint_dpi;
    for (uint32_t ps = NEXT(g_paint_root); ps; ps = NEXT(ps)) {
        uint32_t cur = ps;
        for (;;) {
            int16_t x = (int16_t)W(cur, 0x10), y = (int16_t)W(cur, 0x12);
            uint8_t type = *(uint8_t *)(uintptr_t)(cur + 0x24);
            if (dpi->zoom && type == 2)
                x &= ~1, y &= ~1;
            draw_sprite(see_through(*(uint32_t *)(uintptr_t)cur, type, 0), x, y, dpi);
            uint32_t child = *(uint32_t *)(uintptr_t)(cur + 0x1C);
            if (!child)
                break;
            cur = child;
        }
        uint8_t type = *(uint8_t *)(uintptr_t)(cur + 0x24);
        for (uint32_t at = *(uint32_t *)(uintptr_t)(cur + 0x18); at; at = *(uint32_t *)(uintptr_t)(at + 8))
            draw_sprite(see_through(*(uint32_t *)(uintptr_t)at, type, 1), (int16_t)(W(at, 4) + W(cur, 0x10)),
                        (int16_t)(W(at, 6) + W(cur, 0x12)), dpi);
    }
}
