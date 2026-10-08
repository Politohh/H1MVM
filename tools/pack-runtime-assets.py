"""Build the complete, deterministic single-file client payload (no network access).

Configure once with --configure inputs.json. Paths/interpreter stay in ignored build/.
Every build checks the catalog, provenance and file hashes, even on a cache hit.
"""
import argparse
import hashlib
import json
import re
import shutil
import struct
import sys
import zlib
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
MAGIC = b"H1PACK01"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_name(name):
    if not re.fullmatch(r"[A-Za-z0-9_./-]+", name) or any(
            p in ("", ".", "..") for p in name.split("/")) or PurePosixPath(name).is_absolute():
        raise ValueError("Unsafe payload path: " + name)
    return name


def collect(config):
    files = {}

    def add(name, source, expected=None):
        safe_name(name)
        if name.lower() in {x.lower() for x in files}:
            raise ValueError("Duplicate payload path: " + name)
        source = Path(source)
        data = source.read_bytes()  # Missing required inputs are fatal, never optional.
        sha = digest(data)
        if expected and sha != expected.lower():
            raise ValueError("Source provenance mismatch: " + str(source))
        files[name] = (source, len(data), sha)

    for source in sorted((ROOT / "data/cdata").rglob("*")):
        if source.is_file():
            add("cdata/" + source.relative_to(ROOT / "data/cdata").as_posix(), source)
    for required in ("theater/__init__.lua", "theater/model_swapper.lua", "theater/weapon_swapper.lua"):
        if "cdata/ui_scripts/" + required not in files:
            raise ValueError("Missing client UI: " + required)

    facial = Path(config["facial"])
    provenance = json.loads((facial / "facial-source-provenance.json").read_text())
    hashes = {f["path"]: f["sha256"] for f in provenance["files"]}
    catalog = (ROOT / "src/client/component/demo/cod4_facial_catalog.hpp").read_text()
    parts_section = catalog.split("source_face_parts[]{", 1)[1].split("};", 1)[0]
    parts = set(re.findall(r'\{"[^"]+", "([^"]+)"\}', parts_section))
    clips_section = catalog.split("source_face_clips[]{", 1)[1].split("};", 1)[0]
    clips = set(re.findall(r'"([^"]+)"', clips_section))
    if len(parts) < 42 or len(clips) < 244:
        raise ValueError("Incomplete facial catalog")
    for category, names in (("xmodelparts", parts), ("xanim", clips)):
        for name in sorted(names):
            relative = category + "/" + name
            add("cod4_facial/" + relative, facial / "raw" / relative, hashes["raw/" + relative])
    add("provenance/facial.json", facial / "facial-source-provenance.json")

    sniper = Path(config["snipers"])
    hashes = {f["name"]: f["sha256"] for f in json.loads((sniper / "manifest.json").read_text())["files"]}
    descriptor = json.loads((sniper / "animations.json").read_text())
    if descriptor["version"] != 4 or len(descriptor["rifles"]) != 5:
        raise ValueError("Incomplete classic sniper descriptor")
    required = {"animations.json"}
    for rifle in descriptor["rifles"]:
        required.update((rifle["hands"], rifle["gun"]))
        required.update(action["clip"] for action in rifle["actions"])
    for rifle in ("m40a3", "remington", "barrett", "m21", "dragunov"):
        required.update(f"viewmodel_{rifle}_ads_{direction}" for direction in ("up", "down"))
    for name in sorted(required):
        add("cod4_snipers/" + name, sniper / name, hashes[name])
    add("cod4_snipers/scope.png", config["scope"], config["scope_sha256"])
    add("provenance/snipers.json", sniper / "manifest.json")

    sky_index = ROOT / "data/sky-presets.json"
    skies = json.loads(sky_index.read_text())
    if skies["version"] != 1 or len(skies["assets"]) != 45:
        raise ValueError("Incomplete sky preset index")
    for sky in skies["assets"]:
        if not sky["blocks"] or sum(block["take"] for block in sky["blocks"]) != sky["size"]:
            raise ValueError("Incomplete indexed sky pixels: " + sky["name"])
    add("catalogs/skies.json", sky_index)
    add("tools/ffmpeg.exe", config["ffmpeg"], config["ffmpeg_sha256"])
    for name in ("LICENSE", "README.txt"):
        add("licenses/ffmpeg/" + name, Path(config["ffmpeg"]).parent.parent / name)
    # Capture-only builds retain dependency notices and their exact build recipe.
    ffmpeg_notices = Path(config["ffmpeg"]).parent.parent / "licenses"
    if ffmpeg_notices.is_dir():
        for source in sorted(ffmpeg_notices.rglob("*")):
            if source.is_file():
                add("licenses/ffmpeg/" + source.relative_to(ffmpeg_notices).as_posix(), source)
    add("licenses/h1-mod.txt", ROOT / "LICENSE")
    add("SINGLE-FILE-README.md", ROOT / "docs/single-file-release.md")
    for source in sorted((ROOT / "deps").glob("*/LICENSE*")):
        if source.is_file():
            add("licenses/" + source.parent.name + "/" + source.name, source)
    return files


def pack(config):
    files = collect(config)
    entries = [{"path": name, "size": size, "sha256": sha} for name, (_, size, sha) in sorted(files.items())]
    identity = digest(json.dumps(entries, sort_keys=True, separators=(",", ":")).encode())
    output = BUILD / "runtime-assets.bin"
    stamp = BUILD / "runtime-assets-stamp.json"
    if output.is_file() and stamp.is_file():
        previous = json.loads(stamp.read_text())
        if previous.get("identity") == identity and previous.get("sha256") == hashlib.file_digest(output.open("rb"), "sha256").hexdigest():
            print(f"Verified unchanged bundle: {len(entries)} files, {output.stat().st_size:,} bytes")
            return
    payload = BUILD / "runtime-assets.payload.tmp"
    with payload.open("wb") as out:
        for entry in entries:
            data = files[entry["path"]][0].read_bytes()
            if digest(data) != entry["sha256"]:
                raise ValueError("Source changed during packaging: " + entry["path"])
            compressed = zlib.compress(data, 9)
            entry.update(offset=out.tell(), compressed_size=len(compressed))
            out.write(compressed)
    manifest = {"version": 1, "files": entries}
    header = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()
    temporary = output.with_suffix(".tmp")
    with temporary.open("wb") as out, payload.open("rb") as source:
        out.write(MAGIC + struct.pack("<I", len(header)) + header)
        shutil.copyfileobj(source, out)
    temporary.replace(output)
    payload.unlink()
    (BUILD / "runtime-assets-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    with output.open("rb") as source:
        bundle_hash = hashlib.file_digest(source, "sha256").hexdigest()
    stamp.write_text(json.dumps({"identity": identity, "sha256": bundle_hash}, indent=2))
    print(f"Packed {len(entries)} files: {sum(e['size'] for e in entries):,} -> {output.stat().st_size:,} bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configure", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    BUILD.mkdir(exist_ok=True)
    configuration = BUILD / "runtime-assets-inputs.json"
    if args.configure:
        config = json.loads(args.configure.read_text(encoding="utf-8-sig"))
        config["python"] = sys.executable
        collect(config)
        configuration.write_text(json.dumps(config, indent=2))
    config = json.loads(configuration.read_text())
    if args.check:
        print(f"Verified {len(collect(config))} required input files")
    else:
        pack(config)


if __name__ == "__main__":
    main()
