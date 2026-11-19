"""Check the external Lua expression tables through real librime and the native engine."""
from pathlib import Path
import subprocess, sys, uuid
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from wetype.integration import ROOT, build, stage, SQUIRREL, DEFAULT_SOURCE
from fixture_cleanup import compact

base = ROOT / '.local' / ('lua-expressions-acceptance-' + uuid.uuid4().hex[:8])
if not (ROOT / 'build/wetype-worker').exists() or not (ROOT / 'build/librime-wetype.dylib').exists():
    source = DEFAULT_SOURCE if (DEFAULT_SOURCE / 'src/rime/translator.h').is_file() else ROOT / 'work/librime-1.16.0'
    build(source)
stage(base / 'user', base / 'runtime', copy_engine=False)
(base / 'logs').mkdir()
user = base / 'user'
(user / 'default.custom.yaml').write_text('patch:\n  schema_list:\n    - schema: wetype\n  menu/page_size: 20\n')
# Isolate the mechanism from the shipped supplied tables: empty bodies, then
# fixture rows in the personal overlays exercise merging and hot reload.
for name in ('wetype_emoji.tsv', 'wetype_kaomoji.tsv', 'wetype_symbols.tsv'):
    (user / name).write_text('# supplied table placeholder for acceptance\n')
(user / 'wetype_emoji.custom.tsv').write_text('# personal emoji\n测试\t🍄\n')
(user / 'wetype_kaomoji.custom.tsv').write_text('# personal kaomoji\n测试\t测试(╯°□°)╯\n')
(user / 'wetype_symbols.custom.tsv').write_text('# personal symbols\n测试\t※\n')
binary = ROOT / 'build/rime-lua-expressions-test'
rime_source = DEFAULT_SOURCE if (DEFAULT_SOURCE / 'src/rime_api.h').is_file() else ROOT / 'work/librime-1.16.0'
subprocess.run(['/usr/bin/clang++', '-std=c++17', '-Wno-missing-field-initializers',
                '-I' + str(rime_source / 'src'),
                str(ROOT / 'tests/rime_lua_expressions.cpp'), str(SQUIRREL / 'Frameworks/librime.1.dylib'),
                '-Wl,-rpath,' + str(SQUIRREL / 'Frameworks'), '-o', str(binary)], check=True)
with (base / 'logs/acceptance.log').open('w') as log:
    result = subprocess.run([str(binary), str(user), str(base / 'logs')],
                            stdout=log, stderr=subprocess.STDOUT, timeout=180)
print((base / 'logs/acceptance.log').read_text()[-2500:])
result.check_returncode()
compact(base)
print('Local acceptance logs:', base)
