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
 * mp3recomp.c -- The -z Huffman recompression optimizer.
 *
 * Algorithm (per granule/channel, long blocks only):
 *   1. Compute part2 (scalefactor) bit length to locate part3 in the payload.
 *      - MPEG-1: from scalefac_compress via (slen1,slen2) table, honoring SCFI.
 *      - MPEG-2/2.5: via ISO 13818-3 section 2.4.3.4 derived-slen scheme.
 *   2. Huffman-decode part3 to 576 int16 quantized coefficients using the
 *      original table selections from side_info_t.
 *   3. Band-level dynamic program: for each scalefactor band and each of the
 *      32 pair tables, compute the encoding cost. Then search all legal
 *      (region0_bands, region1_bands) splits and pick the cheapest table per
 *      region. Separately try both count1 tables for the tail.
 *   4. Re-encode with the best configuration. Only keep if the new part3 is
 *      strictly smaller than the original part3.
 *
 * Region count encoding (per MP3 spec):
 *   region0_count (4 bits) = (bands in region 0) - 1  -> 1..16 bands
 *   region1_count (3 bits) = (bands in region 1) - 1  -> 1..8 bands
 *   Region boundaries fall on scalefactor band edges, clamped to 2*big_values.
 *
 * Fail-safe: any decode error, invalid table, or unexpected condition causes
 * the granule/channel (or entire frame) to keep its original encoding.
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "mp3recomp.h"
#include "mp3huffman.h"
#include "mp3tables.h"
#include "mp3bit.h"
#include "mp3parse.h"

/* ------------------------------------------------------------------ */
/* Part 1: Scalefactor (part2) bit length computation                  */
/* ------------------------------------------------------------------ */

/* MPEG-1 (slen1, slen2) pairs indexed by scalefac_compress (0-15).
 * From ISO 11172-3 Table B.7. */
static const uint8_t m1_slen[16][2] = {
    {0,0}, {0,1}, {0,2}, {0,3},
    {3,0}, {1,1}, {1,2}, {1,3},
    {2,1}, {2,2}, {2,3}, {3,1},
    {3,2}, {3,3}, {4,2}, {4,3}
};

/* Compute part2 bit length for MPEG-1 long block.
 * Returns -1 if scalefac_compress is out of range.
 *
 * SCFI handling: for granule 1, scalefactors for a band group are omitted
 * when the corresponding SCFI bit is set (reused from granule 0).
 * SCFI bit groups (long blocks):
 *   bit 0: scalefactor indices 0-5   (6 values, slen1)
 *   bit 1: scalefactor indices 6-10  (5 values, slen1)
 *   bit 2: scalefactor indices 11-15 (5 values, slen2)
 *   bit 3: scalefactor indices 16-20 (5 values, slen2)
 *
 * Note on scfi storage: mp3parse.c packs the 4 SCFI bits per channel as
 *   scfi[ch][0] = (b0<<1)|b1, scfi[ch][1] = (b2<<1)|b3.
 */
static int part2_bits_mpeg1(const side_info_t *side, int g, int ch)
{
    int sfc = side->scalefac_compress[g][ch];
    if (sfc < 0 || sfc > 15)
        return -1;

    int slen1 = m1_slen[sfc][0];
    int slen2 = m1_slen[sfc][1];

    if (g == 0) {
        /* Granule 0: all 21 scalefactors transmitted.
         * Indices 0-10 use slen1 (11 values), 11-20 use slen2 (10 values). */
        return 11 * slen1 + 10 * slen2;
    }

    /* Granule 1: skip SCFI-reused groups. */
    int scfi0 = (side->scfi[ch][0] >> 1) & 1;  /* indices 0-5 */
    int scfi1 = side->scfi[ch][0] & 1;         /* indices 6-10 */
    int scfi2 = (side->scfi[ch][1] >> 1) & 1;  /* indices 11-15 */
    int scfi3 = side->scfi[ch][1] & 1;         /* indices 16-20 */

    int bits = 0;
    if (!scfi0) bits += 6 * slen1;
    if (!scfi1) bits += 5 * slen1;
    if (!scfi2) bits += 5 * slen2;
    if (!scfi3) bits += 5 * slen2;
    return bits;
}

/* MPEG-2/2.5 LSF scalefactor scheme (ISO 13818-3 section 2.4.3.4).
 *
 * Band counts per scalefactor group, indexed [blocknumber][blocktype][group].
 * blocktype: 0=long, 1=short, 2=mixed. We only use long (0).
 * For long blocks a group's count is scalefactor bands.
 */
static const uint8_t lsf_nr_of_sfb[6][3][4] = {
    {{ 6, 5, 5, 5}, { 9, 9, 9, 9}, { 6, 9, 9, 9}},
    {{ 6, 5, 7, 3}, { 9, 9,12, 6}, { 6, 9,12, 6}},
    {{11,10, 0, 0}, {18,18, 0, 0}, {15,18, 0, 0}},
    {{ 7, 7, 7, 0}, {12,12,12, 0}, { 6,15,12, 0}},
    {{ 6, 6, 6, 3}, {12, 9, 9, 6}, { 6,12, 9, 6}},
    {{ 8, 8, 5, 0}, {15,12, 9, 0}, { 6,18, 9, 0}}
};

/* Compute part2 bit length for MPEG-2/2.5 long block (non-intensity).
 * Returns -1 if scalefac_compress is out of range (>511).
 *
 * Derived-slen scheme:
 *   sfc < 400: slen = [(sfc>>4)/5, (sfc>>4)%5, (sfc%16)>>2, sfc%4], block=0
 *   sfc < 500: slen = [((sfc-400)>>2)/5, ((sfc-400)>>2)%5, (sfc-400)%4, 0], block=1
 *   else:      slen = [(sfc-500)/3, (sfc-500)%3, 0, 0], block=2
 * part2 bits = sum(slen[g] * nr_of_sfb[block][0][g]) for g in 0..3.
 */
static int part2_bits_mpeg2(int scalefac_compress)
{
    if (scalefac_compress < 0 || scalefac_compress > 511)
        return -1;

    uint32_t sfc = (uint32_t)scalefac_compress;
    uint32_t slen[4];
    int blocknumber;

    if (sfc < 400) {
        slen[0] = (sfc >> 4) / 5;
        slen[1] = (sfc >> 4) % 5;
        slen[2] = (sfc % 16) >> 2;
        slen[3] = sfc % 4;
        blocknumber = 0;
    } else if (sfc < 500) {
        uint32_t s = sfc - 400;
        slen[0] = (s >> 2) / 5;
        slen[1] = (s >> 2) % 5;
        slen[2] = s % 4;
        slen[3] = 0;
        blocknumber = 1;
    } else {
        uint32_t s = sfc - 500;
        slen[0] = s / 3;
        slen[1] = s % 3;
        slen[2] = 0;
        slen[3] = 0;
        blocknumber = 2;
    }

    int bits = 0;
    for (int g = 0; g < 4; g++)
        bits += (int)(slen[g] * lsf_nr_of_sfb[blocknumber][0][g]);
    return bits;
}

/* Compute part2 bit length for MPEG-1 short block.
 * Returns -1 if not computable (mixed block, invalid).
 *
 * For pure short blocks (block_type=2, mixed_block=0):
 *   - Bands 0-5: 6 bands x 3 windows = 18 scalefactors, slen1 bits each
 *   - Bands 6-11: 6 bands (shared across windows), slen2 bits each
 * Total: 18*slen1 + 6*slen2 bits.
 *
 * Mixed blocks (mixed_block=1) have a complex structure with long-block
 * scalefactors for the first 2 subbands; we skip them (return -1).
 *
 * NOTE: Short block Huffman optimization is currently DISABLED (returns -1)
 * because the fixed region boundary for short blocks is not yet verified.
 * The decode/optimize functions exist but are not called. See GitHub issue.
 */
static int part2_bits_mpeg1_short(const side_info_t *side, int g, int ch)
{
    (void)side; (void)g; (void)ch;
    return -1;  /* Disabled: region boundary not verified */
#if 0
    /* Only pure short blocks (block_type=2, not mixed). */
    if (side->block_type[g][ch] != 2)
        return -1;
    if (side->mixed_block[g][ch] != 0)
        return -1;  /* mixed: skip for now */

    int sfc = side->scalefac_compress[g][ch];
    if (sfc < 0 || sfc > 15)
        return -1;

    int slen1 = m1_slen[sfc][0];
    int slen2 = m1_slen[sfc][1];

    return 18 * slen1 + 6 * slen2;
#endif
}

/* Compute part2 bit length for a granule/channel.
 * Returns -1 if not computable (mixed block, intensity stereo, invalid).
 * Handles both long blocks (window_switching==0) and pure short blocks.
 */
static int part2_bits(const frame_header_t *hdr, const side_info_t *side,
                      int g, int ch)
{
    if (hdr->version == MPEG_1) {
        if (side->window_switching[g][ch] == 0) {
            return part2_bits_mpeg1(side, g, ch);
        } else {
            return part2_bits_mpeg1_short(side, g, ch);
        }
    } else {
        /* MPEG-2/2.5: for now, only long blocks. Short blocks in LSF
         * have a different scalefactor scheme; skip them. */
        if (side->window_switching[g][ch] != 0)
            return -1;
        /* MPEG-2/2.5: skip intensity-stereo right channels (different
         * scalefactor scheme). Intensity stereo is signaled by mode_ext
         * bit 0 when chan_mode is joint stereo. */
        if (hdr->chan_mode == CH_JOINT && (hdr->mode_ext & 0x1) && ch == 1)
            return -1;
        return part2_bits_mpeg2(side->scalefac_compress[g][ch]);
    }
}

/* ------------------------------------------------------------------ */
/* Part 2: The Huffman table optimizer                                 */
/* ------------------------------------------------------------------ */

/* Best configuration found by the optimizer. */
typedef struct {
    int big_values;       /* pairs */
    int table0, table1, table2;
    int region0_count;    /* stored value = bands - 1 */
    int region1_count;    /* stored value = bands - 1 */
    int count1table;      /* 0 = A, 1 = B */
    int count1_quads;     /* number of count1 quads to encode */
    int total_bits;       /* total part3 bits for this config */
} best_cfg_t;

/* Tables 4 and 14 do not exist. */
static int table_exists(int t)
{
    return t != 4 && t != 14;
}

/* Find the last nonzero sample index in coeffs[0..576). Returns -1 if all zero. */
static int find_last_nonzero(const int16_t *coeffs)
{
    for (int i = 575; i >= 0; i--) {
        if (coeffs[i] != 0)
            return i;
    }
    return -1;
}

/* Find the largest sample index with |value| > 1. Returns -1 if none.
 * These samples MUST be in the big_values region (count1 can't encode them). */
static int find_last_big(const int16_t *coeffs)
{
    for (int i = 575; i >= 0; i--) {
        if (coeffs[i] > 1 || coeffs[i] < -1)
            return i;
    }
    return -1;
}

/* Find the optimal Huffman configuration for 576 coefficients.
 *
 * Uses band-level dynamic programming for speed:
 *   1. Precompute cost[band][table] for all 22 bands x 32 tables (704 calls).
 *   2. For each (big_values, b1, b2), sum band costs per region and pick
 *      the cheapest table per region via O(32) scan.
 *
 * band_edges: 23 scalefactor band edges (from mp3_sf_bands, long).
 * orig_big_values: the original big_values (used as a search candidate).
 *
 * Searches over:
 *   - big_values candidates (band-edge-aligned, covering all |v|>1)
 *   - region splits (b1 in 1..16 bands, b2-b1 in 1..8 bands)
 *   - best table per region (0-31, excluding 4/14)
 *   - both count1 tables
 *
 * Returns 0 on success (cfg filled), -1 if no valid config.
 */
static int find_best_config(const int16_t *coeffs, int orig_big_values,
                            const int *band_edges, best_cfg_t *cfg)
{
    int last_nz = find_last_nonzero(coeffs);
    int last_big = find_last_big(coeffs);

    /* Edge case: all zeros. Minimal encoding. */
    if (last_nz < 0) {
        cfg->big_values = 0;
        cfg->table0 = cfg->table1 = cfg->table2 = 0;
        cfg->region0_count = 0;
        cfg->region1_count = 0;
        cfg->count1table = 0;
        cfg->count1_quads = 0;
        cfg->total_bits = 0;
        return 0;
    }

    /* Minimum big_values (pairs) to cover all |v|>1 samples. */
    int min_big_values = (last_big + 2) / 2;
    if (min_big_values < 0) min_big_values = 0;
    if (min_big_values > 288) return -1;

    /* Step 1: Precompute band_cost[band][table].
     * Band b covers samples [band_edges[b], band_edges[b+1]).
     * 22 bands (0-21), 32 tables.
     * Note: allocated on stack (704 ints = ~2.8KB); thread-safe. */
#define NBANDS 22
    int band_cost[NBANDS][32];

    for (int b = 0; b < NBANDS; b++) {
        int s = band_edges[b];
        int e = band_edges[b + 1];
        int pairs = (e - s) / 2;
        for (int t = 0; t <= 31; t++) {
            if (!table_exists(t)) {
                band_cost[b][t] = INT_MAX / 2;
            } else if (pairs <= 0) {
                band_cost[b][t] = 0;
            } else {
                band_cost[b][t] = huff_cost_big(t, coeffs + s, pairs);
            }
        }
    }

    /* Helper: cost of bands [b_start, b_end) with table t. */
    /* (Inline via macro for speed.) */
#define BAND_RANGE_COST(bs, be, t) ({ \
    int _c = 0; \
    for (int _b = (bs); _b < (be); _b++) { \
        int _bc = band_cost[_b][t]; \
        if (_bc >= INT_MAX/2) { _c = INT_MAX/2; break; } \
        _c += _bc; \
        if (_c >= INT_MAX/2) { _c = INT_MAX/2; break; } \
    } \
    _c; \
})

    /* Step 2: Collect big_values candidates (band-edge-aligned). */
    int candidates[8];
    int ncand = 0;

    /* Smallest band-edge >= min_big_values. */
    for (int b = 0; b <= 22 && ncand < 6; b++) {
        int bv = band_edges[b] / 2;
        if (bv < min_big_values)
            continue;
        if (bv > 288)
            break;
        candidates[ncand++] = bv;
        /* Only take the first (smallest) band-edge, plus the original.
         * Larger band-edges just add zero pairs. */
        break;
    }

    /* Add the original big_values as a candidate. */
    if (orig_big_values >= min_big_values && orig_big_values <= 288) {
        int dup = 0;
        for (int i = 0; i < ncand; i++) {
            if (candidates[i] == orig_big_values) { dup = 1; break; }
        }
        if (!dup && ncand < 8)
            candidates[ncand++] = orig_big_values;
    }

    /* Also try one band-edge larger than original (in case original was
     * suboptimal and a slightly larger bv allows better table choices).
     * Actually, larger bv means more pairs in big_values region, which
     * rarely helps. Skip for speed. */

    if (ncand == 0)
        return -1;

    int best_total = INT_MAX / 2;
    best_cfg_t best;
    memset(&best, 0, sizeof(best));
    int found = 0;

    /* Step 3: For each big_values candidate, search (b1, b2) splits. */
    for (int ci = 0; ci < ncand; ci++) {
        int bv = candidates[ci];
        int bv_samples = 2 * bv;

        /* Find bv_band: smallest b with band_edges[b] >= bv_samples. */
        int bv_band = 22;
        for (int b = 0; b <= 22; b++) {
            if (band_edges[b] >= bv_samples) {
                bv_band = b;
                break;
            }
        }

        /* Count1 region. */
        int c1_quads = 0;
        int c1_start = bv_samples;
        if (last_nz >= c1_start) {
            c1_quads = (last_nz - c1_start + 4) / 4;
        }

        /* Precompute count1 costs for both tables. */
        int c1_cost[2] = {0, 0};
        int c1_valid[2] = {1, 1};
        if (c1_quads > 0) {
            for (int c1t = 0; c1t <= 1; c1t++) {
                c1_cost[c1t] = huff_cost_count1(c1t, coeffs + c1_start, c1_quads);
                if (c1_cost[c1t] >= INT_MAX / 2)
                    c1_valid[c1t] = 0;
            }
        }

        /* For each count1 table... */
        for (int c1t = 0; c1t <= 1; c1t++) {
            if (!c1_valid[c1t])
                continue;

            /* Search region splits. */
            int b1_max = bv_band < 16 ? bv_band : 16;
            for (int b1 = 1; b1 <= b1_max; b1++) {
                /* Best table for region 0 (bands 0..b1-1). */
                int best_t0 = -1, best_c0 = INT_MAX / 2;
                for (int t = 0; t <= 31; t++) {
                    if (!table_exists(t)) continue;
                    int c = BAND_RANGE_COST(0, b1, t);
                    if (c < best_c0) { best_c0 = c; best_t0 = t; }
                }
                if (best_t0 < 0) continue;

                int b2_max = b1 + 8;
                if (b2_max > bv_band) b2_max = bv_band;
                for (int b2 = b1 + 1; b2 <= b2_max; b2++) {
                    /* Best table for region 1 (bands b1..b2-1). */
                    int best_t1 = -1, best_c1 = INT_MAX / 2;
                    for (int t = 0; t <= 31; t++) {
                        if (!table_exists(t)) continue;
                        int c = BAND_RANGE_COST(b1, b2, t);
                        if (c < best_c1) { best_c1 = c; best_t1 = t; }
                    }
                    if (best_t1 < 0) continue;

                    /* Best table for region 2 (bands b2..bv_band-1).
                     * Note: if bv is not at a band edge, the last band is
                     * partial. For simplicity, we only use band-edge bv
                     * candidates (except original). For the original bv,
                     * we approximate by using full bands up to bv_band.
                     * This may slightly overestimate cost, but it's safe
                     * (we only keep if smaller than original). */
                    int r2_bands = bv_band - b2;
                    int best_t2 = 0, best_c2 = 0;
                    if (r2_bands > 0) {
                        best_t2 = -1; best_c2 = INT_MAX / 2;
                        for (int t = 0; t <= 31; t++) {
                            if (!table_exists(t)) continue;
                            int c = BAND_RANGE_COST(b2, bv_band, t);
                            if (c < best_c2) { best_c2 = c; best_t2 = t; }
                        }
                        if (best_t2 < 0) continue;
                    }
                    /* If bv is not at band edge, add partial band cost. */
                    if (band_edges[bv_band] > bv_samples) {
                        /* Partial band: samples [bv_samples, band_edges[bv_band]).
                         * Actually, this is backwards. If bv_samples < band_edges[bv_band],
                         * then region 2 ends before the band ends. The band_cost
                         * includes the whole band, so we've overcounted.
                         * For correctness, we should compute the exact cost.
                         * But this only happens for the original bv candidate.
                         * To be safe, we'll compute region 2 cost directly
                         * via huff_cost_big for the exact sample range. */
                        int r2_start = (b2 < bv_band) ? band_edges[b2] : bv_samples;
                        if (r2_start > bv_samples) r2_start = bv_samples;
                        int r2_pairs = (bv_samples - r2_start) / 2;
                        if (r2_pairs > 0) {
                            best_t2 = -1; best_c2 = INT_MAX / 2;
                            for (int t = 0; t <= 31; t++) {
                                if (!table_exists(t)) continue;
                                int c = huff_cost_big(t, coeffs + r2_start, r2_pairs);
                                if (c < best_c2) { best_c2 = c; best_t2 = t; }
                            }
                            if (best_t2 < 0) continue;
                        } else {
                            best_t2 = best_t1;  /* empty, patch later */
                            best_c2 = 0;
                        }
                    }

                    int total = best_c0 + best_c1 + best_c2 + c1_cost[c1t];
                    if (total < best_total) {
                        best_total = total;
                        best.big_values = bv;
                        best.table0 = best_t0;
                        best.table1 = best_t1;
                        best.table2 = best_t2;
                        best.region0_count = b1 - 1;
                        best.region1_count = (b2 - b1) - 1;
                        best.count1table = c1t;
                        best.count1_quads = c1_quads;
                        best.total_bits = total;
                        found = 1;
                    }
                }
            }
        }
    }

    if (!found)
        return -1;

    /* Patch zero-length regions. */
    {
        int r0_end = band_edges[best.region0_count + 1];
        int r1_end = band_edges[best.region0_count + 1 + best.region1_count + 1];
        int bv_samples = 2 * best.big_values;
        if (r0_end > bv_samples) r0_end = bv_samples;
        if (r1_end > bv_samples) r1_end = bv_samples;

        if (r0_end == 0) best.table0 = best.table1;
        if (r1_end == r0_end) best.table1 = best.table0;
        if (bv_samples == r1_end) best.table2 = best.table1;
    }

    *cfg = best;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Short block optimizer                                                */
/* ------------------------------------------------------------------ */

/* Best configuration for a short block. */
typedef struct {
    int big_values;       /* pairs */
    int table0, table1;   /* table_select[0], [1] */
    int count1table;      /* 0 = A, 1 = B */
    int count1_quads;
    int total_bits;
} best_cfg_short_t;

/* Fixed region boundary for short blocks (in samples).
 * Region 0: [0, SHORT_REGION_SPLIT), table0
 * Region 1: [SHORT_REGION_SPLIT, 2*big_values), table1
 * We use one window (192 samples) as the split point. */
#define SHORT_REGION_SPLIT 192

/* Find the optimal Huffman configuration for a short block (576 coeffs).
 *
 * Short blocks use 2 tables (not 3) with a FIXED region boundary
 * (no region counts transmitted). We optimize:
 *   - big_values (must cover all |v|>1)
 *   - table0, table1 (0-31, excluding 4/14)
 *   - count1 table (A/B)
 *
 * Returns 0 on success (cfg filled), -1 if no valid config.
 */
static int find_best_config_short(const int16_t *coeffs, int orig_big_values,
                                  best_cfg_short_t *cfg)
{
    int last_nz = find_last_nonzero(coeffs);
    int last_big = find_last_big(coeffs);

    /* Edge case: all zeros. */
    if (last_nz < 0) {
        cfg->big_values = 0;
        cfg->table0 = cfg->table1 = 0;
        cfg->count1table = 0;
        cfg->count1_quads = 0;
        cfg->total_bits = 0;
        return 0;
    }

    /* Minimum big_values to cover all |v|>1. */
    int min_big_values = (last_big + 2) / 2;
    if (min_big_values < 0) min_big_values = 0;
    if (min_big_values > 288) return -1;

    /* Big_values candidates: smallest covering last_big, plus original. */
    int candidates[4];
    int ncand = 0;
    candidates[ncand++] = min_big_values;
    if (orig_big_values != min_big_values &&
        orig_big_values >= min_big_values && orig_big_values <= 288) {
        candidates[ncand++] = orig_big_values;
    }

    int best_total = INT_MAX / 2;
    best_cfg_short_t best;
    memset(&best, 0, sizeof(best));
    int found = 0;

    for (int ci = 0; ci < ncand; ci++) {
        int bv = candidates[ci];
        int bv_samples = 2 * bv;

        /* Region split (fixed). Clamp to bv_samples. */
        int r0_end = SHORT_REGION_SPLIT;
        if (r0_end > bv_samples) r0_end = bv_samples;

        int r0_pairs = r0_end / 2;
        int r1_pairs = (bv_samples - r0_end) / 2;

        /* Count1 region. */
        int c1_quads = 0;
        if (last_nz >= bv_samples) {
            c1_quads = (last_nz - bv_samples + 4) / 4;
        }

        /* Precompute count1 costs. */
        int c1_cost[2] = {0, 0};
        int c1_valid[2] = {1, 1};
        if (c1_quads > 0) {
            for (int c1t = 0; c1t <= 1; c1t++) {
                c1_cost[c1t] = huff_cost_count1(c1t, coeffs + bv_samples, c1_quads);
                if (c1_cost[c1t] >= INT_MAX / 2)
                    c1_valid[c1t] = 0;
            }
        }

        /* Try all (t0, t1, c1t) combinations. */
        for (int c1t = 0; c1t <= 1; c1t++) {
            if (!c1_valid[c1t]) continue;
            for (int t0 = 0; t0 <= 31; t0++) {
                if (!table_exists(t0)) continue;
                int c0 = (r0_pairs > 0) ?
                    huff_cost_big(t0, coeffs, r0_pairs) : 0;
                if (c0 >= INT_MAX / 2) continue;

                for (int t1 = 0; t1 <= 31; t1++) {
                    if (!table_exists(t1)) continue;
                    int c1 = (r1_pairs > 0) ?
                        huff_cost_big(t1, coeffs + r0_end, r1_pairs) : 0;
                    if (c1 >= INT_MAX / 2) continue;

                    int total = c0 + c1 + c1_cost[c1t];
                    if (total < best_total) {
                        best_total = total;
                        best.big_values = bv;
                        best.table0 = t0;
                        best.table1 = t1;
                        best.count1table = c1t;
                        best.count1_quads = c1_quads;
                        best.total_bits = total;
                        found = 1;
                    }
                }
            }
        }
    }

    if (!found)
        return -1;

    *cfg = best;
    return 0;
}

/* Decoded granule/channel data. (Forward declaration; full definition below.) */
typedef struct {
    int16_t coeffs[576];
    int part2_bits;       /* bit length of part2 (scalefactors) */
    int part3_bits;       /* original bit length of part3 (Huffman) */
    size_t part3_offset;  /* bit offset of part3 within the gc segment */
    int valid;            /* 1 if this gc is eligible for optimization */
} gc_data_t;

/* Decode a short block's part3 to 576 coefficients.
 * Uses fixed region boundary at SHORT_REGION_SPLIT.
 * Returns 0 on success, -1 on error.
 */
static int decode_gc_short(bit_reader_t *r, const side_info_t *side,
                           int g, int ch, int part2_bits, gc_data_t *gd)
{
    memset(gd, 0, sizeof(*gd));
    gd->part2_bits = part2_bits;

    /* Skip part2. */
    bit_reader_skip(r, part2_bits);
    size_t part3_start = bit_reader_tell(r);

    int16_t *coeffs = gd->coeffs;
    memset(coeffs, 0, 576 * sizeof(int16_t));

    int big_values = side->big_values[g][ch];
    if (big_values < 0 || big_values > 288)
        return -1;

    int t0 = side->table_select[g][ch][0];
    int t1 = side->table_select[g][ch][1];
    if (t0 < 0 || t0 > 31 || t1 < 0 || t1 > 31)
        return -1;
    if (t0 == 4 || t0 == 14 || t1 == 4 || t1 == 14)
        return -1;

    int bv_samples = 2 * big_values;
    int r0_end = SHORT_REGION_SPLIT;
    if (r0_end > bv_samples) r0_end = bv_samples;

    /* Region 0. */
    int r0_pairs = r0_end / 2;
    if (huff_decode_big(r, t0, coeffs, r0_pairs) < 0)
        return -1;

    /* Region 1. */
    int r1_pairs = (bv_samples - r0_end) / 2;
    if (huff_decode_big(r, t1, coeffs + r0_end, r1_pairs) < 0)
        return -1;

    /* Count1. */
    int c1t = side->count1table_select[g][ch];
    if (c1t != 0 && c1t != 1)
        return -1;

    size_t part3_end = part3_start + gd->part3_bits;
    int idx = bv_samples;
    while (bit_reader_tell(r) < part3_end && idx < 576) {
        int16_t quad[4];
        size_t before = bit_reader_tell(r);
        int bits = huff_decode_count1(r, c1t, quad, 1);
        if (bits < 0)
            return -1;
        if (bit_reader_tell(r) > part3_end) {
            bit_reader_seek(r, before);
            break;
        }
        for (int i = 0; i < 4 && idx < 576; i++, idx++)
            coeffs[idx] = quad[i];
    }

    gd->part3_offset = part3_start;
    gd->valid = 1;
    return 0;
}

/* Re-encode a short block with the optimal config.
 * Returns part3 bits written, or -1 on error.
 */
static int encode_gc_short(bit_writer_t *w, const int16_t *coeffs,
                           const best_cfg_short_t *cfg)
{
    int bv_samples = 2 * cfg->big_values;
    int r0_end = SHORT_REGION_SPLIT;
    if (r0_end > bv_samples) r0_end = bv_samples;

    size_t start = bit_writer_tell(w);

    int r0_pairs = r0_end / 2;
    if (huff_encode_big(w, cfg->table0, coeffs, r0_pairs) < 0)
        return -1;

    int r1_pairs = (bv_samples - r0_end) / 2;
    if (huff_encode_big(w, cfg->table1, coeffs + r0_end, r1_pairs) < 0)
        return -1;

    if (cfg->count1_quads > 0) {
        if (huff_encode_count1(w, cfg->count1table,
                               coeffs + bv_samples, cfg->count1_quads) < 0)
            return -1;
    }

    return (int)(bit_writer_tell(w) - start);
}

/* ------------------------------------------------------------------ */
/* Part 3: Per-granule/channel decode, optimize, re-encode             */
/* ------------------------------------------------------------------ */

/* Full decode with band edges. Returns 0 on success, -1 on error. */
static int decode_gc_with_bands(bit_reader_t *r, const side_info_t *side,
                                int g, int ch, int part2_bits,
                                const int *band_edges, gc_data_t *gd)
{
    memset(gd, 0, sizeof(*gd));
    gd->part2_bits = part2_bits;

    /* Skip part2. */
    bit_reader_skip(r, part2_bits);
    size_t part3_start = bit_reader_tell(r);

    int big_values = side->big_values[g][ch];
    if (big_values < 0 || big_values > 288)
        return -1;

    int16_t *coeffs = gd->coeffs;
    memset(coeffs, 0, 576 * sizeof(int16_t));

    /* Region boundaries from stored counts (bands-1) through band edges,
     * clamped to 2*big_values. */
    int b1 = side->region0_count[g][ch] + 1;
    int b2 = b1 + side->region1_count[g][ch] + 1;
    if (b1 < 1 || b1 > 16 || b2 <= b1 || b2 > 24)
        return -1;

    int bv_samples = 2 * big_values;
    int r0_end = band_edges[b1];
    int r1_end = band_edges[b2];
    if (r0_end > bv_samples) r0_end = bv_samples;
    if (r1_end > bv_samples) r1_end = bv_samples;
    if (r0_end < 0 || r1_end < r0_end)
        return -1;

    int t0 = side->table_select[g][ch][0];
    int t1 = side->table_select[g][ch][1];
    int t2 = side->table_select[g][ch][2];
    if (t0 < 0 || t0 > 31 || t1 < 0 || t1 > 31 || t2 < 0 || t2 > 31)
        return -1;
    if (t0 == 4 || t0 == 14 || t1 == 4 || t1 == 14 || t2 == 4 || t2 == 14)
        return -1;

    /* Decode region 0: pairs [0, r0_end/2). */
    int r0_pairs = r0_end / 2;
    if (huff_decode_big(r, t0, coeffs, r0_pairs) < 0)
        return -1;

    /* Decode region 1: pairs [r0_end/2, r1_end/2). */
    int r1_pairs = (r1_end - r0_end) / 2;
    if (huff_decode_big(r, t1, coeffs + r0_end, r1_pairs) < 0)
        return -1;

    /* Decode region 2: pairs [r1_end/2, big_values). */
    int r2_pairs = big_values - r1_end / 2;
    if (r2_pairs < 0)
        return -1;
    if (huff_decode_big(r, t2, coeffs + r1_end, r2_pairs) < 0)
        return -1;

    /* Decode count1 quads from bv_samples onwards.
     * We don't know the quad count a priori; decode quads while bits remain
     * in part3 and we haven't hit 576. The huff_decode_count1 handles the
     * overboard rule (nonzero past 576 = error).
     *
     * We decode one quad at a time to avoid overreading. */
    int c1t = side->count1table_select[g][ch];
    if (c1t != 0 && c1t != 1)
        return -1;

    size_t part3_end = part3_start + gd->part3_bits;
    int idx = bv_samples;
    while (bit_reader_tell(r) < part3_end && idx < 576) {
        /* Decode a single quad (4 samples). */
        int16_t quad[4];
        size_t before = bit_reader_tell(r);
        /* Use a temporary reader to decode one quad without overreading. */
        int bits = huff_decode_count1(r, c1t, quad, 1);
        if (bits < 0)
            return -1;
        if (bit_reader_tell(r) > part3_end) {
            /* Overread past part3 end; rewind and stop. */
            bit_reader_seek(r, before);
            break;
        }
        for (int i = 0; i < 4 && idx < 576; i++, idx++)
            coeffs[idx] = quad[i];
        /* Check overboard: quad may have written past 576 internally,
         * but huff_decode_count1 already validates that. */
    }

    gd->part3_offset = part3_start;
    gd->valid = 1;
    return 0;
}

/* Re-encode one granule/channel's coefficients with the optimal config.
 * Returns the number of part3 bits written, or -1 on error.
 */
static int encode_gc(bit_writer_t *w, const int16_t *coeffs,
                     const best_cfg_t *cfg, const int *band_edges)
{
    int bv_samples = 2 * cfg->big_values;
    int b1 = cfg->region0_count + 1;
    int b2 = b1 + cfg->region1_count + 1;

    int r0_end = band_edges[b1];
    int r1_end = band_edges[b2];
    if (r0_end > bv_samples) r0_end = bv_samples;
    if (r1_end > bv_samples) r1_end = bv_samples;

    size_t start = bit_writer_tell(w);

    /* Region 0. */
    int r0_pairs = r0_end / 2;
    if (huff_encode_big(w, cfg->table0, coeffs, r0_pairs) < 0)
        return -1;

    /* Region 1. */
    int r1_pairs = (r1_end - r0_end) / 2;
    if (huff_encode_big(w, cfg->table1, coeffs + r0_end, r1_pairs) < 0)
        return -1;

    /* Region 2. */
    int r2_pairs = cfg->big_values - r1_end / 2;
    if (r2_pairs < 0)
        return -1;
    if (huff_encode_big(w, cfg->table2, coeffs + r1_end, r2_pairs) < 0)
        return -1;

    /* Count1. */
    if (cfg->count1_quads > 0) {
        if (huff_encode_count1(w, cfg->count1table,
                               coeffs + bv_samples, cfg->count1_quads) < 0)
            return -1;
    }

    return (int)(bit_writer_tell(w) - start);
}

/* ------------------------------------------------------------------ */
/* Part 4: recompress_frame()                                          */
/* ------------------------------------------------------------------ */

/* Maximum payload size we'll process (sanity limit: 64KB). */
#define MAX_PAYLOAD_BYTES (64 * 1024)

int recompress_frame(queue_frame_t *qf)
{
    parsed_frame_t *pf = &qf->parsed;
    const frame_header_t *hdr = &pf->header;
    side_info_t *side = &pf->side;

    int granules = pf->granules;
    int channels = pf->channels;

    if (granules <= 0 || granules > 2 || channels <= 0 || channels > 2)
        return 0;  /* invalid; keep original */
    if (qf->payload_bytes == 0 || qf->payload_bytes > MAX_PAYLOAD_BYTES)
        return 0;
    if (!qf->payload_data)
        return 0;

    /* Get long-block scalefactor band edges. */
    const int *band_edges = mp3_sf_bands(hdr->version, hdr->samplerate_idx, 0);
    if (!band_edges)
        return 0;

    /* Step 1: Compute part2 bit lengths and check eligibility. */
    int p2bits[2][2];
    int eligible[2][2];
    int is_short[2][2];  /* 1 if this gc uses short blocks */
    int any_eligible = 0;

    for (int g = 0; g < granules; g++) {
        for (int ch = 0; ch < channels; ch++) {
            eligible[g][ch] = 0;
            is_short[g][ch] = 0;
            p2bits[g][ch] = part2_bits(hdr, side, g, ch);
            if (p2bits[g][ch] >= 0) {
                eligible[g][ch] = 1;
                any_eligible = 1;
                /* Mark short blocks (MPEG-1 only for now). */
                if (hdr->version == MPEG_1 &&
                    side->window_switching[g][ch] != 0) {
                    is_short[g][ch] = 1;
                }
            }
        }
    }

    if (!any_eligible)
        return 0;  /* nothing to optimize */

    /* Step 2: Walk the payload, decoding each gc's part3. */
    bit_reader_t r;
    bit_reader_init(&r, qf->payload_data, qf->payload_bytes);

    gc_data_t gcd[2][2];
    memset(gcd, 0, sizeof(gcd));

    /* Track each gc's segment for reassembly. */
    typedef struct {
        size_t bit_offset;    /* start of part2_3 segment in payload */
        int part2_3_length;   /* total bits for this gc */
        int part2_bits;
        int new_part3_bits;   /* -1 = keep original */
        uint8_t *new_part3;   /* re-encoded part3 bytes (if optimized) */
        size_t new_part3_bytes;
        /* Updated side info fields (if optimized). */
        int new_big_values;
        int new_table0, new_table1, new_table2;
        int new_r0c, new_r1c;
        int new_c1t;
    } gc_seg_t;
    gc_seg_t segs[2][2];
    memset(segs, 0, sizeof(segs));

    int decode_failed = 0;

    for (int g = 0; g < granules && !decode_failed; g++) {
        for (int ch = 0; ch < channels && !decode_failed; ch++) {
            gc_seg_t *sg = &segs[g][ch];
            sg->bit_offset = bit_reader_tell(&r);
            sg->part2_3_length = side->part2_3_length[g][ch];
            sg->part2_bits = p2bits[g][ch];
            sg->new_part3_bits = -1;  /* default: keep original */

            if (sg->part2_3_length <= 0) {
                /* Empty segment; nothing to do. */
                continue;
            }

            /* Bounds check. */
            if (sg->bit_offset + (size_t)sg->part2_3_length >
                qf->payload_bytes * 8) {
                decode_failed = 1;
                break;
            }

            if (!eligible[g][ch]) {
                /* Not eligible; skip over this segment. */
                bit_reader_skip(&r, sg->part2_3_length);
                continue;
            }

            /* Sanity: part2 can't exceed part2_3. */
            if (sg->part2_bits > sg->part2_3_length) {
                bit_reader_skip(&r, sg->part2_3_length);
                eligible[g][ch] = 0;
                continue;
            }

            gcd[g][ch].part3_bits = sg->part2_3_length - sg->part2_bits;

            /* Decode part3 to coefficients.
             * Short blocks use the fixed-region decoder. */
            int dec_rc;
            if (is_short[g][ch]) {
                dec_rc = decode_gc_short(&r, side, g, ch, sg->part2_bits,
                                         &gcd[g][ch]);
            } else {
                dec_rc = decode_gc_with_bands(&r, side, g, ch, sg->part2_bits,
                                              band_edges, &gcd[g][ch]);
            }
            if (dec_rc != 0) {
                /* Decode error: rewind to end of segment and mark ineligible. */
                bit_reader_seek(&r, sg->bit_offset + sg->part2_3_length);
                eligible[g][ch] = 0;
                continue;
            }

            /* Ensure reader is at the end of this gc's segment. */
            size_t expected_end = sg->bit_offset + sg->part2_3_length;
            size_t actual = bit_reader_tell(&r);
            if (actual > expected_end) {
                decode_failed = 1;
                break;
            }
            /* If decoder didn't consume all bits (e.g., trailing padding
             * in part3), skip to the segment end. */
            if (actual < expected_end)
                bit_reader_seek(&r, expected_end);
        }
    }

    if (decode_failed)
        return 0;  /* keep original */

    /* Step 3: Optimize each eligible gc. */
    int any_optimized = 0;

    for (int g = 0; g < granules; g++) {
        for (int ch = 0; ch < channels; ch++) {
            if (!eligible[g][ch] || !gcd[g][ch].valid)
                continue;

            gc_seg_t *sg = &segs[g][ch];
            gc_data_t *gd = &gcd[g][ch];

            if (is_short[g][ch]) {
                /* Short block: optimize with fixed region boundary. */
                best_cfg_short_t scfg;
                if (find_best_config_short(gd->coeffs, side->big_values[g][ch],
                                          &scfg) != 0)
                    continue;

                /* Only keep if strictly smaller. */
                if (scfg.total_bits >= gd->part3_bits)
                    continue;

                /* Re-encode. */
                size_t out_cap = (scfg.total_bits + 7) / 8 + 16;
                uint8_t *out_buf = malloc(out_cap);
                if (!out_buf)
                    continue;

                bit_writer_t w;
                bit_writer_init(&w, out_buf, out_cap);
                int written = encode_gc_short(&w, gd->coeffs, &scfg);
                if (written < 0 || written != scfg.total_bits) {
                    free(out_buf);
                    continue;
                }

                /* Success: record. For short blocks, only table0, table1,
                 * big_values, and count1table are updated. Region counts
                 * and table2 are not used. */
                sg->new_part3 = out_buf;
                sg->new_part3_bytes = bit_writer_bytes(&w);
                sg->new_part3_bits = written;
                sg->new_big_values = scfg.big_values;
                sg->new_table0 = scfg.table0;
                sg->new_table1 = scfg.table1;
                sg->new_table2 = -1;  /* not used for short */
                sg->new_r0c = -1;     /* not used for short */
                sg->new_r1c = -1;     /* not used for short */
                sg->new_c1t = scfg.count1table;
                any_optimized = 1;
                continue;
            }

            /* Long block: Find optimal configuration. */
            best_cfg_t cfg;
            if (find_best_config(gd->coeffs, side->big_values[g][ch],
                                band_edges, &cfg) != 0)
                continue;

            /* Only keep if strictly smaller than original part3. */
            if (cfg.total_bits >= gd->part3_bits)
                continue;

            /* Re-encode part3 with the optimal config. */
            size_t out_cap = (cfg.total_bits + 7) / 8 + 16;
            uint8_t *out_buf = malloc(out_cap);
            if (!out_buf)
                continue;

            bit_writer_t w;
            bit_writer_init(&w, out_buf, out_cap);
            int written = encode_gc(&w, gd->coeffs, &cfg, band_edges);
            if (written < 0 || written != cfg.total_bits) {
                free(out_buf);
                continue;
            }

            /* Success: record the new part3 and updated side info. */
            sg->new_part3 = out_buf;
            sg->new_part3_bytes = bit_writer_bytes(&w);
            sg->new_part3_bits = written;
            sg->new_big_values = cfg.big_values;
            sg->new_table0 = cfg.table0;
            sg->new_table1 = cfg.table1;
            sg->new_table2 = cfg.table2;
            sg->new_r0c = cfg.region0_count;
            sg->new_r1c = cfg.region1_count;
            sg->new_c1t = cfg.count1table;
            any_optimized = 1;
        }
    }

    if (!any_optimized) {
        /* Nothing improved; free any partial allocations and keep original. */
        for (int g = 0; g < granules; g++)
            for (int ch = 0; ch < channels; ch++)
                free(segs[g][ch].new_part3);
        return 0;
    }

    /* Step 4: Reassemble the payload with optimized part3s. */
    /* Compute new total bits. */
    size_t new_total_bits = 0;
    for (int g = 0; g < granules; g++) {
        for (int ch = 0; ch < channels; ch++) {
            gc_seg_t *sg = &segs[g][ch];
            int p23 = sg->part2_3_length;
            if (sg->new_part3_bits >= 0) {
                p23 = sg->part2_bits + sg->new_part3_bits;
            }
            new_total_bits += p23;
            /* Update side info part2_3_length for later. */
            sg->part2_3_length = p23;
        }
    }

    size_t new_bytes = (new_total_bits + 7) / 8;
    uint8_t *new_payload = calloc(1, new_bytes + 8);  /* slack for writer */
    if (!new_payload) {
        for (int g = 0; g < granules; g++)
            for (int ch = 0; ch < channels; ch++)
                free(segs[g][ch].new_part3);
        return 0;
    }

    bit_writer_t w;
    bit_writer_init(&w, new_payload, new_bytes + 8);

    bit_reader_t rr;
    bit_reader_init(&rr, qf->payload_data, qf->payload_bytes);

    for (int g = 0; g < granules; g++) {
        for (int ch = 0; ch < channels; ch++) {
            gc_seg_t *sg = &segs[g][ch];
            int orig_p23 = side->part2_3_length[g][ch];

            /* Copy part2 verbatim (bit by bit to handle unaligned). */
            bit_reader_seek(&rr, sg->bit_offset);
            for (int i = 0; i < sg->part2_bits; i++) {
                uint32_t bit = bit_read(&rr, 1);
                bit_write(&w, bit, 1);
            }

            if (sg->new_part3_bits >= 0) {
                /* Write optimized part3. */
                bit_reader_t pr;
                bit_reader_init(&pr, sg->new_part3, sg->new_part3_bytes);
                for (int i = 0; i < sg->new_part3_bits; i++) {
                    uint32_t bit = bit_read(&pr, 1);
                    bit_write(&w, bit, 1);
                }
            } else {
                /* Copy original part3 verbatim. */
                int orig_p3 = orig_p23 - sg->part2_bits;
                /* rr is already positioned after part2. */
                for (int i = 0; i < orig_p3; i++) {
                    uint32_t bit = bit_read(&rr, 1);
                    bit_write(&w, bit, 1);
                }
            }

            /* Skip to next gc segment in the reader. */
            bit_reader_seek(&rr, sg->bit_offset + (size_t)orig_p23);
        }
    }

    /* Sanity: written bits should match computed total. */
    size_t written_bits = bit_writer_tell(&w);
    if (written_bits != new_total_bits) {
        /* Mismatch; keep original to be safe. */
        free(new_payload);
        for (int g = 0; g < granules; g++)
            for (int ch = 0; ch < channels; ch++)
                free(segs[g][ch].new_part3);
        return 0;
    }

    /* Step 5: Commit the new payload and updated side info. */
    free(qf->payload_data);
    qf->payload_data = new_payload;
    qf->payload_bytes = new_bytes;

    for (int g = 0; g < granules; g++) {
        for (int ch = 0; ch < channels; ch++) {
            gc_seg_t *sg = &segs[g][ch];
            free(sg->new_part3);
            if (sg->new_part3_bits >= 0) {
                side->part2_3_length[g][ch] = sg->part2_3_length;
                side->big_values[g][ch] = sg->new_big_values;
                side->table_select[g][ch][0] = sg->new_table0;
                side->table_select[g][ch][1] = sg->new_table1;
                /* For short blocks, table_select[2] and region counts
                 * are not transmitted; leave them unchanged. */
                if (!is_short[g][ch]) {
                    side->table_select[g][ch][2] = sg->new_table2;
                    side->region0_count[g][ch] = sg->new_r0c;
                    side->region1_count[g][ch] = sg->new_r1c;
                }
                side->count1table_select[g][ch] = sg->new_c1t;
            }
        }
    }

    qf->recompressed = 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Part 4b: Multi-threaded frame recompression                         */
/* ------------------------------------------------------------------ */

/* Work pool for parallel recompression. */
typedef struct {
    queue_frame_t *frames;
    size_t nframes;
    volatile size_t next_idx;  /* atomic counter for work distribution */
} recompress_pool_t;

/* Worker thread: process frames until none remain. */
static void *recompress_worker(void *arg)
{
    recompress_pool_t *pool = (recompress_pool_t *)arg;
    while (1) {
        /* Atomically grab the next frame index. */
        size_t idx = __sync_fetch_and_add(&pool->next_idx, 1);
        if (idx >= pool->nframes)
            break;
        /* Fail-safe: recompress_frame never crashes; on error it
         * keeps the original. */
        recompress_frame(&pool->frames[idx]);
    }
    return NULL;
}

/* Recompress all frames in parallel using N worker threads.
 * Each frame is independent; order is preserved (results stored in-place).
 * If nworkers <= 1, processes single-threaded.
 * Returns 0 on success.
 */
int recompress_frames_parallel(queue_frame_t *frames, size_t nframes,
                               int nworkers)
{
    if (nframes == 0)
        return 0;

    if (nworkers <= 1) {
        /* Single-threaded fallback. */
        for (size_t i = 0; i < nframes; i++) {
            recompress_frame(&frames[i]);
        }
        return 0;
    }

    /* Cap workers at nframes (no point in more threads than frames). */
    if ((size_t)nworkers > nframes)
        nworkers = (int)nframes;

    recompress_pool_t pool;
    pool.frames = frames;
    pool.nframes = nframes;
    pool.next_idx = 0;

    pthread_t *threads = malloc((size_t)nworkers * sizeof(pthread_t));
    if (!threads) {
        /* Fallback to single-threaded on alloc failure. */
        for (size_t i = 0; i < nframes; i++) {
            recompress_frame(&frames[i]);
        }
        return 0;
    }

    int nstarted = 0;
    for (int i = 0; i < nworkers; i++) {
        if (pthread_create(&threads[i], NULL, recompress_worker, &pool) != 0) {
            break;
        }
        nstarted++;
    }

    if (nstarted == 0) {
        /* No threads started; fallback to single-threaded. */
        free(threads);
        for (size_t i = 0; i < nframes; i++) {
            recompress_frame(&frames[i]);
        }
        return 0;
    }

    /* If some threads failed to start, the started ones will still
     * process all frames via the shared atomic counter. */
    for (int i = 0; i < nstarted; i++) {
        pthread_join(threads[i], NULL);
    }

    free(threads);
    return 0;
}

/* Get the default number of workers (number of CPU cores, or 3 if unknown).
 * Matches the reference implementation's default of min(CPU,3)... actually
 * we use all cores for better speed. */
int recompress_default_workers(void)
{
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    long n = (long)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    if (n < 1)
        return 3;
    if (n > 32)
        return 32;  /* sanity cap */
    return (int)n;
}

/* ------------------------------------------------------------------ */
/* Part 5: Side info serializer (for -z updated side info)             */
/* ------------------------------------------------------------------ */

/* Write one granule/channel block (mirrors parse_gc in mp3parse.c). */
static void write_gc(bit_writer_t *w, const side_info_t *side,
                     int g, int ch, mpeg_version_t ver)
{
    bit_write(w, (uint32_t)side->part2_3_length[g][ch], 12);
    bit_write(w, (uint32_t)side->big_values[g][ch], 9);
    bit_write(w, (uint32_t)side->global_gain[g][ch], 8);
    bit_write(w, (uint32_t)side->scalefac_compress[g][ch],
              ver == MPEG_1 ? 4 : 9);
    bit_write(w, (uint32_t)side->window_switching[g][ch], 1);

    if (side->window_switching[g][ch] == 0) {
        bit_write(w, (uint32_t)side->table_select[g][ch][0], 5);
        bit_write(w, (uint32_t)side->table_select[g][ch][1], 5);
        bit_write(w, (uint32_t)side->table_select[g][ch][2], 5);
        bit_write(w, (uint32_t)side->region0_count[g][ch], 4);
        bit_write(w, (uint32_t)side->region1_count[g][ch], 3);
    } else {
        bit_write(w, (uint32_t)side->block_type[g][ch], 2);
        bit_write(w, (uint32_t)side->mixed_block[g][ch], 1);
        bit_write(w, (uint32_t)side->table_select[g][ch][0], 5);
        bit_write(w, (uint32_t)side->table_select[g][ch][1], 5);
        bit_write(w, (uint32_t)side->subblock_gain[g][ch][0], 3);
        bit_write(w, (uint32_t)side->subblock_gain[g][ch][1], 3);
        bit_write(w, (uint32_t)side->subblock_gain[g][ch][2], 3);
    }

    if (ver == MPEG_1)
        bit_write(w, (uint32_t)side->preflag[g][ch], 1);
    bit_write(w, (uint32_t)side->scalefac_scale[g][ch], 1);
    bit_write(w, (uint32_t)side->count1table_select[g][ch], 1);
}

void write_side_info_bytes(const frame_header_t *hdr, const side_info_t *side,
                           int main_data_begin, uint8_t *out)
{
    int mono = (hdr->chan_mode == CH_MONO);
    int granules = (hdr->version == MPEG_1) ? 2 : 1;
    int channels = mono ? 1 : 2;
    int si_size = mp3_side_info_size(hdr);

    bit_writer_t w;
    bit_writer_init(&w, out, (size_t)si_size);
    /* Zero the buffer first (writer only sets bits it writes). */
    memset(out, 0, (size_t)si_size);

    if (hdr->version == MPEG_1) {
        bit_write(&w, (uint32_t)main_data_begin, 9);
        /* Private bits: 5 for mono, 3 for stereo. Original values are not
         * stored in side_info_t; write zeros (matches typical encoders;
         * these bits are informational only). */
        bit_write(&w, 0, mono ? 5 : 3);
        /* SCFI: 4 bits per channel. Unpack from the parser's packing:
         *   scfi[ch][0] = (b0<<1)|b1, scfi[ch][1] = (b2<<1)|b3. */
        for (int ch = 0; ch < channels; ch++) {
            bit_write(&w, (uint32_t)((side->scfi[ch][0] >> 1) & 1), 1);
            bit_write(&w, (uint32_t)(side->scfi[ch][0] & 1), 1);
            bit_write(&w, (uint32_t)((side->scfi[ch][1] >> 1) & 1), 1);
            bit_write(&w, (uint32_t)(side->scfi[ch][1] & 1), 1);
        }
    } else {
        bit_write(&w, (uint32_t)main_data_begin, 8);
        bit_write(&w, 0, mono ? 1 : 2);  /* private bits */
    }

    for (int g = 0; g < granules; g++)
        for (int ch = 0; ch < channels; ch++)
            write_gc(&w, side, g, ch, hdr->version);
}
