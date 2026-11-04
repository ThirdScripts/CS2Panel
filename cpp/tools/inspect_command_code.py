"""Offline PE inspection only; does not open or modify the running game."""
import struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'artifacts/python-deps'))
import capstone
import numpy as np
d=Path(sys.argv[1]).read_bytes(); pe=struct.unpack_from('<I',d,0x3c)[0]
base=struct.unpack_from('<Q',d,pe+24+24)[0]; sections=[]
for i in range(struct.unpack_from('<H',d,pe+6)[0]):
 h=pe+24+struct.unpack_from('<H',d,pe+20)[0]+i*40
 vs,rva,size,raw=struct.unpack_from('<IIII',d,h+8)
 sections.append((d[h:h+8],rva,raw,size))
def raw(rva):
 for _,v,p,n in sections:
  if v<=rva<v+n:return p+rva-v
 raise ValueError(hex(rva))
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
def dis(rva,n=110):
 for op in md.disasm(d[raw(rva):raw(rva)+n],rva): print(hex(op.address),op.mnemonic,op.op_str)
if sys.argv[2].startswith('0x'):
 if len(sys.argv)>3 and sys.argv[3]=='vtable':
  obj=int(sys.argv[2],16); vt=struct.unpack_from('<Q',d,raw(obj))[0]-base
  print('object',hex(obj),'vtable',hex(vt))
  for i in range(52,58):
   fn=struct.unpack_from('<Q',d,raw(vt)+i*8)[0]-base
   print('SLOT',i,hex(fn));dis(fn,100)
  sys.exit()
 dis(int(sys.argv[2],16),int(sys.argv[3]) if len(sys.argv)>3 else 300);sys.exit()
needle=sys.argv[2].encode()+b'\0';targets=[]
for _,v,p,n in sections:
 off=d.find(needle,p,p+n)
 if off>=0:targets.append(v+off-p)
print('imagebase',hex(base),'strings',[hex(x) for x in targets])
for name,v,p,n in sections:
 if not name.startswith(b'.text'):continue
 code=d[p:p+n]
 for align in range(4):
  end=align+(n-align)//4*4
  vals=np.frombuffer(code[align:end],dtype='<i4').astype(np.int64)
  pos=np.arange(len(vals),dtype=np.int64)*4+align
  addr=vals+pos+v+4
  for target in targets:
   for j in np.flatnonzero(addr==target):
    at=int(pos[j])
    for pre in range(2,5):
     ins=list(md.disasm(code[at-pre:at+4],v+at-pre))
     if len(ins)==1 and ins[0].size==pre+4 and 'rip' in ins[0].op_str:
      print('REFERENCE');dis(v+at-pre);break
for target in targets:
 needle=struct.pack('<Q',base+target)
 for _,v,p,n in sections:
  start=p
  while (off:=d.find(needle,start,p+n))>=0:
   print('POINTER',hex(v+off-p),'bytes',d[max(p,off-16):off+24].hex());start=off+1

