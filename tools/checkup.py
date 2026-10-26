"""Repo checkup: where the space and files are, what is stale, and a safe slimming of regenerable output.

  python tools/checkup.py                        report: areas, .cache by owner, heavy file types, stale dirs
  python tools/checkup.py --slim [--hours 12] [--dry-run]
      delete regenerable game output under .cache older than --hours: replays (.repl26a), bundles (.zip,
      .gz), engine dumps (.bin), per-game statistics JSON over 256 KB, game stdout/fuel streams. Notes,
      scripts, experiment spec/summary metadata, game rows (.jsonl), small JSON, toolchains and binaries
      stay, and so does everything in a directory holding a `.keep` file and in .cache/arena/snap
      (arena prune handles snapshots).

Run it at every checkup (with `arena prune --days 2 --snap-days 2`). Use the runtime Python (`arena env`).
"""
from __future__ import annotations

import argparse
import os
import stat
import time
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / '.cache'
BULK = {'.repl26a', '.zip', '.gz', '.bin', '.stdout', '.fuel', '.opaque'}
BIG_JSON = 256 * 1024
SKIP_DIRS = {CACHE / 'arena' / 'snap'}


def is_link(e: os.DirEntry) -> bool:
    """Symlinks and Windows junctions (snapshots link sdk/ with a junction): never walked into, never
    deleted through - a junction in .cache can point at the repository's own sdk/, maps/ or results/."""
    try:
        st = e.stat(follow_symlinks=False)
    except OSError:
        return True
    return e.is_symlink() or bool(getattr(st, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT)


def protected_directory(path: Path) -> bool:
    """Respect protected ancestors even if a .keep appeared after traversal began."""
    for directory in (path, *path.parents):
        try:
            if (directory in SKIP_DIRS or directory.name.lower() in ('fixture', 'fixtures') or
                    (directory / '.keep').exists()):
                return True
        except OSError:
            return True  # uncertainty about protection is not permission to remove contents
        if directory == CACHE:
            break
    return False


def experiment_metadata(path: Path) -> bool:
    try:
        parts = path.relative_to(CACHE).parts
    except ValueError:
        return False
    return len(parts) == 4 and parts[1].lower() == 'exp' and parts[3].lower() in ('spec.json', 'summary.json', 'progress.json')


def scan(path: Path):
    """Yield the DirEntry of every regular file under path, skipping .keep subtrees and links."""
    stack = [path]
    while stack:
        p = stack.pop()
        if protected_directory(Path(p)):
            continue
        try:
            entries = list(os.scandir(p))
        except OSError:
            continue
        if any(e.name == '.keep' for e in entries) or Path(p).name.lower() in ('fixture', 'fixtures'):
            continue  # protected: .keep directories and saved test fixtures
        for e in entries:
            try:
                if is_link(e):
                    continue
                if e.is_dir(follow_symlinks=False):
                    if Path(e.path) not in SKIP_DIRS:
                        stack.append(e.path)
                elif e.is_file(follow_symlinks=False):
                    yield e
            except OSError:
                pass


def size_of(path: Path) -> tuple[int, int, float]:
    size = files = 0
    newest = 0.0
    stack = [str(path)]
    while stack:
        p = stack.pop()
        try:
            with os.scandir(p) as it:
                for e in it:
                    try:
                        if is_link(e):
                            continue
                        if e.is_dir(follow_symlinks=False):
                            stack.append(e.path)
                        elif e.is_file(follow_symlinks=False):
                            st = e.stat(follow_symlinks=False)
                            size, files, newest = size + st.st_size, files + 1, max(newest, st.st_mtime)
                    except OSError:
                        pass
        except OSError:
            pass
    return size, files, newest


def report(stale_hours: float) -> None:
    now = time.time()
    total = size_of(ROOT)
    print(f'repo {total[0] / 1e9:.1f} GB in {total[1]} files')
    for area in sorted(p for p in ROOT.iterdir() if p.is_dir() and p.name not in ('.cache',)):
        s, f, _ = size_of(area)
        if s > 50e6 or f > 5000:
            print(f'  {area.name:20s} {s / 1e9:7.2f} GB {f:8d} files')
    print('.cache by owner (dirs over 0.5 GB or 20K files; stale = untouched for more than '
          f'{stale_hours:g} h):')
    for owner in sorted(p for p in CACHE.iterdir() if p.is_dir()):
        s, f, n = size_of(owner)
        if s < 0.5e9 and f < 20000:
            continue
        print(f'  {owner.name:20s} {s / 1e9:7.2f} GB {f:8d} files  newest {(now - n) / 3600:6.1f} h')
        for sub in sorted((p for p in owner.iterdir() if p.is_dir()), key=lambda p: p.name):
            ss, sf, sn = size_of(sub)
            if ss > 0.5e9 and (now - sn) / 3600 > stale_hours:
                print(f'      stale {sub.name:34s} {ss / 1e9:6.2f} GB {sf:7d} files  {(now - sn) / 3600:6.1f} h')
    by_ext: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    for e in scan(CACHE):
        ext = os.path.splitext(e.name.lower())[1] or e.name.lower()
        try:
            by_ext[ext][0] += e.stat(follow_symlinks=False).st_size
            by_ext[ext][1] += 1
        except OSError:
            pass
    print('.cache file types outside arena snapshots (top 8):')
    for ext, (s, f) in sorted(by_ext.items(), key=lambda kv: -kv[1][0])[:8]:
        print(f'  {ext:14s} {s / 1e9:7.2f} GB {f:8d} files')


def slim(hours: float, dry_run: bool) -> None:
    cutoff = time.time() - hours * 3600
    freed = removed = 0
    per_owner: dict[str, int] = defaultdict(int)
    for e in scan(CACHE):
        name = e.name.lower()
        ext = os.path.splitext(name)[1]
        try:
            st = e.stat(follow_symlinks=False)
        except OSError:
            continue
        if st.st_mtime > cutoff:
            continue
        if experiment_metadata(Path(e.path)):
            continue  # recipes and retained summaries are not regenerable per-game statistics
        bulky = ext in BULK or (ext == '.json' and st.st_size > BIG_JSON)
        if not bulky:
            continue
        # DirEntry.stat() may be cached from discovery. Another owner can refresh a
        # file or protect its directory while this scan is in progress.
        path = Path(e.path)
        if protected_directory(path.parent):
            continue
        try:
            current = path.stat(follow_symlinks=False)
        except OSError:
            continue
        if (not stat.S_ISREG(current.st_mode) or
                bool(getattr(current, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT) or
                current.st_mtime > cutoff or
                (current.st_size, current.st_mtime_ns) != (st.st_size, st.st_mtime_ns)):
            continue
        if not dry_run:
            try:
                os.remove(e.path)
            except OSError:
                continue
        freed += st.st_size
        removed += 1
        per_owner[Path(e.path).relative_to(CACHE).parts[0]] += st.st_size
    verb = 'would free' if dry_run else 'freed'
    for owner, s in sorted(per_owner.items(), key=lambda kv: -kv[1]):
        print(f'  {owner:20s} {s / 1e9:7.2f} GB')
    print(f'{verb} {freed / 1e9:.2f} GB in {removed} files older than {hours:g} h')
    if not dry_run:
        # Empty directories, bottom-up, found without entering links (os.walk would enter junctions).
        dirs: list[str] = []
        stack = [str(CACHE)]
        while stack:
            p = stack.pop()
            if protected_directory(Path(p)):
                continue
            try:
                entries = list(os.scandir(p))
            except OSError:
                continue
            for e in entries:
                try:
                    if not is_link(e) and e.is_dir(follow_symlinks=False) and Path(e.path) not in SKIP_DIRS:
                        dirs.append(e.path)
                        stack.append(e.path)
                except OSError:
                    pass
        empties = 0
        for d in sorted(dirs, key=len, reverse=True):
            try:
                path = Path(d)
                if os.listdir(path) or protected_directory(path):
                    continue
                current = path.stat(follow_symlinks=False)
                if (current.st_mtime > cutoff or stat.S_ISLNK(current.st_mode) or
                        bool(getattr(current, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT)):
                    continue
                os.rmdir(path)
                empties += 1
            except OSError:
                pass
        print(f'removed {empties} empty directories')


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--slim', action='store_true')
    p.add_argument('--hours', type=float, default=12.0)
    p.add_argument('--dry-run', action='store_true')
    p.add_argument('--stale-hours', type=float, default=24.0)
    a = p.parse_args()
    if a.slim:
        slim(a.hours, a.dry_run)
    else:
        report(a.stale_hours)


if __name__ == '__main__':
    main()
