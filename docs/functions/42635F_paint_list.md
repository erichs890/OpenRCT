# 0x42635F `paint_session_begin`, 0x42839E `paint_arrange`, 0x42860C `paint_draw`

| | |
|---|---|
| Section / kind | MASM (paint); called in sequence for each strip by 0x425EE3 (viewport render) and by the picking code |
| Source | [src/paint_list.c](../../src/paint_list.c) |
| REA | disassembly (capstone); `tools/regflow.py` for what callers read back |

These are the stages around the primitives ([parent](4263B8_paint_add_parent.md), [child](426A04_paint_add_child.md), [attached](4270E0_paint_add_attached.md)). Begin resets the session, the primitives fill the depth buckets, arrange turns the buckets into one sorted list, and draw sends it to the sprite drawer.

## 0x42635F paint_session_begin

Input: `ebp` = the pool. It sets `[0x8CFDEC]` (next) = ebp and clears `[0x8FF00C]` (last added). It clears the 256 buckets at 0x8FEBF0 with `rep stosd`, then sets min `[0x8FEFF0]` = 0xFFFFFFFF and max `[0x8FEFF4]` = 0, and clears `[0x8FF008]` and the label list `[0x8FF004]`.

Output: `eax` = 0, `ecx` = 0, `edi` = 0x8FEFF0, the rest preserved. The adapter reproduces this exactly.

## 0x42839E paint_arrange

1. A new root struct is taken from the pool and stored at `[0x8CFDE8]`, then buckets min..max are concatenated after it through `+0x20`. When min = 0xFFFFFFFF it stops here.
2. One pass runs per bucket `b` from min to max − 1. Each pass first marks `+0x17` on the structs of buckets `b` and `b + 1`:
   - bit 0: to be placed;
   - bit 1: may be moved;
   - 0x80: past `b + 1`, which ends the pass.
3. Each struct to be placed is compared with each later movable struct `B`. The comparisons are six unsigned 16-bit tests: A.x < B.xend, A.y < B.yend, A.z < B.ztop, A.xend < B.x, A.yend < B.y, A.ztop < B.z. They build a key, which is appended to `[0xC2D4F0]` (the rotation) and looked up in the byte table 0x78E99C. A non-zero entry moves `B` in front of the struct being placed.
4. The first pass treats bucket `b` like `b + 1`: both get flags 3. Later passes give bucket `b` only bit 0.

The callers' callers read the leftover registers (on the picking path), so this function is written over a register block (`paint_arrange_regs`) and reproduces all seven registers.

## 0x42860C paint_draw

It walks the list after the root. For each struct:

1. It draws the struct, then its child chain (`+0x1C`).
2. It then draws the attached list (`+0x18`, next at `+8`) of the **last** struct in the chain, at screen position + the attached offsets (`+4`/`+6`).
3. At `dpi.zoom` ≠ 0, type-2 sprites (`+0x24`) are aligned to even pixels.
4. The see-through options turn the image into a ghost (`(img & 0x1FFFF) | 0x405E0000`, unless it already has 0x40000000):
   - session flag 2: type 3;
   - flag 4: types 5, 9, 10 and 12, but only type 5 for attached sprites.

Every sprite goes to 0x41DA8B with cx/dx = x/y, ebx = image and edi = dpi. 0x41DA8B is not reconstructed and is reached through `rct_call`. Its use of cx/dx is 16-bit only (checked with regflow), so zero-extended values are safe.

Leftover registers: they come from inside 0x41DA8B and only reach 0x425EE3, whose caller (the viewport_render adapter, pushad/popad) restores everything. The adapter therefore preserves all registers.

## Verification

`tests/test_paint_list.c` runs 2 000 begin cases, 20 000 arrange cases and 20 000 draw cases.

- **Begin and arrange:** the setup uses random buckets and boxes, with narrow and full 16-bit ranges, and random rotation and registers. All seven registers, the writable image and the pool must match.
- **Draw:** 0x41DA8B is patched with a recorder. The setup uses random child chains, attached lists, types, zoom and session flags. The sequence of (cx, dx, ebx, edi) and memory must match.

All of it passes plain and through the hooks. Six mutants were tried: flags of the first pass, one key comparison, the zoom alignment, one ghost type, the saved bucket index, and clearing 0x8FF008. All six are caught.
