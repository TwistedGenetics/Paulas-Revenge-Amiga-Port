#!/bin/sh
# Build PaulasRevenge (AmigaOS 68020+/060 executable) with clang + tools/hunklink.py
set -e
cd "$(dirname "$0")"
CLANG=${CLANG:-clang}
LLC=${LLC:-llc-18}
LLVMDIS=${LLVMDIS:-llvm-dis-18}
mkdir -p build build/asm
CFLAGS="--target=m68k-unknown-none-elf -mcpu=68020 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-common -Wall -Wno-unused-function"
LLCFLAGS="--m68k-ssection-threshold=0 --disable-post-ra"
OBJS="build/start.o"
$CLANG --target=m68k-unknown-none-elf -mcpu=68020 -c amiga/start.s -o build/start.o
for f in amiga/plat_amiga.c src/libc.c src/song.c src/player.c src/sampletools.c src/songio.c src/draw.c src/ui.c src/ui_block.c src/ui_arrange.c src/ui_sample.c src/ui_misc.c; do
  o=build/$(basename "$f" .c).o
  s=build/asm/$(basename "$f" .c)
  # bitcode -> llc so the machine IR listing and the object come from the same codegen
  $CLANG $CFLAGS -emit-llvm -c "$f" -o "$s.bc"
  $LLVMDIS "$s.bc" -o "$s.ll"
  python3 tools/findframeidx.py "$s.ll"
  # LLVM 18 m68k bugs worked around at machine-IR level, see tools/fixccr.py and tools/fixdiv.py
  $LLC -O2 "$s.bc" -stop-before=postrapseudos -o "$s.pre.mir" $LLCFLAGS
  python3 tools/fixccr.py "$s.pre.mir" "$s.fix.mir"
  $LLC -O2 -start-before=postrapseudos "$s.fix.mir" -filetype=obj -o "$o" $LLCFLAGS
  $LLC -O2 -start-before=postrapseudos "$s.fix.mir" -stop-after=stack-frame-layout -o "$s.mir" $LLCFLAGS
  python3 tools/fixdiv.py "$s.mir" "$o"
  OBJS="$OBJS $o"
done
# hunklink.py refuses to link if the backend wrote to a global (illegal PC-relative store)
python3 tools/hunklink.py $OBJS -o build/PaulasRevenge -Map build/PaulasRevenge.map
