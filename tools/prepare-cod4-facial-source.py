"""Fetch and authenticate the complete stock facial conversion input set.

Authored binaries are saved locally, never committed to this repository.
Example: python tools/prepare-cod4-facial-source.py --output F:/Dev/h1-facial-source
Then install its raw directory with install-cod4-facial-source.ps1.
"""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import re
import struct
import urllib.request

COMMIT = "95c76d6a470bea43fdd5ab591afe8475fac7699d"
REPOSITORY = "cod4mw/CoD4-Mod-Tools"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tree", type=Path, help="Existing pinned Git tree JSON")
    parser.add_argument("--catalog", type=Path, help="Regenerate the reviewed C++ source catalog")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.tree:
        tree = json.loads(args.tree.read_text())
    else:
        request = urllib.request.Request(
            f"https://api.github.com/repos/{REPOSITORY}/git/trees/{COMMIT}?recursive=1",
            headers={"User-Agent": "h1-cod4-demo-source-preparation"})
        with urllib.request.urlopen(request, timeout=60) as response:
            tree = json.load(response)
    if tree.get("truncated"):
        raise ValueError("Incomplete Mod Tools Git tree")
    available = {entry["path"]: entry for entry in tree["tree"]}
    repo = Path(__file__).resolve().parents[1]
    source = repo / "src/client/component/demo"
    animation_names = set(re.findall(r'"([^"\n]*)"', (source / "cod4_animation_catalog.hpp").read_text()))
    animation_paths = sorted("raw/xanim/" + name for name in animation_names if "raw/xanim/" + name in available)
    characters = (source / "cod4_character_catalog.hpp").read_text().split("source_characters", 1)[1]
    models = set(re.findall(r'"((?:head|body)_mp_[^"\n]+)"', characters))
    models.update(path.removeprefix("raw/xmodel/") for path in available if path.startswith("raw/xmodel/head_mp_"))

    def fetch(path):
        if path not in available:
            raise ValueError("Missing pinned source: " + path)
        target = args.output / path
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            with urllib.request.urlopen(f"https://raw.githubusercontent.com/{REPOSITORY}/{COMMIT}/{path}", timeout=60) as response:
                data = response.read()
            # Verify before writing, including when a download is interrupted.
            if hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest() != available[path]["sha"]:
                raise ValueError("Downloaded Git blob mismatch: " + path)
            target.write_bytes(data)
        data = target.read_bytes()
        blob = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if blob != available[path]["sha"]:
            raise ValueError("Local Git blob mismatch: " + path)
        return {"path": path, "bytes": len(data), "git_blob": blob, "sha256": hashlib.sha256(data).hexdigest()}

    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        records = list(pool.map(fetch, animation_paths + ["raw/xmodel/" + model for model in sorted(models)]))
    bindings = {}
    for model in sorted(models):
        data = (args.output / "raw/xmodel" / model).read_bytes()
        if struct.unpack_from("<H", data)[0] != 25:
            raise ValueError("Unsupported original XModel: " + model)
        # v25 bounds end at byte 27, then physics-preset string and LOD distance.
        start = data.index(0, 27) + 5
        bindings[model] = data[start:data.index(0, start)].decode("ascii")
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        records += list(pool.map(fetch, sorted({"raw/xmodelparts/" + parts for parts in bindings.values()})))
    manifest = {"repository": REPOSITORY, "commit": COMMIT, "bindings": bindings, "files": records}
    (args.output / "facial-source-provenance.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if args.catalog:
        lines = ['#pragma once', '#include <array>', '#include <string_view>', '#include <utility>', '',
                 'namespace cod4::face', '{',
                 '    // Original Mod Tools 95c76d6: v25 XModels explicitly reference these first-LOD parts.',
                 '    // Complete-body characters (SAS / selected USMC) contain their face rig in the body.',
                 '    inline constexpr std::pair<std::string_view, std::string_view> source_face_parts[]{']
        lines += [f'        {{"{model}", "{parts}"}},' for model, parts in bindings.items()]
        lines += ['    };', '    // Authored files for the 318-entry stock BG table; repeated names are deduplicated.',
                  '    // root/unused and the three engine SAW aim poses have no compiled file.',
                  '    inline constexpr std::string_view source_face_clips[]{']
        lines += [f'        "{path.removeprefix("raw/xanim/")}",' for path in animation_paths]
        lines += ['    };', '    inline std::string_view parts_for(std::string_view model)', '    {',
                  '        for (const auto& [name, parts] : source_face_parts) if (name == model) return parts;',
                  '        return {};', '    }', '}']
        args.catalog.write_text("\n".join(lines) + "\n")
    print(f"Verified {len(animation_paths)} stock BG clips and {len(bindings)} exact character model bindings.")


if __name__ == "__main__":
    main()
