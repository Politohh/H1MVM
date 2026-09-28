"""Validate the native H1 container emitted for the COD4 fixture."""
import collections
import math
import pathlib
import struct
import sys
import zlib

payload = pathlib.Path(sys.argv[1]).read_bytes()
offset = 0
counts = collections.Counter()
sequences, times, archives = [], [], []
footer = None
while offset < len(payload):
    flag = payload[offset]
    offset += 1
    kind, width = flag & 31, 1 if flag & 128 else 4
    assert offset + width <= len(payload), "Truncated chunk length"
    size = int.from_bytes(payload[offset:offset + width], "little")
    offset += width
    assert size <= 131072 and offset + size <= len(payload), "Invalid chunk size"
    chunk = payload[offset:offset + size]
    offset += size
    counts[kind] += 1
    if kind == 31:
        assert size == 0 and offset == len(payload), "Invalid EOF"
        break
    if kind == 2:
        sequence, ack = struct.unpack_from("<II", chunk)
        assert ack == 0x80000000, "Expected native zlib packet"
        stream = zlib.decompressobj()
        decoded = stream.decompress(chunk[8:], 131073)
        assert stream.eof and not stream.unused_data and len(decoded) <= 131072
        assert decoded[0] & 7 == (0 if sequence == 1 else 6), "Invalid service opcode"
        sequences.append(sequence)
        if sequence > 1:
            # H1 message reader uses a separate bit cursor and raw-byte cursor.
            times.append(struct.unpack_from("<i", decoded, 1)[0])
    if kind == 6:
        footer = struct.unpack_from("<ii", chunk)
    if kind == 3:
        assert len(chunk) == 50 and chunk[0] == 0, "Invalid camera archive"
        assert chunk[1] == len(archives) % 256, "Invalid archive index"
        archives.append(chunk[2:])
assert sequences == list(range(1, 2243)), "Missing gamestate/snapshots"
assert counts[31] == 1 and counts[4] == 1, "Missing EOF/configstrings"
assert footer == (33585, 56385)
assert (times[0], times[-1]) == footer
assert all(b > a for a, b in zip(times, times[1:])), "Non-monotonic snapshot clock"
assert [struct.unpack_from('<i', a)[0] for a in archives] == times, "Missing/misaligned camera samples"
if len(sys.argv) > 2:
    source = pathlib.Path(sys.argv[2]).read_bytes()
    assert len(source) == len(archives) * 48, "Source archive count differs"
    for index, archive in enumerate(archives):
        recorded = source[index * 48:(index + 1) * 48]
        # Position, velocity, bob and camera angles remain exact. Only the
        # different movementDir convention is translated for H1 body facing.
        assert archive[:32] == recorded[:32] and archive[36:] == recorded[36:], "Recorded camera changed"
        relative = struct.unpack_from('<i', recorded, 32)[0]
        yaw = struct.unpack_from('<f', recorded, 40)[0]
        packed = struct.unpack_from('<i', archive, 32)[0]
        assert 0 <= packed <= 255, "Body heading outside H1 domain"
        assert abs(math.remainder(packed * 360 / 256 - yaw - relative, 360)) <= 0.704, "Wrong converted body heading"
print(f"PASS: {len(times)} native snapshots, {len(payload)} bytes, {(footer[1]-footer[0])/1000:.1f}s")
