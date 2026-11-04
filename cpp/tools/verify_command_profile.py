"""Read-only verifier. No game process access, profile writes or execution."""
import importlib.util
import json
import sys
from pathlib import Path

tool=Path(__file__).resolve().parents[2]/"tools/compatibility/update.py"
spec=importlib.util.spec_from_file_location("compatibility_update",tool)
updater=importlib.util.module_from_spec(spec)
spec.loader.exec_module(updater)

def inspect(dll, profile):
    p=updater.read_json(profile)
    result=updater.command_candidates(updater.PE(Path(dll).read_bytes()),p)
    result["profile_matches"]=all(p.get(k)==v for k,v in result.items())
    return result

if __name__=="__main__":
    if len(sys.argv)!=3:
        sys.exit("Usage: verify_command_profile.py engine2.dll game-commands.json")
    try:
        result=inspect(*sys.argv[1:])
        print(json.dumps(result,indent=2))
        sys.exit(0 if result["profile_matches"] else 1)
    except (ValueError,OSError,KeyError) as error:
        sys.exit(str(error))
