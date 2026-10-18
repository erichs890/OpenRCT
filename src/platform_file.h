/* Win32 file wrappers (VC6-compiled C, 0x584E30..0x5850C0). */
#ifndef PLATFORM_FILE_H
#define PLATFORM_FILE_H
#include <stdint.h>

uint32_t file_seek_set(void *handle, int32_t pos);           /* 0x584F20 */
uint32_t file_seek_cur(void *handle, int32_t pos);           /* 0x584F37 */
uint32_t file_seek_end(void *handle, int32_t pos);           /* 0x584F4E */
uint32_t file_read(void *handle, void *buf, uint32_t len);   /* 0x584F65 */
uint32_t file_write(void *handle, const void *buf, uint32_t len); /* 0x585000 */

#endif
