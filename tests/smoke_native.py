"""Opt-in real-engine test. No downloads; uses fresh isolated learning data."""
from pathlib import Path
import subprocess
import sys
import uuid
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from wetype.macos import DEFAULT_ENGINE, ROOT, probe

state = 'acceptance-' + uuid.uuid4().hex[:8]
first = probe(DEFAULT_ENGINE, 'nihao', state)
assert '你好' in first['candidates'] and '拟好' in first['candidates'], first
old_rank = first['candidates'].index('拟好')
selected = probe(DEFAULT_ENGINE, 'nihao', state, '拟好')
assert selected['selected'].startswith('拟好; input coverage: 5'), selected
restarted = probe(DEFAULT_ENGINE, 'nihao', state)
new_rank = restarted['candidates'].index('拟好')
assert new_rank < old_rank, (first, restarted)
partial = probe(DEFAULT_ENGINE, 'nihaozhongguo', state + '-partial', '你好')
assert partial['selected'].startswith('你好; input coverage: 5'), partial
assert partial['candidates'][0] == '中国', partial
# No vendor code may execute if the user bypasses the required offline launcher.
raw = subprocess.run([str(ROOT / 'build/wetype-probe'), '/nonexistent/engine.bundle'], capture_output=True, text=True)
assert raw.returncode == 3 and 'refusing engine load' in raw.stderr, raw
print('PASS: real candidates, selection, learning after process restart, partial selection, offline guard')
print(f'拟好 persisted ranking: {old_rank + 1} -> {new_rank + 1}')
print('local test state:', state)
for result in (first, selected, restarted, partial):
    print(result['engine_log'])
