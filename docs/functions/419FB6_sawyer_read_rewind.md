# 0x419FB6 `sawyer_read_rewind`

| | |
|---|---|
| Section / kind | MASM |
| Contract | no inputs; clobbers `eax/ecx/edx` (C call); preserves `ebx/esi/edi/ebp` (checked) |
| Source | [src/sawyer_io.c](../../src/sawyer_io.c) |
| REA | `ev_99db80a466fec4d857518cf67104339002bf5659f199ffd1301d4e2c9c79cadd` (pseudocode); instruction-level contract from the disassembly |

## Behaviour (observed)

`g_io_busy = 3; file_seek_set(file, 0); g_io_busy = 0; rle_state = 0; buf_count = 0`.

## Verification

`test_checksums`: run after every checksum case (1288 calls); state and file position match.
