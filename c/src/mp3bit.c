/*******************************************************************************
 * This file is a part of mp3packer-plus.
 *
 * mp3packer-plus is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mp3packer-plus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mp3packer-plus; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *******************************************************************************
 *
 * mp3bit.c -- Big-endian bitstream reader and writer.
 *
 * Bit numbering matches the MP3 spec: bit 0 is the MSB of byte 0.
 * Bit position p addresses byte (p / 8), bit (7 - (p % 8)) within it.
 *
 * All reads/writes work at arbitrary bit alignment. Reads past the end
 * of the buffer return zero bits (no crash). Writes past capacity are
 * silently dropped (no buffer overflow); callers must size buffers.
 */

#include "mp3bit.h"

/* ------------------------------------------------------------------ */
/* Reader                                                             */
/* ------------------------------------------------------------------ */

void bit_reader_init(bit_reader_t *r, const uint8_t *data, size_t len)
{
    r->data = data;
    r->len = len;
    r->bitpos = 0;
}

/* Read n bits MSB-first. Bits beyond the end of the buffer read as 0. */
uint32_t bit_read(bit_reader_t *r, int nbits)
{
    uint32_t val = 0;
    int i;

    for (i = 0; i < nbits; i++) {
        size_t p = r->bitpos++;
        size_t byte = p >> 3;

        val <<= 1;
        if (byte < r->len) {
            /* bit 0 of the stream = MSB of byte 0 */
            val |= (uint32_t)((r->data[byte] >> (7 - (p & 7))) & 1u);
        }
        /* else: zero-pad */
    }
    return val;
}

uint32_t bit_peek(bit_reader_t *r, int nbits)
{
    size_t saved = r->bitpos;
    uint32_t val = bit_read(r, nbits);
    r->bitpos = saved;
    return val;
}

void bit_reader_skip(bit_reader_t *r, int nbits)
{
    if (nbits > 0) {
        r->bitpos += (size_t)nbits;
    }
}

size_t bit_reader_tell(const bit_reader_t *r)
{
    return r->bitpos;
}

void bit_reader_seek(bit_reader_t *r, size_t bitpos)
{
    r->bitpos = bitpos;
}

int bit_reader_eof(const bit_reader_t *r)
{
    return r->bitpos >= r->len * 8;
}

/* ------------------------------------------------------------------ */
/* Writer                                                             */
/* ------------------------------------------------------------------ */

void bit_writer_init(bit_writer_t *w, uint8_t *data, size_t cap)
{
    w->data = data;
    w->cap = cap;
    w->bitpos = 0;
}

/* Write the low nbits of value, MSB-first, at the current position.
 * Bits beyond capacity are silently dropped. The destination buffer
 * does not need to be pre-zeroed. */
void bit_write(bit_writer_t *w, uint32_t value, int nbits)
{
    int i;

    for (i = nbits - 1; i >= 0; i--) {
        size_t p = w->bitpos++;
        size_t byte = p >> 3;
        uint8_t mask;

        if (byte >= w->cap) {
            continue; /* drop: no room left */
        }
        mask = (uint8_t)(1u << (7 - (p & 7)));
        if ((value >> i) & 1u) {
            w->data[byte] |= mask;
        } else {
            w->data[byte] &= (uint8_t)~mask;
        }
    }
}

size_t bit_writer_tell(const bit_writer_t *w)
{
    return w->bitpos;
}

size_t bit_writer_bytes(const bit_writer_t *w)
{
    return (w->bitpos + 7) / 8;
}
