# 0x4263B8 / 0x42653B / 0x4266CC / 0x42685F `paint_add_parent` (rotations 0–3)

| | |
|---|---|
| Section / kind | MASM (paint); dispatched through the table at 0x4263A8 by `[0xC2D4F0]` rotation |
| Contract | in: `ebx` image, `al`/`cl` x/y offset (i8), `di`/`si` bounding box x/y size, `ah` box height (i8), `dx` z. Out, **reproduced exactly by the adapter** (`paint_add_parent_regs`): CF set = culled or pool full; `ebp` = the paint struct slot; `edx` = `(z + height) << 16 \| z`; when added, `ax`/`cx` = map x/y, `si` = sprite bottom, `edi` = depth bucket, `ebx` = previous bucket head, flags of `and ax, ax`; when culled, the same `ax/cx/si`, `ebx` = drawing target, `di` = the value compared last, flags of that comparison; when the pool is full, the input registers |
| Source | [src/paint_add.c](../../src/paint_add.c) |
| REA | disassembly of the unpacked image (capstone); the four variants diffed against each other |

## Behaviour (observed)

1. `[0x8FF00C] = 0`; the slot is `[0x8CFDEC]`; pool full (`>= [0x8CFDE4]`) → culled.
2. `ps+0 = image`. Offsets and box size are rotated into map axes (rot 0: box−1; rot 1: box x−1, swap, negate; rot 2: negate all; rot 3: box y−1, swap, negate), plus the element's map position (`0x8B5A3C/40`). `ps+0x0C/0x0E` = box end, `ps+8` = z | z_top << 16.
3. Screen position (isometric projection per rotation, y −= z) → `ps+0x10/0x12`. The sprite rectangle from the g1 entry (`0xAFC290 + 16·(image & 0x1FFFF)`: width, height, x/y offset) is stored at `0x8FF000/02` and culled against the dpi in `[0xC1D4B8]` (signed 16-bit, edges exclusive).
4. Fills `ps+4/6` (map x/y), clears `+0x16/+0x18/+0x1C`, copies the element info `0x8B5A44/48/4C` → `+0x24/+0x28/+0x2C`, `[0x8FF00C] = ps`.
5. Depth bucket = (map-diagonal value per rotation, + 0x1000/0x2000 for rotations 1–3), clamped to 0 if negative, `>> 5`, capped at 255 → `ps+0x14`. Pushed onto that bucket's list (`0x8FEBF0[q]`, link at `ps+0x20`), min/max buckets updated (`0x8FEFF0/F4`), and `[0x8CFDEC] += 0x30`.

## Why the leftovers are reproduced

`tools/regflow.py` follows every path after each of the 3 584 call sites: 11 read what this function
leaves in `ebx`/`eax` (e.g. a wooden-supports exit tests `bx`), and about 2 000 return with them still
live. So the adapter rebuilds them exactly instead of preserving the input registers. The trace of which exit
was taken comes from `paint_add_parent_traced`; the plain C API is unaffected.

## Verification

`tests/test_paint_add.c`: 20 000 cases over the 4 rotations (random offsets, boxes, z, flagged image ids,
g1 entries, pool position (sometimes full), drawing target, map position, element info and bucket lists).
The whole writable image is snapshotted and compared, together with the pool, **all seven registers and CF/ZF/SF/OF**
(the reconstruction runs through its register adapter). 10 707 cases added a struct.
Passes plain and through the hooks. Mutants (bucket offset, projection) are caught.
In game: the title park renders with all hooks, including the supports that call this through the table.
