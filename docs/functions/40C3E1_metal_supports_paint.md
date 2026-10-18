# 0x40C3E1 / 0x40C860 `metal_supports_paint (variants 0 / 1)`

| | |
|---|---|
| Section / kind | MASM (paint) |
| Contract | in: `edi` type (0–12), `ebx` segment (0–8), `ax` special (i16), `dx` height, `ebp` image flags; out: CF = no free segment. Preserves `ebx/edx/esi/edi/ebp`, and `eax` unless `special != 0` (then the primitive's ax leaks); clobbers `ecx` |
| Source | [src/paint_supports.c](../../src/paint_supports.c) |
| REA | Disassembly (capstone) of the unpacked image; Ghidra function bodies `ev_617e6a65de622771d5184e416222494137f31c68e5c852dd80d1d73788040cf6` (seeded analysis) |

## Behaviour (observed)

5 261 (0x40C3E1) and 789 (0x40C860) call sites, the two most-called functions in the game.

1. Returns (CF clear) if session flag bit 3 is set or support flag bit 0 is clear.
2. If `height` is below the segment's recorded top (taken): drop by the type height (0x78758E); search up to 4 neighbour segments (0x78737E, by rotation) for one whose top is below; none → CF set. Variant 1 also gives up when the found offset is >= 4. Draw an offset piece (images 0x7874BE, offsets 0x78749E, bounds 0x7874AE) and continue in that segment.
3. Base plate on sloped ground when there are >= 6 units of room (0x7875A8 + slope image 0x787848), bound z 5.
4. Column from the segment top: first piece to the next multiple of 16, then 16-unit pieces; every 4th full piece uses the next image (a joint). Bound z = piece length - 1.
5. Segment top := the height recorded in 0x8BF67C (0xFFFF when the segment was free), slope := 0x20.
6. `special > 0`: child column from `height` to `height + special` (bound offsets 0x8B5FAC). `special < 0` (variant 0 only; variant 1 runs it through the positive path): column from `height - 1` down, images 0x7875DC.

All arithmetic is 16-bit. Sprites go through the original primitives 0x4263A8/0x4269F4[rotation] (not reconstructed yet). The structure matches OpenRCT2's description of RCT2's metal supports, used as a cross-check only.

## Registers the adapter does not reproduce

The original leaves intermediate values in `eax`/`ecx` (metal) and `ebx`/`ecx` (wooden); the adapter
preserves those registers instead. `tools/regflow.py` shows that this cannot be observed. No call site reads
them directly. The track paint routines that call these functions are reached by `jmp` tables and return into the
tile-element loop (`0x42B16E`), which needs only `esi` preserved (it is). That loop masks `ecx` and restores `eax/ecx`,
and passes `ebx`/`edx`/`edi` up to the map-picking callers (`0x425D00`), where the leftover `bh` either reaches
`0x40BA5B` (which overwrites `edx` first) or the game-command flags word (tested only with masks on the low byte).

## Verification

`tests/test_paint_supports.c`: 3000 random cases per variant (type, segment, height, `special`, image flags, rotation; random support segments, ground height/slope, session flags, culling window, a nearly-full paint pool, and for wooden supports the 0xC2D4EC stop value and an attach target). Each side starts from the same snapshot of the whole writable image (.data + DATASEG, 3.8 MB) plus the paint pool; both are compared byte for byte afterwards, with the result (CF/al) and the original's preserved registers. Passes plain and with `--hook all`. Mutants (joint sprite, negative special, stop compare, bound z) are caught. In game: the title park renders with all hooks installed.
