"""Validate real engine candidate coverage for incomplete, literal and typo input."""
from pathlib import Path
import argparse
import statistics
import subprocess
import sys
import time
import uuid
sys.path.insert(0,str(Path(__file__).resolve().parent.parent))
from wetype.integration import build,stage
from wetype.macos import ROOT
from fixture_cleanup import compact

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--keep-fixture',action='store_true',help='retain test runtime after success; failures are always retained')
args=parser.parse_args()

build()
base=ROOT/'.local'/('worker-acceptance-'+uuid.uuid4().hex[:8])
stage(base/'user',base/'runtime',copy_engine=False)
worker=subprocess.Popen([str(base/'runtime/bin/start-engine')],stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE,text=True,bufsize=1)
def read():
    line=worker.stdout.readline().strip()
    if not line:raise RuntimeError('worker exited')
    if line.startswith('ERROR '):raise RuntimeError(bytes.fromhex(line[6:]).decode())
    return line
def send(command):
    worker.stdin.write(command+'\n');worker.stdin.flush()
times=[];deletions=[]
try:
    assert read()=='READY'
    send('OPEN');session=read().split()[1]
    samples=list('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ')+[
        'nihao','hello','english','xian',"xi'an",'zhongguo','shuru','ceshi','wo',
        'women','hao','a',"a'",'x','lv','lve','nihaozhongguo',
        'abcdefghijklmnopqrstuvwxyz','nn','ng','xx','sdf','i','u','v','zhege',
        'beijing','keyi','jintian','ninhao','nih','nh','test','rime','wetype','codex',
        'ABC','Codex','OpenAI','iPhone','NiHao','NIHAO',"Xi'An"]
    for sample in samples:
        send('RESET '+session);assert read()=='OK'
        for length in range(1,len(sample)+1):
            text=sample[:length];started=time.monotonic()
            send('QUERY '+session+' '+text.encode().hex())
            tag,ticket,count=read().split();assert tag=='RESULT'
            assert int(count)>0,(sample,text,'no engine candidates')
            for index in range(int(count)):
                tag,position,cover,payload,preedit=read().split()
                assert tag=='C' and int(position)==index
                assert 0<int(cover)<=len(text),(text,cover,bytes.fromhex(payload).decode())
                assert bytes.fromhex(payload).decode()
                display='' if preedit=='-' else bytes.fromhex(preedit).decode()
                if display:assert display.replace("'",'')==text[:int(cover)].replace("'",''),(text,cover,display)
                if index==0 and length==len(sample):
                    expected={'nihao':"ni'hao",'nihaozhongguo':"ni'hao'zhong'guo",'xian':'xian',"xi'an":"xi'an",'beijin':"bei'jin",'codex':'codex','nh':"n'h"}.get(sample)
                    if expected is not None:assert display==expected,(sample,display,expected)
            assert read()=='END'
            times.append((time.monotonic()-started)*1000)
    text='nihaozhongguowoxihuanzhongwen'
    send('RESET '+session);assert read()=='OK'
    for length in range(len(text),0,-1):
        current=text[:length];started=time.monotonic()
        send('QUERY '+session+' '+current.encode().hex())
        tag,ticket,count=read().split();assert tag=='RESULT'
        for index in range(int(count)):
            tag,position,cover,payload,preedit=read().split()
            assert tag=='C' and int(position)==index and 0<int(cover)<=len(current)
            display='' if preedit=='-' else bytes.fromhex(preedit).decode()
            assert display and display.replace("'",'')==current[:int(cover)].replace("'",''),(current,display)
        assert read()=='END'
        if length<len(text):deletions.append((time.monotonic()-started)*1000)
    send('QUIT');worker.wait(timeout=10);assert worker.returncode==0
    summary=f'PASS: {len(times)} per-prefix engine queries; median {statistics.median(times):.1f} ms, max {max(times):.1f} ms\n'
    summary+=f'PASS: {len(deletions)} consecutive deletions; median {statistics.median(deletions):.1f} ms, max {max(deletions):.1f} ms\n'
    (base/'logs').mkdir(exist_ok=True);(base/'logs/acceptance.log').write_text(summary)
    print(summary,end='')
    compact(base,args.keep_fixture)
    print('Local logs:',base/'logs')
finally:
    if worker.poll() is None:
        worker.terminate()
        try:worker.wait(timeout=5)
        except subprocess.TimeoutExpired:worker.kill();worker.wait()
