# mp3packer-plus

A modern, cross-platform revival of the classic **mp3packer** by Reed Wilson ("Omion") — the lossless MP3 repacker that started it all.

**Original source:** https://github.com/snesnopic/mp3packer (GPL-2.0)
**Upstream:** Reed Wilson's mp3packer v2.05 (2006-2012)

## What is this?

mp3packer losslessly repacks MP3 files to remove padding and optimize frame sizes — without re-encoding. The decoded audio is bit-identical. Typical savings: 2-10% on 320kbps CBR files.

This fork adds:
- **Android (ARM64) builds** — runs in Termux
- **Modern CI** — Linux (x64/ARM64), macOS (x64/ARM64), Windows (x64), Android (ARM64)

## Quick start

### Desktop (Linux/macOS/Windows)
Download from Releases, then:
```
./mp3packer input.mp3 output.mp3
```

### Android (Termux)
1. Install Termux from F-Droid
2. Download the `mp3packer-android-arm64` binary from Releases
3. In Termux:
```
chmod +x mp3packer
./mp3packer /sdcard/Music/input.mp3 /sdcard/Music/output.mp3
```

## Common options

- `mp3packer in.mp3 out.mp3` — repack (keeps ID3v2, minimizes padding)
- `mp3packer -t in.mp3 out.mp3` — also strip ID3v2 tags
- `mp3packer -b 192 in.mp3 out.mp3` — convert to 192kbps CBR
- `mp3packer -r in.mp3 out.mp3` — minimize bit reservoir
- `mp3packer -i in.mp3` — show info only

## License

GPL-2.0 — see LICENSE. Derivative of Reed Wilson's mp3packer.
