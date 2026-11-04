"""Offline compatibility preparation. Never opens game processes or downloads code.
Only profiles pinned to the exact installed binaries can be staged for installation.
Unrecognized binaries yield partial candidates, not fabricated complete profiles.
"""
import argparse
import contextlib
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import tempfile
import uuid

FILES = ("memory.json", "input-buttons.json", "game-commands.json")
GLOBALS = ("pawn", "angles", "sensitivity", "entityList", "localController")
BUTTONS = ("forward", "back", "left", "right", "attack", "attack2", "reload",
           "duck", "turnleft", "turnright", "turnup", "turndown")

class Invalid(ValueError):
    pass

def require(ok, message):
    if not ok:
        raise Invalid(message)

def digest(data):
    return hashlib.sha256(data).hexdigest()

def read_json(path):
    raw, _ = stable_read(path, 1024 * 1024)
    return parse_json(raw, path)

def parse_json(raw, label="JSON"):
    require(len(raw) <= 1024 * 1024, f"JSON too large: {label}")
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, f"Duplicate JSON key: {key}")
            result[key] = value
        return result
    def invalid_constant(value):
        raise Invalid(f"Non-finite JSON constant: {value}")
    def finite_float(value):
        result = float(value)
        require(math.isfinite(result), "Non-finite JSON number")
        return result
    try:
        value = json.loads(raw.decode("utf-8-sig"), object_pairs_hook=pairs,
                           parse_constant=invalid_constant, parse_float=finite_float)
    except RecursionError as exc:
        raise Invalid("JSON nesting exceeds parser limit") from exc
    require(isinstance(value, dict), f"Expected JSON object: {label}")
    pending = [(value, 1)]
    while pending:
        item, depth = pending.pop()
        require(depth <= 64, "JSON nesting exceeds 64 levels")
        children = item.values() if isinstance(item, dict) else item if isinstance(item, list) else ()
        pending.extend((child, depth+1) for child in children if isinstance(child, (dict, list)))
    return value

def file_stamp(stat):
    # Windows path stat and handle fstat can disagree on ctime after atomic rename.
    # File identity, size and last-write time are consistent across both APIs.
    return (stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns)

def stable_read(path, limit):
    path = Path(path)
    with path.open("rb") as stream:
        before = file_stamp(os.fstat(stream.fileno()))
        require(before[2] <= limit, f"File too large: {path.name}")
        raw = stream.read(limit + 1)
        after = file_stamp(os.fstat(stream.fileno()))
    require(len(raw) <= limit and before == after == file_stamp(path.stat()),
            f"File changed during inspection: {path.name}; retry after updates finish")
    return raw, after

@contextlib.contextmanager
def transaction_guard(target):
    """OS-owned lock: shared by install/recover, automatically released on crash.

    Keep this file on disk: unlinking a guard can split concurrent lock owners.
    The existing .lock/journal remain the runtime's transaction markers.
    """
    target = Path(target).resolve()
    require(target.is_dir(), "Target assets directory missing")
    with (target/".profile-update.guard").open("a+b") as stream:
        stream.seek(0, os.SEEK_END)
        if not stream.tell():
            stream.write(b"0"); stream.flush()
        stream.seek(0)
        if os.name == "nt":
            import msvcrt
            try: msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError as exc: raise Invalid("Another install/recovery is active") from exc
            try: yield
            finally:
                stream.seek(0); msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            try: fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError as exc: raise Invalid("Another install/recovery is active") from exc
            try: yield
            finally: fcntl.flock(stream.fileno(), fcntl.LOCK_UN)

def integer(value, label, maximum=0xffffffff):
    require(type(value) is int and 0 < value <= maximum, f"Invalid {label}")
    return value

def profile_hashes(value):
    require(isinstance(value, dict) and set(value) == set(FILES),
            "Expected hashes for exactly three profiles")
    require(all(isinstance(v, str) and re.fullmatch(r"[0-9a-f]{64}", v)
                for v in value.values()), "Invalid profile SHA256")
    return value

def profile_digest(path):
    return digest(stable_read(path, 1024*1024)[0])

class PE:
    def __init__(self, data):
        self.data = data
        require(len(data) >= 64 and data[:2] == b"MZ", "Invalid DOS header")
        p = self.unpack("<I", 60)[0]
        require(p >= 64 and self.take(p, 4) == b"PE\0\0", "Invalid PE signature")
        machine, count, stamp = self.unpack("<HHI", p + 4)
        optional = self.unpack("<H", p + 20)[0]
        require(machine == 0x8664 and 0 < count <= 96 and optional >= 112, "Expected AMD64 PE")
        require(self.unpack("<H", p+24)[0] == 0x20b, "Expected PE32+")
        self.base = self.unpack("<Q", p+48)[0]
        self.size = self.unpack("<I", p+80)[0]
        require(self.size > 0, "Empty image")
        self.sections = []
        self.take(p+24, optional+count*40)
        for i in range(count):
            h = p+24+optional+i*40
            name = self.take(h, 8).rstrip(b"\0").decode("ascii")
            virtual, rva, size, raw = self.unpack("<IIII", h+8)
            require(rva <= self.size and max(virtual, size) <= self.size-rva, "Section outside image")
            self.take(raw, size)
            self.sections.append((name, rva, raw, size))
        self.identity = dict(timestamp=stamp, size=self.size, sha256=digest(data))

    def take(self, at, size):
        require(at >= 0 and size >= 0 and at <= len(self.data) and size <= len(self.data)-at,
                "Truncated PE range")
        return self.data[at:at+size]

    def unpack(self, fmt, at):
        return struct.unpack(fmt, self.take(at, struct.calcsize(fmt)))

    def matches(self, pattern, section, regex=False):
        found = []
        for name, rva, raw, size in self.sections:
            if name != section:
                continue
            data = self.take(raw, size)
            if regex:
                found.extend((raw+m.start(), rva+m.start()) for m in re.finditer(pattern, data, re.S))
            else:
                at = data.find(pattern)
                while at >= 0:
                    found.append((raw+at, rva+at))
                    at = data.find(pattern, at+1)
        return found

    def unique(self, pattern, section=".text", regex=True):
        found = self.matches(pattern, section, regex)
        require(len(found) == 1, f"Expected unique {section} match, found {len(found)}")
        return found[0]

    def rip(self, pattern):
        at, rva = self.unique(pattern)
        result = rva+7+self.unpack("<i", at+3)[0]
        require(0 < result < self.size, "RIP target outside image")
        return result

def global_candidates(pe):
    # Existing prepare_assets.py patterns, now isolated from maps and disk writes.
    prediction = pe.rip(rb"\x48\x8d\x05....\xc3\xcc{8}\x40\x53\x56\x41\x54")
    at, _ = pe.unique(rb"\x4c\x39\xb6....\x74.\x44\x88\xbe")
    pawn = prediction + pe.unpack("<I", at+3)[0]
    input_rva = pe.rip(rb"\x48\x89\x05....\x0f\x57\xc0\x0f\x11\x05")
    at, _ = pe.unique(rb"\xf2\x42\x0f\x10\x84\x28....")
    angles = input_rva + pe.unpack("<I", at+6)[0]
    result = dict(pawn=pawn, angles=angles,
                  sensitivity=pe.rip(rb"\x48\x8d\x0d....\x0f\x57\xc9\x0f\x28\xf0")+8,
                  entityList=pe.rip(rb"\x48\x89\x0d....\xe9....\xcc"),
                  localController=pe.rip(rb"\x48\x8b\x05....\x41\x89\xbe"))
    for key, value in result.items():
        integer(value, key, pe.size-8)
    return result

def command_candidates(pe, profile):
    prefix = profile.get("prefix")
    require(isinstance(prefix, str) and re.fullmatch(r"[0-9a-fA-F]{64}", prefix),
            "Expected exact 32-byte command prefix")
    _, function = pe.unique(bytes.fromhex(prefix), ".text", False)
    _, slot = pe.unique(struct.pack("<Q", pe.base+function), ".rdata", False)
    table = slot-51*8
    require(table > 0, "Vtable underflow")
    require(any(n==".rdata" and r<=table and slot+8<=r+s for n,r,_,s in pe.sections),
            "Vtable span outside .rdata")
    _, obj = pe.unique(struct.pack("<Q", pe.base+table), ".data", False)
    return dict(pe.identity, object=obj, vtable=table, function=function, prefix=prefix.lower())

def same_identity(profile, pe, client=False):
    keys = ("moduleTimestamp", "moduleSize", "clientSha256") if client else ("timestamp", "size", "sha256")
    return all(profile.get(key) == pe.identity[field]
               for key, field in zip(keys, ("timestamp", "size", "sha256")))

def approved(version, client, engine, assets, hashes=None):
    catalog=read_json(Path(__file__).with_name("approved-profiles.json"))
    bundles = catalog.get("bundles")
    require(type(catalog.get("format")) is int and catalog["format"] == 1 and isinstance(bundles, list)
            and all(isinstance(e, dict) for e in bundles), "Invalid approved profile catalog")
    if hashes is None:
        hashes={name:profile_digest(Path(assets)/name) for name in FILES}
    return any(e.get("version")==version and e.get("client_sha256")==client.identity["sha256"]
               and e.get("engine_sha256")==engine.identity["sha256"] and e.get("files")==hashes
               for e in bundles)

def dependency_changes(memory, buttons, commands, report):
    rows = []
    def add(component, field, old, new, available=True):
        rows.append(dict(component=component, field=field, previous=old,
                         observed=new if available else None,
                         status=("unchanged" if type(old) is type(new) and old == new else "changed")
                         if available else "unverified"))
    add("client", "steam_inf_client_version", memory.get("clientVersion"), report["version"])
    for component, profile, identity, keys in (
        ("client", memory, report["client"], ("moduleTimestamp", "moduleSize", "clientSha256")),
        ("buttons", buttons, report["client"], ("moduleTimestamp", "moduleSize", "clientSha256")),
        ("commands", commands, report["engine"], ("timestamp", "size", "sha256"))):
        for key, name in zip(keys, ("timestamp", "size", "sha256")):
            add(component, name, profile.get(key), identity[name])
    for group, profile, fields in (("globals", memory, GLOBALS),
                                  ("commands", commands, ("object", "vtable", "function"))):
        candidates = report["candidates"].get(group, {})
        for field in fields:
            add(group, field, profile.get(field), candidates.get(field), field in candidates)
    # A matching candidate is not validation of the current runtime contract.
    rows.append(dict(component="runtime", field="schema/buttons/command_semantics",
                     previous=None, observed=None, status="unverified"))
    return rows

def inspect(game, assets):
    game, assets = Path(game), Path(assets)
    stamps = {}
    def read(path, limit):
        raw, stamp = stable_read(path, limit)
        stamps[path] = stamp
        return raw
    inf = read(game/"game/csgo/steam.inf", 65535)
    versions = re.findall(r"^ClientVersion=(\d+)\s*$", inf.decode("utf-8-sig"), re.M)
    require(len(versions)==1, "Missing or ambiguous ClientVersion")
    version = integer(int(versions[0]), "ClientVersion", 2147483647)
    client = PE(read(game/"game/csgo/bin/win64/client.dll", 512*1024*1024))
    engine = PE(read(game/"game/bin/win64/engine2.dll", 512*1024*1024))
    profile_bytes = {name: read(assets/name, 1024*1024) for name in FILES}
    profiles = {name: parse_json(raw, name) for name, raw in profile_bytes.items()}
    report = dict(format=1, version=version, client=client.identity, engine=engine.identity,
                  eligible=False, blockers=[], candidates={}, checks={}, live_verified=False)
    memory, buttons, commands = (profiles[n] for n in FILES)
    report["profile_hashes"] = {name: digest(raw) for name, raw in profile_bytes.items()}
    if not approved(version, client, engine, assets, report["profile_hashes"]):
        report["blockers"].append("No approved exact-file bundle in local catalog; candidate generation is not approval.")
    for label, action in (("globals", lambda: global_candidates(client)),
                          ("commands", lambda: command_candidates(engine, commands))):
        try:
            report["candidates"][label] = action()
            report["checks"][label] = "unique offline matches"
        except (ValueError, struct.error) as exc:
            report["blockers"].append(f"{label}: {exc}")
    pinned = memory.get("clientVersion")==version and same_identity(memory, client, True)
    if not pinned:
        report["blockers"].append("Current schema/field semantics require validation for this exact client SHA256; old offsets will not be inherited.")
    if report["candidates"].get("globals") != {key: memory.get(key) for key in GLOBALS}:
        report["blockers"].append("Global RVAs differ from the confirmed source profile.")
    if not same_identity(buttons, client, True):
        report["blockers"].append("Button object identities and names require validation for this exact client SHA256.")
    try:
        entries = buttons.get("buttons", {})
        require(isinstance(entries, dict), "Expected buttons object")
        for name in BUTTONS:
            entry = entries.get(name, {})
            require(isinstance(entry, dict), f"Expected button object: {name}")
            integer(entry.get("rva"), f"button {name}", client.size-128)
    except Invalid as exc:
        report["blockers"].append(str(exc))
    candidate = report["candidates"].get("commands")
    if not candidate or not same_identity(commands, engine) or any(
            candidate.get(key) != commands.get(key) for key in ("object", "vtable", "function")):
        report["blockers"].append("Command chain differs: confirm object/vtable/slot51/prefix and runtime contract; no command execution performed.")
    report["eligible"] = not report["blockers"]
    report["status"] = "confirmed-profile-reuse" if report["eligible"] else "validation-required"
    report["version_identifiers"] = {"steam_inf_client_version": version,
                                     "dumper_build_number": None,
                                     "note": "Different namespaces; never compare or substitute these numbers."}
    report["dependency_changes"] = dependency_changes(memory, buttons, commands, report)
    report["limits"] = [
        "Offline matching is not a live gameplay test.",
        "Maps are not rewritten or certified for a different game version.",
        "No network downloads, game commands, process memory access or protection bypass.",
        "New game builds need new schema/button/runtime evidence before a complete profile can be installed."]
    for path, stamp in stamps.items():
        require(file_stamp(path.stat()) == stamp,
                f"File changed during inspection: {path.name}; retry after updates finish")
    return report, profiles

def write_json(path, obj):
    path = Path(path)
    tmp = path.with_name(path.name+".write-"+uuid.uuid4().hex)
    try:
        with tmp.open("w", encoding="utf-8") as stream:
            stream.write(json.dumps(obj, ensure_ascii=False, indent=2, allow_nan=False)+"\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(tmp, path)
    finally:
        if tmp.exists(): tmp.unlink()

def restore_profiles(backup, target, hashes):
    # Validate the entire backup before modifying any installed file.
    profile_hashes(hashes)
    for name in FILES:
        require(profile_digest(backup/name) == hashes[name], "Recovery backup changed")
    for name in FILES:
        tmp = target/(name+".recover-"+uuid.uuid4().hex)
        try:
            shutil.copyfile(backup/name, tmp)
            require(profile_digest(tmp) == hashes[name], "Recovery copy changed")
            os.replace(tmp, target/name)
        finally:
            if tmp.exists(): tmp.unlink()

def check_enumeration_end(error):
    require(error == 18, f"Process enumeration failed (Windows error {error})")

def runtime_changes(profiles, generated):
    rows = []
    def visit(old, new, path):
        if isinstance(old, dict) and isinstance(new, dict):
            for key in sorted(old.keys() | new.keys()):
                if key not in old or key not in new:
                    rows.append(dict(field=path+"."+key, previous=old.get(key), observed=new.get(key),
                                     status="added" if key in new else "removed"))
                else:
                    visit(old[key], new[key], path+"."+key)
        elif type(old) is not type(new) or old != new:
            rows.append(dict(field=path, previous=old, observed=new, status="changed"))
    for name in FILES:
        visit(profiles[name], generated[name], name)
    return rows

@contextlib.contextmanager
def runtime_attempt(output, offline, game, assets, pid, created):
    # Written before touching the process: a killed collector still leaves its
    # offline findings. Never name this report.json (an installable manifest).
    output.mkdir(parents=True)
    progress = dict(offline, eligible=False, status="runtime-collection-in-progress",
                    game=str(Path(game).resolve()), source_assets=str(Path(assets).resolve()),
                    requested_process=dict(pid=pid, created=created))
    write_json(output/"collection-progress.json", progress)
    try:
        yield
    except (OSError, ValueError, KeyError, struct.error) as error:
        failure = dict(progress, status="runtime-collection-failed", reason=str(error))
        write_json(output/"failure-report.json", failure)
        raise
    else:
        write_json(output/"collection-progress.json", dict(progress, status="runtime-collection-complete"))

def prepare(game, assets, output, runtime_pid=None, runtime_created=None):
    output = Path(output).resolve()
    require(not output.exists(), "Output must be a NEW directory")
    # Failure to parse any input cannot leave an eligible package.
    report, profiles = inspect(game, assets)
    evidence = None
    require(runtime_created is None or runtime_pid is not None, "Creation time requires runtime PID")
    require(runtime_created is None or (type(runtime_created) is int and 0 < runtime_created < 2**64),
            "Invalid process creation time")
    if runtime_pid is not None:
        require(type(runtime_pid) is int and 0 < runtime_pid <= 0xffffffff, "Invalid runtime PID")
        with runtime_attempt(output, report, game, assets, runtime_pid, runtime_created):
            from collect_runtime import collect
            from schema_profile import candidates
            evidence = collect(runtime_pid, assets, runtime_created)
            for name, key in (("client.dll", "client"), ("engine2.dll", "engine")):
                module = evidence["modules"][name]
                require(all(module[field] == report[key][field] for field in ("sha256", "timestamp", "size")),
                        "Runtime DLL differs from selected game installation")
            require(evidence.get("source_profile_hashes") == report.get("profile_hashes")
                    and isinstance(evidence.get("source_profile_hashes"), dict),
                    "Source profiles changed between offline inspection and runtime collection; retry")
            generated = candidates(evidence, profiles[FILES[0]], profiles[FILES[2]], report["version"])
            report["candidate_changes"] = runtime_changes(profiles, generated)
            if report["eligible"] and report["candidate_changes"]:
                report["eligible"] = False
                report["status"] = "runtime-profile-mismatch"
                report["blockers"].append("Runtime evidence differs from the approved profiles; installation refused.")
            report["runtime_collection"] = dict(pid=runtime_pid, created=evidence["created"],
                schema_fields=33, buttons=len(evidence["buttons"]["buttons"]),
                status="structural-evidence-collected", gameplay_verified=False)
            report["limits"] = [line for line in report["limits"] if "process memory access" not in line]
            report["limits"].append("Explicit PID: read-only runtime collection; no game input or commands executed.")
    if runtime_pid is None:
        output.mkdir(parents=True)
    report["game"] = str(Path(game).resolve())
    report["source_assets"] = str(Path(assets).resolve())
    if evidence is not None:
        write_json(output/"runtime-evidence.json", evidence)
        target = output/"candidate-profiles"
        target.mkdir()
        for name, profile in generated.items():
            write_json(target/name, profile)
        report["candidate_hashes"] = {name: profile_digest(target/name) for name in FILES}
    if report["eligible"]:
        target = output/"profiles"
        target.mkdir()
        for name in FILES:
            shutil.copyfile(Path(assets)/name, target/name)
            require(profile_digest(target/name)==report["profile_hashes"][name], "Source changed while staging")
        report["originals_reused"] = True
    write_json(output/"report.json", report)
    return report

def require_idle():
    """Do not replace profiles while any game/panel worker could load them."""
    require(os.name == "nt", "Installation is supported only on Windows")
    import ctypes as c
    from ctypes import wintypes as w
    class Entry(c.Structure):
        _fields_=[("size",w.DWORD),("usage",w.DWORD),("pid",w.DWORD),("heap",c.c_size_t),
                  ("module",w.DWORD),("threads",w.DWORD),("parent",w.DWORD),
                  ("priority",w.LONG),("flags",w.DWORD),("exe",w.WCHAR*260)]
    k=c.WinDLL("kernel32",use_last_error=True)
    k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
    k.Process32FirstW.argtypes=[w.HANDLE,c.POINTER(Entry)];k.Process32NextW.argtypes=k.Process32FirstW.argtypes
    k.CloseHandle.argtypes=[w.HANDLE]
    h=k.CreateToolhelp32Snapshot(2,0)
    require(h not in (None,c.c_void_p(-1).value), "Cannot enumerate processes")
    try:
        e=Entry();e.size=c.sizeof(e);ok=k.Process32FirstW(h,c.byref(e))
        while ok:
            require(e.exe.lower() not in {"cs2.exe","cs2panel.exe","cs2n_background.exe","cs2n_app.exe"},
                    f"Close {e.exe} before profile installation")
            ok=k.Process32NextW(h,c.byref(e))
        check_enumeration_end(c.get_last_error())
    finally:
        k.CloseHandle(h)

def install(package, target, idle=require_idle):
    with transaction_guard(target):
        return _install(package, target, idle)

def _install(package, target, idle):
    package, target = Path(package).resolve(), Path(target).resolve()
    require(target.is_dir(), "Target assets directory missing")
    report = read_json(package/"report.json")
    require(type(report.get("format")) is int and report["format"] == 1,
            "Unsupported package report format")
    require(report.get("eligible") is True, "Package requires validation")
    profile_hashes(report.get("profile_hashes"))
    require(isinstance(report.get("game"), str) and report["game"].strip()
            and isinstance(report.get("client"), dict) and isinstance(report.get("engine"), dict),
            "Incomplete package report identity")
    for name in FILES:
        require(profile_digest(package/"profiles"/name) == report["profile_hashes"][name],
                f"Package modified: {name}")
    fresh, _ = inspect(report["game"], package/"profiles")
    require(fresh["eligible"] and fresh["client"]==report["client"] and fresh["engine"]==report["engine"],
            "Game changed or package no longer matches")
    require(target != (package/"profiles").resolve(), "Cannot install over staged source")
    # Cooperative transaction lock; runtime compatibility checks still remain mandatory.
    lock = target/".profile-update.lock"
    fd = os.open(lock, os.O_CREAT|os.O_EXCL|os.O_WRONLY)
    backup = None
    try:
        os.write(fd, str(os.getpid()).encode("ascii"))
        idle()
        require(not (target/".profile-update-pending.json").exists(),
                "An interrupted transaction needs recovery before another installation")
        if all((target/name).exists() and profile_digest(target/name)==report["profile_hashes"][name] for name in FILES):
            return dict(status="already-current", backup=None)
        backup = target/("profile-backup-"+uuid.uuid4().hex)
        backup.mkdir()
        originals = {}
        for name in FILES:
            require((target/name).is_file(), f"Missing installed profile: {name}")
            originals[name] = profile_digest(target/name)
            shutil.copyfile(target/name, backup/name)
            require(profile_digest(backup/name) == originals[name], f"Backup copy changed: {name}")
        journal = target/".profile-update-pending.json"
        require(all(profile_digest(target/name) == originals[name] for name in FILES),
                "Installed profiles changed while creating backup")
        write_json(journal, {"backup": backup.name, "files": originals})
        try:
            for name in FILES:
                tmp=target/(name+".update-"+uuid.uuid4().hex)
                try:
                    shutil.copyfile(package/"profiles"/name, tmp)
                    require(profile_digest(tmp) == report["profile_hashes"][name],
                            f"Package changed while installing: {name}")
                    os.replace(tmp, target/name)
                finally:
                    if tmp.exists(): tmp.unlink()
            for name in FILES:
                require(profile_digest(target/name)==report["profile_hashes"][name],
                        f"Installed hash mismatch: {name}")
            after, _=inspect(report["game"], package/"profiles")
            require(after["eligible"] and after["client"]==report["client"] and after["engine"]==report["engine"],
                    "Game changed during installation; restoring previous profiles")
            journal.unlink()
        except BaseException:
            restore_profiles(backup, target, originals)
            journal.unlink()
            raise
        return dict(status="installed", backup=str(backup))
    finally:
        os.close(fd)
        lock.unlink()

def process_alive(pid):
    if os.name == "nt":
        import ctypes as c
        from ctypes import wintypes as w
        k=c.WinDLL("kernel32",use_last_error=True)
        k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
        k.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD]
        k.CloseHandle.argtypes=[w.HANDLE]
        h=k.OpenProcess(0x100000,False,pid)
        if not h:
            return c.get_last_error()!=87  # unknown/access denied is not permission to recover
        try:return k.WaitForSingleObject(h,0)!=0
        finally:k.CloseHandle(h)
    try:os.kill(pid,0);return True
    except ProcessLookupError:return False

def recover(target, idle=require_idle, alive=process_alive):
    with transaction_guard(target):
        return _recover(target, idle, alive)

def _recover(target, idle, alive):
    target=Path(target).resolve()
    require(target.is_dir(), "Target assets directory missing")
    idle()
    lock=target/".profile-update.lock"
    if lock.exists():
        text=stable_read(lock, 32)[0].decode("ascii").strip()
        require(text.isdigit() and 0<int(text)<=0xffffffff, "Invalid lock owner; inspect manually")
        require(not alive(int(text)), "Updater is still running; recovery refused")
    journal=target/".profile-update-pending.json"
    if journal.exists():
        data=read_json(journal)
        name=data.get("backup","")
        require(isinstance(name,str) and re.fullmatch(r"profile-backup-[0-9a-f]{32}",name),
                "Invalid recovery backup name")
        backup=(target/name).resolve()
        require(backup.parent==target, "Backup escapes target")
        profile_hashes(data.get("files"))
        restore_profiles(backup, target, data["files"])
        journal.unlink()
    if lock.exists():lock.unlink()
    return {"status":"recovered","target":str(target)}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest="action",required=True)
    p=sub.add_parser("prepare");p.add_argument("--game",required=True);p.add_argument("--assets",required=True);p.add_argument("--output",required=True)
    p.add_argument("--runtime-pid",type=int,help="Explicit CS2 PID for read-only schema/button collection")
    p.add_argument("--runtime-created",type=int,help="Expected process creation FILETIME; requires loaded player")
    p=sub.add_parser("install");p.add_argument("--package",required=True);p.add_argument("--target",required=True)
    p=sub.add_parser("recover");p.add_argument("--target",required=True)
    args=parser.parse_args()
    try:
        if args.action=="prepare":
            result=prepare(args.game,args.assets,args.output,args.runtime_pid,args.runtime_created)
            print(json.dumps(result,ensure_ascii=False,indent=2))
            return 0 if result["eligible"] else 2
        if args.action=="recover":
            print(json.dumps(recover(args.target),ensure_ascii=False,indent=2));return 0
        print(json.dumps(install(args.package,args.target),ensure_ascii=False,indent=2));return 0
    except (OSError,ValueError,KeyError,struct.error) as exc:
        print(json.dumps({"status":"error","reason":str(exc)},ensure_ascii=False));return 1
if __name__=="__main__":
    raise SystemExit(main())
