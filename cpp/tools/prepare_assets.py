"""Build-time only: pin public offsets and a version-matched Source 2 nav mesh.

Binary layout reference: ValveResourceFormat/NavMesh and Awpy nav.py.
The executable does not require Python or download offsets at runtime.
"""
import hashlib
import io
import json
import struct
import re
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAME = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game')
research = ROOT / 'research'
assets = ROOT / 'assets'
manifest = json.loads((research / 'manifest.json').read_text())
version = dict(line.split('=', 1) for line in (GAME / 'csgo/steam.inf').read_text().splitlines())['ClientVersion']
assert version == manifest['client_version'], 'Nav release must match installed CS2'
zip_bytes = (research / 'navs.zip').read_bytes()
assert hashlib.sha256(zip_bytes).hexdigest() == manifest['artifacts']['navs.zip']['sha256']
with zipfile.ZipFile(io.BytesIO(zip_bytes)) as z:
    member = next(n for n in z.namelist() if n.endswith('de_dust2.nav'))
    raw = z.read(member)
(assets / 'de_dust2.nav').write_bytes(raw)
f = io.BytesIO(raw)
def read(fmt):
    return struct.unpack('<' + fmt, f.read(struct.calcsize('<' + fmt)))
def u32():
    return read('I')[0]
magic, navversion, subversion = read('III')
assert magic == 0xFEEDFACE and 31 <= navversion <= 36
read('B')
for _ in range(read('H')[0]): f.read(read('H')[0])
read('B')
def skip_kv3():
    f.seek((f.tell()+7)&~7)
    assert u32() == 0x4B563305, 'Expected KV3 v5'
    f.read(16)
    compression = u32()
    f.read(24)
    uncomp, comp, blocks, blobs = read('IIII')
    f.read(8)
    u1,c1,u2,c2 = read('IIII')
    f.read(32)
    assert blocks == 0 and blobs == 0, 'Unexpected KV3 blobs in nav header'
    f.seek((u1+u2 if compression==0 else c1+c2),1)
if navversion >= 36: skip_kv3()
vertices = [read('fff') for _ in range(u32())]
polygons = []
for _ in range(u32()):
    poly = [vertices[u32()] for _ in range(read('B')[0])]
    if navversion >= 35: u32()
    polygons.append(poly)
if navversion >= 32:
    gravity_flags = u32()
    if gravity_flags & 1: f.read(12*u32())
if navversion >= 35:
    assert u32()==0, 'Movable meshes require explicit transform support'
if navversion >= 36: skip_kv3()
areas = []
for _ in range(u32()):
    identifier, attrs, hull = read('IqB')
    poly = polygons[u32()]
    u32()
    links = []
    for edge in range(len(poly)):
        for _ in range(u32()):
            target, target_edge = read('II')
            links.append((target, edge, target_edge))
    f.read(5)
    for direction in range(2):
        for _ in range(u32()): u32()
    areas.append(dict(id=identifier, poly=poly, hull=hull, attrs=attrs, links=links))
# Hull zero is the normal player hull; do not mix meshes for different bodies.
def planar_area(poly):
    return abs(sum(p[0]*q[1]-p[1]*q[0] for p,q in zip(poly,poly[1:]+poly[:1])))*.5
areas = [a for a in areas if a['hull'] == 0 and len(a['poly'])>=3 and planar_area(a['poly'])>1]
index = {a['id']: i for i, a in enumerate(areas)}
output_areas = []
for a in areas:
    poly = a['poly']
    floor = sum(p[2] for p in poly) / len(poly)
    output_areas.append(dict(poly=[p[:2] for p in poly], heights=[p[2] for p in poly], floor=floor, ceiling=floor+128, flags=0))
connections = []
for i, a in enumerate(areas):
    for target, edge, target_edge in a['links']:
        if target not in index: continue
        j = index[target]
        p, q = a['poly'][edge], a['poly'][(edge+1) % len(a['poly'])]
        other = areas[j]['poly']
        # Clip the shared portal to the overlap of both edge segments. A whole
        # source edge may lead to several smaller areas.
        if target_edge >= len(other): continue
        r, s = other[target_edge], other[(target_edge+1) % len(other)]
        dx, dy = q[0]-p[0], q[1]-p[1]
        length2 = dx*dx+dy*dy
        if length2 < 1: continue
        project = lambda v: ((v[0]-p[0])*dx+(v[1]-p[1])*dy)/length2
        lo, hi = max(0, min(project(r), project(s))), min(1, max(project(r), project(s)))
        if hi-lo < .001: continue
        t = (lo+hi)*.5
        portal = [p[k]+t*(q[k]-p[k]) for k in range(3)]
        # Reject jumping gaps and large drops. Stairs remain connected because
        # this compares boundary heights, not the centres of sloping polygons.
        odx, ody = s[0]-r[0], s[1]-r[1]
        olen2 = odx*odx+ody*ody
        ot = max(0,min(1,((portal[0]-r[0])*odx+(portal[1]-r[1])*ody)/max(olen2,1)))
        landing = [r[k]+ot*(s[k]-r[k]) for k in range(3)]
        gap = ((landing[0]-portal[0])**2+(landing[1]-portal[1])**2)**.5
        step = landing[2]-portal[2]
        if gap > 12 or step > 18 or step < -48: continue
        connections.append(dict(from_=i, to=j, step=step, flags=4, cost=1, portal=portal[:2], portalZ=portal[2]))
for c in connections: c['from'] = c.pop('from_')
mesh = dict(format=1,name='de_dust2', client_version=version, nav_version=navversion,
            source='https://github.com/pnxenopoulos/awpy-data/releases/tag/'+version,
            nav_sha256=hashlib.sha256(raw).hexdigest(), areas=output_areas, connections=connections)
(assets / 'dust2.nav.json').write_text(json.dumps(mesh,separators=(',',':')),encoding='utf8')
offsets = json.loads((research/'offsets.json').read_text())
schema = json.loads((research/'client_dll.json').read_text())['client.dll']['classes']
def field(cls,name): return schema[cls]['fields'][name]
client = (GAME/'csgo/bin/win64/client.dll').read_bytes()
pe = struct.unpack_from('<I',client,0x3c)[0]
sections=[]
section_count=struct.unpack_from('<H',client,pe+6)[0]
optional_size=struct.unpack_from('<H',client,pe+20)[0]
for i in range(section_count):
    h=pe+24+optional_size+i*40
    size,rva,raw_size,raw_ptr=struct.unpack_from('<IIII',client,h+8)
    sections.append((raw_ptr,raw_ptr+raw_size,rva,client[h:h+8]))
def locate(pattern):
    results=[]
    for start,end,rva,name in sections:
        if name.startswith(b'.text'):
            results.extend(start+m.start() for m in re.finditer(pattern,client[start:end],re.DOTALL))
    assert len(results)==1, f'Expected one signature match, got {len(results)} for {pattern!r}'
    return results[0]
def file_rva(pos):
    for start,end,rva,_ in sections:
        if start<=pos<end: return rva+pos-start
    raise ValueError(pos)
def rip(pattern):
    pos=locate(pattern)
    return file_rva(pos)+7+struct.unpack_from('<i',client,pos+3)[0]
# Resolve global addresses from this exact installed binary; public dump
# values may lag one game update. Patterns are documented by a2x/cs2-dumper.
prediction=rip(rb'\x48\x8d\x05....\xc3\xcc{8}\x40\x53\x56\x41\x54')
pawn_member=struct.unpack_from('<I',client,locate(rb'\x4c\x39\xb6....\x74.\x44\x88\xbe')+3)[0]
input_rva=rip(rb'\x48\x89\x05....\x0f\x57\xc0\x0f\x11\x05')
angle_member=struct.unpack_from('<I',client,locate(rb'\xf2\x42\x0f\x10\x84\x28....')+6)[0]
sens_rva=rip(rb'\x48\x8d\x0d....\x0f\x57\xc9\x0f\x28\xf0')+8
entities_rva=rip(rb'\x48\x89\x0d....\xe9....\xcc')
controller_rva=rip(rb'\x48\x8b\x05....\x41\x89\xbe')
profile = dict(clientVersion=int(version), moduleTimestamp=struct.unpack_from('<I',client,pe+8)[0],
    moduleSize=struct.unpack_from('<I',client,pe+24+56)[0], clientSha256=hashlib.sha256(client).hexdigest(),
    source='https://github.com/a2x/cs2-dumper',
    pawn=prediction+pawn_member, angles=input_rva+angle_member,
    sensitivity=sens_rva, sensitivityValue=offsets['client.dll']['dwSensitivity_sensitivity'],
    health=field('C_BaseEntity','m_iHealth'), life=field('C_BaseEntity','m_lifeState'),
    position=field('C_BasePlayerPawn','m_vOldOrigin'), velocity=field('C_BaseEntity','m_vecAbsVelocity'),
    maxTurnDegrees=160, mouseYawDegrees=.022, map='de_dust2')
profile.update(entityList=entities_rva, localController=controller_rva,
    team=field('C_BaseEntity','m_iTeamNum'),flags=field('C_BaseEntity','m_fFlags'),
    enemyEyeAngles=field('C_CSPlayerPawn','m_angEyeAngles'),
    aimPunchServices=field('C_CSPlayerPawn','m_pAimPunchServices'),
    punchAngle=field('CCSPlayer_AimPunchServices','m_predictableBaseAngle'),shotsFired=field('C_CSPlayerPawn','m_iShotsFired'),
    controllerPawn=field('CCSPlayerController','m_hPlayerPawn'), pawnController=field('C_BasePlayerPawn','m_hController'),
    sceneNode=field('C_BaseEntity','m_pGameSceneNode'), sceneOrigin=field('CGameSceneNode','m_vecAbsOrigin'),
    dormant=field('CGameSceneNode','m_bDormant'), viewOffset=field('C_BaseModelEntity','m_vecViewOffset'),
    immunity=field('C_CSPlayerPawn','m_bGunGameImmunity'), scoped=field('C_CSPlayerPawn','m_bIsScoped'),
    weaponServices=field('C_BasePlayerPawn','m_pWeaponServices'), activeWeapon=field('CPlayer_WeaponServices','m_hActiveWeapon'),
    clip=field('C_BasePlayerWeapon','m_iClip1'),reloading=field('C_CSWeaponBase','m_bInReload'),
    itemId=field('C_EconEntity','m_AttributeManager')+field('C_AttributeContainer','m_Item')+field('C_EconItemView','m_iItemDefinitionIndex'),
    fovSensitivity=field('C_BasePlayerPawn','m_flFOVSensitivityAdjust'),
    spottedState=field('C_CSPlayerPawn','m_entitySpottedState'),spottedMask=field('EntitySpottedState_t','m_bSpottedByMask'))
(assets/'memory.json').write_text(json.dumps(profile,indent=2),encoding='utf8')
geometry_zip=research/'geometry.zip'
if geometry_zip.exists():
    contents=geometry_zip.read_bytes()
    assert hashlib.sha256(contents).hexdigest()==manifest['artifacts']['geometry.zip']['sha256']
    with zipfile.ZipFile(io.BytesIO(contents)) as z:
        member=next(n for n in z.namelist() if n.endswith('de_dust2.mesh'))
        (assets/'dust2.mesh').write_bytes(z.read(member))
print(f'CS2 {version}: nav v{navversion}, {len(areas)} areas, {len(connections)} walk connections')
print('Client PE:', profile['moduleTimestamp'], profile['moduleSize'])
