/* See display.h. How it plugs into the game (docs/display.md):
   - The game draws into an 8-bit surface of the desktop size (we lift its
     1280x1024 cap) and uses the area
     its WndProc last got in WM_SIZE (0xC3B004/0xC3B574); it re-lays out its
     windows when that changes. We give it a "game size" = client / zoom.
   - Window: CreateWindowExA (IAT) gets our style and geometry; the window
     class gets our WndProc in front of the game's (RegisterClassA, IAT).
   - Present: the GDI flush 0x57FD3A is replaced by a full-frame blit of the
     game area, scaled to the window (nearest neighbour, letterboxed).
   - Input: mouse messages, GetCursorPos/SetCursorPos/GetWindowRect (IAT) are
     mapped between window pixels and game pixels. */
#include <windows.h>
#include <windowsx.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "display.h"
#include "image.h"
#include "hooks.h"
#include "viewport.h"

#define G(type, addr) (*(type *)(addr))
#define GAME_WINDOW_W G(int32_t, 0x8B9E18)        /* windowed-mode size used to create the window */
#define GAME_WINDOW_H G(int32_t, 0x8B9E1C)
#define GAME_SCREEN_MODE G(uint8_t, 0x88B2DB)     /* 0 = windowed (GDI) */
#define GAME_W G(int32_t, 0xC3B004)               /* game area, from WM_SIZE */
#define GAME_H G(int32_t, 0xC3B574)
#define GAME_FULL_REDRAW G(int32_t, 0xC38798)
#define GAME_DIRTY_COUNT G(int32_t, 0xC3879C)
#define GAME_SURFACE G(uint8_t *, 0xC38760)       /* screen surface: +0x84 bits, +0x88 BITMAPINFO */
#define GAME_PALETTE G(HPALETTE, 0xC39314)
#define GAME_RGB ((void *)0xC38B00)               /* current palette, 256 RGBQUADs */
#define ORIG_SET_SURFACE_COLORS 0x58A349          /* (surface, first, count, rgbquads) */
#define ORIG_REPAINT 0x401120                     /* (RECT *); NULL = full redraw + present */
#define ORIG_GDI_FLUSH 0x57FD3A
#define ORIG_GDI_FLUSH_BLOCKS 0x5800E5            /* the original per-block flush */
#define GAME_CLASS "RollerCoaster Tycoon"
#define MIN_W 640
#define MIN_H 480
#define SURFACE_CLAMPS {0x57F96A, 0x57F973, 0x57F97A, 0x57F983}   /* imm32: 1280, 1280, 1024, 1024 */
#define SURFACE_JLE_W 0x57F96E                    /* the `jle` that keep a smaller desktop size */
#define SURFACE_JLE_H 0x57F97E
#define MAX_SURFACE_W 3840                        /* our cap; the surface is the largest monitor */
#define MAX_SURFACE_H 2160
#define SURFACE_MIN_W 1280                        /* never smaller than the original's surface */
#define SURFACE_MIN_H 1024

static const double zoom_steps[] = {1, 1.25, 1.5, 1.75, 2, 2.5, 3, 3.5, 4, 5, 6};

static char ini[MAX_PATH];
static double zoom;          /* 0 = auto */
static int fullscreen, maximized, creating, restyling;
static RECT window_rect;     /* windowed outer rect */
static HWND hwnd;
static WNDPROC game_proc;
static int borders_dirty = 1;

static HWND(WINAPI *real_CreateWindowExA)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                                          LPVOID);
static ATOM(WINAPI *real_RegisterClassA)(const WNDCLASSA *);
static BOOL(WINAPI *real_GetCursorPos)(POINT *);
static BOOL(WINAPI *real_SetCursorPos)(int, int);
static BOOL(WINAPI *real_GetWindowRect)(HWND, RECT *);

/* ---- settings ---- */

static void save_int(const char *key, int v)
{
    char s[16];
    snprintf(s, sizeof s, "%d", v);
    WritePrivateProfileStringA("display", key, s, ini);
}

static void save_settings(void)
{
    char s[16];
    snprintf(s, sizeof s, "%g", zoom);
    WritePrivateProfileStringA("display", "zoom", s, ini);
    save_int("fullscreen", fullscreen);
    save_int("maximized", maximized);
    save_int("x", window_rect.left);
    save_int("y", window_rect.top);
    save_int("w", window_rect.right - window_rect.left);
    save_int("h", window_rect.bottom - window_rect.top);
}

/* ---- geometry ---- */

static RECT monitor_rect(void)
{
    MONITORINFO mi = {.cbSize = sizeof mi};
    HMONITOR m = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
                      : MonitorFromRect(&window_rect, MONITOR_DEFAULTTOPRIMARY);
    GetMonitorInfoA(m, &mi);
    return mi.rcMonitor;
}

static BOOL CALLBACK largest_monitor(HMONITOR m, HDC dc, LPRECT r, LPARAM out)
{
    (void)m, (void)dc;
    LONG *wh = (LONG *)out;
    if (r->right - r->left > wh[0]) wh[0] = r->right - r->left;
    if (r->bottom - r->top > wh[1]) wh[1] = r->bottom - r->top;
    return TRUE;
}

/* Size the game will give its surface the next time it builds the display:
   the largest monitor, capped. */
static void set_surface_size(void)
{
    LONG wh[2] = {SURFACE_MIN_W, SURFACE_MIN_H};
    EnumDisplayMonitors(NULL, NULL, largest_monitor, (LPARAM)wh);
    static const uint32_t clamps[] = SURFACE_CLAMPS;
    for (int i = 0; i < 4; i++)
        G(uint32_t, clamps[i]) = i < 2 ? (uint32_t)min(wh[0], MAX_SURFACE_W) : (uint32_t)min(wh[1], MAX_SURFACE_H);
}

/* The game's screen surface: the largest game area possible. */
static void surface_size(int *w, int *h)
{
    uint8_t *surface = GAME_SURFACE;
    BITMAPINFO *bmi = surface ? *(BITMAPINFO **)(surface + 0x88) : NULL;
    if (bmi) {
        *w = bmi->bmiHeader.biWidth;
        *h = abs(bmi->bmiHeader.biHeight);
    } else {
        static const uint32_t clamps[] = SURFACE_CLAMPS;
        *w = (int)G(uint32_t, clamps[0]), *h = (int)G(uint32_t, clamps[2]);
    }
}

/* Zoom for a client area: the chosen one (or auto: about 600 game pixels of
   height, in quarter steps), kept within what the game can show: at least
   640x480 game pixels and at most its surface. */
static double effective_zoom(int cw, int ch)
{
    int sw, sh;
    surface_size(&sw, &sh);
    double z = zoom > 0 ? zoom : floor(ch / 600.0 * 4) / 4;
    double lo = fmax(cw / (double)sw, ch / (double)sh);
    double hi = fmin(cw / (double)MIN_W, ch / (double)MIN_H);
    if (z > hi) z = hi;
    if (z < lo) z = lo;
    return z < 1 ? 1 : z;
}

static void game_size_for(int cw, int ch, int *w, int *h)
{
    double z = effective_zoom(cw, ch);
    int sw, sh;
    surface_size(&sw, &sh);
    *w = (int)ceil(cw / z - 0.001);
    *h = (int)ceil(ch / z - 0.001);
    if (*w > sw) *w = sw;
    if (*h > sh) *h = sh;
    if (*w < MIN_W) *w = MIN_W;
    if (*h < MIN_H) *h = MIN_H;
}

typedef struct { double s; int ox, oy, w, h; } Mapping;

/* Game pixels -> client pixels: uniform scale, centred. */
static Mapping mapping(void)
{
    Mapping m = {1, 0, 0, GAME_W, GAME_H};
    RECT r;
    if (!hwnd || !GetClientRect(hwnd, &r) || m.w <= 0 || m.h <= 0) return m;
    m.s = fmin(r.right / (double)m.w, r.bottom / (double)m.h);
    m.ox = (int)((r.right - m.w * m.s) / 2);
    m.oy = (int)((r.bottom - m.h * m.s) / 2);
    return m;
}

static void update_title(void)
{
    char t[160];
    Mapping m = mapping();
    snprintf(t, sizeof t, "RollerCoaster Tycoon  -  %dx%d  -  zoom %.3gx%s  (roda: zoom do mapa, Ctrl+roda: zoom da tela, F11: tela cheia)",
             GAME_W, GAME_H, m.s, zoom > 0 ? "" : " auto");
    SetWindowTextA(hwnd, t);
}

/* Tell the game its area changed (it re-lays out its windows). */
static void resize_game(void)
{
    RECT r;
    if (!hwnd || IsIconic(hwnd) || !GetClientRect(hwnd, &r)) return;
    SendMessageA(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r.right, r.bottom));
    InvalidateRect(hwnd, NULL, FALSE);
}

static void set_zoom(double z)
{
    zoom = z;
    save_settings();
    resize_game();
}

static void zoom_step(int dir)
{
    RECT r;
    GetClientRect(hwnd, &r);
    double cur = effective_zoom(r.right, r.bottom);
    int n = sizeof zoom_steps / sizeof *zoom_steps;
    if (dir > 0) {
        for (int i = 0; i < n; i++)
            if (zoom_steps[i] > cur + 0.01) { set_zoom(zoom_steps[i]); return; }
    } else {
        for (int i = n - 1; i >= 0; i--)
            if (zoom_steps[i] < cur - 0.01) { set_zoom(zoom_steps[i]); return; }
    }
}

static void set_fullscreen(int on)
{
    if (on == fullscreen || !hwnd) return;
    if (on && !maximized) real_GetWindowRect(hwnd, &window_rect);
    fullscreen = on;
    save_settings();
    RECT r = on ? monitor_rect() : window_rect;
    restyling = 1;   /* the intermediate WM_SIZEs must not overwrite window_rect */
    SetWindowLongA(hwnd, GWL_STYLE, (on ? WS_POPUP : WS_OVERLAPPEDWINDOW) | WS_VISIBLE);
    SetWindowPos(hwnd, on ? HWND_TOP : NULL, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_FRAMECHANGED | (on ? 0 : SWP_NOZORDER));
    if (!on && maximized) ShowWindow(hwnd, SW_MAXIMIZE);
    restyling = 0;
}

/* ---- present (replaces 0x57FD3A in the windowed GDI mode) ---- */

static void fill_borders(HDC dc, int x0, int y0, int x1, int y1)
{
    RECT r, c;
    GetClientRect(hwnd, &c);
    HBRUSH b = GetStockObject(BLACK_BRUSH);
    SetRect(&r, 0, 0, c.right, y0), FillRect(dc, &r, b);
    SetRect(&r, 0, y1, c.right, c.bottom), FillRect(dc, &r, b);
    SetRect(&r, 0, y0, x0, y1), FillRect(dc, &r, b);
    SetRect(&r, x1, y0, c.right, y1), FillRect(dc, &r, b);
}

static void __cdecl present(void)
{
    map_magnify_tick();   /* before the dirty check: it marks the view for the next frame */
    if (!GAME_FULL_REDRAW && GAME_DIRTY_COUNT <= 0 && !borders_dirty) return;
    uint8_t *surface = GAME_SURFACE;
    BITMAPINFO *bmi = surface ? *(BITMAPINFO **)(surface + 0x88) : NULL;
    void *bits = surface ? *(void **)(surface + 0x84) : NULL;
    Mapping m = mapping();
    if (!hwnd || !bmi || !bits || bmi->bmiHeader.biBitCount != 8 || bmi->bmiHeader.biWidth < m.w ||
        abs(bmi->bmiHeader.biHeight) < m.h) {
        ((void(__cdecl *)(void))ORIG_GDI_FLUSH_BLOCKS)();   /* not the surface we expect: original path */
        return;
    }
    HDC dc = GetDC(hwnd);
    if (!dc) return;
    HPALETTE old = SelectPalette(dc, GAME_PALETTE, FALSE);
    ((void(__cdecl *)(void *, int, int, void *))ORIG_SET_SURFACE_COLORS)(surface, 0, 256, GAME_RGB);
    RealizePalette(dc);
    SetStretchBltMode(dc, COLORONCOLOR);
    int x1 = m.ox + (int)(m.w * m.s + 0.5), y1 = m.oy + (int)(m.h * m.s + 0.5);
    /* StretchDIBits counts the source y from the bottom even for top-down DIBs (as the game does) */
    StretchDIBits(dc, m.ox, m.oy, x1 - m.ox, y1 - m.oy, 0, abs(bmi->bmiHeader.biHeight) - m.h, m.w, m.h, bits, bmi,
                  DIB_RGB_COLORS, SRCCOPY);
    if (borders_dirty) {
        fill_borders(dc, m.ox, m.oy, x1, y1);
        borders_dirty = 0;
    }
    SelectPalette(dc, old, TRUE);
    ReleaseDC(hwnd, dc);
}

/* ---- input mapping ---- */

static void to_game(int *x, int *y)
{
    Mapping m = mapping();
    int inside_x = *x < m.ox + (int)(m.w * m.s + 0.5), inside_y = *y < m.oy + (int)(m.h * m.s + 0.5);
    *x = (int)floor((*x - m.ox) / m.s);
    *y = (int)floor((*y - m.oy) / m.s);
    if (inside_x && *x >= m.w) *x = m.w - 1;   /* rounding at the far edge */
    if (inside_y && *y >= m.h) *y = m.h - 1;
}

static POINT client_origin(void)
{
    POINT o = {0, 0};
    ClientToScreen(hwnd, &o);
    return o;
}

static BOOL WINAPI my_GetCursorPos(POINT *p)
{
    BOOL ok = real_GetCursorPos(p);
    if (ok && hwnd && IsWindow(hwnd)) {
        POINT o = client_origin();
        int x = p->x - o.x, y = p->y - o.y;
        to_game(&x, &y);
        p->x = o.x + x;
        p->y = o.y + y;
    }
    return ok;
}

static BOOL WINAPI my_SetCursorPos(int x, int y)
{
    if (hwnd && IsWindow(hwnd)) {
        POINT o = client_origin();
        Mapping m = mapping();
        x = o.x + m.ox + (int)((x - o.x) * m.s);
        y = o.y + m.oy + (int)((y - o.y) * m.s);
    }
    return real_SetCursorPos(x, y);
}

/* The game centres the cursor with GetWindowRect; give it the game area. */
static BOOL WINAPI my_GetWindowRect(HWND h, RECT *r)
{
    if (!hwnd || h != hwnd) return real_GetWindowRect(h, r);
    POINT o = client_origin();
    SetRect(r, o.x, o.y, o.x + GAME_W, o.y + GAME_H);
    return TRUE;
}

/* ---- window ---- */

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (creating) hwnd = h;   /* messages sent from inside CreateWindowExA */
    if (h == hwnd) switch (msg) {
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        to_game(&x, &y);
        lp = MAKELPARAM(x, y);
        break;
    }
    case WM_MOUSEWHEEL:   /* Ctrl: pixel zoom; plain: the map's zoom (with magnification past 1x) */
        if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL)
            zoom_step(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1);
        else
            map_zoom_step(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_F11) { set_fullscreen(!fullscreen); return 0; }
        if (GetKeyState(VK_CONTROL) < 0) {
            if (wp == VK_OEM_PLUS || wp == VK_ADD) { zoom_step(1); return 0; }
            if (wp == VK_OEM_MINUS || wp == VK_SUBTRACT) { zoom_step(-1); return 0; }
            if (wp == '0' || wp == VK_NUMPAD0) { set_zoom(0); return 0; }
        }
        break;
    case WM_SYSKEYDOWN:
        if (wp == VK_RETURN) { set_fullscreen(!fullscreen); return 0; }
        break;
    case WM_SYSCHAR:
        if (wp == VK_RETURN) return 0;
        break;
    case WM_EXITSIZEMOVE:
        if (!fullscreen && !maximized) real_GetWindowRect(h, &window_rect);
        save_settings();
        break;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            if (!fullscreen && !restyling) {
                maximized = wp == SIZE_MAXIMIZED;
                if (!maximized && !creating && !restyling) real_GetWindowRect(h, &window_rect);
            }
            int w, gh;
            game_size_for(LOWORD(lp), HIWORD(lp), &w, &gh);
            borders_dirty = 1;
            LRESULT r = CallWindowProcA(game_proc, h, msg, wp, MAKELPARAM(w, gh));
            update_title();
            return r;
        }
        break;
    case WM_GETMINMAXINFO:
        /* not the game's: it caps the window at the screen size it saw at start-up */
        DefWindowProcA(h, msg, wp, lp);
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = 400;
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = 300;
        return 0;
    case WM_DISPLAYCHANGE:
        set_surface_size();   /* a monitor came or went: the game rebuilds its display on this message */
        break;
    case WM_DPICHANGED: {
        const RECT *r = (const RECT *)lp;
        if (!fullscreen)
            SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        borders_dirty = 1;
        ((void(__cdecl *)(RECT *))ORIG_REPAINT)(NULL);   /* full redraw and present */
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }
    return CallWindowProcA(game_proc, h, msg, wp, lp);
}

static ATOM WINAPI my_RegisterClassA(const WNDCLASSA *wc)
{
    if (!IS_INTRESOURCE(wc->lpszClassName) && !strcmp(wc->lpszClassName, GAME_CLASS)) {
        WNDCLASSA c = *wc;
        game_proc = c.lpfnWndProc;
        c.lpfnWndProc = proc;
        c.hbrBackground = GetStockObject(BLACK_BRUSH);
        return real_RegisterClassA(&c);
    }
    return real_RegisterClassA(wc);
}

static HWND WINAPI my_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w, int h,
                                      HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    /* The game's DirectDraw fullscreen modes use a topmost popup: leave those alone. */
    if (IS_INTRESOURCE(cls) || strcmp(cls, GAME_CLASS) || (ex & WS_EX_TOPMOST))
        return real_CreateWindowExA(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
    RECT r = fullscreen ? monitor_rect() : window_rect;
    style = fullscreen ? WS_POPUP | WS_VISIBLE : WS_OVERLAPPEDWINDOW | WS_VISIBLE | (maximized ? WS_MAXIMIZE : 0);
    creating = 1;
    HWND wnd = real_CreateWindowExA(ex, cls, title, style, r.left, r.top, r.right - r.left, r.bottom - r.top,
                                    parent, menu, inst, param);
    creating = 0;
    hwnd = wnd;
    borders_dirty = 1;
    resize_game();   /* the game may have reset its area while creating */
    return wnd;
}

static void hook_import(const char *name, void *mine, void *real)
{
    void **slot = rct_import_slot(name);
    if (!slot) return;
    *(void **)real = *slot;
    *slot = mine;
}

void display_install(const char *ini_path)
{
    snprintf(ini, sizeof ini, "%s", ini_path);
    char s[32];
    GetPrivateProfileStringA("display", "zoom", "0", s, sizeof s, ini);
    zoom = atof(s);
    fullscreen = GetPrivateProfileIntA("display", "fullscreen", 0, ini);
    maximized = GetPrivateProfileIntA("display", "maximized", 0, ini);

    /* default window: 80% of the primary work area, centred */
    RECT work;
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    int ww = GetPrivateProfileIntA("display", "w", 0, ini), wh = GetPrivateProfileIntA("display", "h", 0, ini);
    if (ww < 200 || wh < 150) {
        ww = (work.right - work.left) * 4 / 5;
        wh = (work.bottom - work.top) * 4 / 5;
        window_rect.left = work.left + (work.right - work.left - ww) / 2;
        window_rect.top = work.top + (work.bottom - work.top - wh) / 2;
    } else {
        window_rect.left = GetPrivateProfileIntA("display", "x", work.left, ini);
        window_rect.top = GetPrivateProfileIntA("display", "y", work.top, ini);
    }
    window_rect.right = window_rect.left + ww;
    window_rect.bottom = window_rect.top + wh;

    hook_import("CreateWindowExA", (void *)my_CreateWindowExA, &real_CreateWindowExA);
    hook_import("RegisterClassA", (void *)my_RegisterClassA, &real_RegisterClassA);
    hook_import("GetCursorPos", (void *)my_GetCursorPos, &real_GetCursorPos);
    hook_import("SetCursorPos", (void *)my_SetCursorPos, &real_SetCursorPos);
    hook_import("GetWindowRect", (void *)my_GetWindowRect, &real_GetWindowRect);
    rct_patch_jmp(ORIG_GDI_FLUSH, (void *)present);

    /* The windowed surface: the original takes the desktop size, capped at
       1280x1024. We make it the largest monitor's size (the cap becomes the
       value: its two `jle` are removed), so the window can move to any
       monitor. The dirty grid is bypassed by present() and bounded in
       gfx_invalidate_rect. */
    G(uint16_t, SURFACE_JLE_W) = 0x9090;
    G(uint16_t, SURFACE_JLE_H) = 0x9090;
    set_surface_size();

    /* the windowed GDI mode; its creation size only matters until our first WM_SIZE */
    GAME_WINDOW_W = MIN_W;
    GAME_WINDOW_H = MIN_H;
    GAME_SCREEN_MODE = 0;
}
