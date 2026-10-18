# 0x4288AF / 0x4288E9 `window_zoom_in` / `window_zoom_out`

| | |
|---|---|
| Section / kind | MASM |
| Contract | in `esi` window (its viewport at +8); the original clobbers `edi/ax/bx`; the adapter preserves everything |
| Source | [src/viewport.c](../../src/viewport.c) |
| REA | disassembly of the unpacked image (capstone) |

## Behaviour (observed)

In: if zoom > 0, `zoom--`, view size `sar 1`, and the window's saved view position (+0x182/+0x184) `+= new view size >> 1` (unsigned), so the centre stays put; then invalidate the window (0x57C28A). Out: the inverse up to zoom 2 (`shl 1`, `-= old size >> 1`). A null viewport or a limit does nothing.

Extension (off unless enabled): zoom in at zoom 0 magnifies the view 2× then 4×; zoom out undoes that first. Called from the zoom buttons and keys (0x413DEF/0x413DFE) and from the mouse wheel (display layer).

## Verification

`tests/test_viewport.c`: 0x425EE3 (render strip) and 0x57C28A (invalidate window) are replaced by recorders on both sides; the strip stand-in clobbers eax..ebp like the real one. 20 000 random windows/viewports (zoom 0–2, null viewport): window bytes, viewport and invalidate calls identical.
