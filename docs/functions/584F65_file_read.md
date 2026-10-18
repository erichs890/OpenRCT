# 0x584F65 `file_read`

| | |
|---|---|
| Section / kind | `.text`, VC6 C (cdecl) |
| Signature | `uint32_t file_read(HANDLE h, void *buf, uint32_t len)` |
| Source | [src/platform_file.c](../../src/platform_file.c) |
| REA | `ev_5411a60ec46629ea159efe3e6e0d2a3bb2f2dec2109b7d20699955ba1f622f24` (pseudocode) |
| Callers | 11 asm functions, including 0x419F53 `sawyer_read_byte` and 0x41A0E4 / 0x41A215 (checksum) |

## Behaviour (observed)

- `len != 0xFFFFFFFF`: `ReadFile(h, buf, len)`. Returns the bytes read, or `0xFFFFFFFF` on failure.
- `len == 0xFFFFFFFF`: saves the current position, seeks to the end, seeks back,
  then reads `end - cur` bytes (the rest of the file).

## Verification

`tests/test_sawyer_io.c::test_file_read`, 201 cases: random files of 0–5000 bytes,
random start offsets, random lengths including `0xFFFFFFFF` and lengths past EOF,
and an invalid handle. Return value, buffer contents and final file position match.
Byte match: pending (needs VC6 SP5).
