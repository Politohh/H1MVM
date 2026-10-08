"""Exercise the bundled encoder with Theater's RGB24 stdin capture arguments.

Use an independent full FFmpeg/ffprobe distribution to decode/inspect output.
This deliberately needs no game, network, companion DLLs, or generated lavfi input.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("encoder", type=Path)
    parser.add_argument("reference_bin", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PATH=os.path.join(os.environ["SystemRoot"], "System32"))
    records = []
    codecs = [("libsvtav1", "av1", "yuv420p"), ("libx265", "hevc", "yuv420p"),
              ("libx264", "h264", "yuv420p"), ("prores", "prores", "yuv444p10le")]
    # Distinct top/bottom colors detect accidental omission of vflip.
    for width, height, frames, fps in [(320, 180, 12, 30), (1920, 1080, 4, 60)]:
        frame = bytes([220, 40, 30]) * (width * (height // 2))
        frame += bytes([30, 40, 220]) * (width * (height // 2))
        for encoder, codec, pixel_format in codecs:
            containers = ["mov"] if codec == "prores" else ["mp4", "mkv"]
            for container in containers:
                for flip in (False, True):
                    name = f"{encoder}-{width}-{container}-flip{int(flip)}"
                    target = (args.output / (name + "." + container)).resolve()
                    command = [str(args.encoder.resolve()), "-y", "-f", "rawvideo", "-pixel_format", "rgb24",
                               "-video_size", f"{width}x{height}", "-framerate", str(fps), "-i", "-"]
                    if flip:
                        command += ["-vf", "vflip"]
                    command += ["-an", "-c:v", encoder]
                    if codec != "prores":
                        command += ["-crf", "18"]
                    command += ["-r", str(fps), "-pix_fmt", pixel_format, str(target)]
                    result = subprocess.run(command, input=frame * frames, stdout=subprocess.PIPE,
                                            stderr=subprocess.PIPE, env=env, cwd=args.output, timeout=180)
                    (args.output / (name + ".log")).write_bytes(result.stderr)
                    if result.returncode:
                        raise RuntimeError(f"{name}: encoder exit {result.returncode}; see log")
                    probe = subprocess.check_output([str(args.reference_bin / "ffprobe.exe"), "-v", "error",
                        "-count_frames", "-show_streams", "-of", "json", str(target)], timeout=60)
                    stream = json.loads(probe)["streams"][0]
                    assert stream["codec_name"] == codec, stream
                    assert (stream["width"], stream["height"]) == (width, height), stream
                    assert int(stream["nb_read_frames"]) == frames, stream
                    assert stream["r_frame_rate"] == f"{fps}/1", stream
                    # Decode every frame (not just a thumbnail), then inspect orientation.
                    decoded = subprocess.check_output([str(args.reference_bin / "ffmpeg.exe"), "-v", "error",
                        "-xerror", "-i", str(target), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], timeout=90)
                    assert len(decoded) == len(frame) * frames, name
                    offset = (width * (height // 4) + width // 2) * 3
                    red, _, blue = decoded[offset:offset + 3]
                    assert (blue > red + 100) if flip else (red > blue + 100), name
                    records.append({"name": name, "command": command, "bytes": target.stat().st_size,
                                    "stream": stream, "decoded_bytes": len(decoded)})
                    print("PASS", name, flush=True)
    evidence = {"encoder_sha256": hashlib.sha256(args.encoder.read_bytes()).hexdigest(),
                "cases": records, "clean_path": env["PATH"]}
    (args.output / "results.json").write_text(json.dumps(evidence, indent=2) + "\n")


if __name__ == "__main__":
    main()
