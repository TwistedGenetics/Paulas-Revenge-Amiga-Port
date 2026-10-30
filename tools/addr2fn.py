import sys
m=[(int(l.split()[0],16),l.split()[1]) for l in open(sys.argv[1])]
m.sort()
for a in sys.argv[2:]:
    x=int(a,16)
    best=None
    for addr,name in m:
        if addr<=x: best=(addr,name)
    print(a, '->', best[1], '+0x%x'%(x-best[0]))
