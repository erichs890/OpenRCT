/* Paint session state and the sprite primitives (not reconstructed yet: the
   wrappers call the originals through runtime/image.h). */
#ifndef PAINT_H
#define PAINT_H
#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
    uint16_t height;   /* top of what is already painted in this segment */
    uint8_t slope;     /* 0x20 = flat top left by a support */
    uint8_t _pad;
} SupportSegment;

typedef struct {
    uint16_t height;   /* 0xC2D4E8 */
    uint8_t slope;     /* 0xC2D4EA */
    uint8_t flags;     /* 0xC2D4EB bit 0: supports may be drawn (meaning inferred) */
} PaintSupport;
#pragma pack(pop)

extern SupportSegment g_support_segments[9];   /* 0xC2D4C4 */
extern PaintSupport g_paint_support;           /* 0xC2D4E8 */
extern uint32_t g_paint_rotation;              /* 0xC2D4F0 */
extern uint16_t g_paint_session_flags;         /* 0xC2D4F4 bit 3: skip supports */
extern int16_t g_paint_bound_offset[3];        /* 0x8B5FAC: x, y, z for child sprites */
extern uint32_t g_paint_last_struct;           /* 0x8B5FB4: paint struct that attached sprites hang from */
extern uint16_t g_paint_c2d4ec;                /* 0xC2D4EC: wooden supports stop doubling here (meaning unknown) */

#pragma pack(push, 1)
typedef struct {
    uint8_t offset_x, offset_y;
    int8_t offset_z;
    uint8_t bound_x, bound_y, bound_z;
    uint8_t attach;    /* hang from g_paint_last_struct when there is one */
    uint8_t enabled;
} WoodenSpecial;
#pragma pack(pop)

/* Paint list (paint structs are 0x30 bytes; see src/paint_add.c for the layout). */
#pragma pack(push, 1)
typedef struct {
    uint32_t offset;
    int16_t width, height, x_offset, y_offset;
    uint16_t flags, zoomed;
} G1Element;
typedef struct {
    uint8_t *bits;
    int16_t x, y, width, height, pitch, zoom;
} PaintDpi;
#pragma pack(pop)
extern G1Element g_g1_elements[];               /* 0xAFC290 sprite table */
extern uint8_t *g_paint_next;                   /* 0x8CFDEC next free paint struct */
extern uint8_t *g_paint_end;                    /* 0x8CFDE4 end of the pool */
extern uint8_t *g_paint_last_added;             /* 0x8FF00C */
extern const PaintDpi *g_paint_dpi;             /* 0xC1D4B8 drawing target being painted */
extern int16_t g_paint_map_x, g_paint_map_y;    /* 0x8B5A3C / 0x8B5A40 map position of the element */
extern uint16_t g_paint_element_type;           /* 0x8B5A44 */
extern uint32_t g_paint_element, g_paint_element2;   /* 0x8B5A48 / 0x8B5A4C */
extern int16_t g_paint_sprite_left, g_paint_sprite_top;   /* 0x8FF000 / 0x8FF002 */
extern uint8_t *g_paint_quadrants[256];         /* 0x8FEBF0 depth buckets (lists through ps+0x20) */
extern uint32_t g_paint_quadrant_min, g_paint_quadrant_max;   /* 0x8FEFF0 / 0x8FEFF4 */

/* 0x4263B8 + rotation: returns 0 when the pool is full or the sprite is off
   the drawing target (original: CF); *out_ps = the paint struct slot. */
int paint_add_parent(int rotation, uint32_t image, int8_t x_offset, int8_t y_offset, uint16_t bound_x,
                     uint16_t bound_y, int8_t bound_z, uint16_t z, uint8_t **out_ps);

enum { PAINT_EXIT_POOL_FULL, PAINT_EXIT_CULLED, PAINT_EXIT_ADDED };
enum { PAINT_SET_AX = 1, PAINT_SET_CX = 2, PAINT_SET_SI = 4, PAINT_SET_DI = 8 };
/* What the original leaves in its registers (paint_add_regs rebuilds them). */
typedef struct {
    int exit, set;            /* PAINT_EXIT_*, PAINT_SET_* (low words that were changed) */
    int16_t ax, cx, si, di;
    uint32_t ebx, edi;        /* culled: drawing target; added: previous bucket head, bucket */
    uint16_t edx_hi;          /* box z top */
    uint32_t cmp_a, cmp_b;    /* not added: the comparison that decided it (16-bit signed when culled, */
    int cmp32;                /* 32-bit unsigned when the pool is full); its flags are the original's */
} PaintAddTrace;
int paint_add_parent_traced(int rotation, uint32_t image, int8_t x_offset, int8_t y_offset, uint16_t bound_x,
                            uint16_t bound_y, int8_t bound_z, uint16_t z, uint8_t **out_ps, PaintAddTrace *trace);
/* 0x426A04 + rotation (table 0x4269F4): the box is at g_paint_bound_offset. */
int paint_add_child(int rotation, uint32_t image, int8_t x_offset, int8_t y_offset, uint16_t bound_x,
                    uint16_t bound_y, int8_t bound_z, uint16_t z, uint8_t **out_ps);
int paint_add_child_traced(int rotation, uint32_t image, int8_t x_offset, int8_t y_offset, uint16_t bound_x,
                           uint16_t bound_y, int8_t bound_z, uint16_t z, uint8_t **out_ps, PaintAddTrace *trace);
/* 0x4270E0 + rotation (table 0x4270D0): a child that is not put in a depth
   bucket (the caller attaches it to another paint struct). */
int paint_add_attached(int rotation, uint32_t image, int8_t x_offset, int8_t y_offset, uint16_t bound_x,
                       uint16_t bound_y, int8_t bound_z, uint16_t z, uint8_t **out_ps);
int paint_add_attached_traced(int rotation, uint32_t image, int8_t x_offset, int8_t y_offset, uint16_t bound_x,
                              uint16_t bound_y, int8_t bound_z, uint16_t z, uint8_t **out_ps, PaintAddTrace *trace);
/* kind 0 = parent, 1 = child, 2 = attached; regs: eax ebx ecx edx esi edi ebp + 4 outputs (runtime/hooks.S) */
void paint_add_regs(int kind, int rotation, uint32_t *regs);

/* Paint list walk (src/paint_list.c) */
extern uint32_t g_paint_root;                   /* 0x8CFDE8 first struct of the sorted list (a dummy) */
extern uint32_t g_paint_strings;                /* 0x8FF004 text labels list */
extern uint32_t g_paint_8ff008;                 /* 0x8FF008 cleared with it (meaning unknown) */
extern uint32_t g_paint_sort_start, g_paint_sort_prev;   /* 0x8FEFF8 / 0x8FEFFC */
extern const uint8_t k_paint_sort_table[];      /* 0x78E99C [rotation << 6 | box relation] */
void paint_session_begin(uint8_t *pool);        /* 0x42635F */
void paint_arrange_regs(uint32_t *regs);        /* 0x42839E, regs: eax ebx ecx edx esi edi ebp */
void paint_draw(void);                          /* 0x42860C */

/* Data tables of the original image (kept there: game data is not copied into this repo). */
extern const uint8_t k_support_bound_xy[9][2];        /* 0x78736C */
extern const uint8_t k_metal_segment_search[];        /* 0x78737E [rot*2 + seg*8 + k*0x48] {segment, offset} */
extern const int8_t k_metal_offset_xy[][2];           /* 0x78749E */
extern const uint8_t k_metal_offset_bounds[][2];      /* 0x7874AE */
extern const uint16_t k_metal_offset_images[][8];     /* 0x7874BE [type][offset] */
extern const uint16_t k_metal_type_height[];          /* 0x78758E */
extern const uint16_t k_metal_type_images[][2];       /* 0x7875A8 {base, column} */
extern const uint16_t k_metal_type_images_neg[][2];   /* 0x7875DC {?, column below} */
extern const uint16_t k_support_slope_images[32];     /* 0x787848 */
extern const uint16_t k_wooden_type_images[][4];      /* 0x787610 {double, single, steep, slope} */
extern const uint16_t k_wooden_slope_offsets[32];     /* 0x787808 */
extern const uint16_t k_wooden_special_images[];      /* 0x787670 [type] */
extern const WoodenSpecial k_wooden_specials[];       /* 0x787688 [special - 1] */

/* 0x4263A8[rotation] / 0x4269F4[rotation]. Return 0 when the sprite is culled (CF). */
int paint_add_image_as_parent(uint32_t image, int8_t x, int8_t y, uint16_t bound_x, uint16_t bound_y,
                              uint8_t bound_z, uint16_t z);
int paint_add_image_as_child(uint32_t image, int8_t x, int8_t y, uint16_t bound_x, uint16_t bound_y,
                             uint8_t bound_z, uint16_t z);
/* 0x4270D0[rotation]: like as_child; on success *ps = the new paint struct (original: ebp). */
int paint_add_image_attached(uint32_t image, int8_t x, int8_t y, uint16_t bound_x, uint16_t bound_y,
                             uint8_t bound_z, uint16_t z, uint32_t *ps);

/* 0x40C3E1 (variant 0) / 0x40C860 (variant 1). Returns 1 when no free segment
   was found (original: CF set). */
int metal_supports_paint(int variant, uint32_t type, uint32_t segment, int16_t special, uint16_t height,
                         uint32_t image_flags);

/* 0x40BC98 (variant 0) / 0x40C038 (variant 1). */
#define WOODEN_DRAWN 1       /* original: al = [0x8BF699] */
#define WOODEN_BLOCKED 0x100 /* original: CF */
int wooden_supports_paint(int variant, uint32_t type, int16_t special, uint16_t height, uint32_t image_flags);

#endif
