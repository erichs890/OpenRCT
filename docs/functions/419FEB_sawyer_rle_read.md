# 0x419FEB `sawyer_rle_read`

| | |
|---|---|
| Section / kind | `.text`, MASM (`call 0x419FF6; mov [esi], al; inc esi; loop`) |
| Contract | in: `esi` = dst, `ecx` = n; out: `esi` = dst+n, `ecx` = 0, `al` = last byte |
| Source | [src/sawyer_io.c](../../src/sawyer_io.c) |
| REA | `ev_99db80a466fec4d857518cf67104339002bf5659f199ffd1301d4e2c9c79cadd` |

## Behaviour (observed)

It decodes `n` bytes into `dst`. Because it uses `loop`, `n == 0` runs 2^32 times; the
C version (`do … while (--n)`) keeps that behaviour.

## Verification

`test_rle`: 600 cases (well-formed and arbitrary streams, short and >1 KiB, `n` past the
end of the stream). The output buffer, full `SawyerIO` state, `g_io_busy` and the file position
match. The register contract (`ecx == 0`, `esi == dst+n`) is checked on the original.
