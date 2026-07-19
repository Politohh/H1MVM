# -*- coding: utf-8 -*-
# Faithful port of KisakCOD's static Huffman (huffman.cpp) + MSG_ReadBitsCompress, tested against a real
# .dm_1 type-0 payload. The first network packet is the gamestate, so success = first decompressed svc byte
# is svc_gamestate (1). get_bit is LSB-first: (fin[bloc>>3] >> (bloc&7)) & 1.
import struct

# ---- msg_hData weights (skip the leading array size 256) ----
raw = [int(x) for x in open("hdata_raw.txt")]
assert raw[0] == 256, raw[0]
HDATA = raw[1:257]
assert len(HDATA) == 256

# ---- tree build (Huff_BuildFromData: combine two lowest-weight nodes repeatedly) ----
class Node:
    __slots__ = ("left","right","weight","symbol")
    def __init__(self, symbol, weight):
        self.left=None; self.right=None; self.weight=weight; self.symbol=symbol

def build_tree(hdata, tiebreak):
    # leaves
    nodes = [Node(i, hdata[i]) for i in range(256)]
    # combine two lowest until one remains. tiebreak controls equal-weight ordering.
    seq = 0
    # attach an insertion order for stable tie-break
    order = {id(n): i for i,n in enumerate(nodes)}
    pool = list(nodes)
    def key(n):
        return (n.weight, order[id(n)]) if tiebreak=="stable" else (n.weight,)
    while len(pool) > 1:
        pool.sort(key=key)
        a = pool.pop(0); b = pool.pop(0)
        p = Node(257, a.weight + b.weight)
        p.left = a; p.right = b
        order[id(p)] = 10_000_000 + seq; seq += 1   # new internal nodes go to the back on ties
        pool.append(p)
    return pool[0]

# ---- decode: Huff_offsetReceive + MSG_ReadBitsCompress ----
def decompress(tree, fin, size):
    # size = number of INPUT bytes; decode symbols until we've consumed 8*size input bits.
    # The decoder legitimately overruns the final byte, so pad the input generously.
    fin = bytes(fin) + b"\x00"*16
    out = bytearray()
    bits = 8*size
    bloc = 0
    while bloc < bits:
        node = tree
        guard = 0
        while node is not None and node.symbol == 257:
            bit = (fin[bloc>>3] >> (bloc&7)) & 1
            bloc += 1
            node = node.right if bit else node.left
            guard += 1
            if guard > 64: return bytes(out)  # malformed tree safety
        out.append(node.symbol if node is not None else 0)
    return bytes(out)

# ---- pull the first type-0 payload from the real demo ----
data = open(r"E:\Games\Cod4 1.0\main\demos\backlotdemo.dm_1","rb").read()
n=len(data); p=0
i32=lambda o: struct.unpack_from("<i",data,o)[0]
payloads=[]
while p<n and len(payloads)<3:
    t=data[p]; p+=1
    if t==0:
        seq=i32(p); ln=i32(p+4); p+=8
        if ln==-1: break
        payloads.append((seq, data[p:p+ln])); p+=ln
    elif t==1:
        p+=52
    else: break

SVC={0:"nop",1:"gamestate",2:"configstring",3:"baseline",4:"serverCommand",5:"download",6:"snapshot",7:"EOF"}

for tb in ("stable","weightonly"):
    tree = build_tree(HDATA, tb)
    print("="*70); print("tie-break:", tb); print("="*70)
    for (seq, pl) in payloads[:1]:
        relack = struct.unpack_from("<i", pl, 0)[0]
        comp = pl[4:]
        dec = decompress(tree, comp, len(comp))
        print("  packet seq=%d  payload=%dB  reliableAck=%d  compressed=%dB -> decompressed=%dB" % (
            seq, len(pl), relack, len(comp), len(dec)))
        print("  first 24 decompressed bytes:", dec[:24].hex())
        c0 = dec[0]
        print("  FIRST SVC BYTE = %d (%s)  %s" % (c0, SVC.get(c0,"?"),
              "<<< GAMESTATE - CORRECT" if c0==1 else "<<< wrong, tree/tiebreak off"))
    print()
