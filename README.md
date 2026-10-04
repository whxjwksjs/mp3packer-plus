# mp3packer

This is a port and revival of the original mp3packer created by **Reed Wilson ("Omion")**. The original project, last updated around 2012, is no longer available from its official source.  
This repository is maintained with the following goals:

* Modernization: Make the project compatible with modern OCaml (5.3.0+) and the Dune build system.  
* Cross-platform: Ensure the tool compiles and runs on Linux (x64, ARM64), macOS (x64, ARM64), Windows (x64), and FreeBSD (x64, ARM64).  
* Maintenance: Fix outstanding bugs and ensure stability.
* Library: Expose the core logic as a library for use in other projects.

This project honors the original **GPL-2.0 License** (see LICENSE file).  
The original documentation (c. 2012\) is preserved in the file [index.html](index.html).

## What is mp3packer?

(Abstract from the original documentation)  
MP3packer is a program which can rearrange the data within an MP3 to fulfill specific goals. By default, the program generates the smallest MP3 possible (with the least padding). However, many people also use it to turn VBR files into CBR for use with players which don't support VBR.

## Building from Source

This project uses the Dune build system.

1. Install Dependencies (opam):
   opam install dune dune-configurator

2. Build the Project:
   dune build

3. Run the Executable:  
   The executable will be located at \_build/default/mp3packer.exe.  
   ./\_build/default/mp3packer.exe \--help

## Download

Pre-compiled binaries for Linux, macOS, Windows, and FreeBSD are automatically built for every commit. You can download the binaries from the [Releases section](https://github.com/Snesnopic/mp3packer/actions).
