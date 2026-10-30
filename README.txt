PAULA'S REVENGE 1200                                     version 0.1
A native Amiga 1200 tracker in the spirit of OctaMED 4, built for 060 /
PiStorm machines, with sampling and arranging tools the original Amiga
couldn't do.

REQUIREMENTS
  Amiga 1200 (AGA), Kickstart 3.0 or later, 68020 or better.
  Tuned for a 68060 or PiStorm: 16 mixed tracks need a fast CPU.
  PAL display (640x256). Fast RAM strongly recommended for samples.

RUNNING
  From the floppy image: boot PaulasRevenge.adf, it starts by itself.
  From hard disk: copy PaulasRevenge (and demo.prv) anywhere, then run
  it from a Shell. ESC quits and returns to Workbench.
  Press F10, select demo.prv and press RETURN, then RETURN again to play.

WHAT IT DOES
  - OctaMED-style block editor, 1-16 tracks, blocks of 1-256 lines,
    two-row piano keyboard, F1-F5 octaves, hex command columns.
  - 16-voice software mixer, 16-bit internal, 14-bit stereo Paula output
    (about 28.6 kHz). Alt+I switches Paula-style or smooth interpolation.
  - Arrangement: named SECTIONS (Intro, Drop, Breakdown...) made of
    blocks, laid out on a TIMELINE with a proportional song overview.
  - Linked BLOCK VARIATIONS (Ctrl+L): a copy whose untouched tracks follow
    the original. Edit a track and only that track becomes the variation's
    own. Ctrl+R relinks it. "V" in the section library copies a whole
    section as variations.
  - Sample editor: mouse range selection, zoom, normalise, reverse, fades,
    silence, gain, crop, cut, resample, loop from range, loop crossfade,
    one-level undo.
  - CHOP TO KEYS: H chops into equal slices, T finds the hits. Slice 1
    plays on C-3, slice 2 on C#3 and so on. J copies slices to separate
    instruments.
  - Parallel-port sampler: R records (mono, or left/right with Q), M is a
    live input monitor. Left mouse button stops a recording early.
  - Loads IFF 8SVX, WAV (8/16/24-bit), AIFF and raw samples. Saves songs
    (.prv) and samples (.8svx or .wav).
  Press HELP inside the program for every key.

EFFECT COMMANDS (OctaMED numbering where it exists)
  00xy arpeggio       01xx slide up      02xx slide down
  03xx portamento     04xy vibrato       05/06 porta/vibrato + vol slide
  08xx pan (our own)  09xx speed         0Bxx jump to timeline position
  0Cxx volume 00-40   0Dxy volume slide  0F00 next block
  0Fxx tempo (BPM)    0FFE stop song     0FFF note off
  19xx sample offset  1Fxy delay x ticks, retrigger every y ticks

KNOWN LIMITS IN 0.1
  - No import of OctaMED .med/MMD files yet.
  - Tested in a 68020 simulator and against a PC build of the same code,
    not yet on real hardware. Please report anything odd.
  - The parallel-port sampler code has not been tried with real samplers.
  - NTSC screens will cut off the bottom rows.
