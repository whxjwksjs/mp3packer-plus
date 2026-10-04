#ifndef MP3PACKER_H
#define MP3PACKER_H

/* mp3packer-plus C engine — main CLI */

#define VERSION "1.0.0-c"

typedef struct {
    int min_bitrate;        /* -b: minimum bitrate kbps (0 = none) */
    int delete_begin;       /* -t: strip leading junk */
    int delete_end;         /* -s: strip trailing junk */
    int recompress;         /* -z: Huffman recompression */
    int minimize_reservoir; /* -r: minimize, -R: maximize (default) */
    int force_overwrite;    /* -f */
    int info_only;          /* -i */
    int workers;            /* --workers (for -z) */
    const char *in_path;
    const char *out_path;
} options_t;

void print_usage(const char *prog);

#endif
