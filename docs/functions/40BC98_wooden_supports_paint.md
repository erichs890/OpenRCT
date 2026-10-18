# 0x40BC98 / 0x40C038 `wooden_supports_paint (variants 0 / 1)`

| | |
|---|---|
| Section / kind | MASM (paint) |
| Contract | in: `edi` type (0–11), `ax` special (1-based, 0 = none), `dx` height, `ebp` image flags; out: `al` = drawn (0x8BF699), CF = blocked. Preserves `edx/esi/edi/ebp`; clobbers `ebx` (and `bx` sets ZF), `ecx` |
| Source | [src/paint_supports.c](../../src/paint_supports.c) |
| REA | Disassembly (capstone) of the unpacked image; Ghidra function bodies `ev_617e6a65de622771d5184e416222494137f31c68e5c852dd80d1d73788040cf6` (seeded analysis) |

## Behaviour (observed)

2 677 (0x40BC98) and 448 (0x40C038) call sites.

1. `drawn = 0`; returns if session flag bit 3 is set or support flag bit 0 is clear.
2. Starts at the ground height (0xC2D4E8) rounded up to 16; below that → CF. count = (height - start) / 16.
3. Slope (0xC2D4EA): `& 0x20` steep; `& 0x10` double slope piece (2 child sprites, image + 0x787808[slope], second one + 4), uses 2 counts; `& 0x0F` single slope piece, uses 1; not enough count → CF. If the type has no slope image, it falls back to the steep piece (0x787614) at z - 2. Variant 1 uses bound z 3 instead of 11 for the second/single slope piece and skips the steep piece when count is 0.
4. Column: 32-unit pieces (0x787610, bound z 0x1C, or 0x17 for the last) when z is 32-aligned, count > 1 and z + 16 is not the 0xC2D4EC stop value; otherwise 16-unit pieces (0x787612, bound z 0xC, or 7 for the last).
5. `special`: an extra sprite from the 8-byte table at 0x787688 (offset x/y/z, bounds, attach, enabled) on top of the column, either as a child or attached to the paint struct in 0x8B5FB4 (0x4270D0; the new struct goes in its +0x1C).

## Registers the adapter does not reproduce

The original leaves intermediate values in `eax`/`ecx` (metal) and `ebx`/`ecx` (wooden); the adapter
preserves those registers instead. `tools/regflow.py` shows that this cannot be observed. No call site reads
them directly. The track paint routines that call these functions are reached by `jmp` tables and return into the
tile-element loop (`0x42B16E`), which needs only `esi` preserved (it is). That loop masks `ecx` and restores `eax/ecx`,
and passes `ebx`/`edx`/`edi` up to the map-picking callers (`0x425D00`), where the leftover `bh` either reaches
`0x40BA5B` (which overwrites `edx` first) or the game-command flags word (tested only with masks on the low byte).

## Verification

`tests/test_paint_supports.c`: 3000 random cases per variant (type, segment, height, `special`, image flags, rotation; random support segments, ground height/slope, session flags, culling window, a nearly-full paint pool, and for wooden supports the 0xC2D4EC stop value and an attach target). Each side starts from the same snapshot of the whole writable image (.data + DATASEG, 3.8 MB) plus the paint pool; both are compared byte for byte afterwards, with the result (CF/al) and the original's preserved registers. Passes plain and with `--hook all`. Mutants (joint sprite, negative special, stop compare, bound z) are caught. In game: the title park renders with all hooks installed.
