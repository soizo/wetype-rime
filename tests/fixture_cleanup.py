"""Keep compact diagnostics after a successful isolated native test."""
from pathlib import Path
import shutil


def compact(base: Path, keep: bool = False) -> None:
    if keep:
        return
    logs = base / 'logs'
    logs.mkdir(exist_ok=True)
    for source in (base / 'runtime/state/logs').glob('*.log'):
        if not source.is_file() or source.is_symlink():
            continue
        with source.open('rb') as stream:
            stream.seek(max(0, source.stat().st_size - 256 * 1024))
            (logs / (source.stem + '-tail.log')).write_bytes(stream.read())
    for name in ('user', 'runtime'):
        target = base / name
        if target.is_dir() and not target.is_symlink():
            shutil.rmtree(target)
