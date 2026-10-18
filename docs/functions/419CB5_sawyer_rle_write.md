# 0x419CB5 `sawyer_rle_write`

| | |
|---|---|
| Section / kind | MASM |
| Contract | `mov al,[esi]; call 0x419DB1; inc esi; loop`. In: `esi` = src, `ecx` = n; out: `esi` = src+n, `ecx` = 0 (checked) |
| Source | [src/sawyer_io.c](../../src/sawyer_io.c) |
| REA | `ev_99db80a466fec4d857518cf67104339002bf5659f199ffd1301d4e2c9c79cadd` (pseudocode); instruction-level contract from the disassembly |

## Behaviour (observed)

RLE-encodes n bytes. Because of `loop`, `n == 0` means 2^32.

## Verification

`tests/test_sawyer_io.c::test_writer`: 400 random sessions (data with runs of 1–400 and alphabets of 1–256 symbols, cut into random block/byte/flush operations, with raw bytes in 1 of 4 sessions). For every original call the registers it preserves are checked. The resulting file, the whole `SawyerIO` block, `g_io_busy` and the file position match; every RLE-only session decodes back through the reader; every written checksum validates.
