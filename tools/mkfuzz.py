import random, sys
seed, n = int(sys.argv[1]), int(sys.argv[2])
r = random.Random(seed)
keys = [k for k in range(0x00, 0x5F) if k not in (0x13, 0x37, 0x45, 0x0D, 0x2B, 0x30, 0x0E, 0x1C, 0x2C, 0x3B, 0x47, 0x48, 0x49, 0x4B)]
weights_screen = [0x55, 0x56, 0x57, 0x58, 0x59, 0x5f]
quals = [0, 0, 0, 0, 1, 8, 0x10, 9]
out = ['5 key 59 0', '7 key 4d 0', '9 key 44 0']    # load the demo song first
t = 11
for i in range(n):
    x = r.random()
    if x < 0.06: out.append('%d key %x 0' % (t, r.choice(weights_screen)))
    elif x < 0.10:
        out.append('%d move %d %d' % (t, r.randint(-120, 120), r.randint(-60, 60)))
    elif x < 0.13:
        out.append('%d lmb 1 0' % t); t += 1
        out.append('%d move %d %d' % (t, r.randint(-150, 150), r.randint(-20, 20))); t += 1
        out.append('%d lmb 0 0' % t)
    elif x < 0.16: out.append('%d key 44 %x' % (t, r.choice([0, 1])))      # play
    elif x < 0.18: out.append('%d key 40 0' % t)                            # stop / edit
    else: out.append('%d key %x %x' % (t, r.choice(keys), r.choice(quals)))
    t += r.choice([1, 1, 2, 3])
    if i % 20 == 19: out.append('%d shot z%05d' % (t, t)); t += 1
out.append('%d key 40 0' % t); t += 2
out += ['%d key 45 0' % t, '%d type y' % (t + 1), '%d key 44 0' % (t + 2), '%d key 45 0' % (t + 4), '%d type y' % (t + 5), '%d key 44 0' % (t + 6)]
open(sys.argv[3], 'w').write('\n'.join(out) + '\n')
