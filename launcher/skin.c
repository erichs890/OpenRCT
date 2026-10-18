/* Launcher skin (see skin.h and docs/launcher.md). Everything is drawn straight into
   32-bit DIB sections at an integer scale k, so pixel art never gets resampled. */
#include "skin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "paint.h"

/* Sprite ids in the user's CSG1.DAT (docs/launcher.md). */
enum {
    SPR_GRASS = 1012,         /* flat grass tile, 32x15 */
    SPR_PALETTE = 2024,       /* 236 colours from index 10 */
    SPR_FONT = 45845,         /* + Latin-1 code; the medium UI font (46069: tiny, 46293: big) */
    SPR_ICON_NEW = 64507,     /* title menu buttons */
    SPR_ICON_EXIT = 64510,
    SPR_LOGO = 64511,
};
enum { LOGO, ICON_NEW, ICON_EXIT, GRASS, N_SINGLE };
static const int single_id[N_SINGLE] = {SPR_LOGO, SPR_ICON_NEW, SPR_ICON_EXIT, SPR_GRASS};

typedef struct {
    int16_t w, h, x, y;
    uint8_t *px;   /* palette indices, 0 = transparent */
} Sprite;

static Sprite single[N_SINGLE], glyph[224];
static uint32_t pal[256];
static int loaded, k = 1;
static int ts = 1;   /* scale of the text being drawn: k, or less when the text would not fit its box */
static HFONT sys_font[4];   /* fallback font for text scales 1..4 */
static int sys_line[4];
static HDC measure_dc;

/* Own fallback ramps (picked by eye, not taken from the game): darkest and lightest shade. */
static const struct { int ramp; uint32_t dark, light; } own_ramps[] = {
    {22, 0x262606, 0xd4d49a}, {58, 0x2c0404, 0xf4b4b4}, {94, 0x0c3404, 0xc4f0a4},
    {166, 0x3c0400, 0xffb4a0}, {70, 0x0e260c, 0x9cc864}, {46, 0x402000, 0xfff4c0},
};

uint32_t skin_color(int ramp, int shade)
{
    if (loaded) return pal[ramp + shade];
    uint32_t d = 0x0c1414, l = 0xf0f4f4;   /* grey for any other ramp */
    for (size_t i = 0; i < sizeof own_ramps / sizeof *own_ramps; i++)
        if (own_ramps[i].ramp == ramp) d = own_ramps[i].dark, l = own_ramps[i].light;
    uint32_t c = 0;
    for (int s = 0; s < 24; s += 8)
        c |= (((d >> s & 255) * (11 - shade) + (l >> s & 255) * shade) / 11) << s;
    return c;
}

static COLORREF ref(uint32_t rgb) { return RGB(rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255); }
COLORREF skin_colorref(int ramp, int shade) { return ref(skin_color(ramp, shade)); }

/* ---- loading ---- */

static uint8_t *read_at(HANDLE f, DWORD off, DWORD n)
{
    uint8_t *b = malloc(n ? n : 1);
    DWORD got = 0;
    if (b && SetFilePointer(f, off, NULL, FILE_BEGIN) == off && ReadFile(f, b, n, &got, NULL) && got == n) return b;
    free(b);
    return NULL;
}

/* Decodes one sprite (flags 1: plain, 4: RLE rows). The files are the user's, so every offset is checked. */
static int load_sprite(HANDLE f, DWORD fsize, const G1Element *idx, int n, int id, Sprite *s)
{
    const G1Element *e = &idx[id];
    int w = e->width, h = e->height;
    if (id + 1 >= n || w <= 0 || h <= 0 || w > 640 || h > 480 || (e->flags & 8) || e->offset >= fsize) return 0;
    DWORD len = idx[id + 1].offset > e->offset ? idx[id + 1].offset - e->offset : 0x10000;
    if (len > fsize - e->offset) len = fsize - e->offset;
    if (len > 0x100000) len = 0x100000;
    uint8_t *b = read_at(f, e->offset, len), *px = calloc(w * h, 1);
    int ok = b && px;
    if (ok && !(e->flags & 4)) {
        ok = len >= (DWORD)(w * h);
        if (ok) memcpy(px, b, w * h);
    }
    for (int y = 0; ok && (e->flags & 4) && y < h; y++) {
        ok = (DWORD)(2 * y + 2) <= len;
        for (DWORD p = ok ? b[2 * y] | b[2 * y + 1] << 8 : 0, last = 0; ok && !last;) {
            ok = p + 2 <= len;
            if (!ok) break;
            int run = b[p] & 0x7F, x = b[p + 1];
            last = b[p] & 0x80;
            ok = p + 2 + run <= len && x + run <= w;
            if (ok) memcpy(px + y * w + x, b + p + 2, run);
            p += 2 + run;
        }
    }
    free(b);
    if (!ok) {
        free(px);
        return 0;
    }
    *s = (Sprite){w, h, e->x_offset, e->y_offset, px};
    return 1;
}

static void free_sprites(void)
{
    for (int i = 0; i < N_SINGLE; i++) free(single[i].px);
    for (int i = 0; i < 224; i++) free(glyph[i].px);
    memset(single, 0, sizeof single);
    memset(glyph, 0, sizeof glyph);
    loaded = 0;
}

int skin_load(const char *dir)
{
    free_sprites();
    if (!dir) return 0;
    /* CD/GOG layouts keep the index in Data\, Steam in the "CD" folder RCTdeluxe_install (docs/launcher.md). */
    static const char *const index_paths[] = {"%s\\Data\\CSG1I.DAT", "%s\\RCTdeluxe_install\\Data\\csg1i.dat"};
    char path[MAX_PATH + 64];
    HANDLE f = INVALID_HANDLE_VALUE;
    for (size_t i = 0; f == INVALID_HANDLE_VALUE && i < 2; i++) {
        snprintf(path, sizeof path, index_paths[i], dir);
        f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    }
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD isize = GetFileSize(f, NULL);
    G1Element *idx = isize >= (SPR_LOGO + 2) * 16 && isize < 0x400000 ? (G1Element *)read_at(f, 0, isize) : NULL;
    CloseHandle(f);
    snprintf(path, sizeof path, "%s\\Data\\CSG1.DAT", dir);
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    int n = isize / 16, ok = idx && f != INVALID_HANDLE_VALUE;
    DWORD fsize = ok ? GetFileSize(f, NULL) : 0;
    const G1Element *p = idx ? &idx[SPR_PALETTE] : NULL;
    ok = ok && (p->flags & 8) && p->x_offset >= 0 && p->width > 0 && p->x_offset + p->width <= 256;
    uint8_t *bgr = ok ? read_at(f, p->offset, p->width * 3) : NULL;
    if (bgr)
        for (int i = 0; i < p->width; i++)
            pal[p->x_offset + i] = bgr[3 * i + 2] << 16 | bgr[3 * i + 1] << 8 | bgr[3 * i];
    ok = bgr != NULL;
    for (int i = 0; ok && i < N_SINGLE; i++) ok = load_sprite(f, fsize, idx, n, single_id[i], &single[i]);
    for (int i = 0; ok && i < 224; i++)   /* a missing glyph only sends that text to the system font */
        load_sprite(f, fsize, idx, n, SPR_FONT + 32 + i, &glyph[i]);
    free(bgr);
    free(idx);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (!ok) free_sprites();
    return loaded = ok;
}

void skin_init(int scale)
{
    k = scale;
    measure_dc = CreateCompatibleDC(NULL);
    for (int s = 0; s < 4; s++) {
        sys_font[s] = CreateFontA(-11 * (s + 1), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                  NONANTIALIASED_QUALITY, 0, "Tahoma");
        TEXTMETRICA tm;
        SelectObject(measure_dc, sys_font[s]);
        GetTextMetricsA(measure_dc, &tm);
        sys_line[s] = tm.tmAscent - tm.tmInternalLeading;
    }
}

void skin_free(void)
{
    free_sprites();
    DeleteDC(measure_dc);
    for (int s = 0; s < 4; s++) DeleteObject(sys_font[s]);
}

/* ---- canvas and pixels ---- */

int skin_canvas(Canvas *c, int w, int h)
{
    BITMAPINFO bi = {{sizeof bi.bmiHeader, w, -h, 1, 32, BI_RGB}};
    c->w = w, c->h = h;
    c->dc = CreateCompatibleDC(NULL);
    c->bmp = CreateDIBSection(c->dc, &bi, DIB_RGB_COLORS, (void **)&c->px, NULL, 0);
    if (!c->bmp) {
        DeleteDC(c->dc);
        return 0;
    }
    c->old = SelectObject(c->dc, c->bmp);
    SetBkMode(c->dc, TRANSPARENT);
    return 1;
}

void skin_canvas_free(Canvas *c)
{
    SelectObject(c->dc, c->old);
    DeleteObject(c->bmp);
    DeleteDC(c->dc);
}

void skin_fill(Canvas *c, int x, int y, int w, int h, uint32_t rgb)
{
    GdiFlush();   /* GDI text may still be queued for these pixels */
    int x1 = min(x + w, c->w), y1 = min(y + h, c->h);
    for (int yy = max(y, 0); yy < y1; yy++)
        for (int xx = max(x, 0); xx < x1; xx++) c->px[yy * c->w + xx] = rgb;
}

/* tint < 0: palette colours; otherwise every opaque pixel gets tint (glyphs, disabled icons). */
static void blit(Canvas *c, const Sprite *s, int x, int y, int scale, int64_t tint, const RECT *clip)
{
    RECT r = clip ? *clip : (RECT){0, 0, c->w, c->h};
    r.left = max(r.left, 0), r.top = max(r.top, 0), r.right = min(r.right, c->w), r.bottom = min(r.bottom, c->h);
    GdiFlush();
    for (int sy = 0; sy < s->h; sy++)
        for (int sx = 0; sx < s->w; sx++) {
            uint8_t i = s->px[sy * s->w + sx];
            if (!i) continue;
            uint32_t rgb = tint < 0 ? pal[i] : (uint32_t)tint;
            for (int py = y + sy * scale; py < y + (sy + 1) * scale; py++)
                for (int px = x + sx * scale; px < x + (sx + 1) * scale; px++)
                    if (px >= r.left && px < r.right && py >= r.top && py < r.bottom) c->px[py * c->w + px] = rgb;
        }
}

/* Light from the top left, 1 step of k pixels; outset adds the dark outline the game's buttons have. */
static void bevel(Canvas *c, RECT r, int ramp, int inset, int fill)
{
    int w = r.right - r.left, h = r.bottom - r.top, o = 0;
    if (!inset) {
        skin_fill(c, r.left, r.top, w, h, skin_color(ramp, 1));
        o = k;
    }
    uint32_t tl = skin_color(ramp, inset ? 2 : 10), br = skin_color(ramp, inset ? 10 : 4);
    skin_fill(c, r.left + o, r.top + o, w - 2 * o, h - 2 * o, br);
    skin_fill(c, r.left + o, r.top + o, w - 2 * o - k, h - 2 * o - k, tl);
    skin_fill(c, r.left + o + k, r.top + o + k, w - 2 * o - 2 * k, h - 2 * o - 2 * k, skin_color(ramp, fill));
}

void skin_bevel(Canvas *c, RECT r, int ramp, int inset) { bevel(c, r, ramp, inset, inset ? 5 : 7); }

/* Dotted keyboard-focus frame, k-pixel dots. */
static void focus_frame(Canvas *c, RECT r, uint32_t rgb)
{
    for (int x = r.left; x < r.right; x += 2 * k) {
        skin_fill(c, x, r.top, k, k, rgb);
        skin_fill(c, x, r.bottom - k, k, k, rgb);
    }
    for (int y = r.top; y < r.bottom; y += 2 * k) {
        skin_fill(c, r.left, y, k, k, rgb);
        skin_fill(c, r.right - k, y, k, k, rgb);
    }
}

void skin_grass(Canvas *c, RECT r)
{
    int w = r.right - r.left, h = r.bottom - r.top;
    if (loaded) {   /* the game's tile, staggered like the map */
        skin_fill(c, r.left, r.top, w, h, pal[73]);
        for (int row = -2; row * 8 * k < h + 16 * k; row++)
            for (int col = -1; col * 32 * k < w + 32 * k; col++)
                blit(c, &single[GRASS], r.left + (col * 32 + (row & 1) * 16) * k, r.top + row * 8 * k, k, -1, &r);
        return;
    }
    /* Own pattern: mid green with a fixed scatter of darker and lighter k-pixel tufts. */
    skin_fill(c, r.left, r.top, w, h, skin_color(RAMP_GRASS, 4));
    for (int y = 0; y < h; y += k)
        for (int x = 0; x < w; x += k) {
            unsigned v = (unsigned)(x / k * 7919 + y / k * 104729) * 2654435761u >> 24;
            if (v < 40) skin_fill(c, r.left + x, r.top + y, k, k, skin_color(RAMP_GRASS, 2));
            else if (v > 236) skin_fill(c, r.left + x, r.top + y, k, k, skin_color(RAMP_GRASS, 7));
        }
}

/* Largest integer scale <= k at which w x h fits in the box (never resample). */
static int fit(int w, int h, int bw, int bh)
{
    int s = k;
    while (s > 1 && (w * s > bw || h * s > bh)) s--;
    return s;
}

/* ---- text ---- */

/* Next code point of a UTF-8 string (the sources and the manifest code page are UTF-8). */
static unsigned next_cp(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    unsigned c = *p++;
    if (c >= 0xC0) {
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
        c &= 0x3F >> extra;
        while (extra-- && (*p & 0xC0) == 0x80) c = c << 6 | (*p++ & 0x3F);
    }
    *s = (const char *)p;
    return c;
}

/* The game's font has Latin-1 only; anything else sends the whole string to the system font. */
static int game_font_ok(const char *s)
{
    if (!loaded) return 0;
    while (*s) {
        unsigned c = next_cp(&s);
        if (c < 32 || c > 255 || !glyph[c - 32].px) return 0;
    }
    return 1;
}

static int sys_text(Canvas *c, int x, int y, const char *s, uint32_t rgb)
{
    WCHAR w[512];
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, w, 512) - 1;
    SIZE sz = {0};
    HDC dc = c ? c->dc : measure_dc;
    int fi = min(ts, 4) - 1;
    HFONT old = SelectObject(dc, sys_font[fi]);
    if (c) {
        SetTextColor(dc, ref(rgb));
        SetTextAlign(dc, TA_BASELINE);
        TextOutW(dc, x, y + sys_line[fi], w, max(n, 0));
    }
    GetTextExtentPoint32W(dc, w, max(n, 0), &sz);
    SelectObject(dc, old);
    return sz.cx;
}

/* Glyph pixels: 1 = face, 2 = the drop shadow the game bakes in (drawn only when asked).
   The width includes the shadow column, which doubles as letter spacing. */
static int game_text(Canvas *c, int x, int y, const char *s, uint32_t rgb, int shadow)
{
    int x0 = x;
    while (*s) {
        const Sprite *g = &glyph[next_cp(&s) - 32];
        for (int i = 0; c && i < g->w * g->h; i++)
            if (g->px[i] == 1 || (g->px[i] == 2 && shadow))
                skin_fill(c, x + (g->x + i % g->w) * ts, y + (g->y + i / g->w) * ts, ts, ts,
                          g->px[i] == 1 ? rgb : skin_color(10, 0));
        x += max(g->w, 3) * ts;
    }
    return x - x0;
}

static int line(void) { return loaded ? 9 * ts : sys_line[min(ts, 4) - 1]; }

static int width(const char *s)
{
    return game_font_ok(s) ? game_text(NULL, 0, 0, s, 0, 0) : sys_text(NULL, 0, 0, s, 0);
}

/* Draws s vertically centred in box, at the largest whole scale <= k at which it fits; returns its width.
   c = NULL only measures. */
int skin_text(Canvas *c, RECT box, const char *s, uint32_t rgb, int flags)
{
    for (ts = k; ts > 1 && width(s) > box.right - box.left; ts--) {}
    int w = width(s), x = flags & SK_CENTER ? (box.left + box.right - w) / 2 : box.left;
    int y = (box.top + box.bottom - line()) / 2;
    if (!c) return w;
    if (game_font_ok(s)) return game_text(c, x, y, s, rgb, flags & SK_SHADOW);
    if (flags & SK_SHADOW) sys_text(c, x + ts, y + ts, s, skin_color(10, 0));
    return sys_text(c, x, y, s, rgb);
}

/* ---- logo and widgets ---- */

void skin_logo(Canvas *c, RECT a)
{
    int w = a.right - a.left, h = a.bottom - a.top;
    if (loaded) {
        const Sprite *s = &single[LOGO];
        int f = fit(s->w, s->h, w, h);
        blit(c, s, a.left + (w - s->w * f) / 2, a.top + (h - s->h * f) / 2, f, -1, NULL);
        return;
    }
    /* No game folder yet: the name in a bold system font with a dark outline, not an imitation of the logo. */
    HFONT font = CreateFontA(-30 * k, 0, 0, 0, FW_HEAVY, 0, 0, 0, DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0,
                             "Tahoma");
    HFONT old = SelectObject(c->dc, font);
    static const char *const lines[] = {"RollerCoaster", "Tycoon"};
    SIZE sz;
    GetTextExtentPoint32A(c->dc, "Tycoon", 6, &sz);
    int y = a.top + (h - 2 * sz.cy) / 2;
    SetTextAlign(c->dc, TA_TOP | TA_CENTER);
    for (int i = 0; i < 2; i++, y += sz.cy)
        for (int d = 0; d < 10; d++) {   /* 8 outline offsets, then a drop shadow and the face */
            static const signed char ox[] = {-2, 0, 2, -2, 2, -2, 0, 2, 3, 0}, oy[] = {-2, -2, -2, 0, 0, 2, 2, 2, 3, 0};
            SetTextColor(c->dc, d == 9 ? skin_colorref(46, 9) : skin_colorref(RAMP_ACCENT, d == 8 ? 0 : 2));
            TextOutA(c->dc, a.left + w / 2 + ox[d] * k, y + oy[d] * k, lines[i], (int)strlen(lines[i]));
        }
    SelectObject(c->dc, old);
    DeleteObject(font);
}

static void triangle(Canvas *c, int cx, int cy, int n, int dir, int unit, uint32_t rgb)
{
    /* n rows tall, pointing left (dir < 0) or right */
    for (int r = 0; r < n; r++) {
        int len = min(r, n - 1 - r) + 1;
        int x = dir > 0 ? cx - n / 4 * unit : cx + n / 4 * unit - len * unit;
        skin_fill(c, x, cy - n / 2 * unit + r * unit, len * unit, unit, rgb);
    }
}

static void cross(Canvas *c, int cx, int cy, int n, int unit, uint32_t rgb)
{
    for (int i = 0; i < n; i++) {
        int y = cy - n / 2 * unit + i * unit;
        skin_fill(c, cx - n / 2 * unit + i * unit, y, 2 * unit, unit, rgb);
        skin_fill(c, cx + n / 2 * unit - i * unit - unit, y, 2 * unit, unit, rgb);
    }
}

/* Draws icon centred on (cx, cy) inside a box of bw x bh; returns its height. */
static int icon(Canvas *c, int which, int cx, int cy, int bw, int bh, int64_t tint, int measure)
{
    if (loaded && (which == ICON_PLAY || which == ICON_QUIT)) {
        const Sprite *s = &single[which == ICON_PLAY ? ICON_NEW : ICON_EXIT];
        int f = fit(s->w, s->h, bw, bh);
        if (!measure) blit(c, s, cx - s->w * f / 2, cy - s->h * f / 2, f, tint, NULL);
        return s->h * f;
    }
    uint32_t rgb = tint < 0 ? skin_color(10, 11) : (uint32_t)tint;
    int big = which == ICON_PLAY || which == ICON_QUIT, unit = big ? 2 * k : k, n = big ? 15 : 7;
    if (!measure) {
        if (which == ICON_PLAY || which == ICON_LEFT || which == ICON_RIGHT)
            triangle(c, cx, cy, n, which == ICON_LEFT ? -1 : 1, unit, rgb);
        else cross(c, cx, cy, n, unit, rgb);
    }
    return n * unit;
}

void skin_button(Canvas *c, RECT r, int ramp, int state, const char *label, int ic)
{
    int w = r.right - r.left, h = r.bottom - r.top, off = state & SK_PRESSED ? k : 0;
    if (state & SK_DISABLED) {   /* flat: no relief, faded content */
        skin_fill(c, r.left, r.top, w, h, skin_color(ramp, 3));
        skin_fill(c, r.left + k, r.top + k, w - 2 * k, h - 2 * k, skin_color(ramp, 6));
    } else bevel(c, r, ramp, state & SK_PRESSED, state & SK_PRESSED ? 5 : state & SK_HOVER ? 9 : 7);
    int64_t tint = state & SK_DISABLED ? (int64_t)skin_color(ramp, 4) : -1;
    uint32_t text = state & SK_DISABLED ? skin_color(ramp, 4) : skin_color(10, 11);
    ts = k;
    int lh = label ? line() : 0, gap = label && ic ? 4 * k : 0;
    int ih = ic ? icon(c, ic, 0, 0, w - 8 * k, h - 8 * k - lh - gap, tint, 1) : 0;
    int top = r.top + (h - ih - gap - lh) / 2 + off, cx = r.left + w / 2 + off;
    if (ic) icon(c, ic, cx, top + ih / 2, w - 8 * k, h - 8 * k - lh - gap, tint, 0);
    if (label)
        skin_text(c, (RECT){r.left + 4 * k + off, top + ih + gap, r.right - 4 * k + off, top + ih + gap + lh}, label,
                  text, SK_CENTER | (state & SK_DISABLED ? 0 : SK_SHADOW));
    if (state & SK_FOCUS) focus_frame(c, (RECT){r.left + 3 * k, r.top + 3 * k, r.right - 3 * k, r.bottom - 3 * k},
                                      skin_color(10, 11));
}

void skin_check(Canvas *c, RECT r, int state, int checked, const char *label)
{
    static const char *const tick[] = {"......#", ".....##", "#...##.", "##.##..", ".###...", "..#...."};
    int box = 11 * k, h = r.bottom - r.top, y = r.top + (h - box) / 2, dis = state & SK_DISABLED;
    if (dis) skin_fill(c, r.left, y, box, box, skin_color(RAMP_MAIN, 5));
    else bevel(c, (RECT){r.left, y, r.left + box, y + box}, RAMP_MAIN, 1,
               state & SK_PRESSED ? 3 : state & SK_HOVER ? 7 : 5);
    for (int ty = 0; checked && ty < 6; ty++)
        for (int tx = 0; tx < 7; tx++)
            if (tick[ty][tx] == '#')
                skin_fill(c, r.left + 2 * k + tx * k, y + 3 * k + ty * k, k, k,
                          dis ? skin_color(RAMP_MAIN, 3) : skin_color(10, 11));
    int tx = r.left + box + 6 * k;
    int tw = skin_text(c, (RECT){tx, r.top, r.right - 2 * k, r.bottom}, label,
                       dis ? skin_color(RAMP_MAIN, 4) : skin_color(RAMP_MAIN, state & SK_HOVER ? 11 : 0), 0);
    int ty = (r.top + r.bottom - line()) / 2;   /* ts is still the scale the label used */
    if (state & SK_FOCUS)
        focus_frame(c, (RECT){tx - 2 * k, ty - 2 * k, tx + tw + 2 * k, ty + line() + 3 * k},
                    skin_color(RAMP_MAIN, 0));
}

void skin_badge(Canvas *c, RECT r, int ok, const char *text)
{
    int ramp = ok ? RAMP_OK : RAMP_ERROR;
    bevel(c, r, ramp, 0, 5);
    skin_text(c, (RECT){r.left + 8 * k, r.top, r.right - 4 * k, r.bottom}, text, skin_color(10, 11),
              SK_SHADOW);
}
