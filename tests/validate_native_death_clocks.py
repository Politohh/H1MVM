"""Validate observed native death-clip clocks, independently of conversion."""
import collections
import json
import math
import pathlib
import sys

records = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
assert records, 'No native animation samples'
states = collections.defaultdict(list)
for record in records:
    assert record['connection'] == 10, 'Native playback left CA_ACTIVE'
    for corpse in record['corpses']:
        node = corpse['node']
        if node is None: continue
        phase = node['time']
        assert math.isfinite(phase) and 0 <= phase <= 1, 'Invalid native clip phase'
        assert node['animation'] == corpse['animation'], 'Wrong native tree channel'
        key = (corpse['entity'], corpse['client'], corpse['name'], corpse['tree'])
        states[key].append((record['server_time'], phase))

advancing = []
paused_pairs = 0
completed = 0
for key, samples in states.items():
    progress = 0
    for (old_time, old_phase), (time, phase) in zip(samples, samples[1:]):
        if time < old_time: continue  # A seek starts another observed phase.
        if time == old_time:
            assert phase == old_phase, 'Paused native death animation advanced'
            paused_pairs += 1
        elif 0 <= old_phase < phase < 1:
            progress += 1
    if progress >= 3:
        advancing.append(key)
        completed += any(phase == 1 for _, phase in samples)
        print('NATIVE_DEATH', key[0], key[1], key[2], 'advancing_samples', progress)
assert len(advancing) >= 4, 'Too few observed deaths played through their native clips'
assert completed >= 3, 'Observed native death clips did not reach their final pose'
assert paused_pairs, 'Native pause did not receive coverage'
print(f'PASS: {len(records)} native samples, {len(advancing)} advancing corpse clips, '
      f'{completed} completed clips, {paused_pairs} stable paused pairs')
