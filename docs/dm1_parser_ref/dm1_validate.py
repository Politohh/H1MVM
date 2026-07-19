# -*- coding: utf-8 -*-
# Validate the CoD4 .dm_1 block framing + type-1 client-archive records against a REAL demo.
# Format is authoritative, from KisakCOD:
#   CL_ReadDemoMessage (cl_cgame_mp.cpp): 1 type byte; 0 -> network packet, 1 -> client archive, else EOF
#   CL_WriteDemoMessage (cl_main_mp.cpp:1805): type0 = [i32 seq][i32 len][len bytes]
#   CL_ReadDemoClientArchive + ClientArchiveData: type1 = [i32 index][origin 12][vel 12][movementDir 4][bobCycle 4][serverTime 4][viewangles 12]
# This milestone does NOT decompress the type-0 Huffman payload; it skips it by length.
# It proves: block framing reaches EOF cleanly, archive records are well-formed, serverTimes ascend,
# viewangles are sane. That de-risks the whole parser foundation.
import struct, sys, io

PATH = r"E:\Games\Cod4 1.0\main\demos\backlotdemo.dm_1"
data = open(PATH, "rb").read()
n = len(data)
print("file: %s  (%d bytes)" % (PATH, n))

p = 0
def need(k):
    global p
    if p + k > n:
        raise EOFError("want %d at %d, have %d" % (k, p, n))

i32 = lambda off: struct.unpack_from("<i", data, off)[0]
f32 = lambda off: struct.unpack_from("<f", data, off)[0]
vec = lambda off: (f32(off), f32(off+4), f32(off+8))

n_net = 0
n_arc = 0
net_bytes = 0
seqs = []
arc_times = []
first_arc = None
last_arc = None
bad = []
clean_eof = False
reason = ""

try:
    while p < n:
        t = data[p]; p += 1
        if t == 0:
            # network packet
            if p + 8 > n:
                # trailing type byte with no body -> treat as EOF marker region
                reason = "type0 header truncated (likely EOF pad)"; break
            seq = i32(p); ln = i32(p+4); p += 8
            if ln == -1:
                clean_eof = True; reason = "svc len == -1 (explicit EOF)"; break
            if ln < 0 or p + ln > n:
                bad.append("type0 @%d bad len %d" % (p-8, ln)); reason = "bad type0 len"; break
            seqs.append(seq)
            net_bytes += ln
            p += ln              # SKIP the compressed body this milestone
            n_net += 1
        elif t == 1:
            # client archive, 4 + 12 + 12 + 4 + 4 + 4 + 12 = 52 bytes
            if p + 52 > n:
                reason = "type1 truncated (likely EOF pad)"; break
            index = i32(p)
            origin = vec(p+4)
            vel    = vec(p+16)
            movementDir = i32(p+28)
            bobCycle    = i32(p+32)
            serverTime  = i32(p+36)
            va     = vec(p+40)
            p += 52
            if index < 0 or index >= 256:
                bad.append("arc idx %d out of range @%d" % (index, p-52))
            rec = dict(i=index, st=serverTime, org=origin, va=va, bob=bobCycle, md=movementDir)
            if first_arc is None: first_arc = rec
            last_arc = rec
            arc_times.append(serverTime)
            n_arc += 1
        else:
            reason = "unknown block type %d @%d" % (t, p-1); break
    else:
        clean_eof = True; reason = "reached end of file exactly"
except EOFError as e:
    reason = "EOFError: %s" % e

print("\n=== BLOCK FRAMING ===")
print("  type-0 network packets : %d   (%d payload bytes, will Huffman-decode next milestone)" % (n_net, net_bytes))
print("  type-1 archive records : %d" % n_arc)
print("  stopped at byte %d / %d   reason: %s" % (p, n, reason))
print("  clean parse:", clean_eof and not bad)

print("\n=== TYPE-0 SEQUENCE NUMBERS ===")
if seqs:
    asc = all(b > a for a, b in zip(seqs, seqs[1:]))
    print("  first=%d last=%d count=%d strictly-ascending=%s" % (seqs[0], seqs[-1], len(seqs), asc))

print("\n=== TYPE-1 ARCHIVE (viewangles source) ===")
if arc_times:
    asc = all(b >= a for a, b in zip(arc_times, arc_times[1:]))
    strict = all(b > a for a, b in zip(arc_times, arc_times[1:]))
    print("  serverTime: first=%d last=%d span=%dms  non-decreasing=%s strictly-ascending=%s" % (
        arc_times[0], arc_times[-1], arc_times[-1]-arc_times[0], asc, strict))
    print("  FIRST: idx=%d st=%d origin=(%.1f,%.1f,%.1f) va=(%.2f,%.2f,%.2f) bob=%d md=%d" % (
        first_arc['i'], first_arc['st'], *first_arc['org'], *first_arc['va'], first_arc['bob'], first_arc['md']))
    print("  LAST : idx=%d st=%d origin=(%.1f,%.1f,%.1f) va=(%.2f,%.2f,%.2f) bob=%d md=%d" % (
        last_arc['i'], last_arc['st'], *last_arc['org'], *last_arc['va'], last_arc['bob'], last_arc['md']))
    # sanity: viewangles should be in [-360,360], origin finite
    import math
    va_ok = all(-361 <= v <= 361 for v in (first_arc['va'] + last_arc['va']))
    org_ok = all(math.isfinite(v) for v in (first_arc['org'] + last_arc['org']))
    print("  viewangles in range:", va_ok, " origin finite:", org_ok)

if bad:
    print("\n=== ANOMALIES ===")
    for b in bad[:10]: print("  ", b)

print("\nRESULT:", "PASS" if (clean_eof and not bad and n_arc > 0) else "NEEDS ATTENTION")
