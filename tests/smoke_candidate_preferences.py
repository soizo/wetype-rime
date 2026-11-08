"""Verify persistent candidate preferences with real Rime and native selection."""
from pathlib import Path
import subprocess
import sys
import uuid
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from wetype.integration import ROOT,build,stage,DEFAULT_SOURCE,SQUIRREL
from wetype.modes import SCHEMAS
from fixture_cleanup import compact
build()
base=ROOT/'.local'/('candidate-preferences-'+uuid.uuid4().hex[:8])
stage(base/'user',base/'runtime',copy_engine=False)
(base/'logs').mkdir()
(base/'user/default.custom.yaml').write_text('patch:\n  schema_list:\n'+''.join('    - schema: '+schema+'\n' for schema in SCHEMAS))
binary=ROOT/'build/rime-candidate-preferences'
subprocess.run(['/usr/bin/clang++','-std=c++17','-I'+str(DEFAULT_SOURCE/'src'),str(ROOT/'tests/rime_candidate_preferences.cpp'),str(SQUIRREL/'Frameworks/librime.1.dylib'),'-Wl,-rpath,'+str(SQUIRREL/'Frameworks'),'-o',str(binary)],check=True)
subprocess.run([str(binary),str(base/'user'),str(base/'logs')],check=True,timeout=90)
compact(base)
