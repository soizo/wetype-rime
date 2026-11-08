"""Check real Rime spelling algebra without WeType resources or a live profile."""
from pathlib import Path
import argparse
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from wetype.integration import ROOT, DEFAULT_SOURCE, DEFAULT_BOOST_INCLUDE, SQUIRREL

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--rime-source', type=Path, default=DEFAULT_SOURCE)
args = parser.parse_args()
binary = ROOT / 'build/rime-fuzzy'
binary.parent.mkdir(exist_ok=True)
subprocess.run(['/usr/bin/clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-parameter', '-I' + str(args.rime_source / 'src'),
                '-I' + str(ROOT / 'build/include'), '-I' + str(DEFAULT_BOOST_INCLUDE),
                str(ROOT / 'tests/rime_fuzzy.cpp'), str(SQUIRREL / 'Frameworks/librime.1.dylib'),
                '-Wl,-rpath,' + str(SQUIRREL / 'Frameworks'), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True, timeout=15)
