# -*- coding: utf-8 -*-
# Faithful port of KisakCOD msg_mp.cpp readers: the HYBRID bit/byte stream.
#   MSG_ReadByte/Short/Long use msg.readcount (byte cursor).
#   MSG_ReadBit/Bits use msg.bit; on a fresh byte (bit&7==0) they claim one: bit = 8*readcount++.
# Validated by decoding real snapshot headers out of backlotdemo.dm_1 and matching serverTime to the archive.
import struct

class Msg:
    def __init__(self, data):
        self.data = bytes(data) + b"\x00"*8   # pad; splitData not used
        self.cursize = len(data)
        self.readcount = 0
        self.bit = 0
        self.overflowed = False
    def get_byte(self, where):
        return self.data[where] if where < len(self.data) else 0
    def ReadBit(self):
        lo = self.bit & 7
        if lo == 0:
            if self.readcount >= self.cursize:
                self.overflowed = True; return -1
            self.bit = 8 * self.readcount; self.readcount += 1
        b = self.get_byte(self.bit >> 3)
        r = (b >> (self.bit & 7)) & 1
        self.bit += 1
        return r
    def ReadBits(self, bits):
        value = 0
        for i in range(bits):
            lo = self.bit & 7
            if lo == 0:
                if self.readcount >= self.cursize:
                    self.overflowed = True; return -1
                self.bit = 8 * self.readcount; self.readcount += 1
            value |= ((self.get_byte(self.bit >> 3) >> (self.bit & 7)) & 1) << i
            self.bit += 1
        return value
    def ReadByte(self):
        if self.readcount >= self.cursize:
            self.overflowed = True; return -1
        c = self.get_byte(self.readcount); self.readcount += 1
        return c
    def ReadLong(self):
        if self.readcount + 4 > self.cursize:
            self.overflowed = True; return -1
        c = struct.unpack_from("<i", self.data, self.readcount)[0]
        self.readcount += 4
        return c

def GetMinBitCountForNum(num):
    # 32 - (bsr(num) ^ 0x1F) == num.bit_length()
    return num.bit_length() if num > 0 else 1

# ---- reuse the huffman from dm1_huff.py ----
import os
src = open(os.path.join(os.path.dirname(__file__), "dm1_huff.py")).read().split("# ---- pull the first")[0]
exec(src)                       # HDATA, build_tree, decompress
TREE = build_tree(HDATA, "stable")

# ---- parse the demo, decode first svc byte + snapshot headers ----
data = open(r"E:\Games\Cod4 1.0\main\demos\backlotdemo.dm_1","rb").read()
n=len(data); p=0
i32=lambda o: struct.unpack_from("<i",data,o)[0]
SVC={0:"nop",1:"gamestate",2:"configstring",3:"baseline",4:"serverCommand",5:"download",6:"snapshot",7:"EOF"}
pkts=[]
arc_lo, arc_hi = 21034, 93993
while p<n and len(pkts)<8:
    t=data[p]; p+=1
    if t==0:
        seq=i32(p); ln=i32(p+4); p+=8
        if ln==-1: break
        pkts.append((seq, data[p:p+ln])); p+=ln
    elif t==1: p+=52
    else: break

print("packet | seq | payloadB | reliableAck | first svc | snapshot header")
for (seq,pl) in pkts:
    relack = struct.unpack_from("<i", pl, 0)[0]
    dec = decompress(TREE, pl[4:], len(pl)-4)
    m = Msg(dec)
    cmd = m.ReadByte()
    info = ""
    if cmd == 6:  # snapshot
        st = m.ReadLong(); dn = m.ReadByte(); sf = m.ReadByte()
        lc = m.ReadBit()
        lcf = m.ReadBits(GetMinBitCountForNum(141))
        ok_st = arc_lo <= st <= arc_hi
        ok_lcf = 0 <= lcf <= 141
        info = "serverTime=%d(%s) deltaNum=%d snapFlags=%d lc=%d LastChangedField=%d(%s)" % (
            st, "IN-RANGE" if ok_st else "OUT", dn, sf, lc, lcf, "valid" if ok_lcf else "BAD")
    print("  seq %-4d  %6dB  relAck=%-4d  cmd=%d(%s)  %s" % (
        seq, len(pl), relack, cmd, SVC.get(cmd,"?"), info))
