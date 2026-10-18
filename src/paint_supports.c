/* Support structures drawn under track pieces and paths. */
#include "paint.h"
#include "image.h"

#define ORIG_PAINT_ADD_PARENT 0x4263A8   /* [rotation] */
#define ORIG_PAINT_ADD_CHILD 0x4269F4    /* [rotation] */
#define ORIG_PAINT_ADD_ATTACHED 0x4270D0 /* [rotation] */

extern uint16_t g_support_height_tmp;    /* 0x8BF67C */
extern uint8_t g_wooden_drawn;           /* 0x8BF699 */

static int paint_add(uint32_t table, uint32_t image, int8_t x, int8_t y, uint16_t bound_x, uint16_t bound_y,
                     uint8_t bound_z, uint16_t z, uint32_t *ps)
{
    /* Register contract of the primitives: ebx image, al x, cl y, di/si bound
       x/y, ah bound z, dx z. They clobber everything but dx; CF = culled,
       ebp = the new paint struct. */
    Regs r = {0};
    r.eax = (uint32_t)bound_z << 8 | (uint8_t)x;
    r.ecx = (uint8_t)y;
    r.ebx = image;
    r.edi = bound_x;
    r.esi = bound_y;
    r.edx = z;
    rct_call(((const uint32_t *)table)[g_paint_rotation], &r);
    if (ps)
        *ps = r.ebp;
    return !(r.eflags & 1);
}

int paint_add_image_attached(uint32_t image, int8_t x, int8_t y, uint16_t bound_x, uint16_t bound_y,
                             uint8_t bound_z, uint16_t z, uint32_t *ps)
{
    return paint_add(ORIG_PAINT_ADD_ATTACHED, image, x, y, bound_x, bound_y, bound_z, z, ps);
}

int paint_add_image_as_parent(uint32_t image, int8_t x, int8_t y, uint16_t bound_x, uint16_t bound_y,
                              uint8_t bound_z, uint16_t z)
{
    return paint_add(ORIG_PAINT_ADD_PARENT, image, x, y, bound_x, bound_y, bound_z, z, 0);
}

int paint_add_image_as_child(uint32_t image, int8_t x, int8_t y, uint16_t bound_x, uint16_t bound_y,
                             uint8_t bound_z, uint16_t z)
{
    return paint_add(ORIG_PAINT_ADD_CHILD, image, x, y, bound_x, bound_y, bound_z, z, 0);
}

/* One column piece of `len` units (1..16) at z, from `column` (a 16-image strip). */
static void metal_column(int child, uint16_t column, uint32_t segment, uint16_t len, uint16_t z, uint32_t flags,
                         int joint)
{
    uint16_t image = (uint16_t)(column + len - 1);
    if (joint && len == 16)
        image++;
    const uint8_t *xy = k_support_bound_xy[segment];
    if (child)
        paint_add_image_as_child(image | flags, (int8_t)xy[0], (int8_t)xy[1], 0, 0, 0, z);
    else
        paint_add_image_as_parent(image | flags, (int8_t)xy[0], (int8_t)xy[1], 0, 0, (uint8_t)(len - 1), z);
}

/* Child columns from z up to `top` (the `special` extension above the track). */
static void metal_special(uint16_t column, uint32_t segment, uint16_t z, uint16_t top, uint32_t flags)
{
    g_paint_bound_offset[0] = k_support_bound_xy[segment][0];
    g_paint_bound_offset[1] = k_support_bound_xy[segment][1];
    g_paint_bound_offset[2] = (int16_t)z;
    for (;;) {
        uint16_t end = (uint16_t)(z + 16);
        if (end > top)
            end = top;
        if (end <= z)
            return;
        metal_column(1, column, segment, (uint16_t)(end - z), z, flags, 0);
        z = end;
    }
}

/* 0x40C3E1 / 0x40C860. Draws a metal support from the highest free point of
   `segment` (moving to a neighbouring segment if it is taken) up to `height`,
   then marks the segment as used; `special` adds a column above (> 0) or,
   in variant 0 only, below (< 0) the track. All arithmetic is 16-bit, as in
   the original. */
int metal_supports_paint(int variant, uint32_t type, uint32_t segment, int16_t special, uint16_t height,
                         uint32_t image_flags)
{
    if (g_paint_session_flags & 8)
        return 0;
    if (!(g_paint_support.flags & 1))
        return 0;

    uint32_t seg = segment;
    uint16_t z = height;
    g_support_height_tmp = 0xFFFF;
    if (z < g_support_segments[seg].height) {
        /* the segment is taken: start a short offset piece in a free neighbour */
        g_support_height_tmp = z;
        z = (uint16_t)(z - k_metal_type_height[type]);
        if ((int16_t)z < 0)
            return 0;
        const uint8_t *search = k_metal_segment_search + g_paint_rotation * 2;
        int k;
        for (k = 0; k < 4; k++, search += 0x48)
            if (z > g_support_segments[search[seg * 8]].height)
                break;
        if (k == 4)
            return 1;
        uint8_t next = search[seg * 8], offset = search[seg * 8 + 1];
        if (variant && offset >= 4)
            return 1;
        paint_add_image_as_parent(k_metal_offset_images[type][offset] | image_flags,
                                  (int8_t)(k_support_bound_xy[seg][0] + k_metal_offset_xy[offset][0]),
                                  (int8_t)(k_support_bound_xy[seg][1] + k_metal_offset_xy[offset][1]),
                                  k_metal_offset_bounds[offset][0], k_metal_offset_bounds[offset][1], 1, z);
        seg = next;
    }

    /* base plate on sloped ground, if there is room */
    SupportSegment *s = &g_support_segments[seg];
    uint16_t top = z;
    if (!(s->slope & 0x20) && (int16_t)(z - s->height) >= 6 && k_metal_type_images[type][0] != 0) {
        uint16_t image = (uint16_t)(k_metal_type_images[type][0] + k_support_slope_images[s->slope & 0x1F]);
        paint_add_image_as_parent(image | image_flags, (int8_t)k_support_bound_xy[seg][0],
                                  (int8_t)k_support_bound_xy[seg][1], 0, 0, 5, s->height);
        z = (uint16_t)(s->height + 6);
    } else {
        z = s->height;
    }

    /* column: first piece up to the next multiple of 16, then 16-unit pieces
       with a joint sprite on every 4th full one */
    uint16_t column = k_metal_type_images[type][1];
    uint16_t end = (uint16_t)((z + 16) & 0xFFF0);
    if (end > top)
        end = top;
    uint16_t len = (uint16_t)(end - z);
    if ((int16_t)len > 0)
        metal_column(0, column, seg, len, z, image_flags, 0);
    z = (uint16_t)(z + len);
    for (int i = 0;; i++) {
        end = (uint16_t)(z + 16);
        if (end > top)
            end = top;
        if (end <= z)
            break;
        metal_column(0, column, seg, (uint16_t)(end - z), z, image_flags, i % 4 == 3);
        z = end;
    }

    s->height = g_support_height_tmp;
    s->slope = 0x20;

    if (special == 0)
        return 0;
    if (special < 0 && !variant)
        metal_special(k_metal_type_images_neg[type][1], segment, (uint16_t)(height - 1),
                      (uint16_t)(-special + height - 1), image_flags);
    else
        metal_special(column, segment, height, (uint16_t)(special + height), image_flags);
    return 0;
}

/* Child sprite with its bounding box at (0, 0, z + 2), as the slope pieces use. */
static void wooden_slope_piece(uint32_t image, uint8_t bound_z, uint16_t z)
{
    g_paint_bound_offset[0] = 0;
    g_paint_bound_offset[1] = 0;
    g_paint_bound_offset[2] = (int16_t)(z + 2);
    paint_add_image_as_child(image, 0, 0, 0x20, 0x20, bound_z, z);
}

/* 0x40BC98 / 0x40C038. Wooden supports from the ground under the current tile
   (g_paint_support) up to `height`, in 16-unit pieces (32 where possible); a
   sloped tile starts with a slope piece, a steep one with a steep piece.
   `special` (1-based) adds an extra sprite from k_wooden_specials on top.
   Variant 1 uses a lower bounding box (3 instead of 11) for the upper half of
   a double slope piece and for a single one, and skips the steep piece when
   there is no room for it. */
int wooden_supports_paint(int variant, uint32_t type, int16_t special, uint16_t height, uint32_t image_flags)
{
    g_wooden_drawn = 0;
    if (g_paint_session_flags & 8)
        return 0;
    if (!(g_paint_support.flags & 1))
        return 0;

    uint16_t z = (uint16_t)((g_paint_support.height + 15) & 0xFFF0);
    if (height < z)
        return WOODEN_BLOCKED;
    uint16_t count = (uint16_t)(height - z) >> 4;
    uint8_t slope = g_paint_support.slope;
    const uint16_t *images = k_wooden_type_images[type];
    uint8_t slope_bound_z = variant ? 3 : 0xB;
    int steep = 0;

    if (slope & 0x20) {
        steep = 1;
    } else if (slope & 0x10) {
        if (count < 2)
            return WOODEN_BLOCKED;
        count -= 2;
        if (images[3] == 0) {
            z += 0x20;
            steep = 1;
        } else {
            uint32_t image = (uint16_t)(images[3] + k_wooden_slope_offsets[slope & 0x1F]) | image_flags;
            wooden_slope_piece(image, 0xB, z);
            wooden_slope_piece(image + 4, slope_bound_z, (uint16_t)(z + 0x10));
            g_wooden_drawn = 1;
            z += 0x20;
        }
    } else if (slope & 0x0F) {
        if (count < 1)
            return WOODEN_BLOCKED;
        count -= 1;
        if (images[3] == 0) {
            z += 0x10;
            steep = 1;
        } else {
            uint32_t image = (uint16_t)(images[3] + k_wooden_slope_offsets[slope & 0x1F]) | image_flags;
            wooden_slope_piece(image, slope_bound_z, z);
            z += 0x10;
            g_wooden_drawn = 1;
        }
    }
    if (steep && !(variant && count == 0)) {
        paint_add_image_as_parent(images[2] | image_flags, 0, 0, 0x20, 0x20, 0, (uint16_t)(z - 2));
        g_wooden_drawn = 1;
    }

    while (count != 0) {
        if (!(z & 0x10) && count > 1 && (uint16_t)(z + 0x10) != g_paint_c2d4ec) {
            paint_add_image_as_parent(images[0] | image_flags, 0, 0, 0x20, 0x20, count == 2 ? 0x17 : 0x1C, z);
            z += 0x20;
            count -= 2;
        } else {
            paint_add_image_as_parent(images[1] | image_flags, 0, 0, 0x20, 0x20, count == 1 ? 7 : 0xC, z);
            z += 0x10;
            count -= 1;
        }
        g_wooden_drawn = 1;
    }

    if (special != 0) {
        uint16_t index = (uint16_t)(special - 1);
        const WoodenSpecial *sp = &k_wooden_specials[index];
        if (k_wooden_special_images[type] != 0 && sp->enabled) {
            uint32_t image = (k_wooden_special_images[type] + (uint32_t)index) | image_flags;
            g_paint_bound_offset[0] = sp->offset_x;
            g_paint_bound_offset[1] = sp->offset_y;
            g_paint_bound_offset[2] = (int16_t)(sp->offset_z + z);
            if (sp->attach && g_paint_last_struct) {
                uint32_t ps;
                g_wooden_drawn = 1;
                if (paint_add_image_attached(image, 0, 0, sp->bound_x, sp->bound_y, sp->bound_z, z, &ps))
                    *(uint32_t *)(uintptr_t)(g_paint_last_struct + 0x1C) = ps;
            } else {
                paint_add_image_as_child(image, 0, 0, sp->bound_x, sp->bound_y, sp->bound_z, z);
                g_wooden_drawn = 1;
            }
        }
    }
    return g_wooden_drawn;
}
