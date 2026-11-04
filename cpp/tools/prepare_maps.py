"""Prepare version-pinned navigation and visibility for supported maps; never edits memory profiles."""
import hashlib, io, json, struct, zipfile, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
version = sys.argv[1]
research = ROOT / 'research' / version
assets = ROOT / 'assets'
manifest = json.loads((research / 'manifest.json').read_text())
assert str(manifest['client_version']) == version
archives = {}
for name in ('navs.zip','geometry.zip'):
    data = (research/name).read_bytes()
    assert hashlib.sha256(data).hexdigest() == manifest['artifacts'][name]['sha256'], name
    archives[name] = zipfile.ZipFile(io.BytesIO(data))
for map_name in (sys.argv[2:] or ('de_dust2','de_mirage','de_vertigo','de_cache','de_inferno')):
    raw = archives['navs.zip'].read(next(n for n in archives['navs.zip'].namelist() if n.endswith(map_name+'.nav')))
    (assets/(map_name+'.nav')).write_bytes(raw)
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
    mesh = dict(format=1,name=map_name, client_version=version, nav_version=navversion,
                source='https://github.com/pnxenopoulos/awpy-data/releases/tag/'+version,
                nav_sha256=hashlib.sha256(raw).hexdigest(), areas=output_areas, connections=connections)
    (assets / (map_name.removeprefix('de_')+'.nav.json')).write_text(json.dumps(mesh,separators=(',',':')),encoding='utf8')
    geometry = archives['geometry.zip']
    member = next(n for n in geometry.namelist() if n.endswith(map_name+'.mesh'))
    (assets/(map_name.removeprefix('de_')+'.mesh')).write_bytes(geometry.read(member))
    print(f'{map_name}: version={version} areas={len(areas)} links={len(connections)}')
