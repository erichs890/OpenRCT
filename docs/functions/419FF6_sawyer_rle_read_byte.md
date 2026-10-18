# 0x419FF6 `sawyer_rle_read_byte`

| | |
|---|---|
| Section / kind | `.text`, MASM |
| Contract | in: none; out: `al` = decoded byte; only `al` and the flags change (`eax[31:8]` checked) |
| Source | [src/sawyer_io.c](../../src/sawyer_io.c) |
| REA | `ev_99db80a466fec4d857518cf67104339002bf5659f199ffd1301d4e2c9c79cadd` (pseudocode) |
| Callers | 0x419FEB `sawyer_rle_read` |

## Behaviour (observed)

The streaming decoder for the Sawyer RLE used in SV4/SC4 chunks. State: `rle_state` (i8) 0x8BF8EE,
`rle_count` (u8) 0x8BFD74, run byte `rle_buf[0]` at 0x8BFCF4.

- With an active state and `rle_count != 0`: decrement; a literal (`state > 0`) returns the next
  file byte, a run (`state < 0`) returns `rle_buf[0]`.
- Otherwise it reads a control byte `n`:
  - `n >= 0`: `rle_count = n`, `state = 1`, returns the next byte → **n+1 literals**.
  - `n < 0`: `state = -1`, `rle_count = -n` (8-bit `neg`), `rle_buf[0]` = next byte, returns it →
    **1-n copies**. `n = -128` gives 129 copies, because `neg 0x80 == 0x80`.

Matches OpenRCT2's description of the RCT RLE format (used only as a format reference).

## Verification

`test_rle`: 600 streams, 300 well-formed (literals 1–128, runs 2–129 including n=-128) and
300 of arbitrary bytes. For each, up to 500 single-byte calls through `rct_call`, with
`eax[31:8]` checked; outputs and full state match.
Mutation check: `rle_count = -n-1` is detected.
