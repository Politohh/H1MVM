"""Choose the smallest bundle representation among pixel-identical PNG copies.

Requires Pillow for independent RGBA verification. Also run scope-pixel-parity
against the selected output before shipping to check the client's native decoder.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from PIL import Image


def consolidate(data):
    """Join IDAT chunks without changing compressed pixels or any metadata."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    chunks = []
    offset = 8
    while offset < len(data):
        size = int.from_bytes(data[offset:offset + 4], "big")
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + size]
        crc = int.from_bytes(data[offset + 8 + size:offset + 12 + size], "big")
        assert zlib.crc32(kind + payload) == crc, "PNG chunk CRC mismatch"
        chunks.append((kind, payload))
        offset += size + 12
    assert offset == len(data)
    output = bytearray(data[:8])
    written = False
    for kind, payload in chunks:
        if kind == b"IDAT":
            if written:
                continue
            payload = b"".join(p for k, p in chunks if k == b"IDAT")
            written = True
        output.extend(struct.pack(">I", len(payload)) + kind + payload)
        output.extend(struct.pack(">I", zlib.crc32(kind + payload)))
    return bytes(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("candidates", nargs="*", type=Path)
    args = parser.parse_args()
    assert args.output.resolve() not in {p.resolve() for p in [args.original, *args.candidates]}
    original = Image.open(args.original).convert("RGBA")
    pixels = original.tobytes()
    options = {str(args.original): args.original.read_bytes(),
               "original-consolidated-IDAT": consolidate(args.original.read_bytes())}
    for path in args.candidates:
        candidate = Image.open(path).convert("RGBA")
        assert candidate.size == original.size and candidate.tobytes() == pixels, f"Pixels changed: {path}"
        options[str(path)] = path.read_bytes()
    sizes = {key: {"bytes": len(data), "packed_bytes": len(zlib.compress(data, 9)),
                   "sha256": hashlib.sha256(data).hexdigest()} for key, data in options.items()}
    best = min(options, key=lambda key: sizes[key]["packed_bytes"])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(options[best])
    selected = Image.open(args.output).convert("RGBA")
    assert selected.size == original.size and selected.tobytes() == pixels
    evidence = {"selected": best, "dimensions": original.size,
                "rgba_sha256": hashlib.sha256(pixels).hexdigest(), "candidates": sizes}
    args.output.with_suffix(".evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps(evidence, indent=2))


if __name__ == "__main__":
    main()
