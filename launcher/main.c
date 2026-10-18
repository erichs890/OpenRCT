/* OpenRCT launcher: pick the game folder, check RCT.EXE, then run the original
   game in this process with the reconstructed functions hooked in and,
   optionally, the modern display (runtime/display.c).

   OpenRCT.exe [--play] [--no-hooks] [--classic] [--dir <game folder>] [--open <file.SV4|.SC4>]
   (a file given alone, e.g. dropped on the exe, is opened with --play)
   --play skips the menu (uses the saved or given settings); --classic runs
   the original 640x480 display. */
#include <windows.h>
#include <bcrypt.h>
#include <commctrl.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image.h"
#include "hooks.h"
#include "display.h"
#include "viewport.h"
#include "skin.h"

/* Builds whose addresses the reconstruction is tied to. */
static const struct { const char *sha256, *name; } known[] = {
    {"bdfebd64383b231de0252fe0726523d1c45aa2c4da919c2d7ed8bfb7aaa05c76", "Steam (RCT Deluxe, 2003-03-12)"},
};
static const struct { const char *label, *value; } zooms[] = {
    {"Automático", "0"}, {"1x", "1"}, {"1,5x", "1.5"}, {"2x", "2"}, {"3x", "3"}, {"4x", "4"},
};

enum { ID_DIR = 100, ID_BROWSE, ID_HOOKS, ID_MODERN, ID_FULLSCREEN, ID_ZOOM_PREV, ID_ZOOM_NEXT, ID_PLAY, ID_QUIT,
       ID_CLOSE };
#define VERSION "0.1"   /* launcher/launcher.manifest */

static char ini_path[MAX_PATH], game_dir[MAX_PATH];
static int use_hooks = 1, modern = 1, game_ok, play;
static char open_file[MAX_PATH], game_cmdline[2 * MAX_PATH + 32];

/* The game opens a saved game or scenario given as its command line. */
static LPSTR WINAPI game_GetCommandLineA(void) { return game_cmdline; }
static HWND wnd, play_button, hover, last_focus;
static HFONT font;
static HBRUSH edit_brush;
static HICON app_icon, game_icon;
static int dpi = 96, k = 1, zoom_sel;
static char status[300], art_dir[MAX_PATH] = "?", hooks_label[80];
static Canvas bg;   /* the window's background; owner-drawn controls start from their piece of it */

#define S(x) MulDiv((x), dpi, 96)
#define MENU_W 540
#define MENU_H 540
#define TITLE_BOTTOM (2 * k + S(20))

static void sha256_file(const char *path, char hex[65])
{
    hex[0] = 0;
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    BCRYPT_ALG_HANDLE alg;
    BCRYPT_HASH_HANDLE h;
    uint8_t buf[65536], digest[32];
    DWORD got;
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0);
    while (ReadFile(f, buf, sizeof buf, &got, NULL) && got)
        BCryptHashData(h, buf, got, 0);
    BCryptFinishHash(h, digest, sizeof digest, 0);
    BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", digest[i]);
}

/* Sets game_ok and returns a status line for the menu. */
static const char *check_game(void)
{
    static char msg[300];
    char exe[MAX_PATH + 16], data[MAX_PATH + 16], hex[65];
    snprintf(exe, sizeof exe, "%s\\RCT.EXE", game_dir);
    snprintf(data, sizeof data, "%s\\Data\\CSG1.DAT", game_dir);
    game_ok = 0;
    if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES)
        return "RCT.EXE não encontrado nesta pasta.";
    if (GetFileAttributesA(data) == INVALID_FILE_ATTRIBUTES)
        return "Data\\CSG1.DAT não encontrado: pasta incompleta.";
    sha256_file(exe, hex);
    for (size_t i = 0; i < sizeof known / sizeof *known; i++)
        if (!strcmp(hex, known[i].sha256)) {
            game_ok = 1;
            snprintf(msg, sizeof msg, "OK: %s", known[i].name);
            return msg;
        }
    snprintf(msg, sizeof msg, "Versão de RCT.EXE não suportada (sha256 %.16s…).", hex);
    return msg;
}

static void redraw(void);

/* Revalidates the folder; game art, the window icon and the edit colours follow it. */
static void refresh(void)
{
    snprintf(status, sizeof status, "%s", check_game());
    EnableWindow(play_button, game_ok);
    const char *dir = game_ok ? game_dir : "";
    if (strcmp(dir, art_dir)) {
        snprintf(art_dir, sizeof art_dir, "%s", dir);
        skin_load(game_ok ? game_dir : NULL);
        if (game_icon) DestroyIcon(game_icon);
        char exe[MAX_PATH + 16];
        snprintf(exe, sizeof exe, "%s\\RCT.EXE", game_dir);
        game_icon = game_ok ? ExtractIconA(GetModuleHandleA(NULL), exe, 0) : NULL;
        if (game_icon == (HICON)1) game_icon = NULL;   /* "not an exe" */
        SendMessageA(wnd, WM_SETICON, ICON_BIG, (LPARAM)(game_icon ? game_icon : app_icon));
        SendMessageA(wnd, WM_SETICON, ICON_SMALL, (LPARAM)(game_icon ? game_icon : app_icon));
        if (edit_brush) DeleteObject(edit_brush);
        edit_brush = CreateSolidBrush(skin_colorref(RAMP_MAIN, 5));
    }
    redraw();
}

static void load_settings(void)
{
    GetModuleFileNameA(NULL, ini_path, sizeof ini_path);
    strcpy(strrchr(ini_path, '\\') + 1, "OpenRCT.ini");
    static const char *const defaults[] = {
        "C:\\Program Files (x86)\\Steam\\steamapps\\common\\RollerCoaster Tycoon Deluxe",
        "C:\\GOG Games\\RollerCoaster Tycoon Deluxe",
    };
    GetPrivateProfileStringA("game", "dir", "", game_dir, sizeof game_dir, ini_path);
    for (size_t i = 0; !game_dir[0] && i < sizeof defaults / sizeof *defaults; i++)
        if (GetFileAttributesA(defaults[i]) != INVALID_FILE_ATTRIBUTES)
            strcpy(game_dir, defaults[i]);
    use_hooks = GetPrivateProfileIntA("game", "reconstructed", 1, ini_path);
    modern = GetPrivateProfileIntA("display", "modern", 1, ini_path);
}

static void save_settings(void)
{
    WritePrivateProfileStringA("game", "dir", game_dir, ini_path);
    WritePrivateProfileStringA("game", "reconstructed", use_hooks ? "1" : "0", ini_path);
    WritePrivateProfileStringA("display", "modern", modern ? "1" : "0", ini_path);
}

static void browse(void)
{
    BROWSEINFOA bi = {.hwndOwner = wnd, .lpszTitle = "Pasta do RollerCoaster Tycoon",
                      .ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE};
    PIDLIST_ABSOLUTE id = SHBrowseForFolderA(&bi);
    if (id && SHGetPathFromIDListA(id, game_dir)) {
        SetDlgItemTextA(wnd, ID_DIR, game_dir);
        refresh();
    }
    CoTaskMemFree(id);
}

static HWND add(const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), wnd,
                             (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
    SendMessageA(c, WM_SETFONT, (WPARAM)font, TRUE);
    return c;
}

/* Owner-drawn buttons only lack hover: track it here and repaint on enter/leave. */
static LRESULT CALLBACK button_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
{
    (void)id, (void)ref;
    if (msg == WM_MOUSEMOVE && hover != h) {
        if (hover) InvalidateRect(hover, NULL, FALSE);
        hover = h;
        TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        InvalidateRect(h, NULL, FALSE);
    } else if (msg == WM_MOUSELEAVE) {
        hover = NULL;
        InvalidateRect(h, NULL, FALSE);
    } else if (msg == WM_ERASEBKGND) return 1;
    return DefSubclassProc(h, msg, wp, lp);
}

static HWND button(const char *text, int x, int y, int w, int h, int id, DWORD tab)
{
    HWND c = add("BUTTON", text, BS_OWNERDRAW | tab, x, y, w, h, id);
    SetWindowSubclass(c, button_proc, 0, 0);
    return c;
}

/* Owner-drawn buttons have no check state of their own; checkboxes keep it in GWLP_USERDATA. */
static int checked(int id) { return (int)GetWindowLongPtrA(GetDlgItem(wnd, id), GWLP_USERDATA); }

static void set_checked(int id, int on)
{
    SetWindowLongPtrA(GetDlgItem(wnd, id), GWLP_USERDATA, on);
    InvalidateRect(GetDlgItem(wnd, id), NULL, FALSE);
}

static void enable_display_options(void)
{
    int on = checked(ID_MODERN);
    EnableWindow(GetDlgItem(wnd, ID_FULLSCREEN), on);
    EnableWindow(GetDlgItem(wnd, ID_ZOOM_PREV), on);
    EnableWindow(GetDlgItem(wnd, ID_ZOOM_NEXT), on);
    redraw();   /* the zoom value and its label are painted on the background */
}

static void read_menu(void)
{
    GetDlgItemTextA(wnd, ID_DIR, game_dir, sizeof game_dir);
    use_hooks = checked(ID_HOOKS);
    modern = checked(ID_MODERN);
    WritePrivateProfileStringA("display", "fullscreen", checked(ID_FULLSCREEN) ? "1" : "0", ini_path);
    WritePrivateProfileStringA("display", "zoom", zooms[zoom_sel].value, ini_path);
}

/* Logical (96 dpi) rectangle, like the controls. */
static RECT L(int l, int t, int r, int b) { return (RECT){S(l), S(t), S(r), S(b)}; }

/* Everything that is not a control: frame, title bar, ground, logo, panels, texts and status. */
static void render(void)
{
    uint32_t dark = skin_color(RAMP_MAIN, 0), light = skin_color(RAMP_MAIN, 11), faded = skin_color(RAMP_MAIN, 4);
    skin_bevel(&bg, (RECT){0, 0, bg.w, bg.h}, RAMP_MAIN, 0);
    RECT title = {2 * k, 2 * k, bg.w - 2 * k, TITLE_BOTTOM};
    skin_bevel(&bg, title, RAMP_ACCENT, 0);
    skin_text(&bg, title, "OpenRCT", skin_color(10, 11), SK_CENTER | SK_SHADOW);
    skin_grass(&bg, (RECT){2 * k, TITLE_BOTTOM, bg.w - 2 * k, bg.h - 2 * k});
    skin_logo(&bg, L(0, 24, MENU_W, 144));

    skin_bevel(&bg, L(10, 146, 530, 380), RAMP_MAIN, 0);
    skin_text(&bg, L(20, 150, 520, 168), "O jogo original, com as funções já reconstruídas em C no lugar.",
              light, SK_SHADOW);
    skin_text(&bg, L(20, 170, 410, 188), "Pasta do jogo:", dark, 0);
    skin_bevel(&bg, L(20, 190, 410, 214), RAMP_MAIN, 1);
    skin_badge(&bg, L(20, 222, 520, 244), game_ok, status);
    skin_bevel(&bg, L(20, 278, 520, 370), RAMP_MAIN, 1);
    skin_text(&bg, L(30, 282, 200, 300), "Vídeo", light, SK_SHADOW);
    int on = checked(ID_MODERN);
    skin_text(&bg, L(238, 334, 290, 356), "Zoom:", on ? light : faded, on ? SK_SHADOW : 0);
    skin_bevel(&bg, L(316, 334, 412, 356), RAMP_MAIN, 1);
    skin_text(&bg, L(318, 334, 410, 356), zooms[zoom_sel].label, on ? light : faded, SK_CENTER);

    skin_bevel(&bg, L(10, 388, 318, 508), RAMP_MAIN, 0);
    static const char *const hints[] = {"roda = zoom do mapa (até 4x)", "Ctrl+roda = zoom da tela",
                                        "F11 = tela cheia"};
    skin_text(&bg, L(20, 394, 310, 412), "No jogo:", light, SK_SHADOW);
    for (int i = 0; i < 3; i++) skin_text(&bg, L(30, 414 + 18 * i, 310, 432 + 18 * i), hints[i], dark, 0);
    skin_text(&bg, L(20, 484, 310, 502), "OpenRCT " VERSION, faded, 0);

    RECT foot = {2 * k, S(514), bg.w - 2 * k, bg.h - 2 * k};
    skin_bevel(&bg, foot, RAMP_MAIN, 1);
    skin_text(&bg, (RECT){foot.left + S(6), foot.top, foot.right - S(6), foot.bottom},
              "Projeto de preservação: requer sua cópia original do RollerCoaster Tycoon.", light,
              SK_CENTER | SK_SHADOW);
}

static void redraw(void)
{
    render();
    RedrawWindow(wnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

static void draw_item(const DRAWITEMSTRUCT *d)
{
    RECT r = d->rcItem;
    POINT at = {0, 0};
    MapWindowPoints(d->hwndItem, wnd, &at, 1);
    Canvas c;
    if (!skin_canvas(&c, r.right, r.bottom)) return;
    BitBlt(c.dc, 0, 0, r.right, r.bottom, bg.dc, at.x, at.y, SRCCOPY);
    UINT s = d->itemState;
    int state = (s & ODS_SELECTED ? SK_PRESSED : 0) | (s & ODS_DISABLED ? SK_DISABLED : 0) |
                ((s & ODS_FOCUS) && !(s & ODS_NOFOCUSRECT) ? SK_FOCUS : 0) | (d->hwndItem == hover ? SK_HOVER : 0);
    char text[128];
    GetWindowTextA(d->hwndItem, text, sizeof text);
    switch (d->CtlID) {
    case ID_HOOKS: case ID_MODERN: case ID_FULLSCREEN: skin_check(&c, r, state, checked(d->CtlID), text); break;
    case ID_PLAY: skin_button(&c, r, RAMP_ACCENT, state, text, ICON_PLAY); break;
    case ID_QUIT: skin_button(&c, r, RAMP_MAIN, state, text, ICON_QUIT); break;
    case ID_CLOSE: skin_button(&c, r, RAMP_ACCENT, state & ~SK_FOCUS, NULL, ICON_CLOSE); break;
    case ID_ZOOM_PREV: skin_button(&c, r, RAMP_MAIN, state, NULL, ICON_LEFT); break;
    case ID_ZOOM_NEXT: skin_button(&c, r, RAMP_MAIN, state, NULL, ICON_RIGHT); break;
    default: skin_button(&c, r, RAMP_MAIN, state, text, ICON_NONE);
    }
    BitBlt(d->hDC, 0, 0, r.right, r.bottom, c.dc, 0, 0, SRCCOPY);
    skin_canvas_free(&c);
}

static LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BROWSE: browse(); return 0;
        case ID_HOOKS: case ID_MODERN: case ID_FULLSCREEN:   /* owner-drawn buttons report fast clicks as double */
            if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == BN_DOUBLECLICKED) set_checked(LOWORD(wp), !checked(LOWORD(wp)));
            if (LOWORD(wp) == ID_MODERN) enable_display_options();
            return 0;
        case ID_ZOOM_PREV: case ID_ZOOM_NEXT: {
            int n = sizeof zooms / sizeof *zooms;
            zoom_sel = (zoom_sel + (LOWORD(wp) == ID_ZOOM_NEXT ? 1 : n - 1)) % n;
            redraw();
            return 0;
        }
        case ID_DIR:
            if (HIWORD(wp) == EN_KILLFOCUS) { GetDlgItemTextA(w, ID_DIR, game_dir, sizeof game_dir); refresh(); }
            return 0;
        case IDOK:   /* Enter, from IsDialogMessage */
        case ID_PLAY:
            read_menu();
            refresh();
            if (!game_ok) return 0;
            play = 1;
            DestroyWindow(w);
            return 0;
        case IDCANCEL:   /* Esc from the folder field */
        case ID_CLOSE:
        case ID_QUIT: DestroyWindow(w); return 0;
        }
        break;
    case WM_DRAWITEM: draw_item((const DRAWITEMSTRUCT *)lp); return TRUE;
    case WM_CTLCOLORBTN: return (LRESULT)GetStockObject(NULL_BRUSH);
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, skin_colorref(RAMP_MAIN, 11));
        SetBkColor((HDC)wp, skin_colorref(RAMP_MAIN, 5));
        return (LRESULT)edit_brush;
    case WM_NCHITTEST: {   /* drag by the painted title bar */
        LRESULT hit = DefWindowProcA(w, msg, wp, lp);
        POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
        ScreenToClient(w, &p);
        return hit == HTCLIENT && p.y < TITLE_BOTTOM ? HTCAPTION : hit;
    }
    case WM_ACTIVATE:   /* keep the focus on a control, like a dialog, so IsDialogMessage handles Esc/Enter/Tab */
        if (LOWORD(wp) == WA_INACTIVE) last_focus = GetFocus();
        else if (last_focus) SetFocus(last_focus);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r = ps.rcPaint;
        BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, bg.dc, r.left, r.top, SRCCOPY);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static void menu(void)
{
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    HDC screen = GetDC(NULL);
    dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(NULL, screen);
    k = max(1, (dpi + 48) / 96);   /* pixel art only at whole scales */
    skin_init(k);
    NONCLIENTMETRICSA ncm = {.cbSize = sizeof ncm};
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    ncm.lfMessageFont.lfQuality = NONANTIALIASED_QUALITY;
    font = CreateFontIndirectA(&ncm.lfMessageFont);   /* the folder field stays a native EDIT */

    HINSTANCE inst = GetModuleHandleA(NULL);
    app_icon = LoadIconA(inst, MAKEINTRESOURCEA(1));
    WNDCLASSA wc = {.lpfnWndProc = proc, .hInstance = inst, .lpszClassName = "OpenRCTLauncher",
                    .hCursor = LoadCursor(NULL, IDC_ARROW), .hIcon = app_icon};
    RegisterClassA(&wc);
    RECT work;
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    int w = S(MENU_W), h = S(MENU_H);
    wnd = CreateWindowA(wc.lpszClassName, "OpenRCT", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                        (work.left + work.right - w) / 2, (work.top + work.bottom - h) / 2, w, h, NULL, NULL, inst,
                        NULL);
    skin_canvas(&bg, w, h);

    /* Creation order is the Tab order. */
    button("", MENU_W - 21, 4, 16, 16, ID_CLOSE, 0);
    add("EDIT", game_dir, WS_TABSTOP | ES_AUTOHSCROLL, 25, 193, 381, 18, ID_DIR);
    button("Procurar...", 418, 189, 102, 26, ID_BROWSE, WS_TABSTOP);
    snprintf(hooks_label, sizeof hooks_label, "Usar código reconstruído (%d funções)", rct_hook_count);
    button(hooks_label, 20, 250, 500, 22, ID_HOOKS, WS_TABSTOP);
    button("Tela moderna: widescreen, qualquer resolução e zoom", 30, 302, 480, 22, ID_MODERN, WS_TABSTOP);
    button("Tela cheia", 30, 334, 180, 22, ID_FULLSCREEN, WS_TABSTOP);
    button("", 292, 334, 22, 22, ID_ZOOM_PREV, WS_TABSTOP);
    button("", 414, 334, 22, 22, ID_ZOOM_NEXT, WS_TABSTOP);
    play_button = button("Jogar", 324, 388, 110, 120, ID_PLAY, WS_TABSTOP);
    button("Sair", 440, 400, 90, 108, ID_QUIT, WS_TABSTOP);
    set_checked(ID_HOOKS, use_hooks);
    set_checked(ID_MODERN, modern);
    set_checked(ID_FULLSCREEN, GetPrivateProfileIntA("display", "fullscreen", 0, ini_path) != 0);
    char cur[16];
    GetPrivateProfileStringA("display", "zoom", "0", cur, sizeof cur, ini_path);
    for (size_t i = 0; i < sizeof zooms / sizeof *zooms; i++)
        if (atof(cur) == atof(zooms[i].value)) zoom_sel = (int)i;
    refresh();
    enable_display_options();
    last_focus = game_ok ? play_button : GetDlgItem(wnd, ID_DIR);
    ShowWindow(wnd, SW_SHOW);

    MSG m;
    while (GetMessageA(&m, NULL, 0, 0) > 0)
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE)   /* IsDialogMessage ignores Esc on owner-drawn buttons */
            DestroyWindow(wnd);
        else if (!IsDialogMessageA(wnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
    UnregisterClassA(wc.lpszClassName, inst);
    skin_canvas_free(&bg);
    skin_free();
    DeleteObject(font);
    DeleteObject(edit_brush);
    if (game_icon) DestroyIcon(game_icon);
}

/* Real pixels on high-DPI screens (the modern display scales by itself). */
static void enable_dpi_awareness(void)
{
    typedef BOOL(WINAPI * Fn)(HANDLE);
    Fn f = (Fn)(void *)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
    if (f) f((HANDLE)-4);   /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)inst, (void)prev, (void)cmd, (void)show;
    load_settings();
    for (int i = 1; i < __argc; i++) {
        if (!strcmp(__argv[i], "--play")) play = 1;
        else if (!strcmp(__argv[i], "--no-hooks")) use_hooks = 0;
        else if (!strcmp(__argv[i], "--classic")) modern = 0;
        else if (!strcmp(__argv[i], "--open") && i + 1 < __argc) GetFullPathNameA(__argv[++i], MAX_PATH, open_file, NULL);
        else if (__argv[i][0] != '-' && GetFileAttributesA(__argv[i]) != INVALID_FILE_ATTRIBUTES)
            GetFullPathNameA(__argv[i], MAX_PATH, open_file, NULL), play = 1;   /* a file dropped on the exe */
        else if (!strcmp(__argv[i], "--dir") && i + 1 < __argc) snprintf(game_dir, sizeof game_dir, "%s", __argv[++i]);
    }
    if (modern) enable_dpi_awareness();
    if (play) {
        check_game();
        if (!game_ok) play = 0;
    }
    if (!play) {
        CoInitialize(NULL);
        menu();
        CoUninitialize();
        if (!play) return 0;
        if (modern) enable_dpi_awareness();   /* no-op if already set; the option may have changed */
    }
    save_settings();

    /* The game finds Data\ etc. through the current directory. */
    char exe[MAX_PATH + 16];
    snprintf(exe, sizeof exe, "%s\\RCT.EXE", game_dir);
    SetCurrentDirectoryA(game_dir);
    RctImage img;
    const char *err = rct_load_image(exe, &img);
    if (err) {
        MessageBoxA(NULL, err, "OpenRCT", MB_ICONERROR);
        return 1;
    }
    if (use_hooks) {
        rct_install_hooks(-1);
        g_map_magnify_enabled = modern;   /* zoom in past the closest zoom: 2x, 4x (kept whole by the modern display) */
    }
    if (modern) display_install(ini_path);
    rct_use_game_resources(&img);
    if (open_file[0]) {   /* the game takes everything after the program name, unquoted */
        snprintf(game_cmdline, sizeof game_cmdline, "\"%s\" %s", exe, open_file);
        void **slot = rct_import_slot("GetCommandLineA");
        if (slot) *slot = (void *)game_GetCommandLineA;
    }
    ((void (*)(void))(uintptr_t)img.entry)();   /* WinMainCRTStartup; ends in ExitProcess */
    return 0;
}
