"""Cross-check all Mod Tools xanim names with the recorded MP mapping report.

No asset data is copied. Requires the pinned Mod Tools tree listing and two
scripts from work/cod4-mod-tools-reference (commit recorded in provenance.json).
Run the C++ actor-catalog test first to regenerate the native mapping report.
"""
import csv
import hashlib
import json
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
reference = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "work/cod4-mod-tools-reference"
provenance = json.loads((reference / "provenance.json").read_text())
tree = json.loads((reference / "tree.json").read_text())
assert tree["sha"] == provenance["commit"] and not tree.get("truncated"), "Incomplete/different Mod Tools tree"
for record in provenance["files"]:
    if record["path"] in ("raw/mp/playeranim.script", "raw/animtrees/multiplayer.atr"):
        actual = hashlib.sha256((reference / record["path"]).read_bytes()).hexdigest()
        assert actual == record["sha256"].lower(), f"Mod Tools source changed: {record['path']}"

def script_tokens(path):
    source = re.sub(r"/\*.*?\*/", "", path.read_text(), flags=re.S)
    source = re.sub(r"//[^\n]*", "", source).lower()
    return set(re.findall(r"\b[a-z_][a-z0-9_]*\b", source))

atr = script_tokens(reference / "raw/animtrees/multiplayer.atr")
script = script_tokens(reference / "raw/mp/playeranim.script")
assets = {r["path"].rsplit("/", 1)[1].lower(): r for r in tree["tree"]
          if r["type"] == "blob" and r["path"].startswith("raw/xanim/")}
names = re.findall(r'"([^"\n]+)"\s*,?\s*//\s*\d+',
                   (root / "src/client/component/demo/cod4_animation_catalog.hpp").read_text())
source = {name.lower() for name in names if name not in ("root", "unused")}
assert len(names) == 318 and len(source) == 247
assert source <= atr, f"Recorded names absent from Mod Tools MP tree: {source - atr}"
atr_text = re.sub(r"//[^\n]*", "", (reference / "raw/animtrees/multiplayer.atr").read_text()).lower()
blend_nodes = set(re.findall(r"\b([a-z_][a-z0-9_]*)\s*(?::[^{}\n]*)?\s*\{", atr_text))
assert source <= assets.keys() | blend_nodes, f"Recorded names absent from Mod Tools assets/blend nodes: {source - (assets.keys() | blend_nodes)}"

with (root / "docs/cod4-actor-animation-coverage.tsv").open() as file:
    rows = list(csv.DictReader(file, delimiter="\t"))
mapped = {}
for row in rows:
    mapped.setdefault(row["source_name"].lower(), set()).add(row["native_name"])
assert mapped.keys() == source and len(rows) == 247 * 8, "Incomplete runtime mapping coverage"
native = {line.split()[3].lower() for line in (root / "tests/fixtures/mwr-native-actor-codecs.tsv").read_text().splitlines()}
report = root / "docs/cod4-modtools-xanim-inventory.tsv"
with report.open("w", newline="") as file:
    writer = csv.writer(file, delimiter="\t", lineterminator="\n")
    writer.writerow(["modtools_path", "git_blob", "in_mp_tree", "in_player_script", "recorded_actor_mapping", "native_mp_name_present"])
    for name, record in sorted(assets.items()):
        status = " | ".join(sorted(mapped[name])) if name in mapped else "outside recorded actor table"
        writer.writerow([record["path"], record["sha"], int(name in atr), int(name in script), status, int(name in native)])
print(f"PASS {len(assets)} Mod Tools xanim assets inventoried; {len(assets.keys() & atr)} referenced by MP tree; "
      f"all {len(source)} recorded actor entries ({len(source & assets.keys())} clips, {len(source - assets.keys())} blend nodes) "
      "verified against original tree/assets and mapped for eight weapon families")
