"""Compare instrumented native entity writer/reader boundaries for a replay.

Uses actual MSG_WriteDeltaEntity / MSG_ReadDeltaEntity traces, rather than a
second implementation of their field codecs. Corpse-origin override is read
by CL_ParsePacketEntity after MSG_ReadDeltaEntity returns.
"""
import pathlib
import re
import sys


def trace(path, operation):
    records = {}
    pending = None
    for line in pathlib.Path(path).read_text(encoding="utf-8-sig").splitlines():
        if not line.startswith(f"entity {operation} "):
            continue
        values = {key: int(value) for key, value in re.findall(r"(\w+)=(-?\d+)", line)}
        if " begin " in line:
            assert pending is None, "Missing native entity completion"
            pending = values
        else:
            assert pending is not None and values["number"] == pending["number"]
            key = (pending["time"], pending["number"])
            assert key not in records, f"Duplicate native entity {key}"
            records[key] = (pending, values)
            pending = None
    assert pending is None, "Native entity decoder did not complete"
    assert records, "Missing native entity traces"
    return records


written = trace(sys.argv[1], "write")
decoded = trace(sys.argv[2], "read")
common_times = {time for time, _ in written} & {time for time, _ in decoded}
assert common_times, "Native traces have no common snapshots"
written = {key: value for key, value in written.items() if key[0] in common_times}
decoded = {key: value for key, value in decoded.items() if key[0] in common_times}
assert written.keys() == decoded.keys(), "Native entity IDs diverged"
corpse_states = 0
for key, (start, end) in written.items():
    actual_start, actual_end = decoded[key]
    assert 0 <= key[1] < 2047
    assert actual_end["result"] == 0, f"Entity unexpectedly deleted: {key}"
    assert actual_end["type"] == start["type"], f"Native entity type changed: {key}"
    corpse = start["type"] == 2
    corpse_states += corpse
    assert end["bit"] == actual_end["bit"] + int(corpse), f"Native entity boundary mismatch: {key}"
    assert start["bit"] < actual_start["bit"] <= actual_end["bit"]
assert corpse_states > 0, "Fixture did not exercise corpse-origin override bits"
print(f"PASS: {len(common_times)} native snapshots, {len(written)} entity round trips, "
      f"{corpse_states} corpse-origin flags; exact native IDs, types and bit boundaries")
