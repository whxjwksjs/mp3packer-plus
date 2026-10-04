#ifndef MP3QUEUE_H
#define MP3QUEUE_H

#include "mp3types.h"
#include "mp3parse.h"

/* A frame in the repacking queue */
typedef struct {
    parsed_frame_t parsed;    /* parsed input frame */
    size_t payload_bytes;     /* audio payload bytes (sum of part2_3_length/8) */
    uint8_t *payload_data;    /* copied payload bytes (reservoir-flattened) */
    size_t pad_exact;         /* exactly-determined trailing padding (bytes) */
    int out_bitrate_idx;      /* chosen output bitrate index */
    int out_padding;          /* chosen output padding bit */
    size_t out_size;          /* total output frame size in bytes */
    size_t main_data_begin;   /* output reservoir offset */
} queue_frame_t;

/* Repacker state */
typedef struct {
    queue_frame_t *frames;
    size_t nframes;
    size_t cap;

    /* input file data (for payload extraction) */
    const uint8_t *in_data;
    size_t in_len;

    /* config */
    int min_bitrate;          /* kbps, 0 = none */
    int minimize_reservoir;   /* -r flag */

    /* reservoir */
    int max_reservoir;        /* 511 (MPEG1) or 255 (MPEG2/2.5) */

    /* first frame reference */
    frame_header_t ref_header;

    /* output */
    uint8_t *out_data;
    size_t out_len;
    size_t out_cap;

    /* leading/trailing junk */
    const uint8_t *leading_junk;
    size_t leading_junk_len;
    const uint8_t *trailing_junk;
    size_t trailing_junk_len;
    int delete_leading;
    int delete_trailing;

    /* bit reservoir for input (accumulates main data across frames) */
    uint8_t *reservoir;
    size_t reservoir_len;
    size_t reservoir_cap;

    /* bit reservoir for output (preserves reservoir structure) */
    uint8_t *out_reservoir;
    size_t out_reservoir_len;
    size_t out_reservoir_cap;
} repacker_t;

void repacker_init(repacker_t *r);
void repacker_free(repacker_t *r);

/* Add a parsed frame to the queue. Returns 0 on success. */
int repacker_add_frame(repacker_t *rq, const parsed_frame_t *pf);

/* Run the repacking. Returns 0 on success.
 * Output is in rq->out_data with length rq->out_len.
 */
int repacker_run(repacker_t *rq);

#endif
