"""Verify optional Rime dictionaries in a real, isolated native engine."""
from pathlib import Path
import subprocess
import sys
import uuid
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from wetype.integration import ROOT,stage,DEFAULT_SOURCE,SQUIRREL,build
from fixture_cleanup import compact

build()
base=ROOT/'.local'/('dictionary-acceptance-'+uuid.uuid4().hex[:8])
stage(base/'user',base/'runtime',copy_engine=False)
(base/'logs').mkdir()
encode=lambda value:value.encode().hex() or '-'
class Host:
    def __init__(self):
        self.p=subprocess.Popen([str(base/'runtime/bin/start-engine')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        assert self.read()=='READY'
    def read(self):
        line=self.p.stdout.readline().strip()
        if not line or line.startswith('ERROR '):raise RuntimeError(line)
        return line
    def send(self,message):self.p.stdin.write(message+'\n');self.p.stdin.flush()
    def sync(self,entries):
        self.send('LEXICON_SYNC '+str(len(entries))+'\n'+''.join('L '+encode(code)+' '+encode(text)+' '+str(weight)+'\n' for code,text,weight in entries)+'END_LEXICON')
        assert self.read()=='OK'
    def open(self,mode):self.send('OPEN '+mode);tag,sid=self.read().split();assert tag=='OPENED';return sid
    def query(self,sid,code):
        self.send('QUERY '+sid+' '+encode(code));tag,ticket,count=self.read().split();assert tag=='RESULT';rows=[]
        for _ in range(int(count)):
            tag,index,cover,text,preedit=self.read().split();assert tag=='C';rows.append(bytes.fromhex(text).decode())
        assert self.read()=='END';return ticket,rows
    def close(self):self.send('QUIT');self.p.wait(timeout=15);assert self.p.returncode==0

entries=[('ji,heng,shang,jing','霁珩觞璟',0),('ji,heng,shang,jing','霁蘅觞璟',1000)]
host=Host()
try:
    # Import before any user key: the loader must not depend on a warm session.
    host.sync(entries)
    for mode,code in [('quanpin','jihengshangjing'),('flypy','jihguhjk'),('natural','jihguhjy')]:
        sid=host.open(mode);_,rows=host.query(sid,code)
        assert rows[:2]==['霁蘅觞璟','霁珩觞璟'],(mode,rows)
    # No frontend reordering: exchange native initial weights and query again.
    entries=[(code,text,100000 if weight==0 else 0) for code,text,weight in entries]
    host.sync(entries);_,rows=host.query(sid,'jihguhjy');assert rows[:2]==['霁珩觞璟','霁蘅觞璟'],rows
    # An unchanged redeploy must not re-add entries or change their native score.
    state=base/'runtime/state/learning/rime-lexicon.state';before=state.read_bytes()
    host.sync(entries);assert state.read_bytes()==before
    host.send('LEXICON_SYNC 1\nL '+encode('ji,heng,shang,jing')+' '+encode('非法部分导入')+' nan\nEND_LEXICON')
    assert host.p.stdout.readline().startswith('ERROR ')
    assert state.read_bytes()==before
    host.sync([]);_,rows=host.query(sid,'jihguhjy')
    assert not any(text in rows for _,text,_ in entries),rows
    host.sync(entries)
    ticket,rows=host.query(sid,'jihguhjy')
    host.send('SELECT '+sid+' '+ticket+' '+str(rows.index('霁珩觞璟')));assert host.read()=='OK'
finally:host.close()
# Ownership and learning must survive worker replacement.
host=Host()
try:
    host.sync([]);sid=host.open('quanpin');_,rows=host.query(sid,'jihengshangjing')
    assert '霁珩觞璟' in rows and '霁蘅觞璟' not in rows,rows
finally:host.close()
print('PASS: native weight mapping, full/double pinyin, cold import, disable, invalid batch, unchanged redeploy and persistent learning',flush=True)

# Exercise the official dictionary reader and its import_tables through librime.
user=base/'user'
(user/'default.custom.yaml').write_text('patch:\n  schema_list:\n    - schema: wetype\n    - schema: wetype_flypy\n')
(user/'wetype_extra.dict.yaml').write_text('---\nname: wetype_extra\nversion: "1"\nsort: by_weight\ncolumns: [code, text, weight]\n...\nji heng shang jing\t霁蘅觞璟\t1000\n')
(user/'wetype.dict.yaml').write_text('---\nname: wetype\nversion: "1"\nsort: by_weight\nimport_tables: [wetype_extra]\n...\n姬\tji\t1\n蘅\theng\t1\n觞\tshang\t1\n璟\tjing\t1\n')
# Assert deployment itself succeeds with a disabled main dictionary, rather
# than only checking that input remains available after a deployment error.
with (base/'logs/deploy-acceptance.log').open('w') as log:
    subprocess.run([str(ROOT/'build/wetype-deploy'),str(user),str(SQUIRREL/'SharedSupport'),str(base/'logs'),'wetype','wetype_flypy'],stdout=log,stderr=subprocess.STDOUT,check=True)
binary=ROOT/'build/rime-dictionary-test'
subprocess.run(['/usr/bin/clang++','-std=c++17','-Wno-missing-field-initializers','-I'+str(DEFAULT_SOURCE/'src'),
    str(ROOT/'tests/rime_dictionary.cpp'),str(SQUIRREL/'Frameworks/librime.1.dylib'),'-Wl,-rpath,'+str(SQUIRREL/'Frameworks'),'-o',str(binary)],check=True)
with (base/'logs/rime-acceptance.log').open('w') as log:
    result=subprocess.run([str(binary),str(user),str(base/'logs')],stdout=log,stderr=subprocess.STDOUT,timeout=120)
print((base/'logs/rime-acceptance.log').read_text()[-2500:]);result.check_returncode()
(base/'logs/acceptance.log').write_text('PASS: native dictionary lifecycle, weight mapping and official Rime dictionary import_tables.\n')
compact(base)
print('Local acceptance logs:',base)
