"""Verify a normal Rime layout through a symlinked profile, without live edits."""
from pathlib import Path
import json
import subprocess
import sys
import uuid
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from wetype.integration import ROOT,stage,register_scheme,DEFAULT_SOURCE,SQUIRREL
from fixture_cleanup import compact

base=ROOT/'.local'/('rime-layout-'+uuid.uuid4().hex[:8])
user=base/'user';user.mkdir(parents=True)
alias=base/'linked profile';alias.symlink_to(user.resolve(),target_is_directory=True)
stage(alias,copy_engine=False)
settings=json.loads((user/'wetype/installation.yaml').read_text().split('\n',1)[1])['wetype']
assert not Path(settings['module']).is_absolute()
assert all(not Path(value).is_absolute() for value in settings['launchers'].values())
assert not (user/'wetype.runtime.yaml').exists()
assert (user/'wetype/bin/wetype-worker').is_file()
(user/'wetype_common.custom.yaml').write_text('patch:\n  base/menu/page_size: 5\n  settings/surrounding: false\n')
(user/'wetype_flypy.custom.yaml').write_text('patch:\n  menu/page_size: 7\n  wetype/surrounding: true\n')
register_scheme(alias)
assert (user/'default.custom.yaml').read_text().count('    - schema:')==1
register_scheme(alias,['wetype_flypy'])
legacy = 'P 7175616e70696e 6e6968616f e6938ce5a5bd\n'
(user/'wetype-candidates.state').write_text(legacy)
(user/'wetype-candidate-rules.custom.yaml').write_text(
    '- {layout: quanpin, code: nihao, pin: [拟好]}\n')
logs=base/'logs';logs.mkdir()
binary=ROOT/'build/rime-layout'
subprocess.run(['/usr/bin/clang++','-std=c++17','-I'+str(DEFAULT_SOURCE/'src'),
                str(ROOT/'tests/rime_layout.cpp'),str(SQUIRREL/'Frameworks/librime.1.dylib'),
                '-Wl,-rpath,'+str(SQUIRREL/'Frameworks'),'-o',str(binary)],check=True)
with (logs/'acceptance.log').open('w') as output:
    result=subprocess.run([str(binary),str(alias),str(logs)],stdout=output,stderr=subprocess.STDOUT,timeout=60)
print((logs/'acceptance.log').read_text()[-2500:]);result.check_returncode()
assert not (user/'wetype/state/candidates').exists()
assert (user/'wetype-candidates.state').read_text() == legacy
compact(base)
