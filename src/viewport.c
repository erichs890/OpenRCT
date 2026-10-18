#include <stdlib.h>
#include "viewport.h"
#include "image.h"

#define ORIG_RENDER_STRIP 0x425EE3   /* esi vp, edi dpi, ax/bx/dx/bp view rect x1,y1,x2,y2 */
#define ORIG_WINDOW_INVALIDATE 0x57C28A   /* esi window */
#define ORIG_MAIN_WINDOW 0x57E810    /* -> esi, CF if none */
#define ORIG_INVALIDATE_SCREEN 0x578B61   /* ax/bx/dx/bp = screen x1,y1,x2,y2: redraw that area next frame */
#define WINDOW_VIEWPORT 8
#define WINDOW_SAVED_VIEW_X 0x182
#define WINDOW_SAVED_VIEW_Y 0x184

int g_map_magnify_enabled;
int g_map_magnify = 1;
static Viewport *magnified;          /* the viewport g_map_magnify applies to */

static void render_strip(Viewport *vp, DrawPixelInfo *dpi, int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    Regs r = {0};
    r.esi = (uint32_t)(uintptr_t)vp;
    r.edi = (uint32_t)(uintptr_t)dpi;
    r.eax = (uint16_t)x1;
    r.ebx = (uint16_t)y1;
    r.edx = (uint16_t)x2;
    r.ebp = (uint16_t)y2;
    rct_call(ORIG_RENDER_STRIP, &r);
}

/* The original 0x425E26 (16-bit arithmetic throughout). */
static void render_plain(Viewport *vp, DrawPixelInfo *dpi, int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    if (x2 <= vp->x || y2 <= vp->y)
        return;
    int16_t right = (int16_t)(vp->x + vp->width);
    if (x1 >= right) return;
    if (x1 < vp->x) x1 = vp->x;
    if (x2 > right) x2 = right;
    int16_t bottom = (int16_t)(vp->y + vp->height);
    if (y1 >= bottom) return;
    if (y1 < vp->y) y1 = vp->y;
    if (y2 > bottom) y2 = bottom;

    uint8_t z = vp->zoom;
    x1 = (int16_t)((int16_t)((x1 - vp->x) << z) + vp->view_x);
    x2 = (int16_t)((int16_t)((x2 - vp->x) << z) + vp->view_x);
    y1 = (int16_t)((int16_t)((y1 - vp->y) << z) + vp->view_y);
    y2 = (int16_t)((int16_t)((y2 - vp->y) << z) + vp->view_y);
    /* one split at most: a tall rectangle is drawn as 0x180 rows + the rest */
    if ((uint16_t)(y2 - y1) > 0x180) {
        render_strip(vp, dpi, x1, y1, x2, (int16_t)(y1 + 0x180));
        y1 = (int16_t)(y1 + 0x180);
    }
    render_strip(vp, dpi, x1, y1, x2, y2);
}

static int floor_div(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* Magnified view: the centre 1/m of the viewport, drawn at zoom 0 into a
   scratch buffer, then scaled m times onto the screen. */
static void render_magnified(Viewport *vp, DrawPixelInfo *dpi, int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    static uint8_t scratch[3840 * 2160];
    int m = g_map_magnify;
    if (x1 < vp->x) x1 = vp->x;
    if (y1 < vp->y) y1 = vp->y;
    if (x2 > vp->x + vp->width) x2 = (int16_t)(vp->x + vp->width);
    if (y2 > vp->y + vp->height) y2 = (int16_t)(vp->y + vp->height);
    if (x1 >= x2 || y1 >= y2) return;
    int cx = vp->x + vp->width / 2, cy = vp->y + vp->height / 2;
    int sx1 = cx + floor_div(x1 - cx, m), sx2 = cx + floor_div(x2 - 1 - cx, m) + 1;
    int sy1 = cy + floor_div(y1 - cy, m), sy2 = cy + floor_div(y2 - 1 - cy, m) + 1;
    int sw = sx2 - sx1, sh = sy2 - sy1;
    if ((size_t)sw * sh > sizeof scratch) return;

    DrawPixelInfo tmp = {scratch, (int16_t)sx1, (int16_t)sy1, (int16_t)sw, (int16_t)sh, 0, 0};
    for (int i = 0; i < sw * sh; i++) scratch[i] = 0;
    render_plain(vp, &tmp, (int16_t)sx1, (int16_t)sy1, (int16_t)sx2, (int16_t)sy2);

    int stride = dpi->width + dpi->pitch;
    for (int y = y1; y < y2; y++) {
        const uint8_t *src = scratch + (cy + floor_div(y - cy, m) - sy1) * sw;
        uint8_t *dst = dpi->bits + (y - dpi->y) * stride - dpi->x;
        for (int x = x1; x < x2; x++)
            dst[x] = src[cx + floor_div(x - cx, m) - sx1];
    }
}

void viewport_render(Viewport *vp, DrawPixelInfo *dpi, int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    if (g_map_magnify > 1 && vp == magnified && vp->zoom == 0 && dpi->zoom == 0)
        render_magnified(vp, dpi, x1, y1, x2, y2);
    else
        render_plain(vp, dpi, x1, y1, x2, y2);
}

static void invalidate(uint8_t *window)
{
    Regs r = {0};
    r.esi = (uint32_t)(uintptr_t)window;
    rct_call(ORIG_WINDOW_INVALIDATE, &r);
}

/* 0x4288AF. Halves the view size and moves the saved view position so the
   centre stays put. Extension: at zoom 0, magnify instead. */
void window_zoom_in(uint8_t *window)
{
    Viewport *vp = *(Viewport **)(window + WINDOW_VIEWPORT);
    if (!vp) return;
    if (vp->zoom == 0) {
        if (g_map_magnify_enabled && g_map_magnify < 4) {
            if (vp != magnified) g_map_magnify = 1;
            magnified = vp;
            g_map_magnify *= 2;
            invalidate(window);
        }
        return;
    }
    vp->zoom--;
    vp->view_width >>= 1;
    vp->view_height >>= 1;
    *(int16_t *)(window + WINDOW_SAVED_VIEW_X) += (uint16_t)vp->view_width >> 1;
    *(int16_t *)(window + WINDOW_SAVED_VIEW_Y) += (uint16_t)vp->view_height >> 1;
    invalidate(window);
}

/* 0x4288E9. Doubles the view size (up to zoom 2). Extension: undo
   magnification first. */
void window_zoom_out(uint8_t *window)
{
    Viewport *vp = *(Viewport **)(window + WINDOW_VIEWPORT);
    if (!vp) return;
    if (g_map_magnify > 1 && vp == magnified && vp->zoom == 0) {
        g_map_magnify /= 2;
        invalidate(window);
        return;
    }
    if (vp->zoom == 2) return;
    vp->zoom++;
    uint16_t w = (uint16_t)vp->view_width, h = (uint16_t)vp->view_height;
    vp->view_width = (int16_t)(w << 1);
    vp->view_height = (int16_t)(h << 1);
    *(int16_t *)(window + WINDOW_SAVED_VIEW_X) -= w >> 1;
    *(int16_t *)(window + WINDOW_SAVED_VIEW_Y) -= h >> 1;
    invalidate(window);
}

void map_magnify_unproject(int16_t *x, int16_t *y)
{
    Viewport *vp = magnified;
    if (g_map_magnify <= 1 || !vp || vp->zoom != 0) return;
    if (*x < vp->x || *x >= vp->x + vp->width || *y < vp->y || *y >= vp->y + vp->height) return;
    int cx = vp->x + vp->width / 2, cy = vp->y + vp->height / 2;
    *x = (int16_t)(cx + floor_div(*x - cx, g_map_magnify));
    *y = (int16_t)(cy + floor_div(*y - cy, g_map_magnify));
}

/* The game redraws only what changed: it shifts the screen pixels when the
   view scrolls and invalidates the rectangles of moving sprites, both in
   unmagnified coordinates. So while magnified, the whole view is
   invalidated every frame (only 1/m^2 of it is actually rendered). Also
   drops the magnification if the main window no longer owns that viewport. */
void map_magnify_tick(void)
{
    Viewport *vp = magnified;
    if (g_map_magnify <= 1 || !vp)
        return;
    Regs r = {0};
    rct_call(ORIG_MAIN_WINDOW, &r);
    if ((r.eflags & 1) || *(Viewport **)(uintptr_t)(r.esi + WINDOW_VIEWPORT) != vp) {
        g_map_magnify = 1, magnified = 0;
        return;
    }
    if (vp->zoom != 0)
        return;
    Regs q = {0};
    q.eax = (uint16_t)vp->x;
    q.ebx = (uint16_t)vp->y;
    q.edx = (uint16_t)(vp->x + vp->width);
    q.ebp = (uint16_t)(vp->y + vp->height);
    rct_call(ORIG_INVALIDATE_SCREEN, &q);
}

void map_zoom_step(int dir)
{
    Regs r = {0};
    rct_call(ORIG_MAIN_WINDOW, &r);
    if (r.eflags & 1) return;
    if (dir > 0) window_zoom_in((uint8_t *)(uintptr_t)r.esi);
    else window_zoom_out((uint8_t *)(uintptr_t)r.esi);
}
