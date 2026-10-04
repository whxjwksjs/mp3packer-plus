# mp3packer-plus C Engine

A portable C99 clean-room reimplementation of the mp3packer lossless MP3
repacking engine.

## Why C?

The reference implementation is OCaml 5.3, which is 64-bit only. A C port gives us:
- **32-bit support**: Linux x86, Windows x86, Android armeabi-v7a
- **No runtime**: single static binary, no OCaml runtime dependency
- **Tiny binaries**: suitable for old/weak hardware
- **Every platform**: one codebase builds everywhere via standard toolchains

## Project structure

```
src/
  mp3bit.h/c       Bit reader/writer (MSB-first)
  mp3types.h       Core data structures
  mp3tables.h/c    Bitrate/samplerate/Huffman/scalefactor tables
  mp3crc.h/c       CRC-16 (MP3 frame + LAME tag)
  mp3parse.h/c     Frame parser (header, side info, Xing)
  mp3huffman.h/c   Huffman encoder/decoder
  mp3frame.h/c     Frame decode/encode, -z recompression
  mp3queue.h/c     Repacking engine (reservoir, layout)
  main.c           CLI
  Makefile         Standard build
  ANDROID.md       NDK build instructions
```

## Build

```bash
cd src
make
./mp3packer-plus input.mp3 output.mp3
```

## Status

See `../spec/SPEC.md` for the algorithm specification and
`../spec/MODULE_MAP.md` for the reverse-engineered module map.

### Implementation phases
- [x] Spec + module map
- [ ] Bitstream, CRC, tables (in progress)
- [ ] Parser
- [ ] Huffman codec
- [ ] Frame encode/decode + `-z`
- [ ] Queue engine
- [ ] CLI
- [ ] Validation (bit-identical output)

## License

GPL-2.0. This is a reimplementation inspired by mp3packer by Reed Wilson
("Omion"). The original is GPL-2.0; this port retains GPL-2.0.
