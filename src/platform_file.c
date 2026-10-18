#include <windows.h>
#include "platform_file.h"

/* 0x584F20 / 0x584F37 / 0x584F4E: SetFilePointer, returning the new position
   (the asm uses eax after seek_end as the file size). */
uint32_t file_seek_set(void *handle, int32_t pos) { return SetFilePointer(handle, pos, NULL, FILE_BEGIN); }
uint32_t file_seek_cur(void *handle, int32_t pos) { return SetFilePointer(handle, pos, NULL, FILE_CURRENT); }
uint32_t file_seek_end(void *handle, int32_t pos) { return SetFilePointer(handle, pos, NULL, FILE_END); }

/* 0x584F65. len == 0xFFFFFFFF reads from the current position to EOF.
   Returns the byte count, or 0xFFFFFFFF if ReadFile fails. */
uint32_t file_read(void *handle, void *buf, uint32_t len)
{
    DWORD read;
    if (len == 0xFFFFFFFF) {
        DWORD cur = SetFilePointer(handle, 0, NULL, FILE_CURRENT);
        DWORD end = SetFilePointer(handle, 0, NULL, FILE_END);
        SetFilePointer(handle, cur, NULL, FILE_BEGIN);
        if (!ReadFile(handle, buf, end - cur, &read, NULL))
            read = 0xFFFFFFFF;
    } else {
        if (!ReadFile(handle, buf, len, &read, NULL))
            read = 0xFFFFFFFF;
    }
    return read;
}

/* 0x585000. Returns the byte count, or 0xFFFFFFFF if WriteFile fails. */
uint32_t file_write(void *handle, const void *buf, uint32_t len)
{
    DWORD written;
    return WriteFile(handle, buf, len, &written, NULL) ? written : 0xFFFFFFFF;
}
