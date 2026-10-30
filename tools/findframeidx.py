#!/usr/bin/env python3
"""Find variable-indexed accesses to stack arrays (LLVM 18 m68k drops the index
register when the base is a frame index). Usage: findframeidx.py file.ll ..."""
import re, sys
bad = 0
for path in sys.argv[1:]:
    fn = None
    allocas = set()
    for line in open(path):
        m = re.match(r'define .*@(\w+)\(', line)
        if m: fn = m.group(1); allocas = set(); continue
        m = re.match(r'\s+%([\w.]+) = alloca ', line)
        if m: allocas.add(m.group(1)); continue
        m = re.search(r'getelementptr[^,]*, ptr %([\w.]+)((?:, i\d+ [^,]+)+)', line)
        if m and m.group(1) in allocas:
            idx = re.findall(r'i\d+ (%[\w.]+|-?\d+)', m.group(2))
            if any(i.startswith('%') for i in idx):
                print('%s: %s: variable index into stack array %%%s' % (path.split('/')[-1], fn, m.group(1)))
                bad += 1
sys.exit(1 if bad else 0)
