# 0x41A0E4 `sawyer_check_sv4`

| | |
|---|---|
| Section / kind | MASM |
| Contract | out: **CF clear = valid**; clobbers `eax/ebx/ecx/edx/edi`; preserves `esi/ebp` (checked) |
| Source | [src/sawyer_io.c](../../src/sawyer_io.c) |
| REA | `ev_27fe3d849b56582638770d69fde19b9042eea2bd4436a7e9d5a774d40bed3e5e`. **Ghidra's pseudocode is wrong here**: it drops the three-way compare and the CF result, so the disassembly is authoritative |

## Behaviour (observed)

size = `file_seek_end(file, 0)`; if size < 8, CF is set. Otherwise it sums the first size-4 bytes (seek to 0, read through `buf` in 1 KiB chunks; per byte `low8 += b; rol 3`), reads the last dword into `buf[0..4]`, and accepts `sum + {120001, 108156, 110001}`, or `sum - {...}` when `[0x88B2C0]` is set. Every file call is bracketed by `g_io_busy` 3/0.

Real files: the one shipped `.SV4` uses `+120001`; the 85 `.SC4` use `-120001` (32), `-110001` (31) and `-108156` (22). This function accepts all of them with `[0x88B2C0]` set, which suggests that flag means "scenario" (**inferred**). The three offsets look like base game / Added Attractions / Loopy Landscapes (**inferred**).

## Verification

`tests/test_sawyer_io.c::test_checksums`: all 86 shipped files (`Saved Games/*.SV4`, `Scenarios/*.SC4`) × both validators × `g_checksum_negate` 0/1, plus 300 synthetic files (sizes 0–19 around the limits, random tails, tails valid for each offset). Result (CF), the full state block and the file position match.
