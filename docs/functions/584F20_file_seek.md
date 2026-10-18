# 0x584F20 / 0x584F37 / 0x584F4E `file_seek_set / _cur / _end`

| | |
|---|---|
| Section / kind | VC6 C (cdecl) |
| Contract | `uint32_t f(HANDLE, int32_t pos)`, returning the new position |
| Source | [src/platform_file.c](../../src/platform_file.c) |
| REA | `ev_5411a60ec46629ea159efe3e6e0d2a3bb2f2dec2109b7d20699955ba1f622f24` (Ghidra types them `void`, but the asm callers use `eax` after `seek_end` as the file size) |

## Behaviour (observed)

`return SetFilePointer(h, pos, NULL, FILE_BEGIN / FILE_CURRENT / FILE_END)`. The compiled code is `push ebp; mov ebp, esp; … call [SetFilePointer]; pop ebp; ret`: the frame pointer is kept, so it was not built with /Oy.

## Verification

`test_write_seek`: 4000 random seeks of each kind on 200 files; return values match.
