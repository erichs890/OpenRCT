/* GDI display layer (VC6-compiled C, 0x57EF84..0x58D480). */
#ifndef PLATFORM_DISPLAY_H
#define PLATFORM_DISPLAY_H
#include <stdint.h>

#define GFX_GRID_COLS 20          /* 64-pixel columns: 1280 pixels */
#define GFX_GRID_SIZE 0xA00       /* 20 x 128 rows of 8 pixels: 1280 x 1024 */

extern uint8_t g_gfx_dirty_grid[GFX_GRID_SIZE];   /* 0xC3BE60 */
extern int32_t g_gfx_dirty_count;                 /* 0xC3879C */
extern int32_t g_client_width, g_client_height;   /* 0xC3B004 / 0xC3B574, from WM_SIZE */

void gfx_invalidate_rect(int32_t x1, int32_t y1, int32_t x2, int32_t y2);   /* 0x57FAB6 */

#endif
