"""Build/deploy the official librime native component and isolated engine host."""
from pathlib import Path
import json
import hashlib
import os
import re
import shlex
import shutil
import subprocess
import tempfile
from datetime import datetime
from .macos import ROOT, DEFAULT_ENGINE, verify, dictionary_rows
from .modes import SCHEMAS, WUBI_DICTS, SAVE_OPTIONS_PATCH, native_header

SQUIRREL = Path('/Library/Input Methods/Squirrel.app/Contents')
DEFAULT_SOURCE = Path(os.environ.get('WETYPE_RIME_SOURCE', str(ROOT / '.local/librime')))
DEFAULT_BOOST_INCLUDE = Path(os.environ.get('WETYPE_BOOST_INCLUDE', '/opt/homebrew/include'))
DEFAULT_RUNTIME = Path.home() / 'Library/Application Support/WetypeRime'
DEFAULT_RIME = Path.home() / 'Library/Rime'


def build(source: Path = DEFAULT_SOURCE) -> None:
    headers = source / 'src'
    if not (headers / 'rime/translator.h').is_file():
        raise ValueError('local official librime source headers required; pass --rime-source')
    if not (DEFAULT_BOOST_INCLUDE / 'boost/signals2/connection.hpp').is_file():
        raise ValueError('local Boost headers required; set WETYPE_BOOST_INCLUDE to their include directory')
    out = ROOT / 'build'
    include = out / 'include/rime'
    include.mkdir(parents=True, exist_ok=True)
    (include / 'build_config.h').write_text('#pragma once\n')
    (out / 'include/wetype_modes.h').write_text(native_header())
    common = ['/usr/bin/clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I' + str(out / 'include')]
    subprocess.run(common + [str(ROOT / 'native/worker.cpp'), '-o', str(out / 'wetype-worker')], check=True)
    subprocess.run(common + ['-fobjc-arc','-fvisibility=hidden','-c',str(ROOT / 'native/surrounding_text.mm'),
                             '-o',str(out / 'surrounding_text.o')],check=True)
    subprocess.run(common + ['-Wno-unused-parameter', '-Wno-missing-field-initializers', '-fvisibility=hidden',
                            '-dynamiclib', '-undefined', 'dynamic_lookup', '-I' + str(out / 'include'),
                            '-I' + str(DEFAULT_BOOST_INCLUDE), '-isystem', str(headers), '-isystem', str(source / 'include'),
                            str(ROOT / 'native/rime_wetype.cpp'),
                            str(out / 'surrounding_text.o'),'-framework','InputMethodKit','-framework','AppKit',
                            '-o', str(out / 'librime-wetype.dylib')], check=True)

    subprocess.run(common + ['-Wno-missing-field-initializers', '-I' + str(headers),
                            str(ROOT / 'tools/deploy_rime.cpp'), str(SQUIRREL / 'Frameworks/librime.1.dylib'),
                            '-Wl,-rpath,' + str(SQUIRREL / 'Frameworks'), '-o', str(out / 'wetype-deploy')], check=True)


def atomic_copy(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.' + target.name, dir=target.parent)
    os.close(fd)
    try:
        shutil.copy2(source, temporary)
        os.replace(temporary, target)
    finally:
        Path(temporary).unlink(missing_ok=True)


def sync_configs(rime_dir: Path) -> Path | None:
    """Copy editable configuration verbatim, keeping runtime bindings separate."""
    source = ROOT / 'rime'
    required = [source / (schema + '.schema.yaml') for schema in SCHEMAS]
    for path in required:
        if not path.is_file():
            raise ValueError(f'missing scheme configuration: {path}')
    files = sorted([*source.glob('*.yaml'), *source.glob('*.txt'), *source.glob('lua/*.lua')])
    if any(path.name == 'wetype.runtime.yaml' for path in files):
        raise ValueError('wetype.runtime.yaml is reserved for installation-specific bindings')
    backup = None
    for path in files:
        relative = path.relative_to(source)
        target = rime_dir / relative
        # Optional dictionary starters become personal source files after the
        # first installation. Never replace a user's vocabulary on an upgrade.
        if path.name.endswith('.dict.yaml') and target.exists():
            continue
        if target.exists():
            if target.read_bytes() == path.read_bytes():
                continue
            if backup is None:
                parent = ROOT / '.local/backups'
                parent.mkdir(parents=True, exist_ok=True)
                backup = Path(tempfile.mkdtemp(prefix='config-', dir=parent))
            saved = backup / relative
            saved.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, saved)
        atomic_copy(path, target)
    return backup


def stage(rime_dir: Path, runtime: Path, engine: Path = DEFAULT_ENGINE, copy_engine: bool = True) -> None:
    rime_dir, runtime, engine = rime_dir.resolve(), runtime.resolve(), engine.resolve()
    manifest = verify(engine)
    runtime.mkdir(parents=True, exist_ok=True)
    for name in ('bin', 'state/logs', 'state/home', 'state/tmp', 'state/learning'):
        (runtime / name).mkdir(parents=True, exist_ok=True, mode=0o700)
    atomic_copy(ROOT / 'build/wetype-worker', runtime / 'bin/wetype-worker')
    module = ROOT / 'build/librime-wetype.dylib'
    # Pinned native factories may outlive a Lua state. A distinct path lets a
    # redeploy load the new image without restarting the system input method.
    module_name = 'librime-wetype-' + hashlib.sha256(module.read_bytes()).hexdigest()[:16] + '.dylib'
    atomic_copy(module, runtime / 'bin' / module_name)
    if copy_engine:
        target = runtime / 'engine'
        if target.exists():
            installed = verify(target)
            if (installed["source_sha256"] != manifest["source_sha256"]
                    or installed["files"] != manifest["files"]):
                raise ValueError("runtime contains a different engine; choose a new --runtime directory")
        else:
            shutil.copytree(engine, target, symlinks=True)
        engine = target
    atomic_copy(ROOT / 'native/offline.sb', runtime / 'offline.sb')
    rows = dictionary_rows(engine / 'imeData.bundle')
    rows = sorted((r for r in rows if not r.get('is_cell')), key=lambda r: r['type'] != 262144)
    state = runtime / 'state'
    q = shlex.quote
    launchers = {}
    # The vendor dictionary loader has one active Wubi dictionary per host.
    # Give each Wubi variant its own host/state; all pinyin modes share one.
    for group in ['pinyin', *WUBI_DICTS]:
        suffix = '' if group == 'pinyin' else '-' + group
        learning = state / 'learning' if group == 'pinyin' else state / group / 'learning'
        learning.mkdir(parents=True, exist_ok=True, mode=0o700)
        table = runtime / ('dicts' + suffix + '.tsv')
        chosen = [r for r in rows if r['type'] != 524291 or r['name'] == WUBI_DICTS.get(group)]
        if group != 'pinyin' and not any(r['name'] == WUBI_DICTS[group] for r in chosen):
            raise ValueError('missing native dictionary: ' + WUBI_DICTS[group])
        table.write_text(''.join(f"{engine / 'imeData.bundle' / r['name']}\t{r['type']}\t{r['version']}\n" for r in chosen))
        log = state / ('logs/engine' + suffix + '.log')
        launcher = '#!/bin/sh\nset -eu\n'
        launcher += 'unset DYLD_INSERT_LIBRARIES DYLD_LIBRARY_PATH DYLD_FRAMEWORK_PATH DYLD_FALLBACK_LIBRARY_PATH DYLD_FALLBACK_FRAMEWORK_PATH\n'
        launcher += f'export HOME={q(str(state / "home"))}\nexport CFFIXED_USER_HOME="$HOME"\nexport TMPDIR={q(str(state / "tmp") + "/")}\n'
        launcher += f'export WETYPE_ENGINE_LOG={q(str(log))}\n'
        launcher += f'export WETYPE_CRASH_LOG={q(str(state / "logs"))}/crash{suffix}-$$.log\n'
        launcher += f'exec /usr/bin/sandbox-exec -D {q("WORK=" + str(state))} -D {q("SOURCE=" + manifest["source_app"])} '
        launcher += f'-f {q(str(runtime / "offline.sb"))} {q(str(runtime / "bin/wetype-worker"))} '
        launcher += f'{q(str(engine / "engine.bundle"))} {q(str(learning))} {q(str(table))} {q(group)} '
        launcher += f'2>>{q(str(log))}\n'
        path = runtime / ('bin/start-engine' + suffix)
        path.write_text(launcher);path.chmod(0o755)
        launchers[group] = str(path)
    sync_configs(rime_dir)
    settings = {'module':str(runtime / 'bin' / module_name), 'launcher':launchers['pinyin'],
                'launchers':launchers, 'log_file':str(state / 'logs/rime.log')}
    # Machine-specific bindings are the only generated YAML. Schemes include
    # this node through Rime's native __include mechanism.
    (rime_dir / 'wetype.runtime.yaml').write_text(
        '# Generated interface paths; edit schemes in the project rime/ directory.\n'
        + json.dumps({'wetype': settings}, ensure_ascii=False, indent=2) + '\n')


def register_scheme(rime_dir: Path) -> Path | None:
    path = rime_dir / 'default.custom.yaml'
    original = path.read_text() if path.exists() else ''
    existing = set(re.findall(r'^\s*-\s*schema:\s*[\'\"]?([\w-]+)[\'\"]?\s*(?:#.*)?$', original, re.M))
    missing = [schema for schema in SCHEMAS if schema not in existing]
    save_options = not re.search(r'^\s{2}[\'\"]?switcher/save_options/\+[\'\"]?:',original,re.M)
    if not missing and not save_options:
        return None
    additions = ''.join('    - schema: ' + schema + '\n' for schema in missing)
    lines = original.splitlines(keepends=True)
    index = next((i for i, line in enumerate(lines) if re.fullmatch(r'\s{2}[\'\"]?schema_list(?:/\+)?[\'\"]?:\s*(?:#.*)?\n?', line)), None)
    if index is None:
        if not original:
            changed = 'patch:\n  "schema_list/+":\n' + additions
        elif re.search(r'^patch:\s*(?:#.*)?$', original, re.M):
            if 'schema_list' in original:
                raise ValueError('schema_list uses an unsupported format; add wetype to its list manually')
            changed = original.rstrip() + '\n  "schema_list/+":\n' + additions
        else:
            raise ValueError('default.custom.yaml has no supported patch section')
    else:
        # Append only missing entries; keep existing text, comments and options.
        end = index + 1
        while end < len(lines):
            if re.match(r'^  [^ #\s-]', lines[end]):
                break
            end += 1
        items = [i for i in range(index + 1, end) if re.match(r'^\s{4}-\s*schema:', lines[i])]
        at = items[-1] + 1 if items else index + 1
        if at and not lines[at-1].endswith('\n'):lines[at-1] += '\n'
        lines.insert(at, additions)
        changed = ''.join(lines)
    if save_options:changed += SAVE_OPTIONS_PATCH
    backup = None
    if path.exists():
        backups = ROOT / '.local/backups'
        backups.mkdir(parents=True, exist_ok=True)
        backup = backups / ('default.custom-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.yaml')
        backup.write_text(original)
    path.write_text(changed)
    return backup


def deploy(rime_dir: Path, runtime: Path, default_only: bool = False) -> None:
    log_dir = runtime.resolve() / 'state/logs'
    log_dir.mkdir(parents=True, exist_ok=True)
    with (log_dir / 'deploy.log').open('a') as output:
        command = [str(ROOT / 'build/wetype-deploy'), str(rime_dir.resolve()),
                   str(SQUIRREL / 'SharedSupport'), str(log_dir)]
        if default_only:
            command.append('--default-only')
        else:
            command.extend(SCHEMAS)
        subprocess.run(command,
                       stdout=output, stderr=subprocess.STDOUT, timeout=60, check=True)


def disable(rime_dir: Path, runtime: Path) -> Path | None:
    """Remove only our menu item; retain engine files and all learning data."""
    rime_dir = rime_dir.resolve()
    path = rime_dir / 'default.custom.yaml'
    original = path.read_text()
    item = r'^\s*-\s*schema:\s*[\'\"]?(?:' + '|'.join(map(re.escape,SCHEMAS)) + r')[\'\"]?\s*(?:#.*)?$'
    lines = original.splitlines(keepends=True)
    changed = ''.join(line for line in lines if not re.fullmatch(item, line.rstrip('\r\n'))).replace(SAVE_OPTIONS_PATCH,'')
    if changed == original:
        return None
    others = re.findall(r'^\s*-\s*schema:\s*[\'\"]?([\w-]+)[\'\"]?\s*(?:#.*)?$', changed, re.M)
    if not others:
        raise ValueError('cannot disable the only scheme; add another working scheme first')
    backups = ROOT / '.local/backups'
    backups.mkdir(parents=True, exist_ok=True)
    backup = backups / ('default.custom-before-disable-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.yaml')
    backup.write_text(original)
    user = rime_dir / 'user.yaml'
    old_user = user.read_bytes() if user.exists() else None
    path.write_text(changed)
    try:
        deploy(rime_dir, runtime, default_only=True)
        subprocess.run([str(SQUIRREL / 'MacOS/rime_deployer'), '--set-active-schema', others[0]],
                       cwd=rime_dir, check=True, capture_output=True, timeout=20)
    except Exception:
        path.write_text(original)
        if old_user is None:
            user.unlink(missing_ok=True)
        else:
            user.write_bytes(old_user)
        # Recompile the restored list; preserve the original failure if this also fails.
        try:
            deploy(rime_dir, runtime, default_only=True)
        except (OSError, subprocess.SubprocessError):
            pass
        raise
    return backup
