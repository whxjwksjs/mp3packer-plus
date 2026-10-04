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
 * mp3huffman.c -- MP3 Huffman codec for big-value pairs and count1 quads.
 *
 * Clean-room implementation per SPEC.md section 6 and MODULE_MAP.md
 * Appendix C.3. Codebooks are the ISO 11172-3 tables verbatim
 * (see mp3tables.h).
 *
 * Bit order within a pair codeword (matches the reference):
 *   codeword, x linbits (if |x| >= 15, tables 16-31 only),
 *   x sign (if x != 0), y linbits (if |y| >= 15),
 *   y sign (if y != 0).  Sign bit 1 = negative.
 */

#include <limits.h>
#include <stddef.h>

#include "mp3huffman.h"
#include "mp3tables.h"

/* Cost sentinel for "cannot encode with this table". */
#define HUFF_IMPOSSIBLE (INT_MAX / 2)

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/* Longest codeword in a pair table (for the decode peek window). */
static int pair_maxbits(const huff_pair_t *tab, int n)
{
    int m = 0;
    int i;
    for (i = 0; i < n; i++) {
        if ((int)tab[i].bits > m) {
            m = tab[i].bits;
        }
    }
    return m;
}

static int quad_maxbits(const huff_quad_t *tab, int n)
{
    int m = 0;
    int i;
    for (i = 0; i < n; i++) {
        if ((int)tab[i].bits > m) {
            m = tab[i].bits;
        }
    }
    return m;
}

/* Find pair-table entry index for grid values (lx, ly); -1 if absent. */
static int pair_lookup(const huff_pair_t *tab, int n, int lx, int ly)
{
    int i;
    for (i = 0; i < n; i++) {
        if ((int)tab[i].x == lx && (int)tab[i].y == ly) {
            return i;
        }
    }
    return -1;
}

/* Find quad-table entry index for magnitudes (v0..v3); -1 if absent. */
static int quad_lookup(const huff_quad_t *tab, int n,
                       int v0, int v1, int v2, int v3)
{
    int i;
    for (i = 0; i < n; i++) {
        if (tab[i].v0 == v0 && tab[i].v1 == v1 &&
            tab[i].v2 == v2 && tab[i].v3 == v3) {
            return i;
        }
    }
    return -1;
}

/* Match a codeword against the upcoming bits.
 * Returns the entry index, or -1 if nothing matches.
 * `maxbits` must be <= 32. Consumes nothing; caller skips tab[idx].bits.
 */
static int pair_match(const huff_pair_t *tab, int n,
                      bit_reader_t *r, int maxbits)
{
    uint32_t peeked = bit_peek(r, maxbits);
    int i;
    for (i = 0; i < n; i++) {
        int b = tab[i].bits;
        /* top b bits of the peek window vs the right-aligned code */
        if ((peeked >> (maxbits - b)) == tab[i].code) {
            return i;
        }
    }
    return -1;
}

static int quad_match(const huff_quad_t *tab, int n,
                      bit_reader_t *r, int maxbits)
{
    uint32_t peeked = bit_peek(r, maxbits);
    int i;
    for (i = 0; i < n; i++) {
        int b = tab[i].bits;
        if ((peeked >> (maxbits - b)) == tab[i].code) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Decode big_values pairs                                            */
/* ------------------------------------------------------------------ */

int huff_decode_big(bit_reader_t *r, int table, int16_t *out, int big_values)
{
    const huff_pair_t *tab;
    int n;
    int maxbits;
    int linbits;
    size_t start;
    int p;

    if (big_values < 0) {
        return -1;
    }
    if (table == 0) {
        /* Table 0: no bits, all zeros. */
        for (p = 0; p < 2 * big_values; p++) {
            out[p] = 0;
        }
        return 0;
    }
    if (table < 1 || table > 31) {
        return -1;
    }
    tab = huff_tables[table];
    n = huff_table_sizes[table];
    if (tab == NULL || n == 0) {
        return -1;  /* tables 4 and 14 don't exist */
    }

    maxbits = pair_maxbits(tab, n);
    if (maxbits <= 0 || maxbits > 32) {
        return -1;  /* corrupt table data; peek window must fit u32 */
    }
    linbits = huff_linbits[table];
    start = bit_reader_tell(r);

    for (p = 0; p < big_values; p++) {
        int idx = pair_match(tab, n, r, maxbits);
        int x, y;

        if (idx < 0) {
            return -1;  /* no codeword matches: corrupt stream */
        }
        bit_reader_skip(r, tab[idx].bits);

        x = tab[idx].x;
        y = tab[idx].y;

        /* Linbits escape, then sign bit, per value: x first, then y. */
        if (linbits > 0 && x == 15) {
            x += (int)bit_read(r, linbits);
        }
        if (x != 0) {
            if (bit_read(r, 1)) {
                x = -x;
            }
        }
        if (linbits > 0 && y == 15) {
            y += (int)bit_read(r, linbits);
        }
        if (y != 0) {
            if (bit_read(r, 1)) {
                y = -y;
            }
        }

        out[2 * p] = (int16_t)x;
        out[2 * p + 1] = (int16_t)y;
    }

    return (int)(bit_reader_tell(r) - start);
}

/* ------------------------------------------------------------------ */
/* Decode count1 quads                                                */
/* ------------------------------------------------------------------ */

int huff_decode_count1(bit_reader_t *r, int table_sel, int16_t *out, int count)
{
    const huff_quad_t *tab;
    int n;
    int maxbits;
    size_t start;
    int q;
    int overboard_nonzero = 0;

    if (count < 0 || table_sel < 0 || table_sel > 1) {
        return -1;
    }
    tab = huff_count1[table_sel];
    n = huff_count1_sizes[table_sel];
    if (tab == NULL || n == 0) {
        return -1;
    }

    maxbits = quad_maxbits(tab, n);
    if (maxbits <= 0 || maxbits > 32) {
        return -1;
    }
    start = bit_reader_tell(r);

    for (q = 0; q < count; q++) {
        int idx = quad_match(tab, n, r, maxbits);
        int v[4];
        int k;

        if (idx < 0) {
            return -1;
        }
        bit_reader_skip(r, tab[idx].bits);

        v[0] = tab[idx].v0;
        v[1] = tab[idx].v1;
        v[2] = tab[idx].v2;
        v[3] = tab[idx].v3;

        for (k = 0; k < 4; k++) {
            int pos = 4 * q + k;
            int val = v[k];
            if (val != 0) {
                if (bit_read(r, 1)) {
                    val = -val;
                }
            }
            /* Overboard rule: nonzero values at/after sample 576
             * indicate a corrupt stream. Still write them (caller
             * provides the extra space) so bit accounting stays exact,
             * but report failure at the end. */
            if (pos >= 576 && val != 0) {
                overboard_nonzero = 1;
            }
            out[pos] = (int16_t)val;
        }
    }

    if (overboard_nonzero) {
        return -1;
    }
    return (int)(bit_reader_tell(r) - start);
}

/* ------------------------------------------------------------------ */
/* Encode big_values pairs                                            */
/* ------------------------------------------------------------------ */

int huff_encode_big(bit_writer_t *w, int table, const int16_t *in, int big_values)
{
    const huff_pair_t *tab;
    int n;
    int linbits;
    int maxv;  /* largest magnitude encodable with this table */
    size_t start;
    int p;

    if (big_values < 0) {
        return -1;
    }
    if (table == 0) {
        /* Table 0 writes no bits; input must be all zeros. */
        for (p = 0; p < 2 * big_values; p++) {
            if (in[p] != 0) {
                return -1;
            }
        }
        return 0;
    }
    if (table < 1 || table > 31) {
        return -1;
    }
    tab = huff_tables[table];
    n = huff_table_sizes[table];
    if (tab == NULL || n == 0) {
        return -1;
    }

    linbits = huff_linbits[table];
    if (linbits > 0) {
        maxv = 15 + ((1 << linbits) - 1);
    } else {
        /* largest grid value present in the table */
        int i;
        maxv = 0;
        for (i = 0; i < n; i++) {
            if ((int)tab[i].x > maxv) maxv = tab[i].x;
            if ((int)tab[i].y > maxv) maxv = tab[i].y;
        }
    }

    start = bit_writer_tell(w);

    for (p = 0; p < big_values; p++) {
        int x = in[2 * p];
        int y = in[2 * p + 1];
        int ax = x < 0 ? -x : x;
        int ay = y < 0 ? -y : y;
        int lx, ly;  /* grid coordinates for the codeword lookup */
        int idx;

        if (ax > maxv || ay > maxv) {
            return -1;
        }
        if (linbits > 0) {
            lx = ax >= 15 ? 15 : ax;
            ly = ay >= 15 ? 15 : ay;
        } else {
            lx = ax;
            ly = ay;
        }

        idx = pair_lookup(tab, n, lx, ly);
        if (idx < 0) {
            return -1;
        }

        bit_write(w, tab[idx].code, tab[idx].bits);
        /* x escape + sign, then y escape + sign */
        if (linbits > 0 && ax >= 15) {
            bit_write(w, (uint32_t)(ax - 15), linbits);
        }
        if (x != 0) {
            bit_write(w, x < 0 ? 1u : 0u, 1);
        }
        if (linbits > 0 && ay >= 15) {
            bit_write(w, (uint32_t)(ay - 15), linbits);
        }
        if (y != 0) {
            bit_write(w, y < 0 ? 1u : 0u, 1);
        }
    }

    return (int)(bit_writer_tell(w) - start);
}

/* ------------------------------------------------------------------ */
/* Encode count1 quads                                                */
/* ------------------------------------------------------------------ */

int huff_encode_count1(bit_writer_t *w, int table_sel, const int16_t *in, int count)
{
    const huff_quad_t *tab;
    int n;
    size_t start;
    int q;

    if (count < 0 || table_sel < 0 || table_sel > 1) {
        return -1;
    }
    tab = huff_count1[table_sel];
    n = huff_count1_sizes[table_sel];
    if (tab == NULL || n == 0) {
        return -1;
    }

    start = bit_writer_tell(w);

    for (q = 0; q < count; q++) {
        int v[4];
        int s[4];
        int k;
        int idx;

        for (k = 0; k < 4; k++) {
            int val = in[4 * q + k];
            if (val < -1 || val > 1) {
                return -1;  /* count1 values are -1, 0, or 1 */
            }
            v[k] = val < 0 ? -val : val;
            s[k] = val < 0 ? 1 : 0;
        }

        idx = quad_lookup(tab, n, v[0], v[1], v[2], v[3]);
        if (idx < 0) {
            return -1;
        }

        bit_write(w, tab[idx].code, tab[idx].bits);
        for (k = 0; k < 4; k++) {
            if (v[k] != 0) {
                bit_write(w, (uint32_t)s[k], 1);
            }
        }
    }

    return (int)(bit_writer_tell(w) - start);
}

/* ------------------------------------------------------------------ */
/* Bit-cost estimation (no output written)                            */
/* ------------------------------------------------------------------ */

int huff_cost_big(int table, const int16_t *in, int big_values)
{
    const huff_pair_t *tab;
    int n;
    int linbits;
    int maxv;
    long total = 0;
    int p;

    if (big_values < 0) {
        return HUFF_IMPOSSIBLE;
    }
    if (table == 0) {
        for (p = 0; p < 2 * big_values; p++) {
            if (in[p] != 0) {
                return HUFF_IMPOSSIBLE;
            }
        }
        return 0;
    }
    if (table < 1 || table > 31) {
        return HUFF_IMPOSSIBLE;
    }
    tab = huff_tables[table];
    n = huff_table_sizes[table];
    if (tab == NULL || n == 0) {
        return HUFF_IMPOSSIBLE;
    }

    linbits = huff_linbits[table];
    if (linbits > 0) {
        maxv = 15 + ((1 << linbits) - 1);
    } else {
        int i;
        maxv = 0;
        for (i = 0; i < n; i++) {
            if ((int)tab[i].x > maxv) maxv = tab[i].x;
            if ((int)tab[i].y > maxv) maxv = tab[i].y;
        }
    }

    for (p = 0; p < big_values; p++) {
        int x = in[2 * p];
        int y = in[2 * p + 1];
        int ax = x < 0 ? -x : x;
        int ay = y < 0 ? -y : y;
        int lx, ly;
        int idx;

        if (ax > maxv || ay > maxv) {
            return HUFF_IMPOSSIBLE;
        }
        if (linbits > 0) {
            lx = ax >= 15 ? 15 : ax;
            ly = ay >= 15 ? 15 : ay;
        } else {
            lx = ax;
            ly = ay;
        }
        idx = pair_lookup(tab, n, lx, ly);
        if (idx < 0) {
            return HUFF_IMPOSSIBLE;
        }

        total += tab[idx].bits;
        if (linbits > 0) {
            if (ax >= 15) total += linbits;
            if (ay >= 15) total += linbits;
        }
        if (x != 0) total += 1;
        if (y != 0) total += 1;
        if (total >= HUFF_IMPOSSIBLE) {
            return HUFF_IMPOSSIBLE;
        }
    }

    return (int)total;
}

int huff_cost_count1(int table_sel, const int16_t *in, int count)
{
    const huff_quad_t *tab;
    int n;
    long total = 0;
    int q;

    if (count < 0 || table_sel < 0 || table_sel > 1) {
        return HUFF_IMPOSSIBLE;
    }
    tab = huff_count1[table_sel];
    n = huff_count1_sizes[table_sel];
    if (tab == NULL || n == 0) {
        return HUFF_IMPOSSIBLE;
    }

    for (q = 0; q < count; q++) {
        int v[4];
        int k;
        int idx;

        for (k = 0; k < 4; k++) {
            int val = in[4 * q + k];
            if (val < -1 || val > 1) {
                return HUFF_IMPOSSIBLE;
            }
            v[k] = val < 0 ? -val : val;
        }
        idx = quad_lookup(tab, n, v[0], v[1], v[2], v[3]);
        if (idx < 0) {
            return HUFF_IMPOSSIBLE;
        }
        total += tab[idx].bits;
        for (k = 0; k < 4; k++) {
            if (v[k] != 0) {
                total += 1;
            }
        }
        if (total >= HUFF_IMPOSSIBLE) {
            return HUFF_IMPOSSIBLE;
        }
    }

    return (int)total;
}
