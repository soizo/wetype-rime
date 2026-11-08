"""Exercise native user-word storage and the real Rime stabledb integration."""
from pathlib import Path
import subprocess
import sys
import uuid
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from wetype.integration import ROOT,DEFAULT_SOURCE,SQUIRREL,build,stage
from wetype.modes import SCHEMAS
from fixture_cleanup import compact

build()
base=ROOT/'.local'/('user-words-acceptance-'+uuid.uuid4().hex[:8])
stage(base/'user',base/'runtime',copy_engine=False)
(base/'logs').mkdir()
hex_value=lambda value:value.encode().hex() or '-'

class Host:
    def __init__(self,group='pinyin'):
        name='start-engine'+('' if group=='pinyin' else '-'+group)
        self.process=subprocess.Popen([str(base/'runtime/bin'/name)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        assert self.read()=='READY'
    def send(self,line):self.process.stdin.write(line+'\n');self.process.stdin.flush()
    def read(self):
        line=self.process.stdout.readline().strip()
        if not line or line.startswith('ERROR '):raise RuntimeError(line)
        return line
    def sync(self,source,entries):
        self.send('WORD_SYNC '+hex_value(source)+' '+str(len(entries))+'\n'+''.join('W '+hex_value(code)+' '+hex_value(text)+'\n' for code,text in entries)+'END_WORDS')
        assert self.read()=='OK'
    def words(self):
        self.send('WORDS');tag,count=self.read().split();assert tag=='WORDS'
        out=[]
        for _ in range(int(count)):
            tag,*fields=self.read().split();assert tag=='W'
            out.append(tuple(bytes.fromhex(value).decode() if value!='-' else '' for value in fields))
        assert self.read()=='END';return out
    def rejected(self,request):
        self.send(request)
        assert self.process.stdout.readline().startswith('ERROR '),'invalid request accepted'
    def open(self,mode):self.send('OPEN '+mode);tag,sid=self.read().split();assert tag=='OPENED';return sid
    def query(self,sid,code):
        self.send('QUERY '+sid+' '+hex_value(code));tag,ticket,count=self.read().split();assert tag=='RESULT'
        out=[]
        for _ in range(int(count)):
            tag,index,cover,text,preedit=self.read().split();assert tag=='C'
            out.append(bytes.fromhex(text).decode())
        assert self.read()=='END';return ticket,out
    def select(self,sid,ticket,index):
        self.send('SELECT '+sid+' '+ticket+' '+str(index));assert self.read()=='OK'
    def close(self):
        self.send('QUIT');self.process.wait(timeout=15);assert self.process.returncode==0

host=Host()
try:
    host.send('WORD_SET '+hex_value('outside-rime')+' '+hex_value('keepword')+' '+hex_value('保留原生词'));assert host.read()=='OK'
    host.sync('source-A',[('wtrime','引擎用户词')]);host.sync('source-B',[('wtrimeb','第二个词')])
    for mode in ('quanpin','flypy','natural','microsoft','sogou','ziguang','abc','jiajia'):
        sid=host.open(mode);_,rows=host.query(sid,'wtrime');assert '引擎用户词' in rows,(mode,rows)
    before=host.words()
    host.rejected('WORD_SYNC '+hex_value('source-A')+' 2\nW '+hex_value('wtrime')+' '+hex_value('不能部分导入')+'\nW 78 f0808080\nEND_WORDS')
    assert host.words()==before,'invalid UTF-8 partially changed native words'
    host.sync('source-A',[('wtrime','引擎替换词')]);ticket,rows=host.query(sid,'wtrime')
    assert '引擎替换词' in rows and '引擎用户词' not in rows
    host.send('DELETE '+sid+' '+ticket+' '+str(rows.index('引擎替换词')));assert host.read()=='OK'
    host.rejected('DELETE '+sid+' '+ticket+' 0')
    assert '引擎替换词' not in host.query(sid,'wtrime')[1]
    host.sync('source-A',[])
    assert {word[2] for word in host.words()}=={'保留原生词','第二个词'}
    sid=host.open('quanpin');ticket,rows=host.query(sid,'nihao');old_rank=rows.index('拟好')
    host.select(sid,ticket,old_rank)
    ticket,rows=host.query(sid,'nihao');learned_rank=rows.index('拟好')
    assert learned_rank<old_rank,(old_rank,learned_rank)
    host.send('DELETE '+sid+' '+ticket+' '+str(learned_rank));assert host.read()=='OK'
    rows=host.query(sid,'nihao')[1]
    assert '拟好' not in rows or rows.index('拟好')>learned_rank,rows
    assert {word[2] for word in host.words()}=={'保留原生词','第二个词'}
finally:host.close()
host=Host()
try:assert {word[2] for word in host.words()}=={'保留原生词','第二个词'}
finally:host.close()
print('PASS: native add/update/delete, eight pinyin layouts, learned-candidate deletion, source isolation, invalid batches, stale deletion and persistence',flush=True)

# Use a separate runtime so the integration fixture cannot inherit probe words.
rime_base=base/'rime'
stage(rime_base/'user',rime_base/'runtime',copy_engine=False)
(rime_base/'logs').mkdir()
(rime_base/'user/default.custom.yaml').write_text('patch:\n  schema_list:\n'+''.join('    - schema: '+schema+'\n' for schema in SCHEMAS))
(rime_base/'user/custom_phrase.txt').write_text('# Rime user words\n引擎用户词\twt ri me\t10\n五笔用户词\twu\t5\n禁用用户词\tnegword\t-1\n')
binary=ROOT/'build/rime-user-words'
subprocess.run(['/usr/bin/clang++','-std=c++17','-Wno-missing-field-initializers','-I'+str(DEFAULT_SOURCE/'src'),
                str(ROOT/'tests/rime_user_words.cpp'),str(SQUIRREL/'Frameworks/librime.1.dylib'),
                '-Wl,-rpath,'+str(SQUIRREL/'Frameworks'),'-o',str(binary)],check=True)
with (rime_base/'logs/acceptance.log').open('w') as log:
    result=subprocess.run([str(binary),str(rime_base/'user'),str(rime_base/'logs')],stdout=log,stderr=subprocess.STDOUT,timeout=120)
print((rime_base/'logs/acceptance.log').read_text()[-2500:]);result.check_returncode()
(base/'logs/acceptance.log').write_text('PASS: native user-word storage, source isolation, persistence and real Rime integration.\n')
compact(rime_base);compact(base)
print('Local acceptance logs:',base)
