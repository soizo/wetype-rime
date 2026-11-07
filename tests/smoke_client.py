"""Exercise pipe framing with a malformed response; no vendor engine or Rime data."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
(root/'work').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='client-frames-',dir=root/'work') as temporary:
    base=Path(temporary)
    fixture=base/'frame-fixture'
    fixture.write_text('''#!/usr/bin/env python3
import sys
print('READY',flush=True)
query=0
for line in sys.stdin:
    op=line.split()[0]
    if op=='QUIT':break
    if op=='OPEN':print('OPENED 1',flush=True)
    elif op=='QUERY':
        query+=1
        if query==1:print('RESULT 1 2\\nC 0 4 616263 616263\\nC 1 3 616263 616263\\nEND',flush=True)
        else:print('RESULT 2 1\\nC 0 5 e4bda0e5a5bd 6e692768616f\\nEND',flush=True)
    else:print('OK',flush=True)
''')
    fixture.chmod(0o700)
    binary=base/'client-frames'
    subprocess.run(['/usr/bin/clang++','-std=c++17','-Wall','-Wextra','-Werror',
                    str(root/'tests/client_frames.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary),str(fixture)],check=True,timeout=15)
