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
 * mp3recomp.h -- The -z Huffman recompression optimizer.
 *
 * For each granule/channel with long blocks, this module:
 *   1. Decodes part2 (scalefactors) to locate part3 (Huffman data)
 *   2. Huffman-decodes part3 to 576 quantized coefficients
 *   3. Searches for optimal Huffman table selection via band-level DP
 *   4. Re-encodes with the best tables, keeping the result only if smaller
 *
 * Fail-safe: on any error, the original frame data is kept unchanged.
 */

#ifndef MP3RECOMP_H
#define MP3RECOMP_H

#include "mp3types.h"
#include "mp3queue.h"

/* Recompress a queued frame's payload with optimal Huffman tables.
 *
 * Takes qf->parsed (with side info) and qf->payload_data/qf->payload_bytes.
 * On success (returns 0):
 *   - If optimization helped: qf->payload_data, qf->payload_bytes, and
 *     qf->parsed.side are updated with the new encoding. qf->recompressed=1.
 *   - If not (or on recoverable error): frame is left unchanged, returns 0.
 * Returns -1 on catastrophic error (caller should keep original).
 *
 * Only long blocks (window_switching==0) are optimized. Short/mixed blocks,
 * intensity-stereo right channels, and undecodable frames pass through.
 */
int recompress_frame(queue_frame_t *qf);

/* Serialize side_info_t to bytes (for -z updated side info).
 * Writes mp3_side_info_size(hdr) bytes to out.
 */
void write_side_info_bytes(const frame_header_t *hdr, const side_info_t *side,
                           int main_data_begin, uint8_t *out);

#endif /* MP3RECOMP_H */
