# 0x419CEC `sawyer_write_byte`

| | |
|---|---|
| Section / kind | MASM |
| Contract | in: `al`; preserves every register (checked, including `eax`); flags clobbered |
| Source | [src/sawyer_io.c](../../src/sawyer_io.c) |
| REA | `ev_99db80a466fec4d857518cf67104339002bf5659f199ffd1301d4e2c9c79cadd` (pseudocode); instruction-level contract from the disassembly |

## Behaviour (observed)

If the buffer is full (0x400): `pushal`, `g_io_busy = 3`, `file_write(file, buf, 0x400)`, `g_io_busy = 0`, `write_error = 1` if the count differs, `popal`, then `buf_ptr = buf; buf_count = 0`. Then `*buf_ptr++ = al`; `checksum.low8 += al` (no carry into bit 8); `checksum = rol(checksum, 3)`; `buf_count++`.

## Verification

`tests/test_sawyer_io.c::test_writer`: 400 random sessions (data with runs of 1–400 and alphabets of 1–256 symbols, cut into random block/byte/flush operations, with raw bytes in 1 of 4 sessions). For every original call the registers it preserves are checked. The resulting file, the whole `SawyerIO` block, `g_io_busy` and the file position match; every RLE-only session decodes back through the reader; every written checksum validates.
