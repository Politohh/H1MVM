#!/usr/bin/env python
# CoD4 (iw3mp.exe) IDA-MCP helper. Port 12345.
import sys, json, urllib.request, os

PORT = 12345
OUTDIR = os.path.dirname(os.path.abspath(__file__))

def call(name, args, timeout=90):
    body = json.dumps({"jsonrpc":"2.0","id":1,"method":"tools/call",
                       "params":{"name":name,"arguments":args}}).encode()
    req = urllib.request.Request(f"http://127.0.0.1:{PORT}/mcp", data=body,
                                 headers={"Content-Type":"application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        d = json.load(r)
    txt = d["result"]["content"][0]["text"]
    try:
        return json.loads(txt)
    except Exception:
        return txt

def decompile(addr, save=None):
    j = call("decompile", {"addr": addr})
    if isinstance(j, str):
        return j
    out = [j["code"], "\n// ---- refs ----"]
    for r in j.get("refs", []):
        s = (' "'+r["string"].replace(chr(10),'\\n')+'"') if r.get("string") else ''
        out.append(f"// {r['addr']} {r['name']}{s}")
    text = "\n".join(out)
    if save:
        with open(os.path.join(OUTDIR, save), "w", encoding="utf-8") as f:
            f.write(text)
    return text

def disasm(addr, n=60):
    return call("disasm", {"addr": addr, "count": n})

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "dc":
        addr = sys.argv[2]
        save = sys.argv[3] if len(sys.argv) > 3 else None
        print(decompile(addr, save))
    elif cmd == "dis":
        print(json.dumps(disasm(sys.argv[2], int(sys.argv[3]) if len(sys.argv)>3 else 60), indent=1)[:4000])
    elif cmd == "raw":
        print(json.dumps(call(sys.argv[2], json.loads(sys.argv[3])), indent=1)[:4000])
