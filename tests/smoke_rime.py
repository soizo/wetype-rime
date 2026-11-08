"""Use the machine's real librime in fresh user/learning directories; no GUI edits."""
from pathlib import Path
import argparse
import shutil
import subprocess
import sys
import uuid
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from wetype.integration import build, stage, register_scheme, DEFAULT_SOURCE, DEFAULT_BOOST_INCLUDE, SQUIRREL
from wetype.macos import ROOT
from wetype.modes import SCHEMAS, SAVE_OPTIONS_PATCH
from fixture_cleanup import compact
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--user-profile',type=Path,help='copy static configuration into a fresh test directory; never copy userdb')
parser.add_argument('--baseline-schema',default='')
parser.add_argument('--reloads',type=int,default=0)
parser.add_argument('--timing',action='store_true',help='pace fast typing and held-delete input, plus batched rendering')
parser.add_argument('--keep-fixture',action='store_true',help='retain copied profile/runtime after success; failures are always retained')
args=parser.parse_args()
if args.reloads<0 or args.reloads>30:parser.error('--reloads must be between 0 and 30')
build()
base=ROOT/'.local'/('rime-acceptance-'+uuid.uuid4().hex[:8])
user=base/'user';runtime=base/'runtime';logs=base/'logs'
user.mkdir(parents=True)
if args.user_profile:
    source=args.user_profile.resolve()
    for entry in source.iterdir():
        if entry.is_file() and entry.suffix in ('.yaml','.lua','.txt') and entry.name not in ('user.yaml','installation.yaml'):
            shutil.copy2(entry,user/entry.name)
    for name in ('lua','opencc','cn_dicts','en_dicts','build'):
        if (source/name).is_dir():shutil.copytree(source/name,user/name)
stage(user,runtime,copy_engine=False)
# Exercise a file-only opt-out alongside the enabled shared default.
(user/'wetype_quanpin.custom.yaml').write_text('patch:\n  wetype/surrounding: false\n')
if args.user_profile:register_scheme(user, SCHEMAS)
else:(user/'default.custom.yaml').write_text('patch:\n  schema_list:\n'+''.join('    - schema: '+schema+'\n' for schema in SCHEMAS)+SAVE_OPTIONS_PATCH)
logs.mkdir()
subprocess.run(['/usr/bin/clang++','-std=c++17','-fobjc-arc','-c',str(ROOT/'tests/imk_fixture.mm'),
                '-o',str(ROOT/'build/imk-fixture.o')],check=True)
subprocess.run(['/usr/bin/clang++','-std=c++17','-Wno-missing-field-initializers','-I'+str(DEFAULT_BOOST_INCLUDE),'-I'+str(DEFAULT_SOURCE/'src'),'-I'+str(ROOT/'build/include'),
                str(ROOT/'tests/rime_smoke.cpp'),str(ROOT/'build/imk-fixture.o'),str(SQUIRREL/'Frameworks/librime.1.dylib'),'-framework','AppKit',
                '-Wl,-rpath,'+str(SQUIRREL/'Frameworks'),'-o',str(ROOT/'build/rime-smoke')],check=True)
with (logs/'acceptance.log').open('w') as output:
    command=[str(ROOT/'build/rime-smoke'),str(user),str(logs),args.baseline_schema,str(args.reloads)]
    if args.timing:command.append('--timing')
    result=subprocess.run(command,
                          stdout=output,stderr=subprocess.STDOUT,timeout=90+args.reloads*20)
print((logs/'acceptance.log').read_text()[-2000:])
print('Rime acceptance log:',logs/'acceptance.log')
result.check_returncode()
compact(base, args.keep_fixture)
