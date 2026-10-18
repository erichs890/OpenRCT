/* Buffered file I/O, RLE stream and checksums of save/scenario files (0x419CB5..0x41A32F). */
#ifndef SAWYER_IO_H
#define SAWYER_IO_H
#include <stdint.h>

#define SAWYER_IO_BUFFER 0x400
#define SAWYER_RLE_MAX 0x7D            /* runs/literal blocks are cut at 125 bytes */

#pragma pack(push, 1)
typedef struct {
    uint8_t checksum_ok;               /* 0x8BF8E2 set by sawyer_check_sc4: base-game checksum */
    uint8_t _unused;                   /* 0x8BF8E3 */
    void *file;                        /* 0x8BF8E4 HANDLE */
    uint8_t *buf_ptr;                  /* 0x8BF8E8 */
    uint16_t buf_count;                /* 0x8BF8EC bytes left (read) / used (write) */
    int8_t rle_state;                  /* 0x8BF8EE read: 0 none, 1 literal, -1 run;
                                          write: 0 empty, 1 one byte, 2 run, other = literals */
    uint8_t write_error;               /* 0x8BF8EF */
    uint8_t buf[SAWYER_IO_BUFFER];     /* 0x8BF8F0 */
    uint8_t *literal_ptr;              /* 0x8BFCF0 last literal in rle_buf (write) */
    uint8_t rle_buf[0x80];             /* 0x8BFCF4 [0] is the run byte; literals start at [0] */
    uint8_t rle_count;                 /* 0x8BFD74 */
    uint32_t checksum;                 /* 0x8BFD75 running checksum of written bytes */
} SawyerIO;
#pragma pack(pop)

_Static_assert(sizeof(SawyerIO) == 0x8BFD79 - 0x8BF8E2, "SawyerIO layout");

extern SawyerIO g_sawyer_io;   /* 0x8BF8E2 */
extern uint32_t g_io_busy;     /* 0xC3B59C: 3 while a file call is in flight */
extern uint8_t g_checksum_negate; /* 0x88B2C0: when set, SV4 offsets are subtracted (meaning unknown) */

/* reading */
uint8_t sawyer_read_byte(void);                       /* 0x419F53 */
uint8_t sawyer_rle_read_byte(void);                   /* 0x419FF6 */
void sawyer_rle_read(uint8_t *dst, uint32_t n);       /* 0x419FEB */
void sawyer_read_rewind(void);                        /* 0x419FB6 */

/* writing */
void sawyer_write_begin(void);                        /* 0x419CC0 */
void sawyer_write_byte(uint8_t b);                    /* 0x419CEC */
void sawyer_rle_write_byte(uint8_t b);                /* 0x419DB1 */
void sawyer_rle_flush(void);                          /* 0x419EDE */
void sawyer_write_end(void);                          /* 0x419D6B */
void sawyer_rle_write(const uint8_t *src, uint32_t n); /* 0x419CB5 */

/* checksums: sum = for each byte { low8 += b; rol 3 } over the file minus its last dword */
void sawyer_write_checksum_sv4(void);                 /* 0x41A05F */
void sawyer_write_checksum_sc4(void);                 /* 0x41A0AB */
int sawyer_check_sv4(void);                           /* 0x41A0E4 original: CF clear = valid */
int sawyer_check_sc4(void);                           /* 0x41A215 original: CF clear = valid */

#endif
