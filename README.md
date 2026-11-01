# Paula's Revenge 1200

A native Amiga 1200 tracker in the spirit of OctaMED 4, built for 68060 / PiStorm machines, with sampling and arranging tools the original Amiga couldn't do. Sibling of the desktop [Paula's Revenge](https://github.com/TwistedGenetics/Paula-s-Revenge).

**Status: v0.1.** Verified in a 68020 simulator against a PC build of the same code; not yet run on real hardware.

## Requirements

- Amiga 1200 (AGA), Kickstart 3.0+, 68020 or better (tuned for 68060 / PiStorm)
- PAL display (640×256), Fast RAM recommended

## Getting it

**[Download the latest release](https://github.com/TwistedGenetics/Paulas-Revenge-Amiga-Port/releases/latest)**: `PaulasRevenge.adf` (bootable floppy), or the zip with the executable, demo song, emulator configs and manual.

Every push also builds a test copy, available from the **Actions** tab (latest run → *PaulasRevenge-amiga*).

Boot the ADF, or copy the executable to your hard drive and run it from a Shell. Press **F10**, open `demo.prv`, then **Return** to play. **HELP** lists every key.

## Emulators

Ready-made configs for **WinUAE** (Windows) and **FS-UAE** (Mac/Linux/Windows) are in [`emulators/`](emulators) and included in every build download. They emulate an A1200 with a fast JIT 68040, similar to a PiStorm. You supply your own Kickstart 3.x ROM. See [emulators/EMULATORS.txt](emulators/README.txt).

## Features

- OctaMED-style block editor: 1–16 tracks, blocks of 1–256 lines, two-row piano keyboard, F1–F5 octaves
- 16-voice software mixer, 16-bit internal, 14-bit stereo Paula output (~28.6 kHz); Paula-style or smooth interpolation
- Named **sections** on a **timeline** with a proportional song overview
- **Linked block variations** (Ctrl+L): untouched tracks follow the original, edited tracks become the variation's own
- Sample editor: mouse ranges, zoom, normalise, reverse, fades, gain, crop, cut, resample, loop crossfade, undo
- **Chop to keys**: equal slices (H) or transient detection (T), slice 1 on C-3 upward; slices to instruments (J)
- Parallel-port sampler recording and input monitor
- Loads IFF 8SVX, WAV, AIFF, raw; saves songs (`.prv`) and samples (`.8svx` / `.wav`)

See [README.txt](README.txt) for effect commands and known limits.

## Building

```
CLANG=clang-18 ./build.sh     # -> build/PaulasRevenge
tools/ci.sh                   # build + tests + ADF, as CI runs it
```

Needs clang/llc 18 and Python 3 (plus Pillow for the comparison tests). The LLVM 18 m68k backend has several code-generation bugs; the build works around all of them and fails loudly on anything new. Details in [BUILDING.txt](BUILDING.txt).
