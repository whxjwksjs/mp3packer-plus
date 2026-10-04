#ifndef MP3PARSE_H
#define MP3PARSE_H

#include "mp3types.h"
#include "mp3bit.h"

/* Parse a single frame header from 4 bytes (big-endian).
 * Returns 0 on success, -1 if invalid (bad sync, bad version/layer,
 * bad bitrate/samplerate/emphasis).
 */
int mp3_parse_header(const uint8_t *data, frame_header_t *hdr);

/* Find the next valid frame sync in the buffer.
 * Starts searching at *offset, updates *offset to frame start.
 * Requires 3 consecutive valid frames to confirm (anti-false-positive).
 * Returns 0 on success, -1 if not found.
 */
int mp3_find_sync(const uint8_t *data, size_t len, size_t *offset,
                  const frame_header_t *ref);

/* Parse side info for a frame.
 * data points to byte after header (and CRC if present).
 * Fills side_info_t. Returns bytes consumed, or -1 on error.
 */
int mp3_parse_side_info(const uint8_t *data, size_t len,
                        const frame_header_t *hdr, side_info_t *side);

/* Size of side info in bytes for the given header */
int mp3_side_info_size(const frame_header_t *hdr);

/* Parse a complete frame at offset.
 * Fills parsed_frame_t. Returns frame size in bytes, or -1 on error.
 */
int mp3_parse_frame(const uint8_t *data, size_t len, size_t offset,
                    parsed_frame_t *frame);

/* Check if two headers are compatible (same version, layer, samplerate,
 * channels for strict mode).
 */
int mp3_header_compatible(const frame_header_t *a, const frame_header_t *b,
                          int strict);

#endif
