# 0x41A215 `sawyer_check_sc4`

| | |
|---|---|
| Section / kind | MASM |
| Contract | out: CF clear = valid; also sets `checksum_ok` (0x8BF8E2); clobbers `eax/ebx/ecx/edx/edi`; preserves `esi/ebp` |
| Source | [src/sawyer_io.c](../../src/sawyer_io.c) |
| REA | `ev_27fe3d849b56582638770d69fde19b9042eea2bd4436a7e9d5a774d40bed3e5e`. **Ghidra's pseudocode is wrong here**: it drops the three-way compare and the CF result, so the disassembly is authoritative |

## Behaviour (observed)

Same as 0x41A0E4, with a minimum size of 14 and offsets `sum - {120001, 108156, 108112}` (no negate flag). `checksum_ok = 1` only for `-120001`. It rejects the 31 shipped scenarios that use `-110001`, so its role is not "validate any scenario" (open question).

## Verification

`tests/test_sawyer_io.c::test_checksums`: all 86 shipped files (`Saved Games/*.SV4`, `Scenarios/*.SC4`) × both validators × `g_checksum_negate` 0/1, plus 300 synthetic files (sizes 0–19 around the limits, random tails, tails valid for each offset). Result (CF), the full state block and the file position match.
