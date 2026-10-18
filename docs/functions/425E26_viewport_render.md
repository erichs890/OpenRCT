# 0x425E26 `viewport_render`

| | |
|---|---|
| Section / kind | MASM |
| Contract | in `esi` viewport, `edi` dpi, `ax/bx/dx/bp` = screen x1,y1,x2,y2; preserves all registers (checked) |
| Source | [src/viewport.c](../../src/viewport.c) |
| REA | disassembly of the unpacked image (capstone) |

## Behaviour (observed)

Clips the rectangle to the viewport (signed 16-bit; returns if empty), converts it to view coordinates (`(p - vp.pos) << zoom + vp.view`) and calls the strip renderer 0x425EE3 once, or twice when the height is over 0x180 view units (the first 0x180 rows, then the rest, so only one split).

Extension (off unless `g_map_magnify_enabled`): for the magnified main viewport at zoom 0, the centre 1/m is rendered at zoom 0 into a scratch buffer and scaled m× (2 or 4) onto the screen. See docs/display.md.

## Verification

`tests/test_viewport.c`: 0x425EE3 (render strip) and 0x57C28A (invalidate window) are replaced by recorders on both sides; the strip stand-in clobbers eax..ebp like the real one. 20 000 random viewports/dpis/rectangles (zoom 0–2, partly outside, tall rectangles): identical strip calls. Passes plain and through the hooks.
