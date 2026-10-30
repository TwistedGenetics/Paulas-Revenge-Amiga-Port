#!/bin/bash
# Full build + verification, as run by GitHub Actions. From the repo root:  tools/ci.sh
set -e
cd "$(dirname "$0")/.."
./build.sh
HOSTSRC="../src/song.c ../src/player.c ../src/sampletools.c ../src/songio.c ../src/draw.c ../src/ui.c ../src/ui_block.c ../src/ui_arrange.c ../src/ui_sample.c ../src/ui_misc.c ../src/libc.c"
cd host
gcc -DHOST -O1 -g -fsanitize=address,undefined -Wall -Wno-unused-parameter -o test test.c plat_host.c $HOSTSRC -lm
gcc -DHOST -O1 -g -Wall -Wno-unused-parameter -o replay replay.c $HOSTSRC -lm
gcc -DHOST -O1 -g -Wall -Wno-unused-parameter -o mkdemo mkdemo.c plat_host.c $HOSTSRC -lm
cd ../build
gcc -O2 -Wall -Wno-unused-result -Wno-format-truncation -o m68ksim ../sim/cpu.c ../sim/os.c
mkdir -p simroot unit
echo "== host unit tests"
ASAN_OPTIONS=detect_leaks=0 ../host/test unit
cp unit/test.prv unit/sn.wav simroot/
../host/mkdemo demo.prv unit/demo.wav
echo "== 68k binary vs host build"
../tools/verify.sh
echo "== packaging"
mkdir -p ../dist
printf 'PaulasRevenge\n' > startup-sequence
python3 ../tools/mkadf.py ../dist/PaulasRevenge.adf PaulasRevenge PaulasRevenge=PaulasRevenge demo.prv=demo.prv README.txt=../README.txt s/startup-sequence=startup-sequence
python3 ../tools/readadf.py ../dist/PaulasRevenge.adf
cp PaulasRevenge demo.prv ../dist/
