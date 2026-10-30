#!/bin/bash
# Runs scripted sessions on the 68k binary (in sim/) and on the host build and
# requires identical screens, audio and saved files. Run from build/.
fail=0
for t in smoke full demo fuzz1; do
  rm -rf vH vS vrH vrS; mkdir -p vH vS vrH vrS
  cp simroot/test.prv simroot/sn.wav demo.prv vrH/; cp simroot/test.prv simroot/sn.wav demo.prv vrS/
  ../host/replay ../sim/$t.txt vH vrH > /dev/null
  if ! ./m68ksim PaulasRevenge ../sim/$t.txt vS vrS > vS/log 2>&1; then echo "$t: SIM FAILED"; tail -5 vS/log; fail=1; continue; fi
  if python3 ../tools/compare.py vS vH vrS vrH > vS/cmp 2>&1; then echo "$t: OK ($(grep screens vS/cmp))"
  else echo "$t: MISMATCH"; grep -v identical vS/cmp; fail=1; fi
done
exit $fail
