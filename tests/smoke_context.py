"""Verify received context using the native engine's own snapshot visitor."""
from pathlib import Path
import subprocess
import sys
import uuid
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from wetype.integration import build,stage,atomic_copy
from wetype.macos import ROOT
from fixture_cleanup import compact

build()
base=ROOT/'.local'/('context-acceptance-'+uuid.uuid4().hex[:8])
stage(base/'user',base/'runtime',copy_engine=False)
binary=ROOT/'build/context-test-worker'
subprocess.run(['/usr/bin/clang++','-std=c++17','-Wall','-Wextra','-Werror','-DWETYPE_CONTEXT_TEST',
                '-I'+str(ROOT/'build/include'),str(ROOT/'native/worker.cpp'),'-o',str(binary)],check=True)
atomic_copy(binary,base/'runtime/bin/wetype-worker')
p=subprocess.Popen([str(base/'runtime/bin/start-engine')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
def read():
    line=p.stdout.readline().strip()
    if not line or line.startswith('ERROR '):raise RuntimeError(line)
    return line
def send(line):p.stdin.write(line+'\n');p.stdin.flush()
def query(sid,text,before,after="",on=""):
    send('QUERY '+sid+' '+text.encode().hex()+' '+' '.join(field.encode().hex() or '-' for field in (before,after,on)))
    tag,ticket,count=read().split();assert tag=='RESULT' and int(count)>0
    for _ in range(int(count)):assert read().startswith('C ')
    assert read()=='END'
    send('CONTEXT_STATUS '+sid)
    assert read()==f'CONTEXT 1 1 {len(before.encode())}',(text,before)
try:
    assert read()=='READY'
    send('OPEN');first=read().split()[1]
    send('OPEN');second=read().split()[1]
    query(first,'nihao','我喜欢中文😀','后文🌏','选中😀')
    query(second,'nihao','另一个编辑框')
    query(first,'niha','我喜欢中文😀')
    query(first,'nihao','我喜欢中文😀')
    query(first,'nihao','换了光标位置')
    query(second,'nihao','另一个编辑框')
    send('RESET '+first);assert read()=='OK'
    query(first,'nihao','')
    query(second,'nihao','另一个编辑框')
    query(first,'nihao','','只改变后文')
    query(first,'nihao','','后文变化','选中文字')
    query(first,'nihao','','后文变化','另一段')
    query(first,'nihao','')
    send('QUIT');p.wait(timeout=15);assert p.returncode==0
    (base/'logs').mkdir(exist_ok=True)
    summary='PASS: engine snapshots match UTF-8 context; independent sessions, delete/retype, cursor-context change and reset clearing.\n'
    (base/'logs/acceptance.log').write_text(summary);print(summary,end='')
    compact(base)
finally:
    if p.poll() is None:p.terminate();p.wait(timeout=10)
