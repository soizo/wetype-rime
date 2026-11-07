from __future__ import annotations
import json
import argparse
from pathlib import Path
import sys
import subprocess
from .macos import DEFAULT_ENGINE, prepare, probe, verify, discover


def main() -> int:
    parser = argparse.ArgumentParser(description='WetypeRime local engine integration tools (no network access)')
    from . import __version__
    parser.add_argument('--version', action='version', version='wetype-rime ' + __version__)
    commands = parser.add_subparsers(dest='command', required=True)
    p = commands.add_parser('prepare-macos', help='discover and copy a local Mac engine and dictionaries')
    p.add_argument('app', type=Path)
    p.add_argument('--output', type=Path, default=DEFAULT_ENGINE)
    p.add_argument('--resources', type=Path, help='dictionary directory containing index.json')
    p = commands.add_parser('inspect-macos', help='inspect versions and discover dictionaries without loading code')
    p.add_argument('app', type=Path)
    p.add_argument('--resources', type=Path)
    p = commands.add_parser('verify', help='verify independent engine files against their local manifest')
    p.add_argument('engine', type=Path, nargs='?', default=DEFAULT_ENGINE)
    p = commands.add_parser('probe', help='run the native candidate tester with networking denied')
    p.add_argument('input', help='lowercase pinyin, e.g. nihao')
    p.add_argument('--engine', type=Path, default=DEFAULT_ENGINE)
    p.add_argument('--state', default='smoke', help='independent local learning state name')
    p.add_argument('--select', help='select matching candidate text and feed it back to the engine')
    p.add_argument('--emoji', action='store_true', help='enable native local Unicode Emoji recommendations')
    from .integration import DEFAULT_RIME, DEFAULT_RUNTIME, DEFAULT_SOURCE
    p = commands.add_parser('sync-config', help='copy project rime/ configuration without rebuilding or copying the engine')
    p.add_argument('--rime-dir', type=Path, default=DEFAULT_RIME)
    p = commands.add_parser('install-rime', help='install the official native Rime component and isolated engine host')
    p.add_argument('--rime-dir', type=Path, default=DEFAULT_RIME)
    p.add_argument('--runtime', type=Path, default=DEFAULT_RUNTIME)
    p.add_argument('--rime-source', type=Path, default=DEFAULT_SOURCE)
    p.add_argument('--engine', type=Path, default=DEFAULT_ENGINE)
    p = commands.add_parser('disable-rime', help='disable wetype and restore the first original scheme; keep learning data')
    p.add_argument('--rime-dir', type=Path, default=DEFAULT_RIME)
    p.add_argument('--runtime', type=Path, default=DEFAULT_RUNTIME)
    args = parser.parse_args()
    try:
        if args.command == 'install-rime':
            from .integration import build, stage, register_scheme, deploy
            build(args.rime_source)
            stage(args.rime_dir, args.runtime, args.engine)
            backup = register_scheme(args.rime_dir)
            deploy(args.rime_dir, args.runtime)
            print(f'已安装 wetype 到：{args.rime_dir}')
            print(f'独立引擎与学习数据：{args.runtime}')
            if backup:
                print(f'原配置备份：{backup}')
        elif args.command == 'sync-config':
            from .integration import sync_configs
            backup = sync_configs(args.rime_dir)
            print('已同步 WeTypeRime 配置，请在鼠须管菜单中重新部署。')
            if backup:
                print(f'原配置备份：{backup}')
        elif args.command == 'disable-rime':
            from .integration import disable
            backup = disable(args.rime_dir, args.runtime)
            print('已停用 wetype，保留独立引擎与学习数据。请在鼠须管菜单中重新部署。')
            if backup:
                print(f'停用前配置备份：{backup}')
        elif args.command == 'prepare-macos':
            print(prepare(args.app, args.output, args.resources))
        elif args.command == 'inspect-macos':
            print(json.dumps(discover(args.app, args.resources), ensure_ascii=False, indent=2))
        elif args.command == 'verify':
            manifest = verify(args.engine)
            print(f'OK: {manifest["app_version"]} ({manifest["build"]}), {len(manifest["files"])} files/links')
        else:
            r = probe(args.engine, args.input, args.state, args.select, args.emoji)
            print(f'引擎：{r["engine_version"]}')
            for n, text in enumerate(r['candidates'], 1):
                print(f'{n}. {text}')
            if r['selected']:
                print(f'已反馈选词：{r["selected"]}')
            print(f'运行日志：{r["engine_log"]}')
            print(f'崩溃日志：{r["crash_log"]}')
            print(f'本地学习：{r["learning"]}')
    except (ValueError, OSError, RuntimeError, subprocess.SubprocessError) as e:
        print(f'wetype: {e}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
