# Modern display (widescreen, fullscreen, zoom)

An **enhancement layer**, not a reconstruction: [runtime/display.c](../runtime/display.c), enabled
by the launcher's "Tela moderna" option (on by default; `--classic` turns it off). It keeps the game's own
renderer and windowed GDI mode and only changes how the game's frame reaches the screen.

## What the original does (observed)

- Display modes come from `0x57F730` (called by the asm `0x423A84` with the option byte `[0x88B2DB]`):
  windowed GDI (mode 1, size from `0x8B9E18/0x8B9E1C`, default 640×480) or DirectDraw fullscreen at
  640×480 / 800×600 / 1024×768 / 1152×864 / 1280×1024.
- The screen surface is an 8-bit top-down DIB (`[0xC38760]`: bits `+0x84`, BITMAPINFO `+0x88`;
  created by `0x589F48(surface, w, h)`, pitch at `+0x10`) of the **desktop size, capped at 1280×1024**
  by `0x57F730` (`0x57F967`/`0x57F977`), which also uses it as the window's maximum track size. The asm draws the area its WndProc (`0x581EB2`) last got in **WM_SIZE**
  (`0xC3B004/0xC3B574`) and re-lays out its windows when that changes. Verified by running at
  1280×720 and by reading those globals in a running game (`tools/peek.py`).
- Presenting: `0x57FC45` → GDI flush `0x57FD3A` → per-block blits (64×8 blocks in a 20×128 grid at
  `0xC3BE60`, i.e. at most 1280×1024; the original marks blocks past the grid without a bound,
  see [gfx_invalidate_rect](functions/57FAB6_gfx_invalidate_rect.md)).
- Mouse: position from WM_MOUSEMOVE (`0xC3B450/454`), clicks queued with their lParam position;
  `GetCursorPos`/`SetCursorPos` for centring. WM_GETMINMAXINFO caps the window at the screen size seen at start-up.

## What the layer does

| | |
|---|---|
| Game size | client area / zoom, between 640×480 and the surface size, sent to the game as WM_SIZE |
| Surface | the size of the **largest monitor** (capped at 3840×2160): the 1280×1024 cap at `0x57F967/77` becomes the value (its two `jle` at `0x57F96E/7E` are removed). Recomputed on WM_DISPLAYCHANGE, before the game's own display rebuild |
| Zoom | auto (about 600 game pixels high, in steps of 0.25) or fixed: 1, 1.25, 1.5, 1.75, 2, 2.5, 3, 3.5, 4, 5, 6 |
| Present | `0x57FD3A` replaced by one full-frame StretchDIBits of the game area, nearest-neighbour, letterboxed in black |
| Window | `RegisterClassA`/`CreateWindowExA` (IAT): our WndProc in front of the game's; resizable or borderless fullscreen |
| Input | mouse lParams, `GetCursorPos`/`SetCursorPos`/`GetWindowRect` (IAT) mapped between window and game pixels |
| DPI | the launcher is per-monitor DPI aware, so the window uses real pixels |
| Keys | F11 or Alt+Enter: fullscreen · wheel: map zoom · Ctrl+wheel, Ctrl +/−: pixel zoom · Ctrl+0: auto |
| Settings | `[display]` in `OpenRCT.ini`: `modern`, `zoom` (0 = auto), `fullscreen`, `maximized`, `x y w h` |

## Map zoom in (magnification)

The game's map zoom has 3 levels (0 = closest). With the hooks on, the launcher enables an
extension in the reconstructed [zoom](functions/4288AF_window_zoom.md) and
[viewport_render](functions/425E26_viewport_render.md) functions: zooming in at level 0 magnifies the main view
2×, then 4×. The centre 1/m of the view is rendered at level 0 into a scratch buffer and scaled up; the UI stays
at its size. Map picking (`0x425D00`) gets the matching inverse transform through an entry adapter and a
trampoline. The game itself still sees zoom 0, so scrolling, centring and saved views are unchanged.
Controls: the game's zoom buttons/keys, or the **mouse wheel** (Ctrl+wheel is the pixel zoom).
Checked on the title park: wheel up gives 2× and 4×, wheel down goes back through 1× into the game's zoom-out.

The game redraws only what changed: it **shifts the screen pixels** when the view scrolls and invalidates the
rectangles of moving sprites, both in unmagnified coordinates. That broke a magnified view into patches (reported
on a saved park; reproduced with `--open`: walking guests left fragments). So while magnified, `map_magnify_tick`
(called by the present step every frame) invalidates the whole main view through the game's own `0x578B61`,
so windows are still redrawn on top in order. Only 1/m² of the view is actually rendered, so the cost is small.
Checked on the same park at 2× and 4× after 10 s of guests walking: clean. Magnification is only enabled
with the modern display (which drives that tick).

## Verified

Running game, with `tools/peek.py` reading its memory and window captures via PrintWindow:
1518×769 window → 1215×616 at 1.25×; F11 → 1920×1080 at 1.75× (1098×618) and back to the same
window; maximised → 1280×661 at 1.5×; Ctrl+wheel steps; posted mouse moves land on the expected
game pixels ((700,700) → (560,560)), and a click on the title "load" button opens the game's
Load Game dialog.

Raised surface: fullscreen at zoom 1× runs a 1920×1080 game area (surface 1920×1080, read back
from memory). It ran stable for 90 s and drew the whole area.

A simulated WM_DISPLAYCHANGE (monitor added or resolution changed) goes through the game's full display
rebuild: the window is recreated with our geometry and keeps rendering (checked by capture and by memory reads).

## Limits / next

- Monitors larger than 3840×2160 are limited to that game area (raise `MAX_SURFACE_*` if needed; the cost is memory).
- The in-game fullscreen options (DirectDraw) still work, without these features.
