"""Read-only disassembly of references to the pinned client's input button objects."""
import sys, struct, json
from pathlib import Path
root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root/'artifacts/python-deps'))
import capstone
import numpy as np
data = Path(sys.argv[1]).read_bytes()
pe = struct.unpack_from('<I',data,0x3c)[0]
profile = json.loads((root/'cpp/assets/input-buttons.json').read_text(encoding='utf-8-sig'))
md = capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
targets = [profile['buttons'][name]['rva'] for name in ('forward','attack')]
for i in range(struct.unpack_from('<H',data,pe+6)[0]):
    h=pe+24+struct.unpack_from('<H',data,pe+20)[0]+i*40
    if not data[h:h+8].startswith(b'.text'): continue
    _,rva,size,start=struct.unpack_from('<IIII',data,h+8)
    code=data[start:start+size]
    if len(sys.argv)>2:
        for requested in sys.argv[2:]:
            address=int(requested,16)
            print('\nFUNCTION',hex(address))
            for op in md.disasm(code[address-rva:address-rva+640],address):
                print(hex(op.address),op.mnemonic,op.op_str)
        continue
    candidates=[]
    for alignment in range(4):
        end=alignment+(len(code)-alignment)//4*4
        values=np.frombuffer(code[alignment:end],dtype='<i4').astype(np.int64)
        positions=np.arange(len(values),dtype=np.int64)*4+alignment
        addresses=values+positions+rva+4
        for target in targets:
            for j in np.flatnonzero((addresses>=target-0x30)&(addresses<target+0x58)):
                candidates.append(int(positions[j]))
    for pos in sorted(set(candidates)):
        # Locate the full RIP-relative instruction ending after this displacement.
        for prefix in range(2,5):
            begin=pos-prefix
            ins=list(md.disasm(code[begin:pos+4],rva+begin))
            if len(ins)!=1 or ins[0].size!=prefix+4 or 'rip' not in ins[0].op_str: continue
            print('\nREFERENCE',hex(rva+begin))
            for op in md.disasm(code[begin:begin+100],rva+begin):
                print(hex(op.address),op.mnemonic,op.op_str)
            break
