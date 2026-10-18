/* Viewports: rendering a screen rectangle of a viewport, and zooming. */
#ifndef VIEWPORT_H
#define VIEWPORT_H
#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
    int16_t width, height;      /* +0x00 on screen */
    int16_t x, y;               /* +0x04 screen position */
    int16_t view_x, view_y;     /* +0x08 map-view position of the top-left corner */
    int16_t view_width, view_height;   /* +0x0C = width/height << zoom */
    uint8_t zoom;               /* +0x10 0..2: one screen pixel = 1 << zoom view pixels */
    uint8_t _pad;
    uint16_t flags;             /* +0x12 copied to the paint session flags */
} Viewport;

typedef struct {
    uint8_t *bits;              /* +0x00 pixel at (x, y) */
    int16_t x, y, width, height;
    int16_t pitch;              /* +0x0C bytes skipped after each row */
    int16_t zoom;               /* +0x0E */
} DrawPixelInfo;
#pragma pack(pop)

/* 0x425E26: render the part of [x1,x2) x [y1,y2) (screen) covered by vp into dpi. */
void viewport_render(Viewport *vp, DrawPixelInfo *dpi, int16_t x1, int16_t y1, int16_t x2, int16_t y2);
/* 0x4288AF / 0x4288E9: zoom the viewport of a window (window + 8) in / out. */
void window_zoom_in(uint8_t *window);
void window_zoom_out(uint8_t *window);

/* Extension (off unless enabled, so the functions above match the original):
   zooming in past zoom 0 magnifies the main view 2x, then 4x; zooming out
   undoes that first. */
extern int g_map_magnify_enabled;
extern int g_map_magnify;                 /* 1, 2 or 4 */
/* 0x425D00 prologue: screen position under the magnified view -> the position
   the game's (unmagnified) picking expects. */
void map_magnify_unproject(int16_t *x, int16_t *y);
/* Call once per frame (the display layer does): keeps a magnified view whole. */
void map_magnify_tick(void);
/* Zoom the main window's view one step (dir > 0: in), as the game's buttons do. */
void map_zoom_step(int dir);

#endif
