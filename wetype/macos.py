"""Prepare and probe a local Mac engine, without network access or a VM."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import struct
import subprocess
import tempfile
from datetime import datetime
import uuid

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_ENGINE = ROOT / '.local/engines/macos-3.0.1-680'
SOURCE_SHA256 = '54e4aa54cd5c125455bdd667bbc2f5d762b73d85064826755e7f1d56a9dbb866'
ENGINE_VERSION = '0.45.0.weekly_release/2026.09.23.b2f7520b2'



def sha256(path: Path) -> str:
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def convert_image(data: bytes) -> bytes:
    """Turn an arm64 executable's copy into a loadable Mach-O bundle.

    Keep the empty PAGEZERO load command so compressed rebase/bind segment
    indices stay unchanged. Only its reservation is reduced to zero. Code,
    engine algorithms and model files are not patched.
    """
    if len(data) < 32:
        raise ValueError('truncated Mach-O header')
    magic, cpu, _, filetype, ncmds, size, flags, _ = struct.unpack_from('<8I', data)
    if (magic, cpu, filetype) != (0xFEEDFACF, 0x100000C, 2):
        raise ValueError('expected a thin arm64 Mach-O executable')
    if 32 + size > len(data):
        raise ValueError('truncated load commands')
    out = bytearray(data)
    commands = []
    offset = 32
    pagezero = False
    for _ in range(ncmds):
        if offset + 8 > 32 + size:
            raise ValueError('truncated load command')
        cmd, length = struct.unpack_from('<II', data, offset)
        if length < 8 or length % 8 or offset + length > 32 + size:
            raise ValueError('invalid load command length')
        c = bytearray(data[offset:offset + length])
        if cmd == 0x19:
            if length < 72:
                raise ValueError('truncated segment')
            if c[8:24].rstrip(b'\0') == b'__PAGEZERO':
                if struct.unpack_from('<Q', c, 24)[0] != 0 or struct.unpack_from('<Q', c, 48)[0] != 0:
                    raise ValueError('unexpected PAGEZERO layout')
                struct.pack_into('<Q', c, 32, 0)
                pagezero = True
        # The caller provides main; the copied signature is regenerated locally.
        if cmd not in (0x80000028, 0xE, 0x1D):
            commands.append(c)
        offset += length
    if offset != 32 + size or not pagezero:
        raise ValueError('unsupported load command layout')
    joined = b''.join(commands)
    struct.pack_into('<I', out, 12, 8)  # MH_BUNDLE
    struct.pack_into('<II', out, 16, len(commands), len(joined))
    struct.pack_into('<I', out, 24, flags & ~0x200000)  # MH_PIE
    out[32:32 + size] = joined + bytes(size - len(joined))
    return bytes(out)


def _host_check() -> None:
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise ValueError('this native probe currently requires Apple Silicon macOS')


def dictionary_rows(resources: Path) -> list[dict]:
    """Validate the loader contract and all indexed files before copying/loading."""
    entries = json.loads((resources / 'index.json').read_text())
    if not isinstance(entries, list) or not entries:
        raise ValueError('dictionary index must be a nonempty list')
    seen = set()
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError('invalid dictionary index entry')
        name = entry.get('name')
        if (not isinstance(name, str) or not name or Path(name).is_absolute()
                or '..' in Path(name).parts or any(c in name for c in '\t\r\n\0')):
            raise ValueError(f'unsafe dictionary name: {name}')
        if name in seen:
            raise ValueError(f'duplicate dictionary name: {name}')
        seen.add(name)
        path = resources / name
        if not path.resolve().is_relative_to(resources.resolve()) or not path.is_file():
            raise ValueError(f'dictionary missing or escapes resources: {name}')
        for field in ('type', 'version'):
            value = entry.get(field)
            if type(value) is not int or not 0 <= value <= 0xffffffff:
                raise ValueError(f'invalid dictionary {field}: {name}')
        for algorithm in ('md5', 'sha256'):
            expected = entry.get(algorithm)
            if expected is not None:
                with path.open('rb') as stream:
                    digest = hashlib.file_digest(stream, algorithm).hexdigest()
                if not isinstance(expected, str) or digest != expected.lower():
                    raise ValueError(f'dictionary checksum differs: {name}')
    return entries


def discover(app: Path, resources: Path | None = None) -> dict:
    """Inspect local metadata without loading vendor code or pinning a version."""
    app = app.resolve()
    with (app / 'Contents/Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    name = info.get('CFBundleExecutable')
    if not isinstance(name, str) or Path(name).name != name or name in ('', '.', '..'):
        raise ValueError('invalid CFBundleExecutable')
    source = app / 'Contents/MacOS' / name
    if not source.resolve().is_relative_to(app) or not source.is_file():
        raise ValueError('application executable missing or escapes app')
    if resources is None:
        candidates = []
        for index in sorted((app / 'Contents').rglob('index.json')):
            try:
                dictionary_rows(index.parent)
            except (ValueError, OSError, KeyError, TypeError):
                continue
            candidates.append(index.parent)
        if len(candidates) != 1:
            raise ValueError(f'found {len(candidates)} dictionary indexes; specify --resources explicitly')
        resources = candidates[0]
    resources = resources.resolve()
    rows = dictionary_rows(resources)
    digest = sha256(source)
    return {'source_app': str(app), 'source_executable': str(source),
            'source_resources': str(resources), 'source_sha256': digest,
            'app_version': str(info.get('CFBundleShortVersionString', 'unknown')),
            'build': str(info.get('CFBundleVersion', 'unknown')),
            'engine_version': ENGINE_VERSION if digest == SOURCE_SHA256 else 'unknown',
            'compatibility': 'validated' if digest == SOURCE_SHA256 else 'unvalidated',
            'dictionary_count': len(rows)}


def prepare(app: Path, output: Path, resources: Path | None = None) -> Path:
    _host_check()
    app, output = app.resolve(), output.resolve()
    if output.is_relative_to(app):
        raise ValueError('output must be independent of the source app')
    metadata = discover(app, resources)
    source = Path(metadata['source_executable'])
    if output.exists():
        raise ValueError(f'output already exists; verify it or choose a new directory: {output}')
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix='.prepare-', dir=output.parent))
    try:
        image = staging / 'engine.bundle'
        subprocess.run(['/usr/bin/lipo', str(source), '-thin', 'arm64', '-output', str(image)], check=True, timeout=60)
        image.write_bytes(convert_image(image.read_bytes()))
        frameworks = staging / 'Frameworks'
        frameworks.mkdir()
        # Preserve the full dependency tree rather than a release-specific list.
        source_frameworks = app / 'Contents/Frameworks'
        if source_frameworks.is_dir():
            shutil.copytree(source_frameworks, frameworks, symlinks=True, dirs_exist_ok=True)
        resources = staging / 'imeData.bundle'
        shutil.copytree(Path(metadata['source_resources']), resources)
        dictionary_rows(resources)
        subprocess.run(['/usr/bin/codesign', '--force', '--sign', '-', str(image)], check=True, timeout=60)
        files = {}
        for p in sorted(staging.rglob('*')):
            if p.is_symlink():
                if not p.resolve().is_relative_to(staging.resolve()):
                    raise ValueError(f'external framework symlink: {p}')
                files[p.relative_to(staging).as_posix()] = {'symlink': os.readlink(p)}
            elif p.is_file():
                files[p.relative_to(staging).as_posix()] = {'sha256': sha256(p)}
        manifest = dict(metadata, format=2, files=files)
        (staging / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        staging.rename(output)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return output


def verify(engine: Path) -> dict:
    engine = engine.resolve()
    manifest = json.loads((engine / 'manifest.json').read_text())
    if (manifest.get('format') not in (1, 2)
            or not re.fullmatch(r'[0-9a-f]{64}', str(manifest.get('source_sha256', '')))):
        raise ValueError('unrecognized engine manifest')
    files = manifest.get('files', {})
    if 'engine.bundle' not in files or 'imeData.bundle/index.json' not in files:
        raise ValueError('incomplete engine manifest')
    for name, record in files.items():
        relative = Path(name)
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError(f'unsafe manifest path: {name}')
        p = engine / relative
        if not p.resolve().is_relative_to(engine):
            raise ValueError(f'resource escapes engine directory: {name}')
        if 'symlink' in record:
            if not p.is_symlink() or os.readlink(p) != record['symlink']:
                raise ValueError(f'framework link changed: {name}')
        elif not p.is_file() or sha256(p) != record['sha256']:
            raise ValueError(f'file missing or changed: {name}')
    return manifest


def probe(engine: Path, input_text: str, state: str, select: str | None = None, emoji: bool = False) -> dict:
    _host_check()
    if not re.fullmatch(r"[a-z']{1,128}", input_text):
        raise ValueError('probe input must contain 1–128 lowercase pinyin letters or apostrophes')
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', state):
        raise ValueError('state name must contain letters, numbers, underscores or hyphens')
    if select and (len(select.encode()) > 4095 or any(c in select for c in '\r\n\0')):
        raise ValueError('invalid selection text')
    engine = engine.resolve()
    manifest = verify(engine)
    run = ROOT / '.local/runs' / state
    logs = run / 'logs'
    home, temp = logs / 'home', logs / 'tmp'
    learning = run / 'learning'
    for p in (logs, home, temp, learning):
        p.mkdir(parents=True, exist_ok=True, mode=0o700)
    rows = dictionary_rows(engine / 'imeData.bundle')
    rows = sorted((r for r in rows if not r.get('is_cell')), key=lambda r: r['type'] != 262144)
    table = run / 'dicts.tsv'
    table.write_text(''.join(f"{engine / 'imeData.bundle' / r['name']}\t{r['type']}\t{r['version']}\n" for r in rows))
    build = ROOT / 'build'
    build.mkdir(exist_ok=True)
    binary = build / 'wetype-probe'
    subprocess.run(['/usr/bin/clang', '-Wall', '-Wextra', '-Werror', str(ROOT / 'native/probe.c'), '-o', str(binary)], check=True)
    stamp = datetime.now().astimezone().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6]
    engine_log, crash_log = logs / f'engine-{stamp}.log', logs / f'crash-{stamp}.log'
    env = dict(os.environ)
    # Prevent inherited test selections and dyld injection from altering the run.
    for name in list(env):
        if name.startswith(('DYLD_', 'WETYPE_')):
            env.pop(name)
    env.update(HOME=str(home), CFFIXED_USER_HOME=str(home), TMPDIR=str(temp) + '/',
               WETYPE_CRASH_LOG=str(crash_log))
    if select:
        env['WETYPE_SELECT'] = select
    if emoji:
        env['WETYPE_EMOJI'] = '1'
    command = ['/usr/bin/sandbox-exec', '-D', f'WORK={run}', '-D', f'SOURCE={manifest["source_app"]}',
               '-f', str(ROOT / 'native/offline.sb'), str(binary),
               str(engine / 'engine.bundle'), str(learning), str(table), input_text]
    try:
        with engine_log.open('w') as output:
            result = subprocess.run(command, cwd=run, env=env, stdout=output,
                                    stderr=subprocess.STDOUT, timeout=30)
    except subprocess.TimeoutExpired:
        with crash_log.open('a') as output:
            output.write('probe timed out after 30 seconds\n')
        raise RuntimeError(f'probe timed out; see {engine_log}') from None
    text = engine_log.read_text(errors='replace')
    if result.returncode:
        with crash_log.open('a') as output:
            output.write(f'process exit status: {result.returncode}\n')
        raise RuntimeError(f'probe failed ({result.returncode}); see {engine_log} and {crash_log}')
    candidates = []
    batches = []
    selected = None
    for line in text.splitlines():
        match = re.fullmatch(r'candidate (\d+): (.*)', line)
        if match:
            if match[1] == '1':
                candidates = []
                batches.append(candidates)
            candidates.append(match[2])
        if line.startswith('selected: '):
            selected = line.removeprefix('selected: ')
    version = next((line.removeprefix('engine version: ') for line in text.splitlines()
                    if line.startswith('engine version: ')), manifest['engine_version'])
    return {'engine_version': version, 'input': input_text,
            'candidates': candidates, 'batches': batches, 'selected': selected,
            'engine_log': str(engine_log), 'crash_log': str(crash_log), 'learning': str(learning)}
