# 0x426A04 / 0x426BA8 / 0x426D5D / 0x426F17 `paint_add_child` (rotations 0–3)

| | |
|---|---|
| Section / kind | MASM (paint); dispatched through the table at 0x4269F4 by rotation |
| Contract | in: as [paint_add_parent](4263B8_paint_add_parent.md), plus the box offset in `0x8B5FAC/AE/B0`. Out, **reproduced exactly** (`paint_add_regs` kind 1): CF set = culled or pool full; `ebp` = slot; `edx` = `(box z + height) << 16 \| z`; when culled, `ax` = the value compared last, `cx` = sprite bottom, `ebx` = drawing target (`si/di` untouched); when added, `ax/cx` = box map x/y, `si` = box end y, `edi` = bucket, `ebx` = previous bucket head, flags of `and ax, ax` |
| Source | [src/paint_add.c](../../src/paint_add.c) (shares the rotation, projection, culling and bucket code with the parent) |
| REA | disassembly (capstone); the four variants diffed against each other and against the parent |

## Behaviour (observed)

Like the parent, except that the **sprite** is placed by its own offset (rotated, plus the element's map position), and
the **bounding box** by the global box offset `0x8B5FAC/AE` (rotated the same way) with z from `[0x8B5FB0]` to
`[0x8B5FB0] + height`. The depth bucket is taken from the box position. The culling compares in `ax`/`cx` instead of `di`/`si`,
and the box z goes through `dx`, which is restored with `pop dx` at the end.

## Verification

`tests/test_paint_add.c`: 20 000 cases over the 4 rotations (inside a 40 000-case run with the parent), random box offsets
included. The whole writable image, the pool, all seven registers and CF/ZF/SF/OF match. Passes plain and through
the hooks. A mutated box z is caught. In game: the title park (coasters with loops, supports) renders with it hooked.
