# 0x585000 `file_write`

| | |
|---|---|
| Section / kind | VC6 C (cdecl) |
| Contract | `uint32_t file_write(HANDLE, const void *, uint32_t)` |
| Source | [src/platform_file.c](../../src/platform_file.c) |
| REA | `ev_5411a60ec46629ea159efe3e6e0d2a3bb2f2dec2109b7d20699955ba1f622f24` |

## Behaviour (observed)

Returns the bytes written, or `0xFFFFFFFF` if `WriteFile` fails (`test eax,eax; je; mov eax,[ebp-4]; jmp; or eax,-1`).

## Verification

`test_write_seek`: 200 random writes of 0–5000 bytes plus an invalid handle; return values and file contents match.
