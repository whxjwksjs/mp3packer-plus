#include "mp3parse.h"
#include "mp3tables.h"
#include <string.h>

int mp3_parse_header(const uint8_t *data, frame_header_t *hdr) {
    /* 11-bit sync */
    if (data[0] != 0xFF || (data[1] & 0xE0) != 0xE0)
        return -1;

    int ver_bits = (data[1] >> 3) & 0x03;
    int layer_bits = (data[1] >> 1) & 0x03;

    if (ver_bits == 1) return -1;   /* reserved */
    if (layer_bits != 1) return -1; /* must be Layer III */

    int bitrate_idx = (data[2] >> 4) & 0x0F;
    int sr_idx = (data[2] >> 2) & 0x03;
    int emphasis = data[3] & 0x03;

    if (bitrate_idx == 15) return -1;
    if (bitrate_idx == 0) return -1;  /* free format not supported */
    if (sr_idx == 3) return -1;
    if (emphasis == 2) return -1;

    mpeg_version_t ver;
    switch (ver_bits) {
        case 0: ver = MPEG_25; break;
        case 2: ver = MPEG_2; break;
        case 3: ver = MPEG_1; break;
        default: return -1;
    }

    hdr->version = ver;
    hdr->layer = 3;
    hdr->protection = (data[1] & 0x01);
    hdr->bitrate_idx = bitrate_idx;
    hdr->samplerate_idx = sr_idx;
    hdr->padding = (data[2] >> 1) & 0x01;
    hdr->private_bit = data[2] & 0x01;
    hdr->chan_mode = (channel_mode_t)((data[3] >> 6) & 0x03);
    hdr->mode_ext = (data[3] >> 4) & 0x03;
    hdr->copyright = (data[3] >> 3) & 0x01;
    hdr->original = (data[3] >> 2) & 0x01;
    hdr->emphasis = emphasis;

    return 0;
}

int mp3_side_info_size(const frame_header_t *hdr) {
    int mono = (hdr->chan_mode == CH_MONO);
    if (hdr->version == MPEG_1)
        return mono ? 17 : 32;
    else
        return mono ? 9 : 17;
}

/* Parse one granule/channel block */
static void parse_gc(bit_reader_t *r, side_info_t *side,
                     int g, int ch, mpeg_version_t ver) {
    side->part2_3_length[g][ch] = bit_read(r, 12);
    side->big_values[g][ch] = bit_read(r, 9);
    side->global_gain[g][ch] = bit_read(r, 8);
    side->scalefac_compress[g][ch] = bit_read(r, ver == MPEG_1 ? 4 : 9);
    side->window_switching[g][ch] = bit_read(r, 1);

    if (side->window_switching[g][ch] == 0) {
        side->table_select[g][ch][0] = bit_read(r, 5);
        side->table_select[g][ch][1] = bit_read(r, 5);
        side->table_select[g][ch][2] = bit_read(r, 5);
        side->region0_count[g][ch] = bit_read(r, 4);
        side->region1_count[g][ch] = bit_read(r, 3);
    } else {
        side->block_type[g][ch] = bit_read(r, 2);
        side->mixed_block[g][ch] = bit_read(r, 1);
        side->table_select[g][ch][0] = bit_read(r, 5);
        side->table_select[g][ch][1] = bit_read(r, 5);
        side->subblock_gain[g][ch][0] = bit_read(r, 3);
        side->subblock_gain[g][ch][1] = bit_read(r, 3);
        side->subblock_gain[g][ch][2] = bit_read(r, 3);
    }

    side->preflag[g][ch] = (ver == MPEG_1) ? bit_read(r, 1) : 0;
    side->scalefac_scale[g][ch] = bit_read(r, 1);
    side->count1table_select[g][ch] = bit_read(r, 1);
}

int mp3_parse_side_info(const uint8_t *data, size_t len,
                        const frame_header_t *hdr, side_info_t *side) {
    int mono = (hdr->chan_mode == CH_MONO);
    int granules = (hdr->version == MPEG_1) ? 2 : 1;
    int channels = mono ? 1 : 2;

    memset(side, 0, sizeof(*side));

    bit_reader_t r;
    bit_reader_init(&r, data, len);

    if (hdr->version == MPEG_1) {
        side->main_data_begin = bit_read(&r, 9);
        bit_reader_skip(&r, mono ? 5 : 3);  /* private bits */
        /* SCFI: 4 bits per channel (scalefactor reuse flags) */
        for (int ch = 0; ch < channels; ch++) {
            int b0 = bit_read(&r, 1);
            int b1 = bit_read(&r, 1);
            int b2 = bit_read(&r, 1);
            int b3 = bit_read(&r, 1);
            /* pack into scfi[ch][0..1] as 2-bit pairs */
            side->scfi[ch][0] = (b0 << 1) | b1;
            side->scfi[ch][1] = (b2 << 1) | b3;
        }
    } else {
        side->main_data_begin = bit_read(&r, 8);
        bit_reader_skip(&r, mono ? 1 : 2);  /* private bits */
    }

    for (int g = 0; g < granules; g++)
        for (int ch = 0; ch < channels; ch++)
            parse_gc(&r, side, g, ch, hdr->version);

    return 0;
}

int mp3_parse_frame(const uint8_t *data, size_t len, size_t offset,
                    parsed_frame_t *frame) {
    if (offset + 4 > len) return -1;

    memset(frame, 0, sizeof(*frame));
    frame->file_offset = offset;

    if (mp3_parse_header(data + offset, &frame->header) != 0)
        return -1;

    int bitrate = mp3_bitrates[frame->header.version][frame->header.bitrate_idx];
    int sr = mp3_samplerates[frame->header.version][frame->header.samplerate_idx];
    int fsize = mp3_frame_size(frame->header.version, bitrate,
                               sr, frame->header.padding);

    if (offset + (size_t)fsize > len) return -1;

    frame->raw = data + offset;
    frame->raw_len = fsize;
    frame->granules = (frame->header.version == MPEG_1) ? 2 : 1;
    frame->channels = (frame->header.chan_mode == CH_MONO) ? 1 : 2;

    size_t pos = offset + 4;
    if (frame->header.protection == 0)
        pos += 2;  /* CRC */

    int si_size = mp3_side_info_size(&frame->header);
    if (mp3_parse_side_info(data + pos, si_size, &frame->header,
                            &frame->side) != 0)
        return -1;

    pos += si_size;
    frame->main_data = data + pos;
    frame->main_data_len = offset + fsize - pos;

    return fsize;
}

int mp3_header_compatible(const frame_header_t *a, const frame_header_t *b,
                          int strict) {
    if (a->version != b->version) return 0;
    if (a->layer != b->layer) return 0;
    if (a->samplerate_idx != b->samplerate_idx) return 0;
    if (strict) {
        if (a->chan_mode != b->chan_mode) return 0;
    }
    return 1;
}

/* Find sync with 3-frame confirmation */
int mp3_find_sync(const uint8_t *data, size_t len, size_t *offset,
                  const frame_header_t *ref) {
    size_t pos = *offset;

    while (pos + 4 < len) {
        /* quick sync check */
        if (data[pos] != 0xFF || (data[pos+1] & 0xE0) != 0xE0) {
            pos++;
            continue;
        }

        frame_header_t h;
        if (mp3_parse_header(data + pos, &h) != 0) {
            pos++;
            continue;
        }

        if (ref && !mp3_header_compatible(&h, ref, 0)) {
            pos++;
            continue;
        }

        /* Verify with 2 more consecutive frames */
        int bitrate = mp3_bitrates[h.version][h.bitrate_idx];
        int sr = mp3_samplerates[h.version][h.samplerate_idx];
        int fsize = mp3_frame_size(h.version, bitrate, sr, h.padding);

        int ok = 1;
        size_t p = pos;
        frame_header_t prev = h;
        for (int i = 0; i < 2; i++) {
            p += fsize;
            if (p + 4 > len) { ok = 0; break; }
            if (data[p] != 0xFF || (data[p+1] & 0xE0) != 0xE0) { ok = 0; break; }
            frame_header_t h2;
            if (mp3_parse_header(data + p, &h2) != 0) { ok = 0; break; }
            if (!mp3_header_compatible(&h2, &prev, 1)) { ok = 0; break; }
            bitrate = mp3_bitrates[h2.version][h2.bitrate_idx];
            sr = mp3_samplerates[h2.version][h2.samplerate_idx];
            fsize = mp3_frame_size(h2.version, bitrate, sr, h2.padding);
            prev = h2;
        }

        if (ok) {
            *offset = pos;
            return 0;
        }
        pos++;
    }

    return -1;
}
