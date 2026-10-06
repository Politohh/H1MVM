"""Extract native H1 sky cubemaps from the user's stock map fastfiles.
Requires Python lz4. Writes only to the specified development directory.
Format: H1SKY001 + six little-endian uint32 fields + native face/mip pixels.
No map geometry, shaders, or game binaries are copied.
"""
import argparse, concurrent.futures, hashlib, json, pathlib, re, struct
import lz4.block

MAPS = "ac130 aftermath airlift airplane ambush armada blackout bog_a bog_b cargoship coup hunted icbm jeepride killhouse launchfacility_a launchfacility_b scoutsniper simplecredits sniperescape village_assault village_defend".split()
IMAGE_NAME = re.compile(rb"[A-Za-z0-9_]{0,64}sky[A-Za-z0-9_]{0,64}_ft\x00")
INLINE_NAME = 0xFDFDFDFFFFFFFFFF
INLINE_PIXELS = 0xFDFDFDFFFFFFFFFE

def extract(file, output):
    found = []
    with file.open("rb") as f:
        magic = f.read(8)
        if magic not in (b"S1ff0100",b"S1ffu100") or struct.unpack("<I",f.read(4))[0] != 0x42:
            raise ValueError("Unsupported stock H1 fastfile: " + file.name)
        f.seek(24); count = struct.unpack("<I",f.read(4))[0]
        if count > 100000: raise ValueError("Invalid stream table")
        signed=magic==b"S1ff0100"
        f.seek(28 + count*24 + 18 + (0x8000 if signed else 0))
        blocks = struct.unpack("<I",f.read(4))[0]; f.read(2)
        # The stock signed stream's initial eight bytes occupy its first
        # authentication page. Subsequent pages contain 8 MiB of payload.
        quota = 0x800000 - 8 if signed else 1 << 60
        def take(n):
            nonlocal quota
            parts=[]
            while n:
                if quota == 0: f.seek(0x4000,1); quota=0x800000
                amount=min(n,quota); data=f.read(amount)
                if len(data)!=amount: raise ValueError("Truncated fastfile")
                parts.append(data); quota-=amount; n-=amount
            return b"".join(parts)
        if take(4) != b"\0\0\x01\x04": raise ValueError("Unknown compression stream")
        tail=b""; pending=None
        for _ in range(blocks+1):
            compressed, size = struct.unpack("<II",take(8))
            if not 0<compressed<=0x10200 or not 0<size<=0x10000: raise ValueError(("Invalid LZ4 block",_,compressed,size))
            block=lz4.block.decompress(take(compressed),uncompressed_size=size)
            if len(block)!=size: raise ValueError("Invalid decoded block")
            take((-compressed)&3)
            if pending:
                info,pixels=pending; amount=min(info["size"]-len(pixels),len(block));pixels.extend(block[:amount])
                if len(pixels)==info["size"]:
                    save(info,pixels,output); found.append(info); pending=None
                else: tail=b"";continue
            window=tail+block
            for match in IMAGE_NAME.finditer(window):
                pos=match.start()-104
                if pos<0:continue
                header=window[pos:pos+104]
                fmt=struct.unpack_from("<I",header,24)[0]
                w,h,depth,elements=struct.unpack_from("<4H",header,44)
                levels,streamed=header[52:54]
                size=struct.unpack_from("<I",header,36)[0]
                if header[28]!=5 or streamed or struct.unpack_from("<Q",header,96)[0]!=INLINE_NAME or struct.unpack_from("<Q",header,56)[0]!=INLINE_PIXELS: continue
                if fmt not in (71,72,77,78,95,96,98,99): raise ValueError("Unsupported sky format")
                bytes_per_block=8 if fmt in (71,72) else 16
                expected=sum(max(1,(w>>i)+3>>2)*max(1,(h>>i)+3>>2)*bytes_per_block for i in range(levels))*6
                if w!=h or not 16<=w<=8192 or depth!=1 or elements!=1 or not 1<=levels<=14 or expected!=size: raise ValueError("Invalid sky image layout")
                info={"name":match.group()[:-1].decode(),"zone":file.stem,"format":fmt,"width":w,"height":h,"mips":levels,"flags":header[31],"size":size}
                pixels=bytearray(window[match.end():match.end()+size])
                if len(pixels)==size:save(info,pixels,output);found.append(info)
                else: pending=(info,pixels);break
            tail=window[-256:] if pending is None else b""
        if pending:raise ValueError("Truncated sky pixels")
    return found

def save(info,pixels,output):
    payload=struct.pack("<8s6I",b"H1SKY001",info["format"],info["width"],info["height"],info["mips"],info["flags"],info["size"])+pixels
    info["sha256"]=hashlib.sha256(payload).hexdigest()
    # Each zone writes to its own staging directory; duplicates are reconciled
    # after workers finish, so concurrent extraction never races an asset file.
    folder=output/info["zone"];folder.mkdir(parents=True,exist_ok=True)
    (folder/(info["name"]+".h1sky")).write_bytes(payload)

def main():
    parser=argparse.ArgumentParser();parser.add_argument("game",type=pathlib.Path);parser.add_argument("output",type=pathlib.Path);parser.add_argument("--only");args=parser.parse_args()
    names=MAPS+["common_mp","common_core_mp","common_wet_mp","common_depot_mp"]+[p.stem for p in args.game.glob("mp_*.ff") if not p.stem.endswith(("_tr","_load","_path")) and p.stat().st_size>10000000]
    names=[n for n in names if (args.game/(n+".ff")).is_file() and not n.startswith(("mp_body","mp_head","mp_vm","mp_wm","mp_gloves"))]
    names+= ["patch_"+n for n in list(names) if (args.game/("patch_"+n+".ff")).is_file()]
    if args.only:names=[args.only]
    args.output.mkdir(parents=True,exist_ok=True);inventory=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        jobs={pool.submit(extract,args.game/(n+".ff"),args.output):n for n in names}
        for future in concurrent.futures.as_completed(jobs):
            assets=future.result();inventory.extend(assets);print(jobs[future],len(assets),flush=True)
    unique={}
    for info in inventory:
        old=unique.get(info["name"])
        if old and old["sha256"]!=info["sha256"]:raise ValueError("Conflicting sky image: "+info["name"])
        unique[info["name"]]=info
    (args.output/"manifest.json").write_text(json.dumps({"zones":names,"assets":sorted(unique.values(),key=lambda x:x["name"]),"occurrences":inventory},indent=2))
    print("Verified",len(unique),"unique skies from",len(names),"zones",flush=True)
if __name__=="__main__":main()
