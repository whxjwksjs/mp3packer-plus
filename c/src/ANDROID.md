# Android NDK Build Setup

## Prerequisites
- Android NDK r25 or later
- Set `ANDROID_NDK_HOME` to the NDK path

## Build for all Android ABIs

```bash
export ANDROID_NDK_HOME=/path/to/ndk

# 64-bit ARM (arm64-v8a) — modern phones
$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang \
    -O2 -std=c99 -o mp3packer-android-arm64 \
    mp3bit.c mp3crc.c mp3tables.c mp3parse.c mp3huffman.c mp3queue.c main.c

# 32-bit ARM (armeabi-v7a) — older phones
$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang \
    -O2 -std=c99 -o mp3packer-android-arm32 \
    mp3bit.c mp3crc.c mp3tables.c mp3parse.c mp3huffman.c mp3queue.c main.c

# 64-bit x86 (x86_64) — emulators, Chromebooks
$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/x86_64-linux-android21-clang \
    -O2 -std=c99 -o mp3packer-android-x64 \
    mp3bit.c mp3crc.c mp3tables.c mp3parse.c mp3huffman.c mp3queue.c main.c

# 32-bit x86 — old emulators
$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/i686-linux-android21-clang \
    -O2 -std=c99 -o mp3packer-android-x86 \
    mp3bit.c mp3crc.c mp3tables.c mp3parse.c mp3huffman.c mp3queue.c main.c
```

## Minimum API level
API 21 (Android 5.0) — covers 99%+ of devices in use.

## No dependencies
The binary is statically linked against the NDK's libc. No Google Play
Services, no Java, no Android framework dependencies. Runs in Termux
or via `adb shell`.

## Termux installation
```bash
# Copy to Termux
cp mp3packer-android-arm64 $PREFIX/bin/mp3packer
chmod +x $PREFIX/bin/mp3packer

# For 32-bit ARM devices, use mp3packer-android-arm32 instead
```

## CI
The `.github/workflows/android-ndk.yml` workflow builds all four ABIs
on every push using the NDK Docker image.
