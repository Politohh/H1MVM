"""Package an original research fixture into a COPY of the prototype EXE.

No installed/product binary is modified. Existing embedded entries are verified
and preserved; retail source data is not added to this bundle.
"""
from pathlib import Path
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import shutil
import struct
import zlib
import argparse

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--script',default='duel.gsc')
parser.add_argument('--destination',default='bundled-prototype')
parser.add_argument('--ui')
parser.add_argument('--scoreboard',default='experiments/sp-bots/spai_scoreboard.lua')
parser.add_argument('--source',default='h1-mod.exe')
parser.add_argument('--startup-ui',help='Explicit isolated replacement for startup_lobby.lua')
options=parser.parse_args()
source=(root/options.source).resolve()
if not source.is_relative_to(root) or source.name!='h1-mod.exe':
    raise RuntimeError('Source EXE must stay inside research directory')
script=(root/options.script).resolve()
destination=(root/options.destination/'h1-mod.exe').resolve()
if not script.is_relative_to(root) or not destination.is_relative_to(root) or destination==source:
    raise RuntimeError('Research input/output must stay inside the research directory, without replacing the source EXE')
if destination.exists():raise RuntimeError('Use a fresh package directory; existing evidence is preserved')
k=c.WinDLL('kernel32',use_last_error=True)
k.LoadLibraryExW.argtypes=[w.LPCWSTR,w.HANDLE,w.DWORD];k.LoadLibraryExW.restype=w.HMODULE
k.FindResourceW.argtypes=[w.HMODULE,c.c_void_p,c.c_void_p];k.FindResourceW.restype=w.HANDLE
k.LoadResource.argtypes=[w.HMODULE,w.HANDLE];k.LoadResource.restype=w.HANDLE
k.LockResource.argtypes=[w.HANDLE];k.LockResource.restype=c.c_void_p
k.SizeofResource.argtypes=[w.HMODULE,w.HANDLE];k.SizeofResource.restype=w.DWORD
k.FreeLibrary.argtypes=[w.HMODULE]
k.BeginUpdateResourceW.argtypes=[w.LPCWSTR,w.BOOL];k.BeginUpdateResourceW.restype=w.HANDLE
k.UpdateResourceW.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,w.WORD,c.c_void_p,w.DWORD]
k.EndUpdateResourceW.argtypes=[w.HANDLE,w.BOOL]

def read_bundle(path):
    module=k.LoadLibraryExW(str(path),None,0x22) # data/image resource only; no entrypoint
    if not module:raise c.WinError(c.get_last_error())
    try:
        resource=k.FindResourceW(module,313,10)
        if not resource:raise c.WinError(c.get_last_error())
        loaded=k.LoadResource(module,resource)
        return c.string_at(k.LockResource(loaded),k.SizeofResource(module,resource))
    finally:k.FreeLibrary(module)

def unpack(data):
    assert data[:8]==b'H1PACK01'
    length=struct.unpack_from('<I',data,8)[0]
    manifest=json.loads(data[12:12+length])
    assert manifest['version']==1
    files={}
    for entry in manifest['files']:
        name=entry['path'];assert name not in files
        start=12+length+entry['offset']
        decoded=zlib.decompress(data[start:start+entry['compressed_size']])
        assert len(decoded)==entry['size'] and hashlib.sha256(decoded).hexdigest()==entry['sha256'],name
        files[name]=decoded
    return files

original=unpack(read_bundle(source))
files=dict(original)
replacements={}
if options.startup_ui:
    startup=(root/options.startup_ui).resolve()
    if not startup.is_relative_to(root):raise RuntimeError('Startup UI must stay inside research')
    startup_name='cdata/ui_scripts/patches/startup_lobby.lua'
    assert startup_name in original
    files[startup_name]=startup.read_bytes()
    replacements[startup_name]=dict(before=hashlib.sha256(original[startup_name]).hexdigest(),
                                    after=hashlib.sha256(files[startup_name]).hexdigest())
entry_name='cdata/scripts/mp/zz_spai_probe.gsc'
assert entry_name not in files
files[entry_name]=script.read_bytes()
if options.ui:
    ui=(root/options.ui).resolve()
    if not ui.is_relative_to(root):raise RuntimeError('UI source must stay inside research')
    ui_name='cdata/ui_scripts/spai/__init__.lua'
    assert ui_name not in files
    files[ui_name]=ui.read_bytes()
if options.scoreboard and options.ui:
    score_ui=(root/options.scoreboard).resolve()
    if not score_ui.is_relative_to(root):raise RuntimeError('Scoreboard source must stay inside research')
    score_ui_name='cdata/ui_scripts/spai/spai_scoreboard.lua'
    assert score_ui_name not in files
    files[score_ui_name]=score_ui.read_bytes()
entries=[];payload=bytearray()
for name,data in sorted(files.items()):
    compressed=zlib.compress(data,9)
    entries.append(dict(path=name,size=len(data),sha256=hashlib.sha256(data).hexdigest(),offset=len(payload),compressed_size=len(compressed)))
    payload.extend(compressed)
header=json.dumps(dict(version=1,files=entries),sort_keys=True,separators=(',',':')).encode()
bundle=b'H1PACK01'+struct.pack('<I',len(header))+header+payload
destination.parent.mkdir(exist_ok=True)
shutil.copyfile(source,destination)
handle=k.BeginUpdateResourceW(str(destination),False)
if not handle:raise c.WinError(c.get_last_error())
committed=False
try:
    buffer=c.create_string_buffer(bundle)
    if not k.UpdateResourceW(handle,10,313,1033,buffer,len(bundle)):raise c.WinError(c.get_last_error())
    if not k.EndUpdateResourceW(handle,False):raise c.WinError(c.get_last_error())
    committed=True
finally:
    if not committed:k.EndUpdateResourceW(handle,True)
verified=unpack(read_bundle(destination))
assert verified==files
assert all(verified[name]==data for name,data in original.items() if name not in replacements)
report=dict(source_exe_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
            executable_sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),
            script_sha256=hashlib.sha256(files[entry_name]).hexdigest(),script_path=entry_name,
            bundle_header_sha256=hashlib.sha256(header).hexdigest(),files=len(files),preserved_files=len(original)-len(replacements),
            purpose='Isolated opt-in research fixture; not a release or final Private Match controller.')
if options.ui:report['ui_sha256']=hashlib.sha256(files[ui_name]).hexdigest()
if options.scoreboard and options.ui:report['scoreboard_sha256']=hashlib.sha256(files[score_ui_name]).hexdigest()
if replacements:report['replaced_files']=replacements
(destination.parent/'manifest.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
