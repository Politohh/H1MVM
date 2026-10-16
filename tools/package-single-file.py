"""Stage an exact-tag single-EXE release, checking both embedded payloads."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import mmap
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def sha(path):
    with Path(path).open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def resource_hash(executable, identifier):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
    kernel.LoadLibraryExW.restype = wintypes.HMODULE
    kernel.FindResourceW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p]
    kernel.FindResourceW.restype = wintypes.HANDLE
    kernel.LoadResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
    kernel.LoadResource.restype = wintypes.HANDLE
    kernel.LockResource.argtypes = [wintypes.HANDLE]
    kernel.LockResource.restype = ctypes.c_void_p
    kernel.SizeofResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
    kernel.SizeofResource.restype = wintypes.DWORD
    kernel.FreeLibrary.argtypes = [wintypes.HMODULE]
    module = kernel.LoadLibraryExW(str(executable), None, 2)  # DATAFILE; never executes client code.
    if not module:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        resource = kernel.FindResourceW(module, identifier, 10)
        if not resource:
            raise ValueError(f"Missing embedded resource {identifier}")
        size = kernel.SizeofResource(module, resource)
        address = kernel.LockResource(kernel.LoadResource(module, resource))
        if not address or not size:
            raise ValueError(f"Unreadable embedded resource {identifier}")
        data = (ctypes.c_ubyte * size).from_address(address)
        return hashlib.sha256(data).hexdigest(), size
    finally:
        kernel.FreeLibrary(module)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tag")
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    if git("status", "--porcelain"):
        raise ValueError("Release requires a clean worktree")
    commit = git("rev-parse", "HEAD")
    if git("rev-parse", args.tag + "^{commit}") != commit or git("describe", "--tags", "--exact-match") != args.tag:
        raise ValueError("Release tag must identify the current commit")
    version = (ROOT / "build/src/version.h").read_text()
    if f'#define GIT_DESCRIBE "{args.tag}"' not in version or '#define GIT_DIRTY 0' not in version or commit not in version:
        raise ValueError("Rebuild after committing and tagging the release")
    executable = ROOT / "build/bin/x64/Release/h1-mod.exe"
    with executable.open("rb") as source, mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as binary:
        if binary.find(args.tag.encode() + b"\0") < 0:
            raise ValueError("EXE does not contain the exact release version")
        if binary.find(("H1MVM " + args.tag).encode() + b"\0") < 0:
            raise ValueError("EXE does not contain the H1MVM release display name")
    payloads = {}
    for name, identifier, source in (
            ("runtime", 313, ROOT / "build/runtime-assets.bin"),
            ("tlsdll", 306, ROOT / "build/bin/x64/Release/tlsdll.dll")):
        embedded_hash, size = resource_hash(executable, identifier)
        if embedded_hash != sha(source):
            raise ValueError("Stale embedded " + name + "; rebuild the resource")
        payloads[name] = {"sha256": embedded_hash, "bytes": size}
    destination = args.destination.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    target = destination / "h1-mod.exe"
    if target.exists():
        raise ValueError("Release EXE already staged; use a new directory")
    shutil.copy2(executable, target)
    digest = sha(executable)
    if sha(target) != digest:
        raise ValueError("Staged EXE hash mismatch")
    (destination / "SHA256SUMS.txt").write_text(f"{digest}  h1-mod.exe\n")
    manifest = json.loads((ROOT / "build/runtime-assets-manifest.json").read_text())
    evidence = {"tag": args.tag, "commit": commit, "sha256": digest, "bytes": target.stat().st_size,
                "payloads": payloads, "files": len(manifest["files"])}
    (destination / "release-provenance.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps(evidence, indent=2))


if __name__ == "__main__":
    main()
