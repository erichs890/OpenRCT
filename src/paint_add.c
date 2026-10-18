/* Sprite primitives of the paint list, one function per kind with the
   rotation as a parameter (the original has one copy per rotation, called
   through a table indexed by [0xC2D4F0]):
     parent  0x4263B8 0x42653B 0x4266CC 0x42685F  (table 0x4263A8)
     child   0x426A04 0x426BA8 0x426D5D 0x426F17  (table 0x4269F4)
     attached 0x4270E0 0x42723E 0x4273A8 0x427514 (table 0x4270D0)
   Paint struct (0x30 bytes): +0 image, +4/+6 map x/y of the bounding box,
   +8 z | z_top << 16, +0C/+0E box end x/y, +10/+12 screen x/y, +14 depth
   bucket, +16 flags, +18/+1C attached/child links, +20 next in bucket,
   +24/+28/+2C element info. */
#include "paint.h"

#define PS8(o) (*(uint8_t *)(ps + (o)))
#define PS16(o) (*(int16_t *)(ps + (o)))
#define PS32(o) (*(uint32_t *)(ps + (o)))

/* Rotate an offset (and a bounding box size) from view axes into map axes. */
static void rotate_offset(int rotation, int16_t *ax, int16_t *cx)
{
    int16_t t;
    switch (rotation) {
    case 0: break;
    case 1: t = *cx, *cx = *ax, *ax = (int16_t)-t; break;
    case 2: *cx = (int16_t)-*cx, *ax = (int16_t)-*ax; break;
    default: t = *cx, *cx = (int16_t)-*ax, *ax = t; break;
    }
}

static void rotate_box(int rotation, int16_t *di, int16_t *si)
{
    int16_t t;
    switch (rotation) {
    case 0: (*di)--, (*si)--; break;
    case 1: (*di)--, t = *si, *si = *di, *di = (int16_t)-t; break;
    case 2: *si = (int16_t)-*si, *di = (int16_t)-*di; break;
    default: (*si)--, t = *si, *si = (int16_t)-*di, *di = t; break;
    }
}

/* Isometric projection of map (x, y) at height z. */
static void project(int rotation, int16_t x, int16_t y, uint16_t z, int16_t *sx, int16_t *sy)
{
    switch (rotation) {
    case 0: *sx = (int16_t)(y - x), *sy = (int16_t)((int16_t)(y + x) >> 1); break;
    case 1: *sx = (int16_t)(-x - y), *sy = (int16_t)((int16_t)(y - x) >> 1); break;
    case 2: *sx = (int16_t)(x - y), *sy = (int16_t)((int16_t)(-y - x) >> 1); break;
    default: *sx = (int16_t)(x + y), *sy = (int16_t)((int16_t)(x - y) >> 1); break;
    }
    *sy = (int16_t)(*sy - z);
}

/* Sprite rectangle from the g1 table, culled against the drawing target.
   `reg` is the register the original compares in (its last value is left
   there, as is `bottom` in the other one). */
static int visible(uint32_t image, int16_t sx, int16_t sy, PaintAddTrace *tr, int16_t *reg, int16_t *other)
{
    const G1Element *g1 = &g_g1_elements[image & 0x1FFFF];
    int16_t left = (int16_t)(sx + g1->x_offset), top = (int16_t)(sy + g1->y_offset);
    g_paint_sprite_left = left;
    g_paint_sprite_top = top;
    int16_t right = (int16_t)(left + g1->width), bottom = (int16_t)(top + g1->height);
    const PaintDpi *dpi = g_paint_dpi;
    tr->exit = PAINT_EXIT_CULLED, tr->cmp32 = 0, tr->ebx = (uint32_t)(uintptr_t)dpi;
    *reg = right, *other = bottom;
#define CULL_IF(a, b) do { tr->cmp_a = (uint16_t)(a), tr->cmp_b = (uint16_t)(b); if ((a) <= (b)) return 0; } while (0)
    CULL_IF(right, dpi->x);
    CULL_IF(bottom, dpi->y);
    *reg = (int16_t)(dpi->x + dpi->width);
    CULL_IF(*reg, left);
    *reg = (int16_t)(dpi->y + dpi->height);
    CULL_IF(*reg, top);
#undef CULL_IF
    return 1;
}

/* Common tail: element info, then the depth bucket (one of 256 diagonal
   strips of the box's map position) and the pool pointer. */
static void link(int rotation, uint8_t *ps, int16_t ax, int16_t cx, PaintAddTrace *tr, int attached)
{
    PS8(0x16) = 0;
    PS16(0x04) = ax;
    PS16(0x06) = cx;
    PS32(0x18) = 0;
    PS32(0x1C) = 0;
    PS16(0x24) = (int16_t)g_paint_element_type;
    PS32(0x28) = g_paint_element;
    PS32(0x2C) = g_paint_element2;
    g_paint_last_added = ps;
    if (attached) {   /* not in a bucket: the caller hangs it from another struct */
        tr->exit = PAINT_EXIT_ADDED, tr->edi = g_paint_element2;   /* ebx stays the drawing target */
        g_paint_next = ps + 0x30;
        return;
    }
    int16_t v;
    switch (rotation) {
    case 0: v = (int16_t)(cx + ax); break;
    case 1: v = (int16_t)(cx - ax + 0x1000); break;
    case 2: v = (int16_t)(-(cx + ax) + 0x2000); break;
    default: v = (int16_t)(ax - cx + 0x1000); break;
    }
    uint32_t q = v < 0 ? 0 : (uint16_t)v >> 5;
    if (q > 0xFF) q = 0xFF;
    PS16(0x14) = (int16_t)q;
    PS32(0x20) = (uint32_t)(uintptr_t)g_paint_quadrants[q];
    tr->exit = PAINT_EXIT_ADDED, tr->ebx = PS32(0x20), tr->edi = q;
    g_paint_quadrants[q] = ps;
    if (q < g_paint_quadrant_min) g_paint_quadrant_min = q;
    if (q > g_paint_quadrant_max) g_paint_quadrant_max = q;
    g_paint_next = ps + 0x30;
}

/* Prologue shared by both: slot from the pool (or full). */
static uint8_t *take_slot(uint32_t image, uint8_t **out_ps, PaintAddTrace *tr)
{
    g_paint_last_added = 0;
    uint8_t *ps = g_paint_next;
    *out_ps = ps;
    tr->exit = PAINT_EXIT_POOL_FULL, tr->cmp32 = 1, tr->set = 0;
    tr->cmp_a = (uint32_t)(uintptr_t)ps, tr->cmp_b = (uint32_t)(uintptr_t)g_paint_end;
    if (ps >= g_paint_end)
        return 0;
    PS32(0) = image;
    return ps;
}

/* Parent: the box is at the sprite's offset, its z from z to z + bound_z. */
int paint_add_parent_traced(int rotation, uint32_t image, int8_t x_offset, int8_t y_offset, uint16_t bound_x,
                            uint16_t bound_y, int8_t bound_z, uint16_t z, uint8_t **out_ps, PaintAddTrace *trace)
{
    PaintAddTrace tr_local;
    PaintAddTrace *tr = trace ? trace : &tr_local;
    tr->edx_hi = (uint16_t)(z + bound_z);
    uint8_t *ps = take_slot(image, out_ps, tr);
    if (!ps) return 0;
    int16_t ax = x_offset, cx = y_offset, di = (int16_t)bound_x, si = (int16_t)bound_y;
    rotate_box(rotation, &di, &si);
    rotate_offset(rotation, &ax, &cx);
    ax = (int16_t)(ax + g_paint_map_x);
    cx = (int16_t)(cx + g_paint_map_y);
    PS16(0x0C) = (int16_t)(di + ax);
    PS32(0x08) = (uint32_t)z | (uint32_t)tr->edx_hi << 16;
    PS16(0x0E) = (int16_t)(si + cx);
    int16_t sx, sy;
    project(rotation, ax, cx, z, &sx, &sy);
    PS16(0x10) = sx;
    PS16(0x12) = sy;
    tr->ax = ax, tr->cx = cx, tr->set = PAINT_SET_AX | PAINT_SET_CX | PAINT_SET_SI | PAINT_SET_DI;
    if (!visible(image, sx, sy, tr, &tr->di, &tr->si))
        return 0;
    link(rotation, ps, ax, cx, tr, 0);
    return 1;
}

int paint_add_parent(int rotation, uint32_t image, int8_t x_offset, int8_t y_offset, uint16_t bound_x,
                     uint16_t bound_y, int8_t bound_z, uint16_t z, uint8_t **out_ps)
{
    return paint_add_parent_traced(rotation, image, x_offset, y_offset, bound_x, bound_y, bound_z, z, out_ps, 0);
}

/* Child: the sprite at its own offset, the box at g_paint_bound_offset
   (x, y, z), its z from offset z to offset z + bound_z. Attached: the same,
   but not linked into a depth bucket. */
static int add_child_or_attached(int attached, int rotation, uint32_t image, int8_t x_offset, int8_t y_offset,
                                 uint16_t bound_x, uint16_t bound_y, int8_t bound_z, uint16_t z, uint8_t **out_ps,
                                 PaintAddTrace *trace)
{
    PaintAddTrace tr_local;
    PaintAddTrace *tr = trace ? trace : &tr_local;
    uint16_t box_z = (uint16_t)g_paint_bound_offset[2];
    tr->edx_hi = (uint16_t)(box_z + bound_z);
    uint8_t *ps = take_slot(image, out_ps, tr);
    if (!ps) return 0;
    int16_t ax = x_offset, cx = y_offset;
    rotate_offset(rotation, &ax, &cx);
    ax = (int16_t)(ax + g_paint_map_x);
    cx = (int16_t)(cx + g_paint_map_y);
    int16_t sx, sy;
    project(rotation, ax, cx, z, &sx, &sy);
    PS16(0x10) = sx;
    PS16(0x12) = sy;
    tr->set = PAINT_SET_AX | PAINT_SET_CX;
    if (!visible(image, sx, sy, tr, &tr->ax, &tr->cx))
        return 0;

    int16_t di = (int16_t)bound_x, si = (int16_t)bound_y;
    ax = g_paint_bound_offset[0], cx = g_paint_bound_offset[1];
    rotate_box(rotation, &di, &si);
    rotate_offset(rotation, &ax, &cx);
    ax = (int16_t)(ax + g_paint_map_x);
    cx = (int16_t)(cx + g_paint_map_y);
    di = (int16_t)(di + ax);
    si = (int16_t)(si + cx);
    PS16(0x0C) = di;
    PS32(0x08) = (uint32_t)box_z | (uint32_t)tr->edx_hi << 16;
    PS16(0x0E) = si;
    tr->ax = ax, tr->cx = cx, tr->si = si, tr->set |= PAINT_SET_SI;
    link(rotation, ps, ax, cx, tr, attached);
    return 1;
}

int paint_add_child_traced(int rotation, uint32_t image, int8_t x_offset, int8_t y_offset, uint16_t bound_x,
                           uint16_t bound_y, int8_t bound_z, uint16_t z, uint8_t **out_ps, PaintAddTrace *trace)
{
    return add_child_or_attached(0, rotation, image, x_offset, y_offset, bound_x, bound_y, bound_z, z, out_ps, trace);
}

int paint_add_attached_traced(int rotation, uint32_t image, int8_t x_offset, int8_t y_offset, uint16_t bound_x,
                              uint16_t bound_y, int8_t bound_z, uint16_t z, uint8_t **out_ps, PaintAddTrace *trace)
{
    return add_child_or_attached(1, rotation, image, x_offset, y_offset, bound_x, bound_y, bound_z, z, out_ps, trace);
}

int paint_add_attached(int rotation, uint32_t image, int8_t x_offset, int8_t y_offset, uint16_t bound_x,
                       uint16_t bound_y, int8_t bound_z, uint16_t z, uint8_t **out_ps)
{
    return paint_add_attached_traced(rotation, image, x_offset, y_offset, bound_x, bound_y, bound_z, z, out_ps, 0);
}

int paint_add_child(int rotation, uint32_t image, int8_t x_offset, int8_t y_offset, uint16_t bound_x,
                    uint16_t bound_y, int8_t bound_z, uint16_t z, uint8_t **out_ps)
{
    return paint_add_child_traced(rotation, image, x_offset, y_offset, bound_x, bound_y, bound_z, z, out_ps, 0);
}

/* Register-exact entry for the asm adapters (runtime/hooks.S): r holds the
   entry registers; on return it holds what the original leaves. Asm callers
   read these leftovers (see docs/functions/4263B8_paint_add_parent.md). */
void paint_add_regs(int kind, int rotation, uint32_t *r)
{
    /* r: eax ebx ecx edx esi edi ebp, then out: culled, cmp a, cmp b, cmp is 32-bit */
    enum { EAX, EBX, ECX, EDX, ESI, EDI, EBP, CULLED, CMP_A, CMP_B, CMP32 };
    uint8_t *ps;
    PaintAddTrace tr;
    uint16_t z = (uint16_t)r[EDX];
    static int (*const fn[3])(int, uint32_t, int8_t, int8_t, uint16_t, uint16_t, int8_t, uint16_t, uint8_t **,
                              PaintAddTrace *) = {paint_add_parent_traced, paint_add_child_traced,
                                                  paint_add_attached_traced};
    int ok = fn[kind](
        rotation, r[EBX], (int8_t)r[EAX], (int8_t)r[ECX], (uint16_t)r[EDI], (uint16_t)r[ESI], (int8_t)(r[EAX] >> 8), z,
        &ps, &tr);
    r[EDX] = (uint32_t)z | (uint32_t)tr.edx_hi << 16;
    r[EBP] = (uint32_t)(uintptr_t)ps;
    r[CULLED] = !ok;
    r[CMP_A] = tr.cmp_a, r[CMP_B] = tr.cmp_b, r[CMP32] = tr.cmp32;
    if (tr.exit == PAINT_EXIT_POOL_FULL)
        return;
#define LO16(reg, v) r[reg] = (r[reg] & 0xFFFF0000u) | (uint16_t)(v)
    if (tr.set & PAINT_SET_AX) LO16(EAX, tr.ax);
    if (tr.set & PAINT_SET_CX) LO16(ECX, tr.cx);
    if (tr.set & PAINT_SET_SI) LO16(ESI, tr.si);
    if (tr.exit == PAINT_EXIT_ADDED) r[EDI] = tr.edi;
    else if (tr.set & PAINT_SET_DI) LO16(EDI, tr.di);
#undef LO16
    r[EBX] = tr.ebx;
}
