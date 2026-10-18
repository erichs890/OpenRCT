# Inventory of RCT.EXE (Steam build)

Target: `original/RCT.unpacked.exe` (see [unpacking.md](unpacking.md)),
sha256 `9fa1766a466373e8efd88ddb63628d32620a41efde88391cf6b24dcff5ba7595`.
REA/Ghidra 12.1.4 analysed `original/RCT.analysis.exe`, the same image plus synthetic
export seeds (`tools/seed_exports.py`; sha256 `31ed4beb…`).
Regenerate: `python tools/inventory.py` → `.analysis/functions.csv`, `.analysis/strings.txt`.

## Toolchain (Rich header, observed)

| Tool | Build | Objects | Reading |
|---|---|---|---|
| Utc12 C (VC 6.0) | 8797 | 67 | Visual C++ 6.0 SP5 |
| Utc12 C++ | 8797 | 1 | |
| MASM | 7299 | 17 | MASM 6.14: Sawyer's assembly |
| Linker | 6.00 | | VS 6 linker |
| Imports | | 204 | |

## Layout

| Section | Range | Content (observed / inferred) |
|---|---|---|
| `.text` | 0x401000–0x401421 | small C object: game main + message loop (inferred: frame-pointer C) |
| `.text` | 0x401421–0x57EBE0 | MASM game code: no frame prologues, register calling convention |
| `.text` | 0x57EBE0–0x596000 | VC6 C: Win32 platform layer + MSVC CRT (OEP `0x58E06D`) |
| `CODESEG` | 0x596000–0x787000 | MASM: pointer tables + code reached through them (mostly paint/track tables, inferred) |
| `.rdata` / `.data` / `DATASEG` | 0x787000–0xC3F000 | data; `.data` is 3.8 MB virtual, 216 KB initialised |

Function counts (REA `list_procedures`, `ev_e4b7c190a48bd7a59494763f667097a02ac06e84a0fa705611aa73a31350990b`):
**16 360** functions (15 769 asm, 591 C/CRT), plus 205 imports. About 6 780 are
leaves (no direct or indirect calls, approximate bodies).

Why the seeds were needed: the NeoLite header marks `.text` as `0xC0000080`
(non-executable data); with the real flags restored Ghidra finds 1 577
functions. Exporting call-rel32 targets and code-pointer-table targets brings it
to 16 599 (`ev_617e6a65de622771d5184e416222494137f31c68e5c852dd80d1d73788040cf6`).

## Imports (207)

KERNEL32 84, USER32 51, GDI32 32, WINMM 19, ADVAPI32 9, SHELL32 3, comdlg32 2,
ole32 2, DPLAYX 2, DSOUND 2, DINPUT 1. **No DirectDraw**: frames go out through GDI.

## Subsystems

| Subsystem | Where | Evidence |
|---|---|---|
| Entry / CRT | `0x58E06D` WinMainCRTStartup → `0x581299` WinMain | entry disassembly; `ev_202b49fdfb86c3a0c7d581b4dc9a15d6c2e6f3d2f617dd269d03f86748ae3cb6` |
| Game main + loop | `0x401000`: `while (0x581DB6 msgpump) { 0x581004; 0x42CDEC tick; if [0xC38764] 0x57FC45 present }` | disassembly of 0x401000 |
| Game tick (asm) | `0x42CDEC` | called from the loop; not yet analysed |
| Render out (GDI) | 54 C functions, 0x57EF84–0x58D345 (DIB/blit) | GDI32 import users |
| Window / input | 91 C functions (USER32 + DINPUT), e.g. 0x581DB6, 0x582C1D–0x583901 | USER32/DINPUT import users |
| Audio | 27 C functions, 0x58CB80–0x58DFF3 (DSOUND + WINMM) | import users |
| Network | DPLAYX at 0x595A56/0x595A5C (thunks) | import users |
| File wrappers (C) | 0x584E30 map, 0x584F20/37/4E seek, 0x584F65 read, 0x585000 write, 0x58502C close, 0x585048 open_r, 0x585069 open_w, 0x5850A9 findfirst | `ev_5411a60ec46629ea159efe3e6e0d2a3bb2f2dec2109b7d20699955ba1f622f24` |
| SV4/SC4 stream (asm) | 0x419CB5–0x41A32F: RLE encoder/decoder, 1 KiB buffer, rol-3 checksum (±120001) | `ev_99db80a466fec4d857518cf67104339002bf5659f199ffd1301d4e2c9c79cadd`, `ev_27fe3d849b56582638770d69fde19b9042eea2bd4436a7e9d5a774d40bed3e5e` |
| Save/load callers | 0x416C98–0x44D0D4 (open/read/write users) | E8 xrefs to the file wrappers |
| `.DAT` assets | path table at 0x78CFC8 (`Data\CSG1.DAT`, `CSS*.DAT`, …), no direct code xref (accessed by index) | strings |

## Attack order

Rule: leaves first, then their callers, preferring pure data transforms that
the harness can feed exhaustively. Within a tier, the most-called first.

1. **SV4/SC4 stream (in progress)**: 0x584F65 → 0x419F53 → 0x419FF6 → 0x419FEB,
   then the writer 0x419CEC / 0x419DB1 / 0x419EDE / 0x419D6B / 0x419CB5, init 0x419CC0 / 0x419FB6,
   checksum 0x41A0E4 / 0x41A215 / 0x41A05F / 0x41A0AB.
2. Other C file wrappers (byte-matchable once VC6 is available).
3. Asm leaves with high caller counts: `0x40C3E1` (5 261 call sites),
   `0x40BC98` (2 677), `0x40C860` (789), `0x40C038` (448). Likely sprite/text
   drawing (to confirm).
4. Paint primitives: done: `paint_add_parent` (0x4263A8), `paint_add_child` (0x4269F4) and
   `paint_add_attached` (0x4270D0). Next: the paint list walk and sprite drawing they feed (0x42635F/0x4262C7 paint list walk).
5. Save/load drivers 0x416C98–0x44D0D4 (they need 1).
6. Game tick 0x42CDEC subtree (peeps, rides, finance), top-down by subsystem.
7. Paint/`CODESEG` tables.
8. Platform layer (GDI/DSound/DInput), last: side effects only, tested by capture.
