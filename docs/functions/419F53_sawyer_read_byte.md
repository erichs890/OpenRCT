# 0x419F53 `sawyer_read_byte`

| | |
|---|---|
| Section / kind | `.text`, MASM |
| Contract | in: none; out: `al` = byte. Preserves every other register (including `eax[31:8]`); flags clobbered |
| Source | [src/sawyer_io.c](../../src/sawyer_io.c) |
| REA | `ev_27fe3d849b56582638770d69fde19b9042eea2bd4436a7e9d5a774d40bed3e5e` (pseudocode); disassembly in the commit |
| Callers | 0x419FF6 `sawyer_rle_read_byte` |

## State (`SawyerIO` at 0x8BF8E2, see [src/sawyer_io.h](../../src/sawyer_io.h))

`file` 0x8BF8E4, `buf_ptr` 0x8BF8E8, `buf_count` (u16) 0x8BF8EC, `buf[0x400]` 0x8BF8F0.
Also `g_io_busy` 0xC3B59C, set to 3 around the `file_read` call.

## Behaviour (observed)

When `buf_count == 0` it refills: `pushal`; `g_io_busy = 3`; `file_read(file, buf, 0x400)`;
`g_io_busy = 0`; `popal`; `buf_count = 0x400`; `buf_ptr = buf`. Then it returns
`*buf_ptr++` and decrements `buf_count`.
The return value of `file_read` is ignored, so a short read near EOF still counts
as 0x400 bytes. Past EOF, stale buffer bytes are returned. The reconstruction keeps this.

## Verification

`test_read_byte`, 100 files of 0–3000 bytes, 1–3500 reads each, including reads past EOF
(stale-buffer path). The original runs through `rct_call`; the register contract
is checked on every call. Output bytes, the whole `SawyerIO` block, `g_io_busy` and
the file position match. Mutation check: `buf_count == 1` as the refill condition is detected.
