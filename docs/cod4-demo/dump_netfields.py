#!/usr/bin/env python
# Dump CoD4 entityState per-eType netfield tables from iw3mp.exe IDB (port 12345).
import dc, struct, json

def rd(addr, size):
    r = dc.call("get_bytes", {"regions":[{"addr":hex(addr) if isinstance(addr,int) else addr,"size":size}]})
    hexs = r[0]["data"].split()
    return bytes(int(x,16) for x in hexs)

def u32(b, off): return struct.unpack_from("<I", b, off)[0]
def i32(b, off): return struct.unpack_from("<i", b, off)[0]

def rstr(addr, maxlen=64):
    if addr == 0: return None
    b = rd(addr, maxlen)
    n = b.find(b"\x00")
    return b[:n if n>=0 else maxlen].decode("latin1", "replace")

BASE = 0x6BB010     # array of {char** fields; int numFields} per eType
CNT_TBL = 0x6BAFD4  # per-eType secondary count table (stride 8)

# Dump the eType dispatch table (assume up to 20 eTypes)
region = rd(BASE, 20*8)
print("=== off_6BB010 eType table (fieldsPtr, count) ===")
etypes = []
for e in range(20):
    fptr = u32(region, e*8)
    cnt  = i32(region, e*8+4)
    if fptr < 0x400000 or fptr > 0xd90b000 or cnt < 0 or cnt > 200:
        print(f"  eType {e}: STOP (fptr={fptr:#x} cnt={cnt})")
        break
    etypes.append((e, fptr, cnt))
    print(f"  eType {e:2d}: fields=0x{fptr:06x}  count={cnt}")

# Secondary count table
c2 = rd(CNT_TBL, 20*8)
print("\n=== 0x6BAFD4 secondary table (as int pairs) ===")
for e in range(min(len(etypes)+1,20)):
    print(f"  [{e:2d}] {i32(c2,e*8)}  {i32(c2,e*8+4)}")

# Dump field arrays for the first few eTypes
for (e, fptr, cnt) in etypes[:3]:
    print(f"\n===== eType {e} field array @ 0x{fptr:06x} ({cnt} fields) =====")
    fa = rd(fptr, cnt*16)
    for i in range(cnt):
        nptr = u32(fa, i*16+0)
        foff = i32(fa, i*16+4)
        bits = i32(fa, i*16+8)
        hint = i32(fa, i*16+12)
        name = rstr(nptr)
        print(f"  [{i:2d}] off={foff:4d} bits={bits:4d} hint={hint:3d}  {name!r}")
