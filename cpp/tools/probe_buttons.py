"""Read-only validation of the installed client's button list; never writes game memory."""
import ctypes as c
import json
import re
import struct
import sys
from pathlib import Path

pid, base = int(sys.argv[1]), int(sys.argv[2])
root = Path(__file__).resolve().parents[1]
profile = json.loads((root / 'assets/memory.json').read_text())
binary = Path(sys.argv[3]).read_bytes()
pe = struct.unpack_from('<I', binary, 0x3c)[0]
assert struct.unpack_from('<I', binary, pe+8)[0] == profile['moduleTimestamp']
assert struct.unpack_from('<I', binary, pe+24+56)[0] == profile['moduleSize']
sections = []
for i in range(struct.unpack_from('<H',binary,pe+6)[0]):
    h=pe+24+struct.unpack_from('<H',binary,pe+20)[0]+40*i
    size,rva,raw_size,raw_ptr=struct.unpack_from('<IIII',binary,h+8)
    sections.append((binary[h:h+8],rva,raw_ptr,raw_size))
matches=[]
for name,rva,start,size in sections:
    if name.startswith(b'.text'):
        for m in re.finditer(rb'\x48\x8b\x15....\x48\x85\xd2\x74.\x48\x8b\x02\x48\x85\xc0',binary[start:start+size],re.S):
            pos=start+m.start()
            matches.append(rva+m.start()+7+struct.unpack_from('<i',binary,pos+3)[0])
assert len(matches)==1, f'Button list signature matches: {len(matches)}'
k=c.WinDLL('kernel32',use_last_error=True)
k.OpenProcess.argtypes=[c.c_ulong,c.c_int,c.c_ulong];k.OpenProcess.restype=c.c_void_p
k.ReadProcessMemory.argtypes=[c.c_void_p,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
k.CloseHandle.argtypes=[c.c_void_p]
handle=k.OpenProcess(0x1010,False,pid)
assert handle, f'OpenProcess error {c.get_last_error()}'
def read(addr,size):
    assert 0x10000<=addr<0x0000800000000000
    out=c.create_string_buffer(size);done=c.c_size_t()
    assert k.ReadProcessMemory(handle,addr,out,size,c.byref(done)) and done.value==size
    return out.raw
try:
    pointer=struct.unpack('<Q',read(base+matches[0],8))[0]
    seen=set();buttons={}
    while pointer:
        assert pointer not in seen and len(seen)<128
        seen.add(pointer)
        data=read(pointer,0x90)
        name=read(struct.unpack_from('<Q',data,8)[0],32).split(b'\0')[0].decode('ascii')
        assert name and re.fullmatch(r'[a-zA-Z0-9_+-]+',name)
        buttons[name]={'rva':pointer+0x30-base,'state':struct.unpack_from('<I',data,0x30)[0],
                       'value':struct.unpack_from('<f',data,0x18)[0]}
        pointer=struct.unpack_from('<Q',data,0x88)[0]
    print(json.dumps({'pid':pid,'listRva':matches[0],'buttons':buttons},indent=2))
    if '--watch' in sys.argv:
        import time
        until=time.monotonic()+30
        previous=None
        while time.monotonic()<until:
            sample=read(base+buttons['forward']['rva']-0x30,0x88)
            if sample!=previous:
                print('FORWARD',sample.hex(), 'state',struct.unpack_from('<I',sample,0x30)[0],flush=True)
                previous=sample
            time.sleep(.002)
finally:
    k.CloseHandle(handle)
