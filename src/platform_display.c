#include "platform_display.h"

/* 0x57FAB6. Marks the 64x8 blocks covering [x1,x2] x [y1,y2] (corners in any
   order, clipped to the client size from WM_SIZE) in the dirty grid, adding
   one to the dirty count per block. A right/bottom edge exactly on a block
   boundary still marks that block, so x2 == 1280 spills into the next row,
   as in the original.

   Deliberate fix: the original has no bound on the grid, so a client taller
   than 1024 pixels writes past it. Blocks outside the grid are counted
   but not stored. Nothing changes for sizes the original supports (the
   differential test covers them). */
void gfx_invalidate_rect(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    int32_t t;
    if (x2 < x1) t = x2, x2 = x1, x1 = t;
    if (y2 < y1) t = y2, y2 = y1, y1 = t;
    if (x1 > g_client_width || x2 < 0 || y1 > g_client_height || y2 < 0)
        return;
    if (x1 < 0) x1 = 0;
    if (x2 > g_client_width) x2 = g_client_width;
    if (y1 < 0) y1 = 0;
    if (y2 > g_client_height) y2 = g_client_height;
    if (x2 - x1 <= 0 || y2 - y1 <= 0)
        return;

    int32_t bx = x1 / 64, by = y1 / 8;
    int32_t cols = x2 / 64 - bx + 1, rows = y2 / 8 - by + 1;
    int32_t index = by * GFX_GRID_COLS + bx;
    for (int32_t j = 0; j < rows; j++, index += GFX_GRID_COLS - cols)
        for (int32_t i = 0; i < cols; i++, index++, g_gfx_dirty_count++)
            if ((uint32_t)index < GFX_GRID_SIZE)
                g_gfx_dirty_grid[index] = 1;
}
