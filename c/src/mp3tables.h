/*
 * mp3tables.h -- MP3 static tables for the mp3packer-plus C port.
 *
 * Derived from the classic mp3packer sources by Reed Wilson ("Omion"):
 *   src-c/mp3framehuffman.ml  (Huffman codebooks, linbits)
 *   src-c/mp3frameutils.ml    (scalefactor band edges)
 *   src-c/types.ml            (bitrate / sample-rate tables, frame length)
 * The original program is GPL-2.0; this port carries the same license.
 *
 * Table data was mechanically extracted from the OCaml sources (see the
 * generator notes in the project log) and verified entry-by-entry:
 * every codeword's bit length matches its declared length, every pair
 * table covers a complete kxk (x,y) grid, and entry counts match
 * ISO 11172-3 Table B.7.
 */
#ifndef MP3TABLES_H
#define MP3TABLES_H

#include <stdint.h>
#include "mp3types.h"   /* mpeg_version_t */

/* ------------------------------------------------------------------ */
/* Bitrate tables (kbps), indexed [version][bitrate_idx].              */
/* version uses the mpeg_version_t values: 0=MPEG-2.5, 2=MPEG-2,       */
/* 3=MPEG-1 (1 is the reserved version id, all zeros).                 */
/* Index 0 = free format, 15 = invalid.                                */
/* ------------------------------------------------------------------ */
extern const int mp3_bitrates[4][16];

/* ------------------------------------------------------------------ */
/* Sample-rate tables (Hz), indexed [version][samplerate_idx].         */
/* Index 3 is reserved (0).                                            */
/* ------------------------------------------------------------------ */
extern const int mp3_samplerates[4][4];

/* ------------------------------------------------------------------ */
/* Frame size in bytes, not counting the 4-byte header twice:         */
/*   MPEG-1:      floor(144 * bitrate_bps / samplerate_hz) + padding   */
/*   MPEG-2/2.5:  floor(72  * bitrate_bps / samplerate_hz) + padding   */
/* (72*br/sr == floor(floor(144*br/sr)/2); matches types.ml exactly.)  */
/* ------------------------------------------------------------------ */
int mp3_frame_size(mpeg_version_t v, int bitrate_kbps,
                   int samplerate_hz, int padding);

/* ------------------------------------------------------------------ */
/* Huffman tables.                                                     */
/*                                                                     */
/* Pair tables encode (x, y) with `bits`-bit codeword `code`.          */
/* Quad (count1) tables encode (v0..v3).                               */
/*                                                                     */
/* huff_tables[n]: table for Huffman codebook n (1..31);                */
/*   [0] is NULL (table 0 = "no bits, all zeros", special-cased by      */
/*   decoders), [4] and [14] are NULL (those codebooks don't exist).    */
/*   [17..23] alias [16], [25..31] alias [24] (same pointers).          */
/* huff_table_sizes[n]: entry count (0 for the NULL slots).            */
/* huff_count1[0] = table A, huff_count1[1] = table B.                 */
/* huff_linbits[n]: linbits escape length for table n (0 for n < 16).  */
/* ------------------------------------------------------------------ */
typedef struct {
    uint16_t x, y;
    uint8_t  bits;
    uint32_t code;
} huff_pair_t;

typedef struct {
    uint8_t  v0, v1, v2, v3;
    uint8_t  bits;
    uint32_t code;
} huff_quad_t;

extern const huff_pair_t *huff_tables[32];
extern const int          huff_table_sizes[32];
extern const huff_quad_t *huff_count1[2];      /* A=0, B=1 */
extern const int          huff_count1_sizes[2];
extern const int          huff_linbits[32];    /* indexed by table 0..31 */

/* ------------------------------------------------------------------ */
/* Scalefactor band edges (sample offsets of each band start).         */
/*                                                                     */
/* NOTE on dimensions: the OCaml source defines NINE distinct tables   */
/* (one per sample rate), and they genuinely differ within an MPEG     */
/* version -- e.g. long blocks: 24000 Hz uses band edges 114/540 where */
/* 22050/16000 Hz use 116/522; short blocks have three distinct       */
/* variants inside MPEG-2 alone. Collapsing to one table per version   */
/* would corrupt those files, so the layout keeps all nine:           */
/*   [version_group][rate][band]                                       */
/*   version_group: 0 = MPEG-1, 1 = MPEG-2, 2 = MPEG-2.5               */
/*   rate: index into that version's mp3_samplerates row              */
/*     MPEG-1:   0=44100, 1=48000, 2=32000                            */
/*     MPEG-2:   0=22050, 1=24000, 2=16000                            */
/*     MPEG-2.5: 0=11025, 1=12000, 2=8000                              */
/* Long blocks have 23 edges (22 bands + terminator 576);             */
/* short blocks have 14 edges (13 bands + terminator 192).            */
/* ------------------------------------------------------------------ */
extern const int sf_bands_long[3][3][23];
extern const int sf_bands_short[3][3][14];

/* Convenience accessor; returns NULL for a bad samplerate_idx. */
const int *mp3_sf_bands(mpeg_version_t v, int samplerate_idx, int is_short);

#endif /* MP3TABLES_H */
