"""Exercise the bundled native cell dictionaries and their real Rime settings."""
from pathlib import Path
import json,subprocess,sys,uuid
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from wetype.integration import ROOT,stage,build,SQUIRREL,DEFAULT_SOURCE
from fixture_cleanup import compact
build()
base=ROOT/'.local'/('cells-acceptance-'+uuid.uuid4().hex[:8]);stage(base/'user',base/'runtime',copy_engine=False)
(base/'logs').mkdir()
encode=lambda s:s.encode().hex() or '-'
class Host:
 def __init__(self,group='pinyin'):
  name='start-engine'+('' if group=='pinyin' else '-'+group)
  self.p=subprocess.Popen([str(base/'runtime/bin'/name)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
  assert self.read()=='READY'
 def read(self):
  s=self.p.stdout.readline().strip()
  if not s or s.startswith('ERROR '):raise RuntimeError(s)
  return s
 def send(self,s):self.p.stdin.write(s+'\n');self.p.stdin.flush()
 def cells(self,ids):self.send('CELL_SYNC '+str(len(ids))+''.join(' '+encode(i) for i in ids));assert self.read()=='OK'
 def status(self):
  self.send('CELL_STATUS');tag,n=self.read().split();assert tag=='CELLS';out={}
  for _ in range(int(n)):
   tag,key,kind,active=self.read().split();assert tag=='D';out[bytes.fromhex(key).decode()]=(int(kind),int(active))
  assert self.read()=='END';return out
 def open(self,mode='quanpin',emoji=False):self.send('OPEN '+mode+(' 1' if emoji else ' 0'));tag,sid=self.read().split();assert tag=='OPENED';return sid
 def query(self,sid,code):
  self.send('QUERY '+sid+' '+encode(code));tag,ticket,n=self.read().split();assert tag=='RESULT';rows=[]
  for _ in range(int(n)):
   tag,index,cover,text,pre=self.read().split();assert tag=='C';rows.append(bytes.fromhex(text).decode())
  assert self.read()=='END';return ticket,rows
 def reset(self,sid):self.send('RESET '+sid);assert self.read()=='OK'
 def mappings(self):
  result=subprocess.run(['/usr/sbin/lsof','-p',str(self.p.pid),'-Fn'],capture_output=True,text=True,check=True)
  return result.stdout
 def close(self):self.send('QUIT');self.p.wait(timeout=15);assert self.p.returncode==0
ids=['cell_dict.sc','cell_dict.ssrc','cell_dict.hf']
host=Host()
try:
 status=host.status();assert set(status)==set(ids) and not any(v[1] for v in status.values()),status
 sid=host.open(emoji=True);ticket,baseline=host.query(sid,'wuqiong')
 print('baseline',baseline[:6],flush=True)
 for identifier in ids:
  host.cells([identifier]);assert [key for key,v in host.status().items() if v[1]]==[identifier]
  mappings=host.mappings()
  assert identifier+'.zh' in mappings,('native binary not mapped',identifier)
  ticket,rows=host.query(sid,'wuqiong')
  if identifier=='cell_dict.sc':assert rows.index('无穷')<baseline.index('无穷'),(baseline,rows)
  host.cells([identifier]);same_ticket,same_rows=host.query(sid,'wuqiong');assert same_ticket==ticket and same_rows==rows,'unchanged cell configuration reset composition'
  host.cells([]);ticket,rows=host.query(sid,'wuqiong')
  assert rows[:3]==baseline[:3],(identifier,baseline[:3],rows[:3])
 host.cells(ids);assert all(v[1] for v in host.status().values())
 mappings=host.mappings();assert all(i+'.zh' in mappings for i in ids)
 previous=host.status()
 for request in ('CELL_SYNC 1 '+encode('not-in-this-engine'),'CELL_SYNC 2 '+encode(ids[0])+' '+encode(ids[0]),'CELL_SYNC 1','CELL_SYNC 0 extra'):
  host.send(request);assert host.p.stdout.readline().startswith('ERROR ')
  assert host.status()==previous,'invalid native cell request changed active dictionaries'
 # Cells must not bypass the native Emoji option.
 plain=host.open(emoji=False);_,rows=host.query(plain,'wuqiong');assert '♾️' not in rows,rows
 # Cell attachment must coexist with both ordinary imported vocabulary and
 # the independent native shortcut store used by existing configurations.
 host.send('LEXICON_SYNC 1\nL '+encode('ji,heng,shang,jing')+' '+encode('霁蘅觞璟')+' 1000\nEND_LEXICON');assert host.read()=='OK'
 host.send('WORD_SET '+encode('cell-test-shortcut')+' '+encode('wtrime')+' '+encode('细胞接口短语'));assert host.read()=='OK'
 for selection in ([],ids,[]):
  host.cells(selection);_,rows=host.query(plain,'jihengshangjing');assert '霁蘅觞璟' in rows,rows
  host.reset(plain);_,rows=host.query(plain,'wtrime');assert '细胞接口短语' in rows,rows
 host.send('LEXICON_SYNC 0\nEND_LEXICON');assert host.read()=='OK'
 host.cells([]);normal=host.open();ticket,rows=host.query(normal,'nihao');rank=rows.index('拟好')
 host.send('SELECT '+normal+' '+ticket+' '+str(rank));assert host.read()=='OK'
 host.reset(normal);_,learned=host.query(normal,'nihao');assert learned.index('拟好')<rank,(rows,learned)
 host.cells(ids);host.cells([]);_,retained=host.query(normal,'nihao');assert retained.index('拟好')==learned.index('拟好')
finally:host.close()
host=Host('wubi86')
try:
 sid=host.open('wubi86');_,baseline=host.query(sid,'wq');assert baseline[0]=='你',baseline
 host.cells(ids);_,rows=host.query(sid,'wq');assert rows[0]=='你',rows
 host.cells([]);_,rows=host.query(sid,'wq');assert rows[0]=='你',rows
finally:host.close()
print('PASS: all three native binaries mapped, individual/all enable, disable, native ranking, unchanged configuration, invalid requests, Emoji control, learning and Wubi',flush=True)
# Real Rime schema patches and hot deployment use a separate runtime.
rime=base/'rime';stage(rime/'user',rime/'runtime',copy_engine=False);(rime/'logs').mkdir()
(rime/'user/default.custom.yaml').write_text('patch:\n  schema_list:\n    - schema: wetype\n    - schema: wetype_flypy\n')
binary=ROOT/'build/rime-cells-test'
subprocess.run(['/usr/bin/clang++','-std=c++17','-Wno-missing-field-initializers','-I'+str(DEFAULT_SOURCE/'src'),str(ROOT/'tests/rime_cells.cpp'),str(SQUIRREL/'Frameworks/librime.1.dylib'),'-Wl,-rpath,'+str(SQUIRREL/'Frameworks'),'-o',str(binary)],check=True)
with (rime/'logs/acceptance.log').open('w') as log:
 result=subprocess.run([str(binary),str(rime/'user'),str(rime/'logs')],stdout=log,stderr=subprocess.STDOUT,timeout=120)
print((rime/'logs/acceptance.log').read_text()[-2500:]);result.check_returncode()
(base/'logs/acceptance.log').write_text('PASS: bundled native cells, mapped resources, on/off, source isolation and real Rime patches.\n')
compact(rime);compact(base)
print('Local acceptance logs:',base)
