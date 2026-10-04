#ifndef MP3TYPES_H
#define MP3TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    MPEG_25 = 0,
    MPEG_2  = 2,
    MPEG_1  = 3
} mpeg_version_t;

typedef enum {
    CH_STEREO = 0,
    CH_JOINT  = 1,
    CH_DUAL   = 2,
    CH_MONO   = 3
} channel_mode_t;

typedef struct {
    mpeg_version_t version;
    int layer;              /* always 3 for Layer III */
    int protection;         /* 0 = CRC present, 1 = no CRC */
    int bitrate_idx;        /* 0-15 */
    int samplerate_idx;     /* 0-3 */
    int padding;
    int private_bit;
    channel_mode_t chan_mode;
    int mode_ext;
    int copyright;
    int original;
    int emphasis;
} frame_header_t;

typedef struct {
    int main_data_begin;    /* bytes */
    /* per granule per channel */
    int part2_3_length[2][2];  /* [granule][channel], in bits */
    int big_values[2][2];
    int global_gain[2][2];
    int scalefac_compress[2][2];
    int window_switching[2][2];
    /* long block */
    int table_select[2][2][3];
    int region0_count[2][2];
    int region1_count[2][2];
    /* short block */
    int block_type[2][2];
    int mixed_block[2][2];
    int subblock_gain[2][2][3];
    int preflag[2][2];
    int scalefac_scale[2][2];
    int count1table_select[2][2];
    int scfi[2][2];         /* scalefactor compression info */
} side_info_t;

typedef struct {
    frame_header_t header;
    side_info_t side;
    const uint8_t *raw;     /* pointer to frame start in input */
    size_t raw_len;         /* total frame bytes */
    const uint8_t *main_data; /* pointer to main data start */
    size_t main_data_len;
    int granules;           /* 2 for MPEG-1, 1 for MPEG-2/2.5 */
    int channels;           /* 2 for stereo, 1 for mono */
    size_t file_offset;     /* offset in input file */
} parsed_frame_t;

/* Output frame under construction */
typedef struct {
    uint8_t *header_side;   /* 4 + side_info bytes */
    size_t header_side_len;
    uint8_t *payload;       /* audio data bytes */
    size_t payload_len;
    size_t payload_cap;
    int bitrate_idx;
    int padding;
} out_frame_t;

#endif
