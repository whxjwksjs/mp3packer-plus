#include "mp3packer.h"
#include "mp3queue.h"
#include "mp3parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_usage(const char *prog) {
    printf("mp3packer-plus %s — lossless MP3 repacker (C port)\n", VERSION);
    printf("Usage: %s [options] input.mp3 [output.mp3]\n", prog);
    printf("\nOptions:\n");
    printf("  -b N    Minimum bitrate kbps (0 = none)\n");
    printf("  -t      Strip leading junk (ID3v2)\n");
    printf("  -s      Strip trailing junk (ID3v1/APEv2)\n");
    printf("  -r      Minimize bit reservoir\n");
    printf("  -R      Maximize bit reservoir (default)\n");
    printf("  -z      Recompress Huffman data (lossless, slower)\n");
    printf("  -i      Info only\n");
    printf("  -f      Force overwrite\n");
    printf("  -h      This help\n");
}

static uint8_t *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(len);
    if (fread(data, 1, len, f) != (size_t)len) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len_out = len;
    return data;
}

static int write_file(const char *path, const uint8_t *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return (w == len) ? 0 : -1;
}

int main(int argc, char **argv) {
    options_t opts;
    memset(&opts, 0, sizeof(opts));

    /* Simple arg parsing */
    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-') break;
        if (strcmp(argv[i], "-b") == 0 && i+1 < argc)
            opts.min_bitrate = atoi(argv[++i]);
        else if (strcmp(argv[i], "-t") == 0)
            opts.delete_begin = 1;
        else if (strcmp(argv[i], "-s") == 0)
            opts.delete_end = 1;
        else if (strcmp(argv[i], "-r") == 0)
            opts.minimize_reservoir = 1;
        else if (strcmp(argv[i], "-R") == 0)
            opts.minimize_reservoir = 0;
        else if (strcmp(argv[i], "-z") == 0)
            opts.recompress = 1;
        else if (strcmp(argv[i], "-i") == 0)
            opts.info_only = 1;
        else if (strcmp(argv[i], "-f") == 0)
            opts.force_overwrite = 1;
        else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (i >= argc) {
        print_usage(argv[0]);
        return 1;
    }

    opts.in_path = argv[i++];
    if (i < argc)
        opts.out_path = argv[i];

    if (opts.recompress) {
        printf("Recompression (-z) enabled\n");
    }

    /* Read input */
    size_t in_len;
    uint8_t *in_data = read_file(opts.in_path, &in_len);
    if (!in_data) {
        fprintf(stderr, "Cannot read %s\n", opts.in_path);
        return 1;
    }

    /* Find first frame */
    size_t offset = 0;
    if (mp3_find_sync(in_data, in_len, &offset, NULL) != 0) {
        fprintf(stderr, "No MP3 frames found\n");
        free(in_data);
        return 1;
    }

    if (opts.info_only) {
        printf("First frame at offset %zu\n", offset);
        /* TODO: full info mode */
        free(in_data);
        return 0;
    }

    if (!opts.out_path) {
        fprintf(stderr, "Output path required\n");
        free(in_data);
        return 1;
    }

    /* Check overwrite */
    if (!opts.force_overwrite) {
        FILE *f = fopen(opts.out_path, "rb");
        if (f) {
            fclose(f);
            fprintf(stderr, "Output exists, use -f to overwrite\n");
            free(in_data);
            return 1;
        }
    }

    /* Set up repacker */
    repacker_t rq;
    repacker_init(&rq);
    rq.in_data = in_data;
    rq.in_len = in_len;
    rq.min_bitrate = opts.min_bitrate;
    rq.minimize_reservoir = opts.minimize_reservoir;
    rq.recompress = opts.recompress;
    rq.delete_leading = opts.delete_begin;
    rq.delete_trailing = opts.delete_end;

    /* Leading junk */
    if (offset > 0) {
        rq.leading_junk = in_data;
        rq.leading_junk_len = offset;
    }

    /* Parse all frames */
    size_t pos = offset;
    parsed_frame_t pf;
    size_t last_end = offset;
    while (pos < in_len) {
        int fsize = mp3_parse_frame(in_data, in_len, pos, &pf);
        if (fsize < 0) break;
        
        /* Skip Xing/Info frames (they're metadata, not audio) */
        int si_size = mp3_side_info_size(&pf.header);
        int xing_off = 4 + (pf.header.protection ? 0 : 2) + si_size;
        if (pos + xing_off + 4 <= in_len) {
            const uint8_t *xp = in_data + pos + xing_off;
            if ((xp[0] == 'X' && xp[1] == 'i' && xp[2] == 'n' && xp[3] == 'g') ||
                (xp[0] == 'I' && xp[1] == 'n' && xp[2] == 'f' && xp[3] == 'o')) {
                /* Skip this frame */
                last_end = pos + fsize;
                pos += fsize;
                continue;
            }
        }
        
        if (repacker_add_frame(&rq, &pf) != 0) break;
        last_end = pos + fsize;
        pos += fsize;
    }

    /* Trailing junk */
    if (last_end < in_len) {
        rq.trailing_junk = in_data + last_end;
        rq.trailing_junk_len = in_len - last_end;
    }

    printf("Parsed %zu frames\n", rq.nframes);

    /* Run repacker */
    if (repacker_run(&rq) != 0) {
        fprintf(stderr, "Repacking failed\n");
        repacker_free(&rq);
        free(in_data);
        return 1;
    }

    /* Write output */
    if (write_file(opts.out_path, rq.out_data, rq.out_len) != 0) {
        fprintf(stderr, "Cannot write %s\n", opts.out_path);
        repacker_free(&rq);
        free(in_data);
        return 1;
    }

    printf("Wrote %zu bytes (input %zu bytes)\n", rq.out_len, in_len);

    repacker_free(&rq);
    free(in_data);
    return 0;
}
