#include "sawyer_io.h"
#include "platform_file.h"

#define SV4_BASE 120001u   /* 0x1D4C1 */
#define SV4_ALT1 108156u   /* 0x1A67C */
#define SV4_ALT2 110001u   /* 0x1ADB1 */
#define SC4_ALT2 108112u   /* 0x1A650 */

static uint32_t checksum_step(uint32_t sum, uint8_t b)
{
    sum = (sum & ~0xFFu) | (uint8_t)(sum + b);   /* add al, b (no carry into ah) */
    return sum << 3 | sum >> 29;                  /* rol 3 */
}

/* ---- reading ---- */

/* 0x419F53. Next byte of the file through the 1 KiB buffer. A short read
   still marks the buffer full, so past EOF it returns stale buffer bytes. */
uint8_t sawyer_read_byte(void)
{
    SawyerIO *io = &g_sawyer_io;
    if (io->buf_count == 0) {
        g_io_busy = 3;
        file_read(io->file, io->buf, SAWYER_IO_BUFFER);
        g_io_busy = 0;
        io->buf_count = SAWYER_IO_BUFFER;
        io->buf_ptr = io->buf;
    }
    io->buf_count--;
    return *io->buf_ptr++;
}

/* 0x419FF6. RLE decode: a control byte n >= 0 is followed by n+1 literal
   bytes; n < 0 is followed by one byte repeated 1-n times. Counts are 8-bit,
   so n = -128 gives 129 repeats (neg 0x80 == 0x80). */
uint8_t sawyer_rle_read_byte(void)
{
    SawyerIO *io = &g_sawyer_io;
    int8_t n;
    if (io->rle_state != 0 && io->rle_count != 0) {
        io->rle_count--;
        return io->rle_state > 0 ? sawyer_read_byte() : io->rle_buf[0];
    }
    n = (int8_t)sawyer_read_byte();
    if (n >= 0) {
        io->rle_count = (uint8_t)n;
        io->rle_state = 1;
        return sawyer_read_byte();
    }
    io->rle_state = -1;
    io->rle_count = (uint8_t)-n;
    io->rle_buf[0] = sawyer_read_byte();
    return io->rle_buf[0];
}

/* 0x419FEB. Original: esi = dst, ecx = n, `loop`, so n == 0 means 2^32. */
void sawyer_rle_read(uint8_t *dst, uint32_t n)
{
    do {
        *dst++ = sawyer_rle_read_byte();
    } while (--n);
}

/* 0x419FB6. Back to the start of the file, empty buffer and RLE state. */
void sawyer_read_rewind(void)
{
    g_io_busy = 3;
    file_seek_set(g_sawyer_io.file, 0);
    g_io_busy = 0;
    g_sawyer_io.rle_state = 0;
    g_sawyer_io.buf_count = 0;
}

/* ---- writing ---- */

static void write_buffer(uint32_t n)
{
    SawyerIO *io = &g_sawyer_io;
    g_io_busy = 3;
    uint32_t written = file_write(io->file, io->buf, n);
    g_io_busy = 0;
    if (written != n)
        io->write_error = 1;
}

/* 0x419CC0 */
void sawyer_write_begin(void)
{
    SawyerIO *io = &g_sawyer_io;
    io->buf_ptr = io->buf;
    io->buf_count = 0;
    io->rle_state = 0;
    io->write_error = 0;
    io->checksum = 0;
}

/* 0x419CEC. Buffered write of one raw byte; updates the running checksum. */
void sawyer_write_byte(uint8_t b)
{
    SawyerIO *io = &g_sawyer_io;
    if (io->buf_count == SAWYER_IO_BUFFER) {
        write_buffer(SAWYER_IO_BUFFER);
        io->buf_ptr = io->buf;
        io->buf_count = 0;
    }
    *io->buf_ptr++ = b;
    io->checksum = checksum_step(io->checksum, b);
    io->buf_count++;
}

/* Control byte count-1, then rle_buf[0..count) ; leaves rle_count = 0.
   `dec byte / jne`: a count of 0 writes 256 bytes. */
static void emit_literals(void)
{
    SawyerIO *io = &g_sawyer_io;
    const uint8_t *p = io->rle_buf;
    sawyer_write_byte((uint8_t)(io->rle_count - 1));
    do {
        sawyer_write_byte(*p++);
    } while (--io->rle_count);
}

/* Control byte 1-count, then the run byte. rle_count is left as is. */
static void emit_run(void)
{
    sawyer_write_byte((uint8_t)(1 - g_sawyer_io.rle_count));
    sawyer_write_byte(g_sawyer_io.rle_buf[0]);
}

/* 0x419DB1. RLE encoder state machine (see the doc for the state table). */
void sawyer_rle_write_byte(uint8_t b)
{
    SawyerIO *io = &g_sawyer_io;
    switch ((uint8_t)io->rle_state) {
    case 0:
        io->rle_buf[0] = b;
        io->rle_state = 1;
        return;
    case 1:
        if (b == io->rle_buf[0]) {
            io->rle_count = 2;
            io->rle_state = 2;
        } else {
            io->rle_buf[1] = b;
            io->literal_ptr = &io->rle_buf[1];
            io->rle_count = 2;
            io->rle_state = 3;
        }
        return;
    case 2:
        if (b == io->rle_buf[0] && io->rle_count < SAWYER_RLE_MAX) {
            io->rle_count++;
            return;
        }
        emit_run();
        io->rle_buf[0] = b;
        io->rle_state = 1;
        return;
    default:
        if (io->rle_count >= SAWYER_RLE_MAX) {
            emit_literals();
            io->rle_buf[0] = b;
            io->rle_state = 1;
        } else if (b == *io->literal_ptr) {
            /* the last literal and b start a run */
            io->rle_count--;
            emit_literals();
            io->rle_buf[0] = b;
            io->rle_state = 2;
            io->rle_count = 2;
        } else {
            *++io->literal_ptr = b;
            io->rle_count++;
        }
        return;
    }
}

/* 0x419EDE. Emit whatever the encoder still holds. */
void sawyer_rle_flush(void)
{
    SawyerIO *io = &g_sawyer_io;
    switch ((uint8_t)io->rle_state) {
    case 0:
        return;
    case 1:
        sawyer_write_byte(0);
        sawyer_write_byte(io->rle_buf[0]);
        break;
    case 2:
        emit_run();
        break;
    default:
        emit_literals();
        break;
    }
    io->rle_state = 0;
}

/* 0x419D6B. Flush the encoder and the partial buffer (buf_count is not reset). */
void sawyer_write_end(void)
{
    sawyer_rle_flush();
    if (g_sawyer_io.buf_count)
        write_buffer(g_sawyer_io.buf_count);
}

/* 0x419CB5. Original: esi = src, ecx = n, `loop`, so n == 0 means 2^32. */
void sawyer_rle_write(const uint8_t *src, uint32_t n)
{
    do {
        sawyer_rle_write_byte(*src++);
    } while (--n);
}

/* ---- checksums ---- */

static void write_dword(uint32_t v)
{
    SawyerIO *io = &g_sawyer_io;
    *(uint32_t *)io->buf = v;
    g_io_busy = 3;
    file_write(io->file, io->buf, 4);
    g_io_busy = 0;
}

/* 0x41A05F */
void sawyer_write_checksum_sv4(void)
{
    uint32_t v = g_sawyer_io.checksum + SV4_BASE;
    if (g_checksum_negate)
        v -= 2 * SV4_BASE;
    write_dword(v);
}

/* 0x41A0AB */
void sawyer_write_checksum_sc4(void)
{
    write_dword(g_sawyer_io.checksum - SV4_BASE);
}

/* Shared body of 0x41A0E4 / 0x41A215: file size via seek_end, then the sum of
   all bytes but the last 4 (read in 1 KiB chunks through buf), then that dword. */
static int file_sum(uint32_t min_size, uint32_t *sum, uint32_t *stored)
{
    SawyerIO *io = &g_sawyer_io;
    g_io_busy = 3;
    uint32_t n = file_seek_end(io->file, 0);
    g_io_busy = 0;
    if (n < min_size)
        return 0;
    n -= 4;
    g_io_busy = 3;
    file_seek_set(io->file, 0);
    g_io_busy = 0;
    uint32_t s = 0;
    while (n) {
        uint32_t chunk = n > SAWYER_IO_BUFFER ? SAWYER_IO_BUFFER : n;
        g_io_busy = 3;
        file_read(io->file, io->buf, chunk);
        g_io_busy = 0;
        for (uint32_t i = 0; i < chunk; i++)
            s = checksum_step(s, io->buf[i]);
        n -= chunk;
    }
    g_io_busy = 3;
    file_read(io->file, io->buf, 4);
    g_io_busy = 0;
    *sum = s;
    *stored = *(uint32_t *)io->buf;
    return 1;
}

/* 0x41A0E4. Saved game: sum + 120001, 108156 or 110001 (three game versions,
   inferred); subtracted instead when g_checksum_negate is set. */
int sawyer_check_sv4(void)
{
    uint32_t sum, stored;
    if (!file_sum(8, &sum, &stored))
        return 0;
    if (g_checksum_negate)
        return stored == sum - SV4_BASE || stored == sum - SV4_ALT1 || stored == sum - SV4_ALT2;
    return stored == sum + SV4_BASE || stored == sum + SV4_ALT1 || stored == sum + SV4_ALT2;
}

/* 0x41A215. Scenario: sum - 120001, 108156 or 108112. checksum_ok is set only
   for the first one. */
int sawyer_check_sc4(void)
{
    uint32_t sum, stored;
    g_sawyer_io.checksum_ok = 0;
    if (!file_sum(0xE, &sum, &stored))
        return 0;
    g_sawyer_io.checksum_ok = stored == sum - SV4_BASE;
    return stored == sum - SV4_BASE || stored == sum - SV4_ALT1 || stored == sum - SC4_ALT2;
}
