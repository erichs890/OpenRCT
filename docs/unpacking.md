# Unpacking RCT.EXE

| | |
|---|---|
| Input | `original/RCT.EXE` (Steam), 809 908 bytes, sha256 `bdfebd64383b231de0252fe0726523d1c45aa2c4da919c2d7ed8bfb7aaa05c76` |
| Output | `original/RCT.unpacked.exe`, 8 699 904 bytes, sha256 `9fa1766a466373e8efd88ddb63628d32620a41efde88391cf6b24dcff5ba7595` |
| Command | `python tools/unpack.py original/RCT.EXE original/RCT.unpacked.exe` (deterministic) |
| Deps | `pefile`, `unicorn==2.1.1` |

## The wrapper

The shipped exe is wrapped by **ExeLock** ("StratusBurg, LLC / Lee Hasiuk"),
which uses a NeoLite 2.x loader. Ghidra sees only 30 procedures, all in the
`.neolit` section. `.text`, `CODESEG`, `.rdata` and `.data` have
`PointerToRawData = 0`; the real image is a compressed overlay starting at
file offset `0x8BFA`.
(REA: `ev_7fce0a3e22883a4fec0d54c08ef655757f7dede520b277b5d0534776e1f1647f` overview,
`ev_c9af11b0a9fdf679fd7a5e87ad6819cbf99f2ee3daf2b6dfaf729c2df5d946e2` stub decompilation.)

Loader flow (`0xC4628A` entry → `0xC4633D` → `0xC465A9`):

1. Map its own file. The header at file offset `[0xC4629F]` = `0x8BFA` must start with magic `0x04181996`.
2. `0xC47150(src, dst, size, work)` (cdecl, chunked: a `u16` header, bit 15 =
   compressed via `0xC471FE`, otherwise a literal copy) unpacks stub stage 2 to
   `EP+H[0x08]` (size `H[0x0C]`) and `EP+H[0x10]` (size `H[0x14]`). Stage 2 contains `0xC47C57`.
3. Skip `H[0x18]` bytes of stub relocations (applied only when not loaded at `0x400000`; the exe has no relocs).
4. For each of `H[0x1C]` 16-byte entries at `H+0x54` `{section_idx, flags, size, arg}`,
   `0xC47C57(name, &src, &dst, arg, size, w1, w2, w3, flags>>5 & 1)` decompresses the section.
5. Resolve the imports at RVA `H[0x24]` (normal `IMAGE_IMPORT_DESCRIPTOR`s; IAT `H[0x28]`, size `H[0x2C]`).
6. Patch the entry `JMP` to the OEP RVA `H[0x20]` and jump.

Header fields: `H[0x20]` OEP = `0x18E06D`, `H[0x24]` import directory, `H[0x28]`/`H[0x2C]` IAT.

## What the tool does

`tools/unpack.py` re-implements steps 1, 3, 4 and 5 (writes the data directory
only, no binding) in Python. It runs **only** `0xC47150` and `0xC47C57` under
Unicorn, with a fake VirtualAlloc/VirtualFree. The game code is never executed.
It writes a PE whose file layout equals its memory layout
(`FileAlignment = SectionAlignment`), with the OEP and the import/IAT
directories restored. It also restores the **section characteristics**: the
wrapper's headers mark `.text` and `CODESEG` as `0xC0000080` (non-executable
data), and the real values are the `flags` field of each entry (`0x60000020`
for code), which the stub hands to VirtualProtect. Without that, Ghidra finds
only 940 functions.

## Result

- OEP `0x58E06D`: the MSVC 6 `WinMainCRTStartup` prologue (`push -1; push 0x8878C0; push 0x58E590; mov eax, fs:[0]`).
- 207 imports: KERNEL32 84, USER32 51, GDI32 32, WINMM 19, ADVAPI32 9, SHELL32 3, comdlg32 2, ole32 2, DPLAYX 2, DSOUND 2, DINPUT 1.
