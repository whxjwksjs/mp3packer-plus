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
 * mp3huffman.h -- MP3 Huffman codec for big-value pairs and count1 quads.
 *
 * Implements the ISO 11172-3 Huffman codebooks used by mp3packer:
 *   - Pair tables 1..31 (ht4/ht14 don't exist; 17-23 alias 16, 25-31 alias 24)
 *   - Count1 quad tables A and B
 *   - Table 0 = "no bits, all zeros"
 *
 * Tables come from mp3tables.h. Bit I/O uses mp3bit.h (bit 0 = MSB).
 */

#ifndef MP3HUFFMAN_H
#define MP3HUFFMAN_H

#include <stdint.h>
#include "mp3bit.h"

/* Decode big_values pairs from bitstream.
 * table: 1..31 (0 = all zeros, no bits read)
 * out: int16 array, stores 2*big_values samples
 * Returns number of bits consumed, or -1 on error.
 */
int huff_decode_big(bit_reader_t *r, int table, int16_t *out, int big_values);

/* Decode count1 quadruples.
 * table_sel: 0 = table A, 1 = table B
 * out: int16 array, stores 4*count samples (may write past 576, caller handles)
 * Returns number of bits consumed, or -1 on error.
 *
 * Overboard rule: values at out[i] with i >= 576 must be zero;
 * a nonzero value there signals a corrupt stream (returns -1).
 */
int huff_decode_count1(bit_reader_t *r, int table_sel, int16_t *out, int count);

/* Encode big_values pairs to bitstream.
 * in: int16 array with 2*big_values samples
 * Returns number of bits written, or -1 if a value can't be
 * represented with the given table.
 */
int huff_encode_big(bit_writer_t *w, int table, const int16_t *in, int big_values);

/* Encode count1 quadruples.
 * in: int16 array, 4*count samples (each must be -1, 0, or 1)
 * Returns number of bits written, or -1 on invalid input.
 */
int huff_encode_count1(bit_writer_t *w, int table_sel, const int16_t *in, int count);

/* Compute bit cost of encoding with the given table (without writing).
 * Used by the -z optimizer to try different tables.
 * Returns INT_MAX/2 (effectively infinite) if unencodable.
 */
int huff_cost_big(int table, const int16_t *in, int big_values);
int huff_cost_count1(int table_sel, const int16_t *in, int count);

#endif /* MP3HUFFMAN_H */
