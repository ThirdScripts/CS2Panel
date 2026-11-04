import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
import json
import subprocess
import sys
spec=importlib.util.spec_from_file_location("update",Path(__file__).with_name("update.py"))
u=importlib.util.module_from_spec(spec);spec.loader.exec_module(u)

def pe_bytes():
    data=bytearray(2048);data[:2]=b"MZ";struct.pack_into("<I",data,60,128)
    data[128:132]=b"PE\0\0";struct.pack_into("<HHI",data,132,0x8664,1,123)
    struct.pack_into("<H",data,148,240);struct.pack_into("<H",data,152,0x20b)
    struct.pack_into("<Q",data,176,0x180000000);struct.pack_into("<I",data,208,0x3000)
    h=392;data[h:h+8]=b".text\0\0\0";struct.pack_into("<IIII",data,h+8,512,0x1000,512,512)
    data[520:524]=b"TEST"
    return bytes(data)

class Tests(unittest.TestCase):
    def test_pe_metadata(self):
        p=u.PE(pe_bytes());self.assertEqual(p.identity["timestamp"],123)
        self.assertEqual(p.unique(b"TEST",regex=False),(520,0x1008))
    def test_pe_truncated(self):
        for size in (0,63,129,399):
            with self.assertRaises(u.Invalid):u.PE(pe_bytes()[:size])
    def test_pe_wrong_architecture(self):
        b=bytearray(pe_bytes());struct.pack_into("<H",b,132,0x14c)
        with self.assertRaises(u.Invalid):u.PE(b)
    def test_pe_section_overflow(self):
        b=bytearray(pe_bytes());struct.pack_into("<I",b,408,0xffffffff)
        with self.assertRaises(u.Invalid):u.PE(b)
    def test_ambiguous_signature(self):
        b=bytearray(pe_bytes());b[540:544]=b"TEST"
        with self.assertRaises(u.Invalid):u.PE(b).unique(b"TEST",regex=False)
    def test_absent_signature(self):
        with self.assertRaises(u.Invalid):u.PE(pe_bytes()).unique(b"absent",regex=False)
    def test_empty_command_prefix(self):
        with self.assertRaises(u.Invalid):u.command_candidates(u.PE(pe_bytes()),{"prefix":""})
    def test_integer_rejects_bool(self):
        for value in (True,0,-1,1.5,"20"):
            with self.assertRaises(u.Invalid):u.integer(value,"test")
    def test_duplicate_json(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/"x.json";p.write_text('{"a":1,"a":2}')
            with self.assertRaises(u.Invalid):u.read_json(p)
    def test_catalog_rejects_modified_profiles(self):
        with tempfile.TemporaryDirectory() as td:
            for n in u.FILES:(Path(td)/n).write_text("{}")
            self.assertFalse(u.approved(2000935,u.PE(pe_bytes()),u.PE(pe_bytes()),td))
    def fixture(self,td):
        package=Path(td)/"package";target=Path(td)/"target"
        (package/"profiles").mkdir(parents=True);target.mkdir()
        hashes={}
        for n in u.FILES:
            (package/"profiles"/n).write_text('{"new":1}')
            (target/n).write_text('{"old":1}')
            hashes[n]=u.digest((package/"profiles"/n).read_bytes())
        report={"format":1,"eligible":True,"game":"fake","client":{"sha256":"client"},"engine":{"sha256":"engine"},"profile_hashes":hashes}
        u.write_json(package/"report.json",report)
        return package,target,report
    def test_install_backup_and_all_files(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td)
            with mock.patch.object(u,"inspect",return_value=(r,{})):
                result=u.install(p,t,idle=lambda:None)
            self.assertEqual(result["status"],"installed")
            for n in u.FILES:
                self.assertEqual((t/n).read_text(),'{"new":1}')
                self.assertEqual((Path(result["backup"])/n).read_text(),'{"old":1}')
    def test_install_modified_package_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td);(p/"profiles"/u.FILES[0]).write_text("{}")
            with self.assertRaises(u.Invalid):u.install(p,t,idle=lambda:None)
            self.assertEqual((t/u.FILES[0]).read_text(),'{"old":1}')
    def test_install_blocked_package(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td);r["eligible"]=False;u.write_json(p/"report.json",r)
            with self.assertRaises(u.Invalid):u.install(p,t,idle=lambda:None)
    def test_install_busy_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td)
            with mock.patch.object(u,"inspect",return_value=(r,{})):
                with self.assertRaises(u.Invalid):u.install(p,t,idle=lambda:u.require(False,"busy"))
            self.assertFalse((t/".profile-update.lock").exists())
    def test_transaction_lock(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td);(t/".profile-update.lock").write_text("busy")
            with mock.patch.object(u,"inspect",return_value=(r,{})):
                with self.assertRaises(FileExistsError):u.install(p,t,idle=lambda:None)
    def test_partial_write_rolls_back(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td);replace=u.os.replace;count=0
            def fail_second(a,b):
                nonlocal count
                if Path(b).name in u.FILES:
                    count+=1
                    if count==2:raise OSError("simulated disk failure")
                replace(a,b)
            with mock.patch.object(u,"inspect",return_value=(r,{})),mock.patch.object(u.os,"replace",side_effect=fail_second):
                with self.assertRaises(OSError):u.install(p,t,idle=lambda:None)
            for n in u.FILES:self.assertEqual((t/n).read_text(),'{"old":1}')
            self.assertFalse((t/".profile-update-pending.json").exists())
    def test_game_update_rolls_back(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td);changed=dict(r,eligible=False)
            with mock.patch.object(u,"inspect",side_effect=[(r,{}),(changed,{})]):
                with self.assertRaises(u.Invalid):u.install(p,t,idle=lambda:None)
            for n in u.FILES:self.assertEqual((t/n).read_text(),'{"old":1}')
    def test_already_current(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td)
            for n in u.FILES:(t/n).write_bytes((p/"profiles"/n).read_bytes())
            with mock.patch.object(u,"inspect",return_value=(r,{})):
                self.assertEqual(u.install(p,t,idle=lambda:None)["status"],"already-current")
    def test_prepare_never_overwrites_directory(self):
        with tempfile.TemporaryDirectory() as td:
            with self.assertRaises(u.Invalid):u.prepare("missing","missing",td)


    def test_recover_interrupted_transaction(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td)
            backup=t/("profile-backup-"+"a"*32);backup.mkdir()
            hashes={}
            for name in u.FILES:
                (backup/name).write_text("original")
                hashes[name]=u.digest((backup/name).read_bytes())
                (t/name).write_text("partial")
            u.write_json(t/".profile-update-pending.json",{"backup":backup.name,"files":hashes})
            (t/".profile-update.lock").write_text("123")
            u.recover(t,idle=lambda:None,alive=lambda _:False)
            for name in u.FILES:self.assertEqual((t/name).read_text(),"original")
            self.assertFalse((t/".profile-update.lock").exists())
    def test_recover_rejects_live_updater(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td);(t/".profile-update.lock").write_text("123")
            with self.assertRaises(u.Invalid):u.recover(t,idle=lambda:None,alive=lambda _:True)
    def test_recover_rejects_escape(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td)
            u.write_json(t/".profile-update-pending.json",{"backup":"../outside","files":dict.fromkeys(u.FILES,"x")})
            with self.assertRaises(u.Invalid):u.recover(t,idle=lambda:None)
    def test_recover_rejects_corrupt_backup(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td);backup=t/("profile-backup-"+"b"*32);backup.mkdir()
            for name in u.FILES:(backup/name).write_text("corrupt")
            u.write_json(t/".profile-update-pending.json",{"backup":backup.name,"files":dict.fromkeys(u.FILES,"x")})
            with self.assertRaises(u.Invalid):u.recover(t,idle=lambda:None)
            for name in u.FILES:self.assertEqual((t/name).read_text(),'{"old":1}')

    def test_enumeration_failure_is_not_idle(self):
        u.check_enumeration_end(18)
        for error in (0,5,6,299):
            with self.assertRaises(u.Invalid):u.check_enumeration_end(error)

    def test_json_rejects_nonfinite_constants(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/"test.json"
            for value in ("NaN","Infinity","-Infinity"):
                p.write_text('{"value":'+value+'}')
                with self.assertRaises(u.Invalid):u.read_json(p)

    def test_json_size_limit(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/"test.json";p.write_bytes(b" "*(1024*1024+1))
            with self.assertRaises(u.Invalid):u.read_json(p)

    def test_json_failed_replace_preserves_previous(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/"journal.json";p.write_text('{"old":1}')
            with mock.patch.object(u.os,"replace",side_effect=OSError("disk failure")):
                with self.assertRaises(OSError):u.write_json(p,{"new":1})
            self.assertEqual(u.read_json(p),{"old":1})
            self.assertEqual(list(Path(td).iterdir()),[p])

    def test_failed_rollback_copy_keeps_target_and_recovery_journal(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td);copy=u.shutil.copyfile
            def fail_rollback(src,dst):
                if ".recover-" in Path(dst).name:
                    Path(dst).write_bytes(b"partial")
                    raise OSError("rollback disk failure")
                return copy(src,dst)
            with mock.patch.object(u,"inspect",side_effect=[(r,{}),(dict(r,eligible=False),{})]),mock.patch.object(u.shutil,"copyfile",side_effect=fail_rollback):
                with self.assertRaises(OSError):u.install(p,t,idle=lambda:None)
            for name in u.FILES:self.assertEqual((t/name).read_text(),'{"new":1}')
            self.assertTrue((t/".profile-update-pending.json").exists())
            self.assertFalse(list(t.glob("*.recover-*")))
            u.recover(t,idle=lambda:None)
            for name in u.FILES:self.assertEqual((t/name).read_text(),'{"old":1}')

    def test_dependency_changes_preserve_unknowns(self):
        identity={"timestamp":1,"size":2,"sha256":"x"}
        report={"version":3,"client":identity,"engine":identity,"candidates":{}}
        rows=u.dependency_changes({"clientVersion":3,"pawn":123},{},{},report)
        self.assertEqual(rows[0]["status"],"unchanged")
        pawn=next(r for r in rows if r["field"]=="pawn")
        self.assertEqual(pawn["status"],"unverified")
        self.assertIsNone(pawn["observed"])
        self.assertEqual(rows[-1]["status"],"unverified")

    def test_dependency_changes_detect_real_candidate_change(self):
        identity={"timestamp":1,"size":2,"sha256":"x"}
        report={"version":3,"client":identity,"engine":identity,"candidates":{"globals":{"pawn":456}}}
        rows=u.dependency_changes({"clientVersion":2,"pawn":123},{},{},report)
        pawn=next(r for r in rows if r["field"]=="pawn")
        self.assertEqual((pawn["previous"],pawn["observed"],pawn["status"]),(123,456,"changed"))
        self.assertEqual(rows[0]["status"],"changed")

    def test_json_rejects_float_overflow(self):
        for text in (b'{"x":1e999}', b'{"x":-1e999}'):
            with self.assertRaises(u.Invalid):u.parse_json(text)

    def test_stable_read_after_atomic_json_replace(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/"a.json";u.write_json(p,{"x":1});u.write_json(p,{"x":2})
            self.assertEqual(u.read_json(p),{"x":2})

    def test_guard_excludes_install_and_recovery(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td)
            with u.transaction_guard(t):
                with self.assertRaises(u.Invalid):u.install(p,t,idle=lambda:None)
                with self.assertRaises(u.Invalid):u.recover(t,idle=lambda:None)
            with u.transaction_guard(t):pass

    def test_guard_excludes_other_process_and_releases_after_exit(self):
        with tempfile.TemporaryDirectory() as td:
            script="import sys;sys.path.insert(0,sys.argv[1]);import update;\nwith update.transaction_guard(sys.argv[2]): print('locked',flush=True);sys.stdin.read()"
            child=subprocess.Popen([sys.executable,"-u","-c",script,str(Path(u.__file__).parent),td],
                                   stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            try:
                self.assertEqual(child.stdout.readline().strip(),"locked")
                with self.assertRaises(u.Invalid):
                    with u.transaction_guard(td):pass
                child.kill();child.communicate(timeout=5)
                with u.transaction_guard(td):pass
            finally:
                if child.poll() is None:child.kill()
                child.communicate(timeout=5)

    def inspection_fixture(self,td):
        game=Path(td)/"game";assets=Path(td)/"assets";assets.mkdir()
        for rel in ("game/csgo/bin/win64/client.dll","game/bin/win64/engine2.dll"):
            p=game/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(pe_bytes())
        (game/"game/csgo/steam.inf").write_text("ClientVersion=3\n")
        for name in u.FILES:(assets/name).write_text("{}")
        return game,assets

    def test_inspect_rejects_game_update_mid_scan(self):
        with tempfile.TemporaryDirectory() as td:
            game,assets=self.inspection_fixture(td)
            def changed(_):
                (game/"game/csgo/steam.inf").write_text("ClientVersion=44444\n")
                return {}
            with mock.patch.object(u,"global_candidates",side_effect=changed):
                with self.assertRaisesRegex(u.Invalid,"File changed"):u.inspect(game,assets)

    def test_inspect_rejects_source_profile_change_mid_scan(self):
        with tempfile.TemporaryDirectory() as td:
            game,assets=self.inspection_fixture(td)
            def changed(_):
                (assets/"memory.json").write_text('{"pawn":999}')
                return {}
            with mock.patch.object(u,"global_candidates",side_effect=changed):
                with self.assertRaisesRegex(u.Invalid,"File changed"):u.inspect(game,assets)

    def test_malformed_buttons_are_reported_as_blocker(self):
        with tempfile.TemporaryDirectory() as td:
            game,assets=self.inspection_fixture(td)
            for buttons in ([],{"forward":[]}):
                u.write_json(assets/"input-buttons.json",{"buttons":buttons})
                report,_=u.inspect(game,assets)
                self.assertFalse(report["eligible"])
                self.assertTrue(any("Expected button" in x for x in report["blockers"]))

    def test_prepare_does_not_rehash_changed_source_as_approved(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td)
            def inspected(*args):
                (p/"profiles"/u.FILES[0]).write_text('{"modified":true}')
                return dict(r),{}
            output=Path(td)/"output"
            with mock.patch.object(u,"inspect",side_effect=inspected):
                with self.assertRaisesRegex(u.Invalid,"Source changed"):u.prepare("fake",p/"profiles",output)
            self.assertFalse((output/"report.json").exists())

    def test_corrupt_copy_never_replaces_installed_profile(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td);copy=u.shutil.copyfile;replace=u.os.replace
            replaced=[]
            def corrupt(src,dst):
                if ".update-" in Path(dst).name:Path(dst).write_text("corrupt")
                else:copy(src,dst)
            def record(src,dst):
                if ".update-" in Path(src).name:replaced.append(dst)
                replace(src,dst)
            with mock.patch.object(u,"inspect",return_value=(r,{})),mock.patch.object(u.shutil,"copyfile",side_effect=corrupt),mock.patch.object(u.os,"replace",side_effect=record):
                with self.assertRaisesRegex(u.Invalid,"Package changed"):u.install(p,t,idle=lambda:None)
            self.assertEqual(replaced,[])
            for name in u.FILES:self.assertEqual((t/name).read_text(),'{"old":1}')

    def test_package_report_rejects_malformed_contract(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td)
            for field,value in (("format",True),("format",2),("profile_hashes",None),
                                ("profile_hashes",[]),("profile_hashes",{}),
                                ("game",[]),("client",[]),("engine",None)):
                with self.subTest(field=field,value=value):
                    u.write_json(p/"report.json",dict(r,**{field:value}))
                    with self.assertRaises(u.Invalid):u.install(p,t,idle=lambda:None)
                    for name in u.FILES:self.assertEqual((t/name).read_text(),'{"old":1}')

    def test_recovery_rejects_malformed_hashes(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td)
            for hashes in (None,[],{},dict.fromkeys(u.FILES,True),dict.fromkeys(u.FILES,"z"*64)):
                u.write_json(t/".profile-update-pending.json",{"backup":"profile-backup-"+"a"*32,"files":hashes})
                with self.assertRaises(u.Invalid):u.recover(t,idle=lambda:None)
                self.assertTrue((t/".profile-update-pending.json").exists())

    def test_recovery_rejects_unbounded_pid(self):
        with tempfile.TemporaryDirectory() as td:
            lock=Path(td)/".profile-update.lock"
            for text in ("9"*100,"4294967296","0","-1"):
                lock.write_text(text)
                with self.assertRaises(u.Invalid):u.recover(td,idle=lambda:None,alive=lambda _:False)
                self.assertTrue(lock.exists())

    def test_unapproved_build_never_produces_installable_profiles(self):
        with tempfile.TemporaryDirectory() as td:
            game,assets=self.inspection_fixture(td);output=Path(td)/"result"
            report=u.prepare(game,assets,output)
            self.assertFalse(report["eligible"])
            self.assertFalse((output/"profiles").exists())
            self.assertEqual(u.read_json(output/"report.json")["status"],"validation-required")

    def test_cli_malformed_report_returns_json_not_traceback(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td);u.write_json(p/"report.json",{"eligible":True,"format":1,"profile_hashes":[]})
            result=subprocess.run([sys.executable,u.__file__,"install","--package",str(p),"--target",str(t)],
                                  capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,1)
            self.assertEqual(json.loads(result.stdout)["status"],"error")
            self.assertNotIn("Traceback",result.stderr)

    def test_deep_json_has_controlled_error(self):
        with self.assertRaisesRegex(u.Invalid,"nesting"):
            u.parse_json(b'{"x":'+b'['*2000+b'0'+b']'*2000+b'}')

    def test_backup_copy_corruption_aborts_before_install(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td);copy=u.shutil.copyfile
            def corrupt(src,dst):
                if Path(dst).parent.name.startswith("profile-backup-"):Path(dst).write_text("damaged")
                else:copy(src,dst)
            with mock.patch.object(u,"inspect",return_value=(r,{})),mock.patch.object(u.shutil,"copyfile",side_effect=corrupt):
                with self.assertRaisesRegex(u.Invalid,"Backup copy changed"):u.install(p,t,idle=lambda:None)
            self.assertFalse((t/".profile-update-pending.json").exists())
            for name in u.FILES:self.assertEqual((t/name).read_text(),'{"old":1}')

    def test_wrapper_explicit_game_path_wins_and_ini_section_is_respected(self):
        shell=u.shutil.which("pwsh") or u.shutil.which("powershell")
        if not shell:self.skipTest("PowerShell unavailable")
        with tempfile.TemporaryDirectory() as td:
            folder=Path(td);panel=folder/"panel";panel.mkdir()
            wrapper=folder/"Update-Compatibility.ps1"
            u.shutil.copyfile(Path(u.__file__).with_name(wrapper.name),wrapper)
            (folder/"update.py").write_text("import sys,json;from pathlib import Path;Path(__file__).with_name('args.json').write_text(json.dumps(sys.argv[1:]))")
            (panel/"panel-settings.ini").write_text('[Slot2]\nGameDir=wrong-slot\n[Paths]\n  GameDir = "configured game"  \n')
            for options,expected in (([],"configured game"),(["-GameDirectory","explicit game"],"explicit game")):
                result=subprocess.run([shell,"-NoProfile","-File",str(wrapper),"-PanelDirectory",str(panel),*options],
                                      capture_output=True,text=True,timeout=20)
                self.assertEqual(result.returncode,0,result.stderr)
                args=json.loads((folder/"args.json").read_text())
                self.assertEqual(args[args.index("--game")+1],expected)

    def test_real_process_crash_after_first_install_file_is_recoverable(self):
        with tempfile.TemporaryDirectory() as td:
            p,t,r=self.fixture(td)
            script="""
import sys,os
from pathlib import Path
sys.path.insert(0,sys.argv[1]);import update as u
p,t=Path(sys.argv[2]),Path(sys.argv[3])
r=u.read_json(p/'report.json');u.inspect=lambda *args:(r,{})
replace=os.replace
def crash(src,dst):
 replace(src,dst)
 if '.update-' in Path(src).name:os._exit(27)
os.replace=crash
u.install(p,t,idle=lambda:None)
"""
            result=subprocess.run([sys.executable,"-c",script,str(Path(u.__file__).parent),str(p),str(t)],
                                  capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,27,result.stderr)
            self.assertEqual((t/u.FILES[0]).read_text(),'{"new":1}')
            self.assertEqual((t/u.FILES[1]).read_text(),'{"old":1}')
            self.assertTrue((t/".profile-update-pending.json").exists())
            u.recover(t,idle=lambda:None)
            for name in u.FILES:self.assertEqual((t/name).read_text(),'{"old":1}')
            self.assertFalse((t/".profile-update-pending.json").exists())
            self.assertFalse((t/".profile-update.lock").exists())

if __name__=="__main__":unittest.main()
