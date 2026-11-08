"""Check native local expressions and editable tables through real librime."""
from pathlib import Path
import argparse,shutil,subprocess,sys,uuid
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from wetype.integration import ROOT,build,stage,SQUIRREL,DEFAULT_SOURCE
from fixture_cleanup import compact
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--mainland-dir',type=Path,required=True,help='Generated OpenCCGovForRime package')
args=parser.parse_args()
build()
base=ROOT/'.local'/('expressions-acceptance-'+uuid.uuid4().hex[:8]);stage(base/'user',base/'runtime',copy_engine=False);(base/'logs').mkdir()
encode=lambda s:s.encode().hex() or '-'
class Host:
 def __init__(self,runtime):
  self.p=subprocess.Popen([str(runtime/'bin/start-engine')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
  assert self.read()=='READY'
 def read(self):
  line=self.p.stdout.readline().strip()
  if not line or line.startswith('ERROR '):raise RuntimeError(line)
  return line
 def send(self,line):self.p.stdin.write(line+'\n');self.p.stdin.flush()
 def open(self,flags,mode='quanpin'):
  self.send('OPEN '+mode+' '+str(flags));tag,sid=self.read().split();assert tag=='OPENED';return sid
 def query(self,sid,code):
  self.send('QUERY '+sid+' '+encode(code));tag,ticket,n=self.read().split();assert tag=='RESULT';rows=[]
  for _ in range(int(n)):
   tag,index,cover,text,pre=self.read().split();assert tag=='C';rows.append(bytes.fromhex(text).decode())
  assert self.read()=='END';return ticket,rows
 def close(self):self.send('QUIT');self.p.wait(timeout=15);assert self.p.returncode==0
host=Host(base/'runtime')
try:
 off=host.open(0);_,rows=host.query(off,'kaixin');assert '开心(*^▽^*)' not in rows,rows
 kao=host.open(2);ticket,rows=host.query(kao,'kaixin');assert '开心(*^▽^*)' in rows,rows
 host.send('SELECT '+kao+' '+ticket+' '+str(rows.index('开心(*^▽^*)')));assert host.read()=='OK'
 _,rows=host.query(kao,'xiaolian');assert '😊' not in rows,rows
 symbol=host.open(4);_,rows=host.query(symbol,'xingxing');assert '☆' in rows and '★' in rows,rows
 _,rows=host.query(symbol,'wuqiong');assert '∞' in rows,rows
 _,rows=host.query(off,'xingxing');assert '☆' not in rows and '★' not in rows,rows
 emoji=host.open(1);_,rows=host.query(emoji,'xiaolian');assert any(word in rows for word in ('😊','😄','😃','😆')),rows
 flypy=host.open(6,'flypy');_,rows=host.query(flypy,'kdxb');assert '开心(*^▽^*)' in rows,rows
 _,rows=host.query(flypy,'xkxk');assert '☆' in rows and '★' in rows,rows
 for flags in range(8,17):
  host.send('OPEN quanpin '+str(flags));assert host.p.stdout.readline().startswith('ERROR ')
 _,rows=host.query(off,'nihao');assert rows[0]=='你好',rows
finally:host.close()
print('PASS: native kaomoji, symbols, Unicode Emoji, independent options, native selection and double pinyin',flush=True)
options=ROOT/'build/session-options-test'
subprocess.run(['/usr/bin/clang++','-std=c++17','-Wall','-Wextra','-Werror',str(ROOT/'tests/session_options.cpp'),'-o',str(options)],check=True)
subprocess.run([str(options)],check=True)
rime=base/'rime';stage(rime/'user',rime/'runtime',copy_engine=False);(rime/'logs').mkdir()
user=rime/'user';(user/'default.custom.yaml').write_text('patch:\n  schema_list:\n    - schema: wetype\n')
patch=(ROOT/'examples/rime-extensions/expressions/wetype.custom.yaml').read_text()
# Obsolete/custom option bindings cannot reactivate Emoji Plus.
patch+='  wetype/emoji_plus_option: emoji_plus\n'
patch+='  engine/filters: [simplifier@traditional, uniquifier]\n  traditional:\n    option_name: traditionalization\n    opencc_config: s2t.json\n    excluded_types: [user_table]\n'
(user/'wetype.custom.yaml').write_text(patch)
(user/'kaomoji.txt').write_text('后(๑•̀ㅂ•́)و✧\tuukao\t100\n第二个颜文字\tuukao\t10\n')
(user/'symbol_emoji.txt').write_text('☆彡\tuusym\t100\n')
# Installation must not replace either personal table with a starter.
stage(user,rime/'runtime',copy_engine=False)
assert '后(๑•̀ㅂ•́)و✧' in (user/'kaomoji.txt').read_text()
assert '☆彡' in (user/'symbol_emoji.txt').read_text()
binary=ROOT/'build/rime-expressions-test'
subprocess.run(['/usr/bin/clang++','-std=c++17','-Wno-missing-field-initializers','-I'+str(DEFAULT_SOURCE/'src'),str(ROOT/'tests/rime_expressions.cpp'),str(SQUIRREL/'Frameworks/librime.1.dylib'),'-Wl,-rpath,'+str(SQUIRREL/'Frameworks'),'-o',str(binary)],check=True)
with (rime/'logs/acceptance.log').open('w') as log:
 result=subprocess.run([str(binary),str(user),str(rime/'logs')],stdout=log,stderr=subprocess.STDOUT,timeout=120)
print((rime/'logs/acceptance.log').read_text()[-2500:]);result.check_returncode()
gov=base/'rime-gov';stage(gov/'user',gov/'runtime',copy_engine=False);(gov/'logs').mkdir()
shutil.copy2(args.mainland_dir/'wetype_flypy_gov.schema.yaml',gov/'user/wetype_flypy_gov.schema.yaml')
shutil.copytree(args.mainland_dir/'opencc',gov/'user/opencc')
(gov/'user/default.custom.yaml').write_text('patch:\n  schema_list:\n    - schema: wetype_flypy_gov\n')
(gov/'user/wetype_flypy_gov.custom.yaml').write_text(patch)
(gov/'user/kaomoji.txt').write_text('后(๑•̀ㅂ•́)و✧\tuukao\t100\n第二个颜文字\tuukao\t10\n')
(gov/'user/symbol_emoji.txt').write_text('☆彡\tuusym\t100\n')
with (gov/'logs/acceptance.log').open('w') as log:
 result=subprocess.run([str(binary),str(gov/'user'),str(gov/'logs'),'wetype_flypy_gov'],stdout=log,stderr=subprocess.STDOUT,timeout=120)
print((gov/'logs/acceptance.log').read_text()[-2500:]);result.check_returncode()
compact(gov)
host=Host(rime/'runtime')
try:
 host.send('WORDS');assert host.read()=='WORDS 0','Rime expression table leaked into native shortcuts';assert host.read()=='END'
finally:host.close()
(base/'logs/acceptance.log').write_text('PASS: native local expressions, ABI isolation, official Rime tables, literal output, editing and live switches.\n')
compact(rime);compact(base);print('Local acceptance logs:',base)
