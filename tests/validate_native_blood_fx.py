"""Compare recorded impacts to decoded H1 fields and actual native FX allocations."""
import collections
import json
import pathlib
import re
import struct
import sys

conversion = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8-sig")
playback = pathlib.Path(sys.argv[2]).read_text(encoding="utf-8-sig")
world = json.loads(pathlib.Path(sys.argv[3]).read_text())
events = {}
for line in conversion.splitlines():
    if not line.startswith("bullet impact time="):
        continue
    fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
    number = int(fields["native_entity"])
    assert number not in events, "Duplicate conversion impact identity"
    events[number] = fields
assert len(events) == 25, "Missing Promod impact events"

observed = {e["number"]: e for e in world["entities"] if e["number"] in events}
assert observed.keys() == events.keys(), "Missing decoded native impact entities"
for number, event in events.items():
    state = observed[number]
    data = bytes.fromhex(state["hex"])
    assert data[10] == 20 + int(event["native_event"])
    assert data[11] == 7, "Recorded flesh surface was lost by native serialization"
    assert struct.unpack_from("<H", data, 2)[0] == int(event["shooter"])
    target = int(event["target"])
    assert struct.unpack_from("<H", data, 4)[0] == (2046 if target in (0,1022) else 2047 if target == 1023 else target)
    assert data[12] == int(event["client"]) and data[28] == 1
    assert struct.unpack_from("<I", data, 36)[0] == int(event["normal"])
    assert struct.unpack_from("<I", data, 204)[0] == int(event["flags"])
    assert tuple(state["origin"]) == tuple(map(float, event["origin"].split(",")))

dispatches = collections.Counter()
allocations = collections.Counter()
visibility = {}
effects = set()
for line in playback.splitlines():
    if line.startswith("native bullet visibility entity="):
        fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
        number = int(fields["entity"])
        assert number in events and number not in visibility, "Duplicate visibility pass"
        assert fields["unbound_flesh"] == "1"
        visibility[number] = fields["culled"] == "1"
    if line.startswith("native bullet impact entity="):
        fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
        number = int(fields["entity"])
        assert number in events
        assert int(fields["recorded"]) == int(events[number]["time"])
        assert int(fields["event"]) == int(events[number]["native_event"])
        assert int(fields["age"]) == int(fields["playback"]) - int(fields["recorded"])
        assert fields["reconstruct"] == "0", "Use uninterrupted playback for coverage"
        dispatches[number] += 1
    if line.startswith("native bullet FX entity="):
        fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
        number = int(fields["entity"])
        assert number in events
        assert fields["effect"].startswith("vfx/weaponimpact/flesh_")
        assert int(fields["time"]) == int(events[number]["time"])
        assert tuple(map(float, fields["origin"].split(","))) == tuple(map(float, events[number]["origin"].split(",")))
        assert fields["spawned"] == "1", "Native blood allocation failed"
        effects.add(fields["effect"])
        allocations[number] += 1
assert dispatches.keys() == events.keys() and all(n == 1 for n in dispatches.values()), "Missing/duplicate native impact dispatch"
general = {n for n, e in events.items() if e["source_event"] == "41"}
assert len(general) == 21 and visibility.keys() == general, "Missing native flesh visibility decisions"
visible = {number for number, culled in visibility.items() if not culled}
assert allocations.keys() == visible, "Missing visible native blood bursts"
assert all(n == 1 for n in allocations.values()), "Duplicate blood burst"
assert "time=52477634" in playback, "Replay did not reach the original end"
print(f"PASS: {len(events)} native impacts dispatched once; {len(visible)} MWR blood FX allocated at exact recorded positions/times; {len(general)-len(visible)} native PVS culls; no missing/duplicate visible bursts; all flesh surfaces and world targets survive native decoding; full 60s end reached")
print("PASS: native blood assets: " + ", ".join(sorted(effects)))
