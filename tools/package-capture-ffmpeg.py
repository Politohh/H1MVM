"""Retain the custom encoder's notices, pinned sources and reproducible recipe.

Only bin/, LICENSE, README.txt and licenses/ enter the runtime bundle. source/
is kept separately alongside the development distribution for release publishing.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

REPO = Path(__file__).resolve().parents[1]
PINS = {
    "x264": ("https://code.videolan.org/videolan/x264.git", "b35605ace3ddf7c1a5d67a2eb553f034aef41d55"),
    "x265": ("https://bitbucket.org/multicoreware/x265_git.git", "1d117bed4747758b51bd2c124d738527e30392cb"),
    "svt-av1": ("https://gitlab.com/AOMediaCodec/SVT-AV1.git", "6dbe8506261ce72c8886c82ffed5bf1608ab12aa"),
}
FFMPEG_SHA = "8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_root", type=Path)
    parser.add_argument("ffmpeg_archive", type=Path)
    parser.add_argument("toolchain_notices", type=Path)
    args = parser.parse_args()
    root = args.build_root.resolve()
    dist = root / "dist"
    notices = dist / "licenses"
    sources = dist / "source"
    notices.mkdir(parents=True, exist_ok=True)
    sources.mkdir(parents=True, exist_ok=True)
    assert sha(args.ffmpeg_archive) == FFMPEG_SHA, "Unexpected FFmpeg source archive"
    shutil.copy2(args.ffmpeg_archive, sources / args.ffmpeg_archive.name)
    info = {"ffmpeg_source_sha256": FFMPEG_SHA, "ffmpeg_version": "9.0.2", "sources": {},
            "encoder_sha256": sha(dist / "bin/ffmpeg.exe")}
    for name, (url, commit) in PINS.items():
        src = root / "src" / name
        head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=src, text=True).strip()
        assert head == commit, (name, head)
        assert not subprocess.check_output(["git", "diff", "HEAD"], cwd=src), "Modified codec source"
        target = sources / (name + ".tar")
        subprocess.run(["git", "archive", "--format=tar", "--prefix=" + name + "/",
                        "--output=" + str(target), "HEAD"], cwd=src, check=True)
        info["sources"][name] = {"url": url, "commit": commit, "archive_sha256": sha(target)}
    for name, source in {
        "x264-COPYING": root / "src/x264/COPYING",
        "x265-COPYING": root / "src/x265/COPYING",
        "SVT-AV1-LICENSE.md": root / "src/svt-av1/LICENSE.md",
        "SVT-AV1-LICENSE-BSD2.md": root / "src/svt-av1/LICENSE-BSD2.md",
        "SVT-AV1-PATENTS.md": root / "src/svt-av1/PATENTS.md",
        "FFmpeg-LICENSE.md": root / "src/ffmpeg-9.0.2/LICENSE.md",
        "build-capture-ffmpeg.sh": REPO / "tools/build-capture-ffmpeg.sh",
        "capture-ffmpeg.md": REPO / "docs/capture-ffmpeg.md",
    }.items():
        shutil.copy2(source, notices / name)
    shutil.copytree(args.toolchain_notices, notices / "toolchain", dirs_exist_ok=True)
    shutil.copy2(root / "src/ffmpeg-9.0.2/COPYING.GPLv2", dist / "LICENSE")
    shutil.copy2(REPO / "tools/build-capture-ffmpeg.sh", sources)
    buildconf = subprocess.check_output([str(dist / "bin/ffmpeg.exe"), "-buildconf"], stderr=subprocess.STDOUT)
    (notices / "buildconf.txt").write_bytes(buildconf)
    (notices / "build-provenance.json").write_text(json.dumps(info, indent=2) + "\n")
    (dist / "README.txt").write_text(
        "H1 Theater capture-only FFmpeg 9.0.2, Windows x64, GPL-2.0-or-later.\n"
        "Includes libx264, libx265, libsvtav1 and native ProRes; RGB24 stdin; MP4/MKV/MOV.\n"
        "No network, audio, ffprobe, ffplay or unrelated codecs. No companion DLLs required.\n"
        "Dependency notices, exact source revisions and build recipe: licenses/.\n"
        "Complete corresponding source is retained in this distribution's source/ directory\n"
        "for separate release distribution. It is not needed to run the client.\n"
        "Upstream FFmpeg: https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz\n"
        "Client build recipe: https://github.com/Politohh/h1-cod4-demo/tree/main/tools\n"
        "Built using w64devkit 2.10.0 / GCC 16.2.0, NASM 2.16.03, CMake 3.31.10 (x265)\n"
        "and CMake 4.4.3 (SVT-AV1).\n")
    print(json.dumps(info, indent=2))


if __name__ == "__main__":
    main()
