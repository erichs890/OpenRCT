# 0x57FAB6 `gfx_invalidate_rect`

| | |
|---|---|
| Section / kind | VC6 C (cdecl), display layer |
| Signature | `void gfx_invalidate_rect(int x1, int y1, int x2, int y2)` |
| Source | [src/platform_display.c](../../src/platform_display.c) |
| REA | disassembly; callers include the WM_PAINT path `0x401120` and the WndProc |

## Behaviour (observed)

It swaps the corners into order and rejects rectangles outside `[0, w] × [0, h]`, where w/h are the client
size from WM_SIZE (`0xC3B004/0xC3B574`). It clips, and returns on an empty rectangle. Then it sets the
grid byte (`0xC3BE60`, 20 columns of 64 px, rows of 8 px) for every block from `(x1/64, y1/8)` to
`(x2/64, y2/8)` inclusive, adding 1 to `0xC3879C` per block. An edge exactly on a block boundary
still marks that block, so x2 = 1280 spills into the next row.

**Deliberate fix**: the original does not bound the index, so a client taller than 1024 writes past
the 0xA00-byte grid. Here blocks outside the grid are counted but not stored. This makes no difference
for any size the original supports.

## Verification

`tests/test_platform_display.c`: 20 000 random rectangles (corners in any order, partly or fully
outside, extreme values) on the original's sizes 640×480 … 1280×1024 and random sizes up to
1280×1016. The grid and count match the original. A 1920×1200 client leaves the memory after the grid untouched.
