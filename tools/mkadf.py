#!/usr/bin/env python3
"""Create a bootable 880 KB Amiga floppy image (FFS, DOS\\1).
usage: mkadf.py out.adf VOLUMENAME path_in_adf=hostfile ...
Directories are created as needed (e.g. s/startup-sequence=...)."""
import struct, sys, time

BS = 512
NBLOCKS = 1760
ROOT = 880
HT = 72

BOOTCODE = bytes.fromhex(
    '43fa0018'      # lea dosname(pc),a1
    '4eaeffa0'      # jsr FindResident(a6)
    '4a80'          # tst.l d0
    '670a'          # beq.s fail
    '2040'          # movea.l d0,a0
    '20680016'      # movea.l 22(a0),a0   (rt_Init)
    '7000'          # moveq #0,d0
    '4e75'          # rts
    '70ff'          # fail: moveq #-1,d0
    '4e75'          # rts
) + b'dos.library\0'

def boot_checksum(b):
    s = 0
    for i in range(0, 1024, 4):
        if i == 4: continue
        s += struct.unpack('>I', b[i:i + 4])[0]
        if s > 0xFFFFFFFF: s = (s & 0xFFFFFFFF) + 1
    return (~s) & 0xFFFFFFFF

def block_checksum(b, off=20):
    s = 0
    for i in range(0, BS, 4):
        if i == off: continue
        s = (s + struct.unpack('>I', b[i:i + 4])[0]) & 0xFFFFFFFF
    return (-s) & 0xFFFFFFFF

def name_hash(name):
    h = len(name)
    for c in name.encode('latin-1'):
        if 97 <= c <= 122: c -= 32
        h = (h * 13 + c) & 0x7FF
    return h % HT

class Disk:
    def __init__(self, volname):
        self.img = bytearray(NBLOCKS * BS)
        self.used = {0, 1, ROOT, ROOT + 1}
        self.next = ROOT + 2
        self.blocks = {}          # block number -> bytearray (headers / dirs)
        self.dirs = {ROOT: {}}    # dir block -> {name: block}
        self.parent = {ROOT: 0}
        self.volname = volname
        now = time.gmtime()
        days = int(time.time() // 86400) - 2922        # Amiga epoch 1978-01-01
        self.stamp = (days, now.tm_hour * 60 + now.tm_min, now.tm_sec * 50)

    def alloc(self):
        while self.next in self.used:
            self.next += 1
            if self.next >= NBLOCKS: self.next = 2
        b = self.next
        self.used.add(b)
        return b

    def put_name(self, blk, name):
        blk[432] = len(name)
        blk[433:433 + len(name)] = name.encode('latin-1')

    def put_stamp(self, blk, off):
        struct.pack_into('>III', blk, off, *self.stamp)

    def link(self, dirblk, name, blk):
        if len(name) > 30: sys.exit('name too long: ' + name)
        self.dirs[dirblk][name] = blk

    def mkdir(self, parent, name):
        if name in self.dirs[parent]: return self.dirs[parent][name]
        b = self.alloc()
        self.dirs[b] = {}
        self.parent[b] = parent
        self.link(parent, name, b)
        blk = bytearray(BS)
        struct.pack_into('>I', blk, 0, 2)                 # T_HEADER
        struct.pack_into('>I', blk, 4, b)
        self.put_stamp(blk, 420)
        self.put_name(blk, name)
        struct.pack_into('>I', blk, 500, parent)
        struct.pack_into('>i', blk, 508, 2)               # ST_USERDIR
        self.blocks[b] = blk
        return b

    def add_file(self, path, data):
        parts = path.split('/')
        d = ROOT
        for p in parts[:-1]: d = self.mkdir(d, p)
        name = parts[-1]
        hdr = self.alloc()
        self.link(d, name, hdr)
        datablocks = []
        for i in range(0, len(data), BS):
            b = self.alloc()
            chunk = data[i:i + BS]
            self.img[b * BS:b * BS + len(chunk)] = chunk
            datablocks.append(b)
        # header + extension blocks, 72 pointers each, stored in reverse order
        groups = [datablocks[i:i + HT] for i in range(0, len(datablocks), HT)] or [[]]
        owners = [hdr] + [self.alloc() for _ in groups[1:]]
        for gi, (owner, group) in enumerate(zip(owners, groups)):
            blk = bytearray(BS)
            struct.pack_into('>I', blk, 0, 2 if gi == 0 else 16)     # T_HEADER / T_LIST
            struct.pack_into('>I', blk, 4, owner)
            struct.pack_into('>I', blk, 8, len(group))
            if group: struct.pack_into('>I', blk, 16, group[0])
            for k, db in enumerate(group):
                struct.pack_into('>I', blk, 24 + (HT - 1 - k) * 4, db)
            if gi + 1 < len(owners): struct.pack_into('>I', blk, 504, owners[gi + 1])
            if gi == 0:
                struct.pack_into('>I', blk, 324, len(data))
                self.put_stamp(blk, 420)
                self.put_name(blk, name)
                struct.pack_into('>I', blk, 500, d)
            else:
                struct.pack_into('>I', blk, 500, hdr)
            struct.pack_into('>i', blk, 508, -3)              # ST_FILE
            self.blocks[owner] = blk

    def finish(self, path):
        # hash tables (with chains)
        for dblk, entries in self.dirs.items():
            table = [0] * HT
            for name, b in sorted(entries.items()):
                h = name_hash(name)
                if table[h]:
                    # append to chain end
                    cur = table[h]
                    while struct.unpack('>I', self.blocks[cur][496:500])[0]:
                        cur = struct.unpack('>I', self.blocks[cur][496:500])[0]
                    struct.pack_into('>I', self.blocks[cur], 496, b)
                else:
                    table[h] = b
            if dblk == ROOT:
                root = bytearray(BS)
                struct.pack_into('>I', root, 0, 2)
                struct.pack_into('>I', root, 12, HT)
                struct.pack_into('>i', root, 312, -1)          # bitmap valid
                struct.pack_into('>I', root, 316, ROOT + 1)    # bitmap block
                self.put_stamp(root, 420)                      # root alteration
                self.put_name(root, self.volname)
                self.put_stamp(root, 472)                      # disk alteration
                self.put_stamp(root, 484)                      # creation
                struct.pack_into('>I', root, 508, 1)           # ST_ROOT
                self.blocks[ROOT] = root
            for k, b in enumerate(table):
                struct.pack_into('>I', self.blocks[dblk], 24 + k * 4, b)
        for b, blk in self.blocks.items():
            struct.pack_into('>I', blk, 20, block_checksum(blk))
            self.img[b * BS:(b + 1) * BS] = blk
        # bitmap: bit set = free, covering blocks 2..1759
        bm = bytearray(BS)
        for i in range(NBLOCKS - 2):
            blkno = i + 2
            if blkno not in self.used:
                bm[4 + (i // 32) * 4 + (3 - (i % 32) // 8)] |= 1 << (i % 8)
        struct.pack_into('>I', bm, 0, block_checksum(bm, 0))
        self.img[(ROOT + 1) * BS:(ROOT + 2) * BS] = bm
        # boot block
        boot = bytearray(1024)
        boot[0:4] = b'DOS\x01'
        struct.pack_into('>I', boot, 8, ROOT)
        boot[12:12 + len(BOOTCODE)] = BOOTCODE
        struct.pack_into('>I', boot, 4, boot_checksum(boot))
        self.img[0:1024] = boot
        open(path, 'wb').write(self.img)

if __name__ == '__main__':
    d = Disk(sys.argv[2])
    for spec in sys.argv[3:]:
        dst, src = spec.split('=', 1)
        d.add_file(dst, open(src, 'rb').read())
    d.finish(sys.argv[1])
    print('wrote %s: %d of %d blocks used' % (sys.argv[1], len(d.used), NBLOCKS))
