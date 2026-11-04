"""Explicit-PID, read-only collection. Evidence is never automatic approval."""
import argparse
import ctypes as c
import json
import math
from pathlib import Path
import re
import struct
import time
from update import PE, require, stable_read, digest, command_candidates, global_candidates, parse_json, FILES


class ProfileSnapshot:
    def __init__(self, assets):
        self.assets=Path(assets);self.profiles={};self.hashes={};self.stamps={}
        for name in FILES:
            raw, stamp=stable_read(self.assets/name,1024*1024)
            self.profiles[name]=parse_json(raw,name)
            self.hashes[name]=digest(raw);self.stamps[name]=stamp

    def verify(self):
        for name in FILES:
            raw, stamp=stable_read(self.assets/name,1024*1024)
            require(stamp==self.stamps[name] and digest(raw)==self.hashes[name],
                    f'Source profile changed during runtime collection: {name}; retry')


class Process:
    def __init__(self, pid):
        self.k = c.WinDLL('kernel32', use_last_error=True)
        k = self.k
        k.OpenProcess.argtypes = [c.c_ulong, c.c_int, c.c_ulong]
        k.OpenProcess.restype = c.c_void_p
        k.CloseHandle.argtypes = [c.c_void_p]
        k.ReadProcessMemory.argtypes = [c.c_void_p, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
        k.GetProcessTimes.argtypes = [c.c_void_p]+[c.c_void_p]*4
        k.K32EnumProcessModules.argtypes = [c.c_void_p, c.c_void_p, c.c_ulong, c.POINTER(c.c_ulong)]
        k.K32GetModuleFileNameExW.argtypes = [c.c_void_p, c.c_void_p, c.c_wchar_p, c.c_ulong]
        k.GetExitCodeProcess.argtypes = [c.c_void_p, c.POINTER(c.c_ulong)]
        self.handle = k.OpenProcess(0x1010, False, pid)
        require(self.handle, f'Cannot open PID {pid} for reading: {c.get_last_error()}')
        try:
            self.created = self.creation()
            modules = (c.c_void_p*2048)(); needed = c.c_ulong()
            require(k.K32EnumProcessModules(self.handle, modules, c.sizeof(modules), c.byref(needed)), 'Cannot enumerate modules')
            require(needed.value <= c.sizeof(modules), 'Module list overflow')
            self.modules = {}
            for base in modules[:needed.value//c.sizeof(c.c_void_p)]:
                name = c.create_unicode_buffer(32768)
                require(k.K32GetModuleFileNameExW(self.handle, base, name, len(name)), 'Cannot resolve module path')
                key = Path(name.value).name.lower()
                if key not in ('cs2.exe', 'client.dll', 'engine2.dll', 'schemasystem.dll'):
                    continue
                require(key not in self.modules, 'Duplicate module name')
                self.modules[key] = (base, Path(name.value))
            require('cs2.exe' in self.modules, 'Target is not CS2')
        except BaseException:
            self.close()
            raise

    def creation(self):
        values = [c.c_ulonglong() for _ in range(4)]
        require(self.k.GetProcessTimes(self.handle, *[c.byref(v) for v in values]), 'Cannot identify process creation')
        return values[0].value

    def alive(self):
        code = c.c_ulong()
        require(self.k.GetExitCodeProcess(self.handle, c.byref(code)) and code.value == 259, 'CS2 exited')
        require(self.creation() == self.created, 'Process identity changed')

    def read(self, address, size):
        require(0x10000 <= address < 0x800000000000 and 0 < size <= 64*1024*1024, 'Invalid read range')
        data = c.create_string_buffer(size); done = c.c_size_t()
        require(self.k.ReadProcessMemory(self.handle, address, data, size, c.byref(done)) and done.value == size,
                f'Cannot read {address:#x}, size={size}')
        return data.raw

    def q(self, address):
        return struct.unpack('<Q', self.read(address, 8))[0]

    def string(self, address):
        raw = self.read(address, 128)
        require(b'\0' in raw, 'Unterminated schema string')
        return raw.split(b'\0', 1)[0].decode('utf-8')

    def close(self):
        if self.handle:
            self.k.CloseHandle(self.handle); self.handle = None


def schema(process, base, pe):
    at, rva = pe.unique(rb'\x4c\x8d\x35....\x0f\x28\x45')
    system = base+rva+7+pe.unpack('<i', at+3)[0]
    count = struct.unpack('<i', process.read(system+0x190, 4))[0]
    require(0 < count < 128, 'Unsupported schema scope count/layout')
    scopes = process.q(system+0x198)
    classes = {}; seen = set(); matched = 0
    for i in range(count):
        scope = process.q(scopes+i*8)
        if process.string(scope+8) != 'client.dll':
            continue
        matched += 1
        require(matched == 1, 'Ambiguous client schema')
        for bucket in range(256):
            for delta in (8, 16):
                node = process.q(scope+0x560+0x60+bucket*24+delta)
                chain = set()
                while node:
                    require(node not in chain and len(chain) < 10000, 'Schema chain cycle/overflow')
                    if node in seen: break
                    chain.add(node); seen.add(node)
                    require(len(seen) < 20000, 'Schema node count overflow')
                    binding = process.q(node+16); node = process.q(node+8)
                    if not binding: continue
                    raw = process.read(binding, 0x70)
                    name = process.string(struct.unpack_from('<Q', raw, 8)[0])
                    size = struct.unpack_from('<i', raw, 0x20)[0]
                    n = struct.unpack_from('<h', raw, 0x24)[0]
                    require(0 <= n < 2048 and 0 < size < 1048576, 'Unsupported schema class layout')
                    fields = struct.unpack_from('<Q', raw, 0x30)[0]; result = {}
                    for j in range(n):
                        field = process.read(fields+j*32, 32)
                        fname = process.string(struct.unpack_from('<Q', field)[0])
                        offset = struct.unpack_from('<i', field, 16)[0]
                        require(0 <= offset < size and fname not in result, 'Invalid/duplicate schema field')
                        result[fname] = offset
                    entry = dict(size=size, fields=result)
                    require(name not in classes or classes[name] == entry, 'Conflicting schema class')
                    classes[name] = entry
    require(matched == 1 and len(classes) > 100, 'Incomplete client schema')
    return classes


def buttons(process, base, pe):
    rva = pe.rip(rb'\x48\x8b\x15....\x48\x85\xd2\x74.\x48\x8b\x02\x48\x85\xc0')
    pointer = process.q(base+rva); seen = set(); result = {}
    while pointer:
        require(pointer not in seen and len(seen) < 128, 'Button list cycle/overflow')
        seen.add(pointer)
        raw = process.read(pointer, 0x90)
        name = process.string(struct.unpack_from('<Q', raw, 8)[0])
        require(re.fullmatch(r'[a-zA-Z0-9_+-]+', name) and name not in result, 'Invalid/duplicate button name')
        require(0 <= pointer-base < pe.size-0x90, 'Button outside client module')
        result[name] = dict(rva=pointer+0x30-base)
        pointer = struct.unpack_from('<Q', raw, 0x88)[0]
    # The two vertical turn objects are not in the linked list. Find their exact
    # name pointers in .data and confirm the same layout used by MemoryInput.
    for name in ('turnup', 'turndown'):
        _, string_rva = pe.unique(name.encode()+b'\0', '.rdata', False)
        matches = []
        for index, (section, start, _, size) in enumerate(pe.sections):
            if section != '.data': continue
            nt = pe.unpack('<I', 0x3c)[0]
            section_header = nt+24+pe.unpack('<H', nt+20)[0]+index*40
            size = max(size, pe.unpack('<I', section_header+8)[0])
            raw = process.read(base+start, size)
            needle = struct.pack('<Q', base+string_rva)
            for at in range(0, len(raw)-8+1, 8):
                if raw[at:at+8] == needle:
                    matches.append(start+at+0x28)
        require(len(matches) == 1, f'Ambiguous {name} object')
        require(process.string(process.q(base+matches[0]-0x28)) == name, 'Turn object name mismatch')
        result[name] = dict(rva=matches[0])
    return dict(listRva=rva, buttons=result)


def phase(name, action):
    try:
        return action()
    except (OSError, ValueError, KeyError, struct.error) as error:
        raise ValueError(f'{name}: {error}') from error

def validate_player(snapshot):
    # Same plausibility limits as Cs2MemoryProvider::poll. Zero health is a
    # legitimate dead player, not missing data or a compatibility failure.
    require(type(snapshot['health']) is int and 0 <= snapshot['health'] <= 500, 'Player health failed validation')
    require(type(snapshot['life']) is int and 0 <= snapshot['life'] <= 3, 'Player life state failed validation')
    for name, limit in (('position',32768), ('velocity',10000), ('angles',361)):
        vector=snapshot[name]
        require(isinstance(vector,(list,tuple)) and len(vector)==3 and
                all(type(v) in (int,float) and math.isfinite(v) and abs(v)<=limit for v in vector),
                f'Player {name} failed validation')
    require(abs(snapshot['angles'][0])<=90.1, 'Player pitch failed validation')
    value=snapshot['sensitivity']
    require(type(value) in (int,float) and math.isfinite(value) and .01<=value<=100,
            'Sensitivity failed validation; value offset needs review')
    return dict(snapshot,available=True,validation='plausibility-only')

def player_snapshot(process, base, globals_, fields, sensitivity_offset):
    require(type(sensitivity_offset) is int and 0 < sensitivity_offset < 0x10000, 'Invalid sensitivity value offset')
    pawn=process.q(base+globals_['pawn'])
    require(pawn != 0, 'Откройте локальный матч с ботами и повторите сбор: игрок пока недоступен.')
    sensitivity=process.q(base+globals_['sensitivity'])
    values=dict(
        health=struct.unpack('<i',process.read(pawn+fields['health'],4))[0],
        life=process.read(pawn+fields['life'],1)[0],
        position=struct.unpack('<fff',process.read(pawn+fields['position'],12)),
        velocity=struct.unpack('<fff',process.read(pawn+fields['velocity'],12)),
        angles=struct.unpack('<fff',process.read(base+globals_['angles'],12)),
        sensitivity=struct.unpack('<f',process.read(sensitivity+sensitivity_offset,4))[0])
    require(process.q(base+globals_['pawn'])==pawn, 'Player changed during capture; retry')
    require(process.q(base+globals_['sensitivity'])==sensitivity, 'Sensitivity object changed during capture; retry')
    return validate_player(values)

def collect(pid, assets, expected_created=None):
    process = Process(pid)
    try:
        if expected_created is not None:
            require(process.created == expected_created, 'CS2 restarted; select the environment again')
        source=ProfileSnapshot(assets)
        modules = {}; binaries = {}; stamps = {}
        for name in ('client.dll', 'engine2.dll', 'schemasystem.dll'):
            base, path = process.modules[name]
            raw, stamp = stable_read(path, 512*1024*1024); pe = PE(raw)
            header = bytearray(process.read(base, 1024))
            base_field = struct.unpack_from('<I', raw, 0x3c)[0]+48
            require(base_field+8 <= len(header), 'PE header too large for identity check')
            require(struct.unpack_from('<Q', header, base_field)[0] in (base, pe.base), 'Unexpected mapped image base')
            header[base_field:base_field+8] = raw[base_field:base_field+8]
            require(header == raw[:1024], f'Loaded {name} header differs from disk')
            modules[name] = dict(base=base, path=str(path), **pe.identity)
            binaries[name] = pe; stamps[path] = (stamp, digest(raw))
        client = binaries['client.dll']; engine = binaries['engine2.dll']
        base = modules['client.dll']['base']; engine_base = modules['engine2.dll']['base']
        commands = command_candidates(engine, source.profiles['game-commands.json'])
        require(process.q(engine_base+commands['object']) == engine_base+commands['vtable'], 'Live command object mismatch')
        require(process.q(engine_base+commands['vtable']+51*8) == engine_base+commands['function'], 'Live command vtable mismatch')
        require(process.read(engine_base+commands['function'], 32).hex() == commands['prefix'], 'Live command prefix mismatch')
        result = dict(format=1, pid=pid, created=process.created, received_unix=time.time(),
                      source_profile_hashes=source.hashes,
                      modules=modules, globals=phase('globals', lambda: global_candidates(client)), commands=commands,
                      schema=phase('schema', lambda: schema(process, modules['schemasystem.dll']['base'], binaries['schemasystem.dll'])),
                      buttons=phase('buttons', lambda: buttons(process, base, client)), live_verified=False,
                      limits=['Read-only structural evidence; input/command behavior not tested; not approved for installation.'])
        if expected_created is not None:
            from schema_profile import offsets
            fields = offsets(result['schema'])
            sensitivity_offset=source.profiles['memory.json'].get('sensitivityValue')
            snapshots=[]
            for index in range(2):
                if index: time.sleep(.05)
                snapshots.append(phase('player snapshot',lambda: player_snapshot(process,base,result['globals'],fields,sensitivity_offset)))
            result['player_snapshot']=snapshots[-1]
            result['player_samples']=snapshots
        process.alive()
        source.verify()
        for path, (stamp, sha) in stamps.items():
            raw, after = stable_read(path, 512*1024*1024)
            require(after == stamp and digest(raw) == sha, 'Module file changed during capture')
        return result
    finally:
        process.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = collect(args.pid, args.assets)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # A failed capture never replaces existing evidence.
    with args.output.open('x', encoding='utf-8') as out:
        json.dump(result, out, indent=2)
    print(f"Captured {len(result['schema'])} classes and {len(result['buttons']['buttons'])} buttons; read-only evidence saved.")
