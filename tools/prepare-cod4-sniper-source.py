"""Prepare pinned COD4 ADS clips and the scope from a local COD4 install.

Authored assets stay outside Git, as with the facial conversion inputs.
Requires Pillow for decoding the original DXT5 scope image.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import urllib.request
import zipfile
from concurrent.futures import ThreadPoolExecutor

COMMIT = "95c76d6a470bea43fdd5ab591afe8475fac7699d"
REPOSITORY = "cod4mw/CoD4-Mod-Tools"

# Player-state action numbers, NOT WeaponAnimSlot asset indices. Verified from
# COD4 CG_UpdateViewWeaponAnim's 30-entry dispatch table. Keep the converter's
# existing H1 tokens so already converted recordings use the corrected clips.
ACTION_FIELDS = [
    (0, "idleAnim", ""), (1, "idleAnim", ""),
    (2, "fireAnim", "fireTime"), (3, "lastShotAnim", "fireTime"), (4, "rechamberAnim", "rechamberTime"),
    (5, "adsFireAnim", "fireTime"), (6, "adsLastShotAnim", "fireTime"), (7, "adsRechamberAnim", "rechamberTime"),
    (8, "meleeAnim", "meleeTime"), (9, "meleeChargeAnim", "meleeChargeTime"),
    (10, "dropAnim", "dropTime"), (11, "raiseAnim", "raiseTime"), (12, "firstRaiseAnim", "firstRaiseTime"),
    (13, "reloadAnim", "reloadTime"), (14, "reloadEmptyAnim", "reloadEmptyTime"),
    (15, "reloadStartAnim", "reloadStartTime"), (16, "reloadEndAnim", "reloadEndTime"),
    (17, "altDropAnim", "altDropTime"), (18, "altRaiseAnim", "altRaiseTime"),
    (19, "quickDropAnim", "quickDropTime"), (20, "quickRaiseAnim", "quickRaiseTime"),
    (21, "emptyDropAnim", "emptyDropTime"), (22, "emptyRaiseAnim", "emptyRaiseTime"),
    (23, "sprintInAnim", "sprintInTime"), (24, "sprintLoopAnim", "sprintLoopTime"),
    (25, "sprintOutAnim", "sprintOutTime"), (26, "holdFireAnim", "fireTime"),
    (27, "detonateAnim", "detonateTime"), (28, "nightVisionWearAnim", "nightVisionWearTime"),
    (29, "nightVisionRemoveAnim", "nightVisionRemoveTime")]
H1_ACTIONS = [0,1,2,3,4,5,6,7,10,10,17,18,19,20,21,22,23,25,26,27,28,29,30,31,33,34,38,40,41,42]
RECOIL_FIELDS = [prefix + field for prefix in ("ads", "hip") for field in (
    "GunKickReducedKickPercent", "GunKickPitchMin", "GunKickPitchMax",
    "GunKickYawMin", "GunKickYawMax", "GunKickAccel", "GunKickSpeedMax",
    "GunKickSpeedDecay", "GunKickStaticDecay", "ViewKickPitchMin", "ViewKickPitchMax",
    "ViewKickYawMin", "ViewKickYawMax", "ViewKickCenterSpeed")]


def prepare_animation_set(entries, output, fetch, main):
    rifles = []
    paths = set()
    for weapon in ("m40a3_mp", "remington700_mp", "barrett_mp", "m21_mp", "dragunov_mp"):
        with zipfile.ZipFile(main / "iw_11.iwd") as archive:
            data = archive.read("weapons/mp/" + weapon)
        output.joinpath(weapon + ".weapon").write_bytes(data)
        fields = data.decode("latin1").split("\\")
        values = dict(zip(fields[1::2], fields[2::2]))
        if any(int(values[prefix + "GunKickReducedKickBullets"]) for prefix in ("ads", "hip")):
            raise ValueError("Unsupported original sniper reduced-kick bullet count")
        model = values["gunModel"].lower()
        model_data = fetch("raw/xmodel/" + model)
        # v25 XModel: first LOD parts name follows the fixed bounds/header.
        if struct.unpack_from("<H", model_data)[0] != 25:
            raise ValueError("Unsupported sniper model")
        part = model_data[32:].split(b"\0", 1)[0].decode("ascii").lower()
        paths.add("raw/xmodelparts/" + part)
        paths.add("raw/xmodelparts/viewhands_usmc3")
        actions = []
        for source, field, timer in ACTION_FIELDS:
            name = values.get(field, "").lower()
            if not name:
                # CG_CreateWeaponViewModelXAnim falls back to idle for every
                # absent action, including semi-auto rechamber and empty clips.
                name = values["idleAnim"].lower()
            path = "raw/xanim/" + name
            if path not in entries:
                raise ValueError("Missing authored animation: " + path)
            paths.add(path)
            if any(action["logical"] == H1_ACTIONS[source] for action in actions):
                continue # H1 shares the melee/charge token; prefer melee.
            actions.append({"logical": H1_ACTIONS[source], "clip": name,
                            "timer_ms": round(float(values.get(timer, "0")) * 1000),
                            # StartWeaponAnim g_animRateOffsets: fire/bolt and
                            # detonate play naturally; timed actions scale the
                            # complete clip to their weapon-definition duration.
                            "scale_to_timer": 8 <= source <= 26 or source in (28, 29),
                            "loop": source in (0, 1, 24)})
        rifles.append({"weapon": weapon, "definition_sha256": hashlib.sha256(data).hexdigest(),
                       "model_git_blob": entries["raw/xmodel/" + model]["sha"],
                       "hands": "viewhands_usmc3.rig", "gun": part + ".rig",
                       "recoil": [float(values[field]) for field in RECOIL_FIELDS],
                       "actions": actions})
    def download(path):
        data = fetch(path)
        name = path.split("/")[-1] + (".rig" if "/xmodelparts/" in path else "")
        output.joinpath(name).write_bytes(data)
        return {"name": name, "git_blob": entries[path]["sha"], "sha256": hashlib.sha256(data).hexdigest()}
    with ThreadPoolExecutor(max_workers=8) as pool:
        files = list(pool.map(download, sorted(paths)))
    descriptor = {"version": 4, "rifles": rifles}
    output.joinpath("animations.json").write_text(json.dumps(descriptor, indent=2))
    files.append({"name": "animations.json", "sha256": hashlib.sha256(output.joinpath("animations.json").read_bytes()).hexdigest()})
    return files


def main():
    from PIL import Image
    parser = argparse.ArgumentParser()
    parser.add_argument("--cod4-main", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--full", action="store_true", help="Prepare original sniper actions and rest rigs")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(
        f"https://api.github.com/repos/{REPOSITORY}/git/trees/{COMMIT}?recursive=1",
        headers={"User-Agent": "h1-cod4-demo"})
    with urllib.request.urlopen(request, timeout=60) as response:
        tree = json.load(response)
    if tree.get("truncated"):
        raise ValueError("Incomplete source tree")
    entries = {value["path"].lower(): value for value in tree["tree"]}
    def fetch(path):
        entry = entries[path.lower()]
        with urllib.request.urlopen(f"https://raw.githubusercontent.com/{REPOSITORY}/{COMMIT}/{entry['path']}", timeout=60) as response:
            data = response.read()
        digest = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if digest != entry["sha"]:
            raise ValueError("Original asset hash mismatch: " + path)
        return data
    files = []
    for rifle in ("m40a3", "remington", "barrett", "m21", "dragunov"):
        for direction in ("up", "down"):
            name = f"viewmodel_{rifle}_ads_{direction}"
            path = "raw/xanim/" + name
            data = fetch(path)
            digest = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
            if digest != entries[path]["sha"]:
                raise ValueError("Original animation hash mismatch: " + name)
            (args.output / name).write_bytes(data)
            files.append({"name": name, "git_blob": digest, "sha256": hashlib.sha256(data).hexdigest()})
    if args.full:
        files.extend(prepare_animation_set(entries, args.output, fetch, args.cod4_main))
        # ADS clips can also appear in the full set; install each file once.
        files = list({value["name"]: value for value in files}.values())
    with zipfile.ZipFile(args.cod4_main / "iw_03.iwd") as archive:
        data = archive.read("images/scope_overlay_m40a3_1024#0.iwi")
    width, height = struct.unpack_from("<HH", data, 6)
    if data[:6] != b"IWi\x06\x0d\xc3" or width != 1024 or height != 1024 or len(data) != 28 + width * height:
        raise ValueError("Unsupported original scope image")
    header = (struct.pack("<7I", 124, 0x81007, height, width, width * height, 0, 1) + bytes(44)
              + struct.pack("<8I", 32, 4, int.from_bytes(b"DXT5", "little"), 0, 0, 0, 0, 0)
              + struct.pack("<5I", 0x1000, 0, 0, 0, 0))
    Image.open(io.BytesIO(b"DDS " + header + data[28:])).save(args.output / "scope.png")
    files.append({"name": "scope.png", "source_sha256": hashlib.sha256(data).hexdigest(),
                  "sha256": hashlib.sha256((args.output / "scope.png").read_bytes()).hexdigest()})
    (args.output / "manifest.json").write_text(json.dumps({"repository": REPOSITORY, "commit": COMMIT, "files": files}, indent=2))
    print(f"Prepared {len(files)} authenticated sniper assets in {args.output}")


if __name__ == "__main__":
    main()
