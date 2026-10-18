# 0x41A05F `sawyer_write_checksum_sv4`

| | |
|---|---|
| Section / kind | MASM |
| Contract | clobbers `eax/ecx/edx`; preserves `ebx/esi/edi/ebp` (checked) |
| Source | [src/sawyer_io.c](../../src/sawyer_io.c) |
| REA | `ev_27fe3d849b56582638770d69fde19b9042eea2bd4436a7e9d5a774d40bed3e5e`. **Ghidra's pseudocode is wrong here**: it drops the three-way compare and the CF result, so the disassembly is authoritative |

## Behaviour (observed)

`buf[0..4] = checksum + 120001` (`checksum - 120001` if `[0x88B2C0]` is set); writes those 4 bytes (`g_io_busy` 3/0).

## Verification

`tests/test_sawyer_io.c::test_writer`: 400 random sessions (data with runs of 1–400 and alphabets of 1–256 symbols, cut into random block/byte/flush operations, with raw bytes in 1 of 4 sessions). For every original call the registers it preserves are checked. The resulting file, the whole `SawyerIO` block, `g_io_busy` and the file position match; every RLE-only session decodes back through the reader; every written checksum validates.
