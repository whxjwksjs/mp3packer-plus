#ifndef MP3BIT_H
#define MP3BIT_H

#include <stdint.h>
#include <stddef.h>

/* Bit 0 = MSB of byte 0 (big-endian bit numbering, matches MP3 spec) */

typedef struct {
    const uint8_t *data;
    size_t len;         /* in bytes */
    size_t bitpos;      /* current bit position */
} bit_reader_t;

typedef struct {
    uint8_t *data;
    size_t cap;         /* in bytes */
    size_t bitpos;
} bit_writer_t;

void bit_reader_init(bit_reader_t *r, const uint8_t *data, size_t len);

/* Read up to 32 bits. Zero-pads past end of buffer. */
uint32_t bit_read(bit_reader_t *r, int nbits);

/* Peek without advancing */
uint32_t bit_peek(bit_reader_t *r, int nbits);

void bit_reader_skip(bit_reader_t *r, int nbits);
size_t bit_reader_tell(const bit_reader_t *r);  /* in bits */
void bit_reader_seek(bit_reader_t *r, size_t bitpos);
int bit_reader_eof(const bit_reader_t *r);

void bit_writer_init(bit_writer_t *w, uint8_t *data, size_t cap);
void bit_write(bit_writer_t *w, uint32_t value, int nbits);
size_t bit_writer_tell(const bit_writer_t *w);  /* in bits */
size_t bit_writer_bytes(const bit_writer_t *w); /* bytes written (rounded up) */

#endif
