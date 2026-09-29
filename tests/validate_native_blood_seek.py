"""Check an observed native rewind and stable paused blood rendering."""
import collections
import pathlib
import re
import sys

before, after, seek = [pathlib.Path(name).read_text(encoding='utf-8-sig') for name in sys.argv[1:4]]

def frame(text):
    lines = [line for line in text.splitlines() if line.startswith('frame state=')]
    assert lines, 'Missing native frame samples'
    return {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', lines[-1])}

old, new = frame(before), frame(after)
assert old['state'] == new['state'] == 10 and old['cgame'] == new['cgame'] == 1
assert old['time'] == new['time'] and old['snap'] == new['snap'], 'Paused native clock moved'
assert new['draws'] - old['draws'] >= 100, 'Insufficient paused render observations'
for prefix in ['native bullet impact ', 'native bullet FX ', 'native bullet visibility ']:
    assert before.count(prefix) == after.count(prefix), 'Paused frames replayed a blood event'

events = {}
allocations = collections.Counter()
for line in seek.splitlines():
    fields = dict(re.findall(r'(\w+)=([^ ]+)', line))
    if line.startswith('native bullet impact entity='):
        number = int(fields['entity'])
        assert number not in events, 'Seek repeated a stable impact identity'
        assert fields['reconstruct'] == '1', 'Not a reconstruction pass'
        assert int(fields['age']) == int(fields['playback']) - int(fields['recorded'])
        events[number] = fields
    if line.startswith('native bullet FX entity='):
        assert fields['effect'].startswith('vfx/weaponimpact/flesh_')
        if fields['spawned'] == '1':
            allocations[int(fields['entity'])] += 1
assert events and allocations, 'No reconstructed native blood burst'
assert all(count == 1 and number in events for number, count in allocations.items())
for line in seek.splitlines():
    if not line.startswith('native bullet FX entity='): continue
    fields = dict(re.findall(r'(\w+)=([^ ]+)', line))
    assert fields['time'] == events[int(fields['entity'])]['recorded'], 'Seek reset FX birth time'
print(f"PASS: {len(events)} native impact reconstructed once at its original clock; paused time {new['time']} stayed fixed across {new['draws']-old['draws']} additional rendered frames with no repeated impact/FX calls")
