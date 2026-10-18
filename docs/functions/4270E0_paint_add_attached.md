# 0x4270E0 / 0x42723E / 0x4273A8 / 0x427514 `paint_add_attached` (rotations 0–3)

| | |
|---|---|
| Section / kind | MASM (paint); dispatched through the table at 0x4270D0 by rotation |
| Contract | as [paint_add_child](426A04_paint_add_child.md), reproduced exactly (`paint_add_regs` kind 2), except when added: `ebx` stays the drawing target and `edi` = `[0x8B5A4C]` (no bucket) |
| Source | [src/paint_add.c](../../src/paint_add.c) |
| REA | disassembly (capstone), diffed against the child variant |

## Behaviour (observed)

Identical to the child up to the element info and `[0x8FF00C] = ps`, then it only advances the pool
(`[0x8CFDEC] += 0x30`). It is not linked into a depth bucket: the caller hangs it from another paint struct with
the returned `ebp` (e.g. the wooden supports' special sprite stores it at `[[0x8B5FB4] + 0x1C]`).

## Verification

`tests/test_paint_add.c`: 20 000 cases over the 4 rotations (in a 60 000-case run with parent and child). The writable
image, the pool, all seven registers and CF/ZF/SF/OF match. Passes plain and through the hooks. In game: the title
parks render with all 40 hooks.
