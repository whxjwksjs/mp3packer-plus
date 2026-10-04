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
 * mp3crc.h -- CRC-16 implementations used by mp3packer.
 *
 * Two variants, faithfully translated from the original OCaml crc.ml:
 *
 * 1. MP3 frame CRC ("mp3_*"): generator polynomial 0x18005, i.e. 0x8005
 *    with the x^16 term implied. MSB-first (non-reflected) table.
 *    Initial value 0xFFFF, no final XOR. Used for the 16-bit CRC that
 *    optionally follows an MP3 frame header (protection bit clear).
 *
 * 2. LAME tag CRC ("lame_*"): the table stolen directly from LAME's
 *    VbrTag.c. It is a *reflected* (LSB-first) CRC with polynomial
 *    0xA001 -- the bit-reversal of the MP3 CRC's 0x8005 -- i.e.
 *    CRC-16/ARC, NOT CRC-16/CCITT (0x1021). Initial value 0x0000.
 *    Used for the CRC field of the LAME/Xing tag.
 */

#ifndef MP3CRC_H
#define MP3CRC_H

#include <stdint.h>
#include <stddef.h>

/* --- MP3 frame CRC: poly 0x8005 (x^16 implied), init 0xFFFF, no final XOR --- */

uint16_t mp3_crc_update(uint16_t crc, uint8_t byte);
uint16_t mp3_crc_compute(const uint8_t *data, size_t len); /* initial 0xFFFF */

/* --- LAME tag CRC: reflected poly 0xA001 (CRC-16/ARC), init 0x0000 --- */

uint16_t lame_crc_update(uint16_t crc, uint8_t byte);
uint16_t lame_crc_compute(const uint8_t *data, size_t len); /* initial 0x0000 */

#endif
