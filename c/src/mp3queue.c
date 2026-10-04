#include "mp3queue.h"
#include "mp3tables.h"
#include "mp3bit.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void repacker_init(repacker_t *r) {
    memset(r, 0, sizeof(*r));
    r->cap = 1024;
    r->frames = malloc(r->cap * sizeof(queue_frame_t));
    r->out_cap = 1024 * 1024;
    r->out_data = malloc(r->out_cap);
    r->reservoir_cap = 4096;
    r->reservoir = malloc(r->reservoir_cap);
    r->out_reservoir_cap = 4096;
    r->out_reservoir = malloc(r->out_reservoir_cap);
}

void repacker_free(repacker_t *r) {
    for (size_t i = 0; i < r->nframes; i++)
        free(r->frames[i].payload_data);
    free(r->frames);
    free(r->out_data);
    free(r->reservoir);
    free(r->out_reservoir);
    memset(r, 0, sizeof(*r));
}

/* Compute payload bytes from part2_3_length values */
static size_t compute_payload(const parsed_frame_t *pf) {
    size_t bits = 0;
    for (int g = 0; g < pf->granules; g++)
        for (int ch = 0; ch < pf->channels; ch++)
            bits += pf->side.part2_3_length[g][ch];
    return (bits + 7) / 8;
}

int repacker_add_frame(repacker_t *rq, const parsed_frame_t *pf) {
    if (rq->nframes >= rq->cap) {
        rq->cap *= 2;
        rq->frames = realloc(rq->frames, rq->cap * sizeof(queue_frame_t));
        if (!rq->frames) return -1;
    }

    /* Append this frame's main data to the reservoir */
    size_t main_data_len = pf->main_data_len;
    const uint8_t *main_data_ptr = pf->main_data;
    
    /* Ensure reservoir has capacity */
    while (rq->reservoir_len + main_data_len > rq->reservoir_cap) {
        rq->reservoir_cap *= 2;
        rq->reservoir = realloc(rq->reservoir, rq->reservoir_cap);
        if (!rq->reservoir) return -1;
    }
    memcpy(rq->reservoir + rq->reservoir_len, main_data_ptr, main_data_len);
    
    /* Compute payload extraction */
    size_t payload_bytes = compute_payload(pf);
    
    /* Bounds check: main_data_begin cannot exceed reservoir_len */
    if (pf->side.main_data_begin > rq->reservoir_len) {
        /* Corrupt frame; treat as if main_data_begin = reservoir_len */
        /* For now, skip the frame */
        return 0;
    }
    
    size_t start_offset = rq->reservoir_len - pf->side.main_data_begin;
    
    /* Bounds check */
    if (start_offset + payload_bytes > rq->reservoir_len + main_data_len) {
        /* Corrupt frame: not enough data in reservoir */
        /* For now, skip it or use what we have */
        payload_bytes = (rq->reservoir_len + main_data_len > start_offset) 
                      ? (rq->reservoir_len + main_data_len - start_offset) : 0;
    }
    
    queue_frame_t *qf = &rq->frames[rq->nframes++];
    memset(qf, 0, sizeof(*qf));
    qf->parsed = *pf;
    qf->payload_bytes = payload_bytes;
    
    /* Copy payload bytes (reservoir-flattened) */
    if (payload_bytes > 0) {
        qf->payload_data = malloc(payload_bytes);
        if (!qf->payload_data) return -1;
        memcpy(qf->payload_data, rq->reservoir + start_offset, payload_bytes);
    }
    
    /* Update reservoir: keep the unused tail */
    size_t new_reservoir_len = (rq->reservoir_len + main_data_len) - (start_offset + payload_bytes);
    /* The tail starts at start_offset + payload_bytes in the combined buffer */
    /* Combined buffer is rq->reservoir[0..rq->reservoir_len+main_data_len) */
    /* We need to move the tail to the beginning */
    if (new_reservoir_len > 0) {
        memmove(rq->reservoir, rq->reservoir + start_offset + payload_bytes, new_reservoir_len);
    }
    rq->reservoir_len = new_reservoir_len;
    /* Enforce max reservoir size (511 for MPEG-1, 255 for MPEG-2) */
    size_t max_res = (pf->header.version == MPEG_1) ? 511 : 255;
    if (rq->reservoir_len > max_res) {
        size_t excess = rq->reservoir_len - max_res;
        memmove(rq->reservoir, rq->reservoir + excess, max_res);
        rq->reservoir_len = max_res;
    }

    if (rq->nframes == 1) {
        rq->ref_header = pf->header;
        rq->max_reservoir = (pf->header.version == MPEG_1) ? 511 : 255;
    }

    return 0;
}

/* Find smallest bitrate index whose frame can hold data_bytes of payload.
 * Returns bitrate index, sets *padding.
 */
static int bytes_to_bitrate(repacker_t *rq, size_t data_bytes,
                            int *padding) {
    const frame_header_t *h = &rq->ref_header;
    int sr = mp3_samplerates[h->version][h->samplerate_idx];
    int si_size = mp3_side_info_size(h);
    int crc_size = 0;  /* output never has CRC */

    /* data_bytes = payload + required padding
     * frame size must be >= 4 + si_size + data_bytes */

    for (int bi = 1; bi <= 14; bi++) {
        int br = mp3_bitrates[h->version][bi];
        if (br == 0) continue;

        /* try unpadded first */
        int sz = mp3_frame_size(h->version, br, sr, 0);
        int capacity = sz - 4 - si_size - crc_size;
        if (capacity >= (int)data_bytes) {
            *padding = 0;
            return bi;
        }

        /* try padded */
        sz = mp3_frame_size(h->version, br, sr, 1);
        capacity = sz - 4 - si_size - crc_size;
        if (capacity >= (int)data_bytes) {
            *padding = 1;
            return bi;
        }
    }

    /* Nothing fits — use max bitrate, padded */
    *padding = 1;
    return 14;
}

static void out_reserve(repacker_t *rq, size_t n) {
    while (rq->out_len + n > rq->out_cap) {
        rq->out_cap *= 2;
        rq->out_data = realloc(rq->out_data, rq->out_cap);
    }
}

static void out_write(repacker_t *rq, const uint8_t *data, size_t n) {
    out_reserve(rq, n);
    memcpy(rq->out_data + rq->out_len, data, n);
    rq->out_len += n;
}

/* Synthesize output frame header */
static void make_header(const repacker_t *rq, const queue_frame_t *qf,
                        uint8_t out[4]) {
    const frame_header_t *in = &qf->parsed.header;
    const frame_header_t *ref = &rq->ref_header;

    out[0] = 0xFF;
    out[1] = 0xE0;

    /* version */
    int ver_bits;
    switch (ref->version) {
        case MPEG_25: ver_bits = 0; break;
        case MPEG_2: ver_bits = 2; break;
        case MPEG_1: ver_bits = 3; break;
        default: ver_bits = 3; break;
    }
    out[1] |= (ver_bits << 3) | (1 << 1) | 1;  /* layer III, no CRC */

    out[2] = (qf->out_bitrate_idx << 4) |
             (ref->samplerate_idx << 2) |
             (qf->out_padding << 1) |
             (ref->private_bit & 1);

    out[3] = (in->chan_mode << 6) |
             (in->mode_ext << 4) |
             (in->copyright << 3) |
             (in->original << 2) |
             (in->emphasis);
}

/* Write side info with updated main_data_begin.
 * Copies the input side info bytes and patches main_data_begin.
 */
static void make_side_info(const queue_frame_t *qf, uint8_t *out) {
    const parsed_frame_t *pf = &qf->parsed;
    int si_size = mp3_side_info_size(&pf->header);

    /* Copy original side info */
    size_t si_offset = 4 + (pf->header.protection == 0 ? 2 : 0);
    memcpy(out, pf->raw + si_offset, si_size);

    /* Patch main_data_begin (first 9 bits MPEG1, 8 bits MPEG2) */
    int bits = (pf->header.version == MPEG_1) ? 9 : 8;
    int mdb = qf->main_data_begin;

    /* Clear and set the bits */
    for (int i = 0; i < bits; i++) {
        int bitpos = i;
        int byte = bitpos / 8;
        int bit = 7 - (bitpos % 8);
        if (mdb & (1 << (bits - 1 - i)))
            out[byte] |= (1 << bit);
        else
            out[byte] &= ~(1 << bit);
    }
}

int repacker_run(repacker_t *rq) {
    if (rq->nframes == 0) return -1;

    /* Step 1: Backwards pass — determine exact padding.
     * For now, simple version: each frame's payload must fit,
     * padding is whatever's needed. The full mark_q1 lookahead
     * is an optimization; start with the simple greedy version.
     */

    /* Step 2: Choose output bitrate for each frame */
    for (size_t i = 0; i < rq->nframes; i++) {
        queue_frame_t *qf = &rq->frames[i];
        size_t needed = qf->payload_bytes + qf->pad_exact;

        int padding;
        int bi = bytes_to_bitrate(rq, needed, &padding);

        /* Apply minimum bitrate */
        if (rq->min_bitrate > 0) {
            int min_bi = 1;
            for (int j = 1; j <= 14; j++) {
                int br = mp3_bitrates[rq->ref_header.version][j];
                if (br >= rq->min_bitrate) { min_bi = j; break; }
            }
            if (bi < min_bi) {
                bi = min_bi;
                /* recompute padding for the larger size */
                int sr = mp3_samplerates[rq->ref_header.version]
                                          [rq->ref_header.samplerate_idx];
                int br = mp3_bitrates[rq->ref_header.version][bi];
                int sz0 = mp3_frame_size(rq->ref_header.version, br, sr, 0);
                int si_size = mp3_side_info_size(&rq->ref_header);
                int cap0 = sz0 - 4 - si_size;
                padding = (cap0 >= (int)needed) ? 0 : 1;
            }
        }

        qf->out_bitrate_idx = bi;
        qf->out_padding = padding;

        int br = mp3_bitrates[rq->ref_header.version][bi];
        int sr = mp3_samplerates[rq->ref_header.version]
                                  [rq->ref_header.samplerate_idx];
        qf->out_size = mp3_frame_size(rq->ref_header.version, br, sr, padding);
    }

    /* Step 3: Layout — maximize reservoir (default).
     * Track where each frame's payload goes.
     * For now: payload stays in its own frame (main_data_begin = 0).
     * The full reservoir optimization comes next.
     */
    for (size_t i = 0; i < rq->nframes; i++) {
        rq->frames[i].main_data_begin = 0;
    }

    /* Step 4: Write output */
    /* Leading junk */
    if (!rq->delete_leading && rq->leading_junk_len > 0)
        out_write(rq, rq->leading_junk, rq->leading_junk_len);

    /* Xing/Info frame (MPEG-1 64kbps, size depends on sample rate) */
    {
        int sr = mp3_samplerates[rq->ref_header.version][rq->ref_header.samplerate_idx];
        int xing_fsize = mp3_frame_size(rq->ref_header.version, 64, sr, 0);
        uint8_t *xing = calloc(1, xing_fsize);
        if (!xing) return -1;
        /* Header: FF FB 54 00 (MPEG-1, Layer III, 64kbps, 48kHz, stereo) */
        /* Note: sample rate and channel mode should match ref_header, but
           for now we use the common 48kHz stereo template like the original */
        xing[0] = 0xFF;
        xing[1] = 0xFB;
        /* Build byte 2 from ref_header: bitrate idx 5 (64kbps), sample rate, padding 0 */
        int sr_idx = rq->ref_header.samplerate_idx;
        xing[2] = (5 << 4) | (sr_idx << 2);
        /* Byte 3: channel mode from ref_header, others 0 */
        int cm = 0; /* stereo */
        if (rq->ref_header.chan_mode == CH_MONO) cm = 3;
        else if (rq->ref_header.chan_mode == CH_JOINT) cm = 1;
        else if (rq->ref_header.chan_mode == CH_DUAL) cm = 2;
        xing[3] = (cm << 6);
        
        /* Side info is zeros (size depends on version/channel) */
        int xing_si_size = mp3_side_info_size(&rq->ref_header);
        /* Xing header at offset 4 + si_size */
        int xing_off = 4 + xing_si_size;
        /* "Info" for CBR, "Xing" for VBR */
        /* For now, always use "Info" since we're preserving input bitrate structure */
        /* TODO: detect VBR vs CBR properly */
        memcpy(xing + xing_off, "Info", 4);
        /* Flags: frames (1) + bytes (2) + TOC (4) = 7 */
        xing[xing_off + 4] = 0;
        xing[xing_off + 5] = 0;
        xing[xing_off + 6] = 0;
        xing[xing_off + 7] = 7;
        /* Frame count */
        uint32_t nframes = (uint32_t)rq->nframes;
        xing[xing_off + 8] = (nframes >> 24) & 0xFF;
        xing[xing_off + 9] = (nframes >> 16) & 0xFF;
        xing[xing_off + 10] = (nframes >> 8) & 0xFF;
        xing[xing_off + 11] = nframes & 0xFF;
        /* Byte count: total audio data bytes */
        size_t total_bytes = 0;
        for (size_t i = 0; i < rq->nframes; i++)
            total_bytes += rq->frames[i].out_size;
        total_bytes += xing_fsize; /* include Xing frame itself */
        uint32_t bcount = (uint32_t)total_bytes;
        xing[xing_off + 12] = (bcount >> 24) & 0xFF;
        xing[xing_off + 13] = (bcount >> 16) & 0xFF;
        xing[xing_off + 14] = (bcount >> 8) & 0xFF;
        xing[xing_off + 15] = bcount & 0xFF;
        /* TOC: 100 bytes linear ramp */
        for (int i = 0; i < 100; i++)
            xing[xing_off + 16 + i] = (uint8_t)((i * 255) / 99);
        
        out_write(rq, xing, xing_fsize);
        free(xing);
    }

    /* Frames - reservoir-preserving output */
    /* 
     * For VBR files with reservoir usage, we preserve the original frame
     * structure (sizes and mdb values) to ensure correctness. This doesn't
     * achieve optimal packing, but produces valid output.
     * 
     * TODO: Implement full Q3 queue for optimal VBR packing.
     */
    uint8_t hdr[4];
    
    /* Check if any frame uses reservoir */
    int uses_reservoir = 0;
    for (size_t i = 0; i < rq->nframes; i++) {
        if (rq->frames[i].parsed.side.main_data_begin > 0) {
            uses_reservoir = 1;
            break;
        }
    }
    
    if (uses_reservoir) {
        /* VBR mode: copy frame structure verbatim, regenerate Xing */
        /* For each frame, write header/side (with original mdb) + original main data */
        for (size_t i = 0; i < rq->nframes; i++) {
            queue_frame_t *qf = &rq->frames[i];
            parsed_frame_t *pf = &qf->parsed;
            
            /* Write header (preserve original bitrate) */
            uint8_t ohdr[4];
            memcpy(ohdr, pf->raw, 4);
            /* Clear CRC bit (we don't write CRCs) */
            ohdr[1] |= 0x01;
            out_write(rq, ohdr, 4);
            
            /* Write side info verbatim (preserve mdb) */
            int si_size = mp3_side_info_size(&pf->header);
            int si_offset = 4 + (pf->header.protection == 0 ? 2 : 0);
            out_write(rq, pf->raw + si_offset, si_size);
            
            /* Write main data verbatim */
            out_write(rq, pf->main_data, pf->main_data_len);
        }
    } else {
        /* CBR mode: optimize frame sizes (no reservoir needed) */
        for (size_t i = 0; i < rq->nframes; i++) {
            queue_frame_t *qf = &rq->frames[i];

            make_header(rq, qf, hdr);
            out_write(rq, hdr, 4);

            int si_size = mp3_side_info_size(&qf->parsed.header);
            out_reserve(rq, si_size);
            /* For CBR, mdb is always 0 */
            qf->main_data_begin = 0;
            make_side_info(qf, rq->out_data + rq->out_len);
            rq->out_len += si_size;

            /* Payload: copy from extracted buffer */
            if (qf->payload_bytes > 0 && qf->payload_data)
                out_write(rq, qf->payload_data, qf->payload_bytes);

            /* Padding to fill the frame */
            size_t written = 4 + si_size + qf->payload_bytes;
            size_t pad_needed = qf->out_size - written;
            out_reserve(rq, pad_needed);
            memset(rq->out_data + rq->out_len, 0, pad_needed);
            rq->out_len += pad_needed;
        }
    }

    /* Trailing junk */
    if (!rq->delete_trailing && rq->trailing_junk_len > 0)
        out_write(rq, rq->trailing_junk, rq->trailing_junk_len);

    return 0;
}
