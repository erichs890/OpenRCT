# 0x419DB1 `sawyer_rle_write_byte`

| | |
|---|---|
| Section / kind | MASM |
| Contract | in: `al`; clobbers `eax` (`ah` is state scratch, `al` may come back as the old run byte); preserves `ebx/ecx/edx/esi/edi/ebp` (checked) |
| Source | [src/sawyer_io.c](../../src/sawyer_io.c) |
| REA | `ev_99db80a466fec4d857518cf67104339002bf5659f199ffd1301d4e2c9c79cadd` (pseudocode); instruction-level contract from the disassembly |

## Behaviour (observed)

Encoder state machine (`rle_state`, `rle_count`, `rle_buf` at 0x8BFCF4, whose `[0]` is both the run byte and the first literal; `literal_ptr` at 0x8BFCF0):

| state | input b | action |
|---|---|---|
| 0 | any | `rle_buf[0] = b`, state 1 |
| 1 | `== rle_buf[0]` | count 2, state 2 (run) |
| 1 | other | `rle_buf[1] = b`, `literal_ptr = &rle_buf[1]`, count 2, state 3 (literals) |
| 2 | same and count < 125 | count++ |
| 2 | otherwise | emit `1-count` and the run byte; `rle_buf[0] = b`; state 1 (count left as is) |
| any other (3, and also the reader's 0xFF) | count >= 125 | emit `count-1` and `count` literals; `rle_buf[0] = b`; state 1 |
| | `== *literal_ptr` | count--, emit the literals; `rle_buf[0] = b`; state 2, count 2 |
| | otherwise | `*++literal_ptr = b`, count++ |

Emitting literals loops with `dec byte / jne`, so it leaves `rle_count = 0` (a count of 0 would emit 256).

## Verification

`tests/test_sawyer_io.c::test_writer`: 400 random sessions (data with runs of 1–400 and alphabets of 1–256 symbols, cut into random block/byte/flush operations, with raw bytes in 1 of 4 sessions). For every original call the registers it preserves are checked. The resulting file, the whole `SawyerIO` block, `g_io_busy` and the file position match; every RLE-only session decodes back through the reader; every written checksum validates.
