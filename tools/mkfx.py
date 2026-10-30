# generates sim/fx.txt: types an effects-heavy block, plays it, records from the sampler
f = []; t = [5]
def at(kind, a='', step=2):
    f.append('%d %s %s' % (t[0], kind, a)); t[0] += step
def key(code, q=0): at('key', '%x %x' % (code, q))
UP, DOWN, RIGHT, LEFT, TAB = 0x4c, 0x4d, 0x4e, 0x4f, 0x42
hexkey = {'0':0x0a,'1':1,'2':2,'3':3,'4':4,'5':5,'6':6,'7':7,'8':8,'9':9,'a':0x20,'b':0x35,'c':0x33,'d':0x22,'e':0x12,'f':0x23}
key(0x59); key(0x4d); key(0x44); at('shot', 'f00_loaded')          # load test.prv
key(0x55)                                                          # block editor
key(0x36, 8)                                                       # Ctrl+N new block
key(0x0a, 0x10)                                                    # Alt+0: step 0
key(0x5e); key(0x5e); key(0x5e)                                    # instrument 4
key(0x40)                                                          # edit on
cur = {'line': 0, 'col': 0}
def go(line, col):
    while cur['line'] < line: key(DOWN); cur['line'] += 1
    while cur['line'] > line: key(UP); cur['line'] -= 1
    while cur['col'] < col: key(RIGHT); cur['col'] += 1
    while cur['col'] > col: key(LEFT); cur['col'] -= 1
def note(line, k): go(line, 0); key(k)
def cmd(line, hx):
    go(line, 3)
    for i, c in enumerate(hx):
        go(line, 3 + i); key(hexkey[c])
# track 0: pitch effects on the looped sub (instr 4)
note(0, 0x31); cmd(0, '0037')
cmd(8, '0108')
note(16, 0x33); cmd(16, '0208')
note(24, 0x34); cmd(24, '0310')
cmd(32, '0448')
cmd(40, '0502')
cmd(48, '0620')
cmd(52, '0d04')
cmd(56, '0c20')
cmd(60, '0880')
note(62, 0x01)                                                     # note off
go(0, 0); key(TAB); cur['col'] = 0
key(0x5e)                                                          # instrument 5 (chopped break)
for line, k, c in [(0, 0x31, None), (4, 0x21, '1f20'), (8, 0x32, '1904'), (12, 0x22, '1f03'), (16, 0x33, None), (20, 0x34, '0c18'), (24, 0x24, None), (28, 0x35, '1f12')]:
    note(line, k)
    if c: cmd(line, c)
go(0, 0); key(TAB); cur['col'] = 0
cmd(30, '0903'); cmd(31, '0f8c'); cmd(63, '0f00')
key(0x40)                                                          # edit off
at('shot', 'f01_fx_block')
go(0, 0); key(0x44, 1)                                             # Shift+Return: loop block
t[0] += 200
at('shot', 'f02_fx_playing')
key(0x40)                                                          # stop
key(0x08, 0x10)                                                    # Alt+I smooth mixing
key(0x44, 1); t[0] += 120; key(0x40)
key(0x34, 0x10); key(0x34, 0x11)                                   # Alt+V up, Alt+Shift+V down
# sampler: monitor + record 1 s at 22050 Hz into a fresh slot
key(0x57); key(0x1b); key(0x1b); key(0x1b); key(0x1b); key(0x1b)    # F8, ] x5
key(0x37); t[0] += 4; at('shot', 'f03_monitor'); key(0x37)
key(0x10)                                                          # Q: right channel
key(0x13); at('type', '22050'); key(0x44); at('type', '1'); key(0x44)
t[0] += 6
at('shot', 'f04_recorded')
key(0x20); key(0x40); t[0] += 30                                   # select all, play
# block/section management paths
key(0x55); key(0x46, 8); at('type', 'y'); key(0x44)                # Ctrl+Del delete block
key(0x56); key(TAB); key(TAB); key(0x36); at('type', 'outro'); key(0x44)   # new section
key(0x17); key(TAB); key(0x3a); key(0x0c); key(0x0b)               # put on timeline, move, +/- 
key(TAB); key(0x20); key(0x36); key(0x26 if False else 0x46)       # add block, new block, delete slot
at('shot', 'f05_arrange')
key(0x58); key(0x46); at('shot', 'f06_instr_cleared')               # F9, clear instrument
key(0x45); at('type', 'y'); key(0x44)
open('sim/fx.txt', 'w').write('\n'.join(f) + '\n')
print(len(f), 'actions, last frame', t[0])
