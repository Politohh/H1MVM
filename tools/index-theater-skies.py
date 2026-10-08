"""Index stock sky pixel blocks without shipping any pixels. Development-only lz4 dependency."""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import struct
import lz4.block


def index_zone(game, zone, assets):
    source = game / (zone + ".ff")
    blocks, starts = [], {}
    with source.open("rb") as file:
        magic = file.read(8)
        if magic not in (b"S1ff0100", b"S1ffu100") or struct.unpack("<I", file.read(4))[0] != 0x42:
            raise ValueError("Unsupported stock fastfile: " + zone)
        file.seek(24)
        count = struct.unpack("<I", file.read(4))[0]
        if count > 100000:
            raise ValueError("Invalid stream table")
        signed = magic == b"S1ff0100"
        file.seek(28 + count * 24 + 18 + (0x8000 if signed else 0))
        block_count = struct.unpack("<I", file.read(4))[0]
        file.read(2)
        quota = 0x800000 - 8 if signed else 1 << 60

        def take(size):
            nonlocal quota
            pieces, segments = [], []
            while size:
                if not quota:
                    file.seek(0x4000, 1)
                    quota = 0x800000
                amount = min(size, quota)
                segments.append([file.tell(), amount])
                data = file.read(amount)
                if len(data) != amount:
                    raise ValueError("Truncated fastfile")
                pieces.append(data)
                quota -= amount
                size -= amount
            return b"".join(pieces), segments

        if take(4)[0] != b"\0\0\x01\x04":
            raise ValueError("Unknown compression stream")
        cursor, tail = 0, b""
        for _ in range(block_count + 1):
            compressed, size = struct.unpack("<II", take(8)[0])
            if not 0 < compressed <= 0x10200 or not 0 < size <= 0x10000:
                raise ValueError("Invalid LZ4 block")
            encoded, segments = take(compressed)
            block = lz4.block.decompress(encoded, uncompressed_size=size)
            if len(block) != size:
                raise ValueError("Invalid decoded block")
            take((-compressed) & 3)
            blocks.append(dict(position=cursor, decoded=size, segments=segments))
            window = tail + block
            for asset in assets:
                name = asset["name"]
                if name in starts:
                    continue
                needle = name.encode() + b"\0"
                offset = window.find(needle)
                if offset < 104:
                    continue
                native = window[offset - 104:offset]
                if (native[28] != 5 or native[53] != 0 or
                        struct.unpack_from("<Q", native, 96)[0] != 0xFDFDFDFFFFFFFFFF or
                        struct.unpack_from("<Q", native, 56)[0] != 0xFDFDFDFFFFFFFFFE):
                    continue
                actual = (struct.unpack_from("<I", native, 24)[0],
                          *struct.unpack_from("<2H", native, 44), native[52], native[31],
                          struct.unpack_from("<I", native, 36)[0])
                expected = tuple(asset[k] for k in ("format", "width", "height", "mips", "flags", "size"))
                if actual != expected:
                    raise ValueError("Sky header provenance mismatch: " + name)
                starts[name] = cursor - len(tail) + offset + len(needle)
            cursor += size
            tail = window[-256:]

        result = []
        for asset in assets:
            start = starts[asset["name"]]
            end = start + asset["size"]
            selected = []
            sha = hashlib.sha256(struct.pack("<8s6I", b"H1SKY001", *(asset[k] for k in
                ("format", "width", "height", "mips", "flags", "size"))))
            total = 0
            for block in blocks:
                first, last = max(start, block["position"]), min(end, block["position"] + block["decoded"])
                if last <= first:
                    continue
                skip, size = first - block["position"], last - first
                data = b""
                for offset, length in block["segments"]:
                    file.seek(offset)
                    data += file.read(length)
                decoded = lz4.block.decompress(data, uncompressed_size=block["decoded"])
                sha.update(decoded[skip:skip + size])
                total += size
                selected.append(dict(decoded=block["decoded"], segments=block["segments"], skip=skip, take=size))
            if total != asset["size"] or sha.hexdigest() != asset["sha256"]:
                raise ValueError("Indexed pixels differ from verified preset: " + asset["name"])
            result.append(dict(asset, source_size=source.stat().st_size, blocks=selected))
    print(f"Verified {zone}: {len(result)} skies", flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    groups = {}
    for asset in manifest["assets"]:
        groups.setdefault(asset["zone"], []).append(asset)
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        results = list(pool.map(lambda pair: index_zone(args.game, *pair), groups.items()))
    assets = sorted((a for group in results for a in group), key=lambda a: a["name"])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(version=1, assets=assets), separators=(",", ":")) + "\n")
    print(f"Verified {len(assets)} presets; index {args.output.stat().st_size:,} bytes; no pixels included")


if __name__ == "__main__":
    main()
