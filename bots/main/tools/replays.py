"""Our side's logs from local replays and online downloads (shared by the bots/main log analyzers).

PATH arguments name replay bundles (*.repl26a), online match zips (results/online/ranked/<id>/
match.zip), online group zips (results/online/<version>/<group>/group.zip, a zip of match zips) or
directories, searched recursively for all three. Online downloads carry only our team's logs.
Which side of a local replay is ours:
  --side a|b   that side in every local replay;
  --bot SEL    the side SEL (<suite>/<bot>, a bare bot name or a glob) played, read from run-batch
               manifests (*.txt rows "bot-a bot-b map replay [--statistics path]") in the directory;
  otherwise    every side whose stdout carries bots/main log lines. Against another main-family bot
               (the usual main sweeps) both sides do and both are counted, with a note.
Each Game has the source, our side, that side's stdout, the statistics (manifest --statistics path or
the same-stem .json beside a replay; <match-id>-summary.json or summary.json beside online zips) and
the map name recovered from the statistics' map hash, when available.
"""
from __future__ import annotations

import argparse
import fnmatch
import hashlib
import io
import json
import re
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

ROOT = Path(__file__).resolve().parents[3]
STDOUT = re.compile(r"team-([ab])\.stdout")
MAIN_LOG = re.compile(rb"^(?:t r\d+ k\d+ pvp|i r\d+ k\d+ fuel|death r\d+ k\d+|T r\d+ p\d|P r\d+ k\d+ p|Q \d+ \d+ )", re.M)
_maps: dict[str, str] | None = None


@dataclass
class Game:
    source: str
    side: str            # "a" or "b"
    text: str            # our side's stdout
    stats: dict | None = None
    map: str | None = None

    @property
    def team(self) -> str:
        return "teamA" if self.side == "a" else "teamB"


def add_arguments(p: argparse.ArgumentParser) -> None:
    p.add_argument("paths", nargs="+", help="replays, online match/group zips or directories")
    p.add_argument("--side", choices=["a", "b"], help="our side in every local replay")
    p.add_argument("--bot", help="our bot, matched against run-batch manifests beside the replays")


def map_name(stats: dict | None) -> str | None:
    """Map name (maps/ relative stem, e.g. 00corners or legacy/corners) from a statistics map hash."""
    global _maps
    digest = ((stats or {}).get("match") or {}).get("mapHash")
    if not digest:
        return None
    if _maps is None:
        _maps = {}
        for p in sorted((ROOT / "maps").rglob("*.map26a")):
            _maps.setdefault(hashlib.sha256(p.read_bytes()).hexdigest(), p.relative_to(ROOT / "maps").with_suffix("").as_posix())
    return _maps.get(digest, f"map-{digest[:8]}")


def _load(path: Path | None) -> dict | None:
    try:
        return json.loads(path.read_text(encoding="utf-8")) if path and path.is_file() else None
    except (OSError, json.JSONDecodeError):
        return None


def _key(path: str | Path) -> str:
    p = Path(path)
    return str((p if p.is_absolute() else ROOT / p).resolve()).lower()


def _bot_id(project: str) -> str:
    parts = Path(project).parts
    for i in range(len(parts) - 3, -1, -1):
        if parts[i].lower() == "bots":
            return f"{parts[i + 1]}/{parts[i + 2]}"
    return Path(project).name


def _manifests(directory: Path) -> dict[str, tuple[str, str, Path | None]]:
    rows: dict[str, tuple[str, str, Path | None]] = {}
    for f in sorted(directory.rglob("*.txt")):
        try:
            text = f.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        for line in text.splitlines():
            parts = line.split()
            if len(parts) < 4 or not parts[3].endswith(".repl26a"):
                continue
            stats = parts[parts.index("--statistics") + 1] if "--statistics" in parts[4:-1] else None
            rows[_key(parts[3])] = (_bot_id(parts[0]), _bot_id(parts[1]), Path(stats) if stats else None)
    return rows


def _matches(bot_id: str, sel: str) -> bool:
    return bot_id == sel or bot_id.endswith("/" + sel) or fnmatch.fnmatchcase(bot_id, sel)


def _stdouts(z: zipfile.ZipFile) -> dict[str, str]:
    out = {}
    for name in z.namelist():
        m = STDOUT.fullmatch(name)
        if m:
            out[m.group(1)] = z.read(name).replace(b"\x00", b"").decode("utf-8", "replace")
    return out


def games(paths: list[str], side: str | None = None, bot: str | None = None) -> Iterator[Game]:
    both = unmatched = 0
    for raw in paths:
        p = Path(raw)
        if p.is_dir():
            files = sorted(p.rglob("*.repl26a")) + sorted(p.rglob("group.zip")) + sorted(p.rglob("match.zip"))
            manifest = _manifests(p) if bot and not side else {}
        elif p.is_file():
            files, manifest = [p], (_manifests(p.parent) if bot and not side else {})
        else:
            print(f"{p}: not found", file=sys.stderr)
            continue
        for f in files:
            try:
                if f.suffix == ".repl26a":
                    with zipfile.ZipFile(f) as z:
                        outs = _stdouts(z)
                    row = manifest.get(_key(f))
                    stats = _load(row[2] if row and row[2] else f.with_suffix(".json"))
                    if side:
                        sides = [side]
                    elif bot:
                        sides = [s for s, b in zip("ab", row[:2]) if _matches(b, bot)] if row else []
                        unmatched += not sides
                    else:
                        sides = [s for s in "ab" if MAIN_LOG.search(outs.get(s, "").encode())]
                        both += len(sides) == 2
                    for s in sides:
                        yield Game(str(f), s, outs.get(s, ""), stats, map_name(stats))
                elif f.name == "group.zip":
                    with zipfile.ZipFile(f) as group:
                        for name in sorted(n for n in group.namelist() if n.endswith(".zip")):
                            stem = Path(name).stem
                            match_id = stem.split("-", 1)[1] if re.match(r"\d+-", stem) else stem
                            stats = _load(f.parent / f"{match_id}-summary.json")
                            with zipfile.ZipFile(io.BytesIO(group.read(name))) as z:
                                for s, text in sorted(_stdouts(z).items()):
                                    yield Game(f"{f}!{name}", s, text, stats, map_name(stats))
                else:
                    stats = _load(f.parent / "summary.json")
                    with zipfile.ZipFile(f) as z:
                        for s, text in sorted(_stdouts(z).items()):
                            yield Game(str(f), s, text, stats, map_name(stats))
            except (OSError, zipfile.BadZipFile) as exc:
                print(f"{f}: unreadable ({exc})", file=sys.stderr)
    if both:
        print(f"note: {both} local replays carry main logs on both sides; both counted (use --side or --bot)",
              file=sys.stderr)
    if unmatched:
        print(f"note: {unmatched} local replays without a manifest row naming {bot!r} were skipped", file=sys.stderr)
