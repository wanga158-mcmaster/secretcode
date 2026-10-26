"""Shared harness of the bots/main game fixtures (main-<name>-fixture.py).

A fixture builds the bot under test (--bot, default main/main) and its opponent as arena
snapshots (content hash, built once; knob overrides go into the snapshot, never the working tree),
plays release games with `cambc run-batch` inside the machine-wide arena slot pool, and returns
each game with our side's stdout and the match statistics. Replays are written to
.cache/main/fixtures/<fixture>/<suite>-<bot>/ and removed when the fixture passes (kept on a
failure or with --keep). Every fixture also fails on a bot fault or fuel exhaustion on our side.

run-batch first re-checks each bot's build in its snapshot directory, and two cambc processes
doing that for one directory at once fail on Windows ("failed to persist temporary file: Access
is denied"). So the harness holds each snapshot's lock (.cache/arena/snap/<version>.lock) until
the first game starts, and retries that failure a few times (other tools may not take the lock).
"""
from __future__ import annotations

import argparse
import contextlib
import json
import random
import shutil
import subprocess
import sys
import threading
import time
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
import core  # noqa: E402

GAME_TIMEOUT = 1800  # wall-clock seconds per game (run-batch --timeout)
BUILD_RACE = "failed to persist temporary file"
ATTEMPTS = 6


def options(doc: str) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=doc.strip().splitlines()[0])
    p.add_argument("--bot", default="main/main",
                   help="bot under test, <suite>/<bot> (default main/main); an older bot such as "
                        "anchors/main-v24 shows the regression the fixture guards against")
    p.add_argument("--keep", action="store_true", help="keep the replays even when the fixture passes")
    return p.parse_args()


@dataclass
class Game:
    map: str
    side: str            # our side: "a" or "b"
    opponent: str
    replay: Path
    stats_path: Path
    stdout: str = ""
    stats: dict = field(default_factory=dict)

    @property
    def team(self) -> str:
        return "teamA" if self.side == "a" else "teamB"

    @property
    def faults(self) -> int:
        return int((self.stats.get("diagnostics", {}).get("botFaults") or {}).get(self.team, 0) or 0)

    @property
    def exhausted(self) -> int:
        return sum(len(g.get("entries", [])) for g in self.stats.get("events", [])
                   if g.get("kind") == "fuelExhausted" and (g.get("owner") or {}).get("team") == self.team)

    @property
    def result(self) -> str:
        outcome = self.stats.get("match", {}).get("outcome")
        return "?" if outcome is None else "win" if outcome == self.team else "loss"

    @property
    def healthy(self) -> bool:
        return bool(self.stats) and self.faults == 0 and self.exhausted == 0

    def health(self) -> str:
        if not self.stats:
            return "no statistics"
        return f"{self.result}, faults {self.faults}, fuel exhaustion {self.exhausted}"


def out_dir(fixture: str, bot_id: str) -> Path:
    return ROOT / ".cache" / "main" / "fixtures" / fixture / bot_id.replace("/", "-")


def play(fixture: str, bot_id: str, games: list[tuple[str, str, str]],
         overrides: dict[str, str] | None = None) -> list[Game] | None:
    """games: (map, our side "a"|"b", opponent): a bot id, optionally with knob overrides as
    "bot|k=v,k=v" (e.g. a strategy persona of an anchor). Returns the played games, or None after
    printing why the builds or the batch failed."""
    def spec(o: str):
        bot, _, knobs = o.partition("|")
        return core.bot(bot), dict(kv.split("=", 1) for kv in knobs.split(",") if kv)
    try:
        ours = core.prepare(core.bot(bot_id), overrides)
        opponents = {o: core.prepare(*spec(o)) for o in sorted({g[2] for g in games})}
    except core.ArenaError as exc:
        print(f"setup failed: {exc}")
        return None
    for ent in [ours, *opponents.values()]:
        if not ent.ok:
            print(f"build failed: {ent.bot.id}@{ent.version}\n    " + "\n    ".join(ent.log.strip().splitlines()[-8:]))
            return None
    out = out_dir(fixture, bot_id)
    if out.exists():
        shutil.rmtree(out)  # an earlier run's replays (run-batch refuses to overwrite)
    out.mkdir(parents=True)
    played, rows = [], []
    for i, (map_name, side, opp) in enumerate(games):
        g = Game(map_name, side, opp, out / f"{i}-{map_name}-{side}.repl26a", out / f"{i}-{map_name}-{side}.json")
        a, b = (ours.path, opponents[opp].path) if side == "a" else (opponents[opp].path, ours.path)
        rows.append(f"{a} {b} {core.map_path(map_name)} {g.replay} --statistics {g.stats_path} --json")
        played.append(g)
    manifest = out / "manifest.txt"
    manifest.write_text("\n".join(rows) + "\n", encoding="utf-8")
    versions = sorted({ent.version for ent in [ours, *opponents.values()]})
    with core.Slots(len(games), minimum=1, quiet=True) as slots:
        for attempt in range(1, ATTEMPTS + 1):
            code, log = _run_batch(manifest, slots.count, versions, GAME_TIMEOUT * len(games) + 600)
            if code == 0 or BUILD_RACE not in log or any(g.replay.exists() for g in played) or attempt == ATTEMPTS:
                break
            time.sleep(random.uniform(2.0, 4.0 * attempt))  # another process was re-checking the same build
    (out / "run-batch.log").write_text(log, encoding="utf-8", errors="replace")
    if code != 0:
        print(f"run-batch failed (exit {code}, attempt {attempt}); log in {out / 'run-batch.log'}")
        return None
    for g in played:
        try:
            with zipfile.ZipFile(g.replay) as z:
                g.stdout = z.read(f"team-{g.side}.stdout").replace(b"\x00", b"").decode("utf-8", "replace")
            g.stats = json.loads(g.stats_path.read_text(encoding="utf-8"))
        except (OSError, KeyError, zipfile.BadZipFile, json.JSONDecodeError) as exc:
            print(f"{g.map} side {g.side}: unreadable output ({exc}); replays in {out}")
            return None
    return played


def _run_batch(manifest: Path, jobs: int, versions: list[str], timeout: int) -> tuple[int, str]:
    """One run-batch call; the snapshot locks are held only until its first game starts."""
    cmd = [str(core.CAMBC), "run-batch", str(manifest), "--release", "--jobs", str(jobs), "--timeout", str(GAME_TIMEOUT)]
    lines: list[str] = []
    started = threading.Event()
    with contextlib.ExitStack() as locks:
        for ver in versions:  # sorted: concurrent fixtures take them in the same order
            locks.enter_context(core.FileLock(core.CACHE / "snap" / f"{ver}.lock"))
        proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                errors="replace")

        def pump() -> None:
            for line in proc.stdout:
                lines.append(line)
                if "STARTING" in line:
                    started.set()
            started.set()
        reader = threading.Thread(target=pump, daemon=True)
        reader.start()
        started.wait(timeout=900)
    try:
        code = proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        code = proc.wait()
        lines.append(f"run-batch killed after {timeout}s\n")
    reader.join(timeout=60)
    return code, "".join(lines)


def finish(fixture: str, bot_id: str, games: list[Game] | None, passed: bool, keep: bool) -> int:
    """Final verdict line; removes the replays of a passing run unless kept. Returns the exit status."""
    unhealthy = sum(1 for g in games or [] if not g.healthy)
    ok = games is not None and passed and not unhealthy
    out = out_dir(fixture, bot_id)
    if ok and not keep:
        shutil.rmtree(out, ignore_errors=True)
    verdict = "ok" if ok else "FAIL" + (f" ({unhealthy} game(s) with faults or fuel exhaustion)" if unhealthy else "")
    print(verdict + (f"; replays in {out}" if out.exists() else ""))
    return 0 if ok else 1
