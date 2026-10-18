/* Launcher skin: RCT-style bevels, sprites and text drawn into 32-bit canvases.
   Game art is read at run time from the user's folder (docs/launcher.md); without it
   everything falls back to original GDI drawing. */
#ifndef SKIN_H
#define SKIN_H
#include <windows.h>
#include <stdint.h>

/* Palette ramps, named by their first index (12 shades, dark to light). The theme is RAMP_MAIN. */
#define RAMP_MAIN 22     /* olive window body */
#define RAMP_ACCENT 58   /* maroon title bar and "Jogar" */
#define RAMP_OK 94       /* green status */
#define RAMP_ERROR 166   /* red status */
#define RAMP_GRASS 70    /* fallback ground */

enum { SK_HOVER = 1, SK_PRESSED = 2, SK_FOCUS = 4, SK_DISABLED = 8, SK_SHADOW = 16, SK_CENTER = 32 };
enum { ICON_NONE, ICON_PLAY, ICON_QUIT, ICON_LEFT, ICON_RIGHT, ICON_CLOSE };

typedef struct {
    HDC dc;
    HBITMAP bmp, old;
    uint32_t *px;   /* 0xRRGGBB, top-down */
    int w, h;
} Canvas;

void skin_init(int k);                   /* integer pixel-art scale; creates the fallback fonts */
int skin_load(const char *game_dir);     /* NULL = fallbacks; returns 1 when the game art loaded */
void skin_free(void);
uint32_t skin_color(int ramp, int shade);
COLORREF skin_colorref(int ramp, int shade);

int skin_canvas(Canvas *c, int w, int h);
void skin_canvas_free(Canvas *c);
void skin_fill(Canvas *c, int x, int y, int w, int h, uint32_t rgb);
void skin_bevel(Canvas *c, RECT r, int ramp, int inset);
void skin_grass(Canvas *c, RECT r);
void skin_logo(Canvas *c, RECT area);
int skin_text(Canvas *c, RECT box, const char *s, uint32_t rgb, int flags);   /* SK_SHADOW, SK_CENTER */
void skin_button(Canvas *c, RECT r, int ramp, int state, const char *label, int icon);
void skin_check(Canvas *c, RECT r, int state, int checked, const char *label);
void skin_badge(Canvas *c, RECT r, int ok, const char *text);
#endif
