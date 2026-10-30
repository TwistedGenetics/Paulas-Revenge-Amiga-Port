import os, struct, wave, hashlib, sys
from PIL import Image, ImageChops
S, H, RS, RH = sys.argv[1:5]
bad = 0
shots = sorted(f for f in os.listdir(S) if f.endswith('.ppm'))
for f in shots:
    d = ImageChops.difference(Image.open(S + '/' + f).convert('RGB'), Image.open(H + '/' + f).convert('RGB')).getbbox()
    if d: print('  %-26s DIFFERS in box %s' % (f, d)); bad += 1
print('screens: %d compared, %d differ' % (len(shots), bad))
w = wave.open(S + '/audio.wav'); n = w.getnframes(); sim = struct.unpack('<%dh' % (n * 2), w.readframes(n))
raw = open(H + '/audio.raw', 'rb').read(); host = struct.unpack('<%dh' % (len(raw) // 2), raw)
o = 2048; m = min(len(sim) - o, len(host))
ad = sum(1 for i in range(m) if sim[o + i] != host[i])
print('audio: %d of %d samples differ (%d non-silent)' % (ad, m, sum(1 for x in host if x)))
fd = 0
for f in sorted(set(os.listdir(RS)) | set(os.listdir(RH))):
    h = lambda p: hashlib.md5(open(p, 'rb').read()).hexdigest() if os.path.exists(p) else None
    same = h(RS + '/' + f) == h(RH + '/' + f)
    fd += not same
    print('  file %-12s %s' % (f, 'identical' if same else 'DIFFERS'))
sys.exit(1 if bad or ad or fd else 0)
