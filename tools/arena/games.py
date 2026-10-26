"""Play engine: run games through `cambc run-batch --release` and record one JSON row per game.

Library (other arena modules):
    from games import GameSpec, play, perspective, health, flagged
    rows = play([GameSpec(a, b, "00collision", {"subject": "A"})], out_dir)   # a, b: core.Entrant
CLI:
    arena play BOT OPP[,OPP...] [--maps pool] [--sides both|A|B] [--set k=v ...] [--label NAME]
               [--jobs N] [--rounds N] [--out DIR] [--keep-replays flagged|all|none]

Game row (play() returns them, appends them to <out>/games.jsonl and to `log` when given):
  ts (ISO-8601 UTC), map (canonical name), a / b (entrant keys <suite>/<bot>@<version> of teams A / B),
  a_name / b_name (label or bot id), winner "A"|"B"|"draw"|"error"|"skipped", rounds (completed),
  A / B: {gems, gold, kills, deaths, clears, faults, exhausted, errors, fuel_max, fuel_at_budget} (ints),
  replay (repo-relative path or null), tags (free-form; "subject": "A"|"B" marks the entrant under
  test), error (text, only for error/skipped), truncated (true, only for games cut by --rounds).
Team numbers: gems = submitted gems; gold/kills/deaths = champion metrics (earnedGold, kills, deaths);
clears = campClear event entries; faults = diagnostics.botFaults; exhausted = fuelExhausted event
entries; errors = lines of the team's stdout in the replay matching /\\berror\\b/i (bots log caught
engine errors at the turn boundary); fuel_max / fuel_at_budget = the largest fuel use of an ordinary
gameplay turn and the number of ordinary turns at or above the play budget (suspended turns), from the
replay's team-x.fuel stream. stall (derived, never stored) = no camp clear in a finished game.

Outcomes: a game with an unbuilt entrant is "skipped" (error = build log tail) and never reaches a
manifest; a game whose statistics are missing or unreadable is "error" (reason from the
<replay>.error.log, the batch status line or the run-batch log). Games cut by --rounds have no
statistics (the CLI exports them for finished matches only): they become "draw" rows with
truncated=true, where only errors and fuel are measured (fuel_at_budget > 0 reports as "suspended").
Files: <out>/games/<name>.repl26a beside <name>.json (statistics, kept only with keep_replays="all");
replays of unflagged games are deleted unless "all"; error logs and the manifest/log of any chunk
with an error are kept.
"""
from __future__ import annotations

import hashlib
import itertools
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import zipfile
from contextlib import ExitStack
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import core  # noqa: E402
from core import (ArenaError, Bot, CACHE, CAMBC, Entrant, FileLock, ROOT, Slots, append_jsonl, read_jsonl,  # noqa: E402,F401
                  map_info, map_path, map_set, prepare, prepare_many, select, slot_count)
from core import bot as find_bot  # noqa: E402

TEAM_KEYS = ("gems", "gold", "kills", "deaths", "clears", "faults", "exhausted", "errors", "fuel_max",
             "fuel_at_budget")
WINNERS = ("A", "B", "draw", "error", "skipped")
KEEP = ("flagged", "all", "none")
FUEL_MAGIC = b"a62leuf\0"
FUEL_HEADER = 13            # 8-byte magic + 5-byte header, then u64 LE values
PLAY_BUDGET = 100_000       # used only when a fuel stream carries no budget values
MAX_PICKS = 5               # draft size bound when a map's draft count is unknown
ERROR_LINE = re.compile(r"\berror\b|engine_error\b", re.I)  # "... error: ..." and P5 UNEXPECTED_ENGINE_ERROR
STATUS_LINE = re.compile(r"^\[.\]\s+(.*)$")
GAME_TIMEOUT = int(os.environ.get("ARENA_GAME_TIMEOUT") or 1800)  # wall-clock s per match (0 = none)
READY = ".batch-ready"      # marker in a snapshot dir: run-batch has configured its release build
PERSIST_RACE = "failed to persist temporary file"  # concurrent build checks on one snapshot (Windows)


def _engine() -> str:
    """Engine version recorded on every row (the ladder's format), so ratings never mix engines."""
    try:
        m = json.loads((ROOT / "docs/reference/current-version.json").read_text(encoding="utf-8"))
        return str(m["engine"]["version"])
    except (OSError, KeyError, ValueError):
        return "unknown"


ENGINE = _engine()


@dataclass
class GameSpec:
    a: Entrant
    b: Entrant
    map: str
    tags: dict = field(default_factory=dict)


# ----------------------------------------------------------------------------- parsing
def _int(v) -> int:
    try:
        return int(v)
    except (TypeError, ValueError):
        try:
            return int(float(v))
        except (TypeError, ValueError):
            return 0


def _side(team) -> str | None:
    if isinstance(team, dict):
        team = team.get("team")
    return {"teamA": "A", "teamB": "B"}.get(team)


def _zero() -> dict:
    return dict.fromkeys(TEAM_KEYS, 0)


def parse_statistics(stats: dict) -> dict:
    """Winner, completed rounds, team totals and draft shape from a `--statistics --json` export:
    {"winner", "rounds", "A": {...}, "B": {...}, "roster": {"A": (picks, bodies), "B": (...)}}.
    Raises ValueError/KeyError/TypeError on an unusable export."""
    match = stats["match"]
    outcome = match.get("outcome")
    if outcome == "draw":
        winner = "draw"
    else:
        winner = _side(outcome)
        if winner is None:
            raise ValueError(f"unknown match outcome {outcome!r}")
    teams = {"A": _zero(), "B": _zero()}
    for t in stats.get("teams") or []:
        s = _side(t.get("team"))
        if s:
            teams[s]["gems"] = _int(t.get("submittedGems"))
    roster = (stats.get("roster") or {}).get("champions") or []
    team_of = {r.get("id"): _side(r.get("team")) for r in roster}
    for c in stats.get("champions") or []:
        s = _side(c.get("team")) or team_of.get(c.get("id"))
        if s:
            m = c.get("metrics") or {}
            teams[s]["gold"] += _int(m.get("earnedGold"))
            teams[s]["kills"] += _int(m.get("kills"))
            teams[s]["deaths"] += _int(m.get("deaths"))
    for g in stats.get("events") or []:
        key = {"fuelExhausted": "exhausted", "campClear": "clears"}.get(g.get("kind"))
        s = _side(g.get("owner"))
        if key and s:
            teams[s][key] += len(g.get("entries") or [])
    for team, n in ((stats.get("diagnostics") or {}).get("botFaults") or {}).items():
        s = _side(team)
        if s:
            teams[s]["faults"] = _int(n)
    picks: dict[str, set] = {"A": set(), "B": set()}
    bodies = {"A": 0, "B": 0}
    for r in roster:  # Cassi & Issac: one draft pick, two bodies
        s = _side(r.get("team"))
        if s:
            picks[s].add(r.get("draftPick"))
            bodies[s] += 1
    return {"winner": winner, "rounds": _int(match.get("roundsCompleted")), "A": teams["A"], "B": teams["B"],
            "roster": {s: (len(picks[s]), bodies[s]) for s in "AB"}}


def fuel_profile(raw: bytes, picks: int, bodies: int) -> tuple[int, int]:
    """(fuel_max, fuel_at_budget) over ordinary gameplay turns of one team-x.fuel stream: 8-byte
    magic, 5-byte header, u64 LE values: 8 budgets (draft first/other, play first/other, twice),
    then the fuel used per own turn: `picks` draft turns, `bodies` first gameplay turns (double
    budget), then ordinary turns. A suspended turn shows the full budget."""
    if len(raw) < FUEL_HEADER or raw[:8] != FUEL_MAGIC:
        raise ValueError("not a fuel stream")
    n = (len(raw) - FUEL_HEADER) // 8
    values = struct.unpack_from(f"<{n}Q", raw, FUEL_HEADER)
    budgets, turns = values[:8], values[8:]
    budget = budgets[3] if len(budgets) > 3 and budgets[3] else PLAY_BUDGET
    ordinary = turns[picks + bodies:]
    return max(ordinary, default=0), sum(1 for v in ordinary if v >= budget)


def count_errors(stdout: bytes) -> int:
    """Lines matching /\\berror\\b|engine_error\\b/i in a team stdout stream (NUL padding removed)."""
    text = stdout.replace(b"\0", b"").decode("utf-8", "replace")
    return sum(1 for line in text.splitlines() if ERROR_LINE.search(line))


def parse_replay(replay: Path, roster: dict) -> tuple[dict, list[str]]:
    """Per team {"errors", "fuel_max", "fuel_at_budget"} from a replay bundle (a zip holding
    team-{a,b}.stdout and .fuel), plus notes about anything that could not be read."""
    out = {s: {"errors": 0, "fuel_max": 0, "fuel_at_budget": 0} for s in "AB"}
    notes: list[str] = []
    try:
        with zipfile.ZipFile(replay) as z:
            names = set(z.namelist())
            for s in "AB":
                x = s.lower()
                if f"team-{x}.stdout" in names:
                    out[s]["errors"] = count_errors(z.read(f"team-{x}.stdout"))
                else:
                    notes.append(f"no team-{x}.stdout")
                if f"team-{x}.fuel" in names:
                    try:
                        out[s]["fuel_max"], out[s]["fuel_at_budget"] = fuel_profile(z.read(f"team-{x}.fuel"),
                                                                                    *roster[s])
                    except (ValueError, struct.error) as exc:
                        notes.append(f"team-{x}.fuel: {exc}")
                else:
                    notes.append(f"no team-{x}.fuel")
    except (OSError, zipfile.BadZipFile) as exc:
        notes.append(f"replay unreadable: {exc}")
    return out, notes


def batch_status(text: str) -> dict[str, str]:
    """Final status per replay file name (lower case) from run-batch output, e.g. "TEAM A WIN (x) ...",
    "TRUNCATED ...", "FAILED: runner exited ... (log: ...) ..."; STARTING lines are ignored."""
    status: dict[str, str] = {}
    last = None
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("Replay:"):
            name = re.split(r"[\\/]", s[len("Replay:"):].strip())[-1].lower()
            if last and not last.startswith("STARTING"):
                status[name] = last
            last = None
        else:
            m = STATUS_LINE.match(s)
            if m:
                last = m.group(1).strip()
    return status


# ----------------------------------------------------------------------------- rows and health
def _now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _rel(p: Path) -> str:
    p = Path(p).resolve()
    return p.relative_to(ROOT).as_posix() if p.is_relative_to(ROOT) else str(p)


def _tail(text: str, lines: int = 12, limit: int = 2000) -> str:
    kept = [l.rstrip() for l in (text or "").strip().splitlines() if l.strip()][-lines:]
    return "\n".join(kept)[-limit:]


def _base_row(g: GameSpec, map_name: str) -> dict:
    return {"ts": _now(), "map": map_name, "a": g.a.key, "b": g.b.key, "a_name": g.a.name, "b_name": g.b.name,
            "winner": "error", "rounds": 0, "A": _zero(), "B": _zero(), "replay": None, "tags": dict(g.tags or {}),
            "engine": ENGINE}


def _guess_roster(map_name: str) -> dict:
    """Draft shape when no statistics exist: the map's draft count (else the maximum), and one body
    more than picks (Cassi & Issac may be drafted), so a double-budget first turn is never read as
    an ordinary turn; at most one ordinary turn goes unmeasured."""
    picks = map_info(map_name).get("draft") or MAX_PICKS
    return {"A": (picks, picks + 1), "B": (picks, picks + 1)}


def game_row(g: GameSpec, map_name: str, replay: Path, stats: Path, status: str = "", rounds: int = 0,
             batch_log: str = "", code: int = 0, *, fallback_roster: dict | None = None) -> dict:
    """The row of one game that went through run-batch (outputs at `replay` and `stats`)."""
    row = _base_row(g, map_name)
    row["replay_health_complete"] = False
    detailed_capture_complete = True
    replay, stats = Path(replay), Path(stats)
    if stats.is_file():
        try:
            document = json.loads(stats.read_text(encoding="utf-8"))
            parsed = parse_statistics(document)
            diagnostics = document.get("diagnostics") or {}
            if "detailedCaptureComplete" in diagnostics:
                detailed_capture_complete = diagnostics["detailedCaptureComplete"] is True
        except (OSError, ValueError, KeyError, TypeError, AttributeError) as exc:
            row["error"] = f"unreadable statistics {stats.name}: {exc!r}"
            if replay.is_file():
                row["replay"] = _rel(replay)
            return row
        row.update(winner=parsed["winner"], rounds=parsed["rounds"], A=parsed["A"], B=parsed["B"])
        roster = parsed["roster"]
    elif replay.is_file() and (status.startswith("TRUNCATED") or (rounds > 0 and not status.startswith("FAILED"))):
        m = re.search(r"Round\s+(\d+)\s*/", status)
        row.update(winner="draw", truncated=True, rounds=rounds if rounds > 0 else (int(m.group(1)) - 1 if m else 0))
        roster = fallback_roster if fallback_roster is not None else _guess_roster(map_name)
    else:
        err = replay.with_name(replay.name + ".error.log")
        if err.is_file():
            reason = _tail(err.read_text(encoding="utf-8", errors="replace"), 15)
        elif status:
            reason = status
        else:
            reason = f"no statistics (run-batch exit {code})" + (f":\n{_tail(batch_log, 10)}" if batch_log else "")
        row["error"] = reason or "no statistics"
        if replay.is_file():
            row["replay"] = _rel(replay)
        return row
    if replay.is_file():
        try:
            measured, notes = parse_replay(replay, roster)
            if not notes:
                # fuel_profile tolerates short budget tables and trailing bytes;
                # neither is a complete capture, even when its numbers parse.
                with zipfile.ZipFile(replay) as bundle:
                    for s in "ab":
                        size = bundle.getinfo(f"team-{s}.fuel").file_size
                        if size < FUEL_HEADER + 8 * 8 or (size - FUEL_HEADER) % 8:
                            notes.append(f"team-{s}.fuel: incomplete fuel stream")
        except (OSError, ValueError, KeyError, RuntimeError, NotImplementedError, zipfile.BadZipFile) as exc:
            measured, notes = {}, [f"replay health unreadable: {exc}"]
        for s in "AB":
            row[s].update(measured.get(s, {}))
        if not detailed_capture_complete:
            notes.append("statistics detailed capture incomplete")
        row["replay"] = _rel(replay)
        row["replay_health_complete"] = not notes
        if notes:
            row["note"] = "; ".join(notes)
    else:
        row["note"] = "replay missing: errors and fuel not measured"
    return row


def _stall(row: dict, side: str) -> bool:
    return (row.get("winner") in ("A", "B", "draw") and not row.get("truncated")
            and _int((row.get(side) or {}).get("clears")) == 0)


def perspective(row: dict, side: str | None = None) -> dict | None:
    """The row seen from `side` (else row["tags"]["subject"]); None when no subject is known."""
    side = str(side or (row.get("tags") or {}).get("subject") or "").upper()
    if side not in ("A", "B"):
        return None
    other = "B" if side == "A" else "A"
    me, op = row.get(side) or {}, row.get(other) or {}
    w = row.get("winner")
    result = "win" if w == side else "loss" if w == other else (w if w in ("draw", "error", "skipped") else "error")
    return {"result": result, "side": side, "map": row.get("map"), "me": row.get(side.lower()),
            "opponent": row.get(other.lower()),
            "opponent_name": row.get(f"{other.lower()}_name") or row.get(other.lower()),
            "gems": _int(me.get("gems")), "opp_gems": _int(op.get("gems")),
            "gold": _int(me.get("gold")), "opp_gold": _int(op.get("gold")),
            "kills": _int(me.get("kills")), "deaths": _int(me.get("deaths")),
            "clears": _int(me.get("clears")), "opp_clears": _int(op.get("clears")),
            "faults": _int(me.get("faults")), "exhausted": _int(me.get("exhausted")),
            "errors": _int(me.get("errors")), "fuel_max": _int(me.get("fuel_max")),
            "fuel_at_budget": _int(me.get("fuel_at_budget")), "stall": _stall(row, side),
            "truncated": bool(row.get("truncated")), "rounds": _int(row.get("rounds")),
            "tags": row.get("tags") or {}}


def health(row: dict, side: str) -> list[str]:
    """Health problems of one team, e.g. ["faults 1", "exhausted 3", "errors 2", "stall"]; [] when clean.
    "suspended N" appears when ordinary turns hit the budget but no exhaustion was counted (truncated games)."""
    side = str(side).upper()
    t = row.get(side) or {}
    out = [f"{k} {_int(t.get(k))}" for k in ("faults", "exhausted", "errors") if _int(t.get(k))]
    if not _int(t.get("exhausted")) and _int(t.get("fuel_at_budget")):
        out.append(f"suspended {_int(t.get('fuel_at_budget'))}")
    if _stall(row, side):
        out.append("stall")
    return out


def flagged(row: dict) -> bool:
    """True when the game failed or was skipped, or any team shows a health problem."""
    return row.get("winner") in ("error", "skipped") or any(health(row, s) for s in "AB")


# ----------------------------------------------------------------------------- running
def _slug(s: str, n: int = 24) -> str:
    return re.sub(r"[^A-Za-z0-9.-]+", "-", str(s)).strip("-")[:n] or "x"


def _snapshot_locks(paths) -> list[Path]:
    """Sorted per-snapshot lock files (core.prepare's) for the entrant dirs inside .cache/arena/snap."""
    snaps = (CACHE / "snap").resolve()
    roots = set()
    for p in paths:
        p = Path(p).resolve()
        if p.is_relative_to(snaps) and p != snaps:
            roots.add(p.relative_to(snaps).parts[0])
    return [snaps / f"{r}.lock" for r in sorted(roots)]


def _run_batch(manifest: Path, jobs: int, rounds: int, log_path: Path, entrants=()) -> int:
    """One `cambc run-batch` call (cwd ROOT), output to log_path; returns the exit code (-1: not started).

    run-batch first re-checks every entrant's build, which persists a temporary file inside the
    snapshot's build directory; two processes doing that on one snapshot fail on Windows. So the
    snapshot locks are held (in sorted order) from launch until the first match starts, and a
    batch that failed with that race before starting any match is retried after a short jitter."""
    cmd = [str(CAMBC), "run-batch", str(manifest), "--release", "--jobs", str(max(1, jobs)), "--color", "never"]
    if rounds > 0:
        cmd += ["--rounds", str(rounds)]
    if GAME_TIMEOUT > 0:
        cmd += ["--timeout", str(GAME_TIMEOUT)]
    locks = _snapshot_locks(entrants)
    for attempt in range(5):
        with open(log_path, "w", encoding="utf-8", errors="replace") as fh:
            held = []
            try:
                for lock in locks:
                    held.append(FileLock(lock).__enter__())
                try:
                    proc = subprocess.Popen(cmd, cwd=ROOT, stdout=fh, stderr=subprocess.STDOUT)
                except OSError as exc:
                    fh.write(f"cannot run {CAMBC}: {exc}\n")
                    return -1
                while held and proc.poll() is None:
                    time.sleep(0.05)
                    try:
                        if "STARTING" in log_path.read_text(encoding="utf-8", errors="replace"):
                            break
                    except OSError:
                        pass
            finally:
                for lock in reversed(held):
                    lock.__exit__(None, None, None)
            code = proc.wait()
        text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
        if code == 0 or PERSIST_RACE not in text or "STARTING" in text:
            return code
        time.sleep(0.5 + (os.getpid() % 7) * 0.3 + attempt)  # another process won the race: back off
    return code


def _warm(ents: list[Entrant], map_file: Path) -> None:
    """A fresh snapshot's first run-batch reconfigures its release build (CMake writes into the build
    dir) before later runs report it up to date. Do that once per snapshot under a lock, with a
    one-round self-play, so concurrent batches never configure one build directory together."""
    snaps = (CACHE / "snap").resolve()
    todo: dict[Path, Path] = {}
    for e in ents:
        p = Path(e.path).resolve()
        if e.ok and p.is_relative_to(snaps) and p != snaps:
            root = snaps / p.relative_to(snaps).parts[0]
            if not (root / READY).is_file():
                todo[root] = p
    if not todo:
        return

    def one(item):
        root, path = item
        with FileLock(root.with_name(root.name + ".warm.lock")):
            if (root / READY).is_file():
                return
            tmp = CACHE / "warm" / f"{root.name}-{os.getpid()}"
            shutil.rmtree(tmp, ignore_errors=True)
            tmp.mkdir(parents=True)
            replay = tmp / "warm.repl26a"
            (tmp / "manifest.txt").write_text(f"{path} {path} {map_file} {replay}\n", encoding="utf-8")
            with Slots(1, quiet=True):
                _run_batch(tmp / "manifest.txt", 1, 1, tmp / "run-batch.log", entrants=[path])
            if replay.is_file():
                (root / READY).write_text(_now() + "\n", encoding="utf-8")
                (root / core.TRIMMED).unlink(missing_ok=True)  # the warm-up rebuilt a trimmed snapshot
            shutil.rmtree(tmp, ignore_errors=True)

    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=min(8, len(todo))) as pool:
        list(pool.map(one, todo.items()))


@dataclass
class _Planned:
    index: int
    spec: GameSpec
    map_name: str
    map_file: Path
    replay: Path
    stats: Path

    @property
    def outputs(self) -> tuple[Path, Path, Path]:
        return self.replay, self.stats, self.replay.with_name(self.replay.name + ".error.log")


_CALLS = itertools.count(1)  # distinguishes batch files of several play() calls within one second


def _remove(path: Path) -> bool:
    """Delete a regenerable output; False when it still exists (e.g. held open by a viewer)."""
    try:
        Path(path).unlink(missing_ok=True)
        return True
    except OSError:
        return not Path(path).exists()


def _record(out: Path, log: Path | None, rows: list[dict]) -> None:
    if rows:
        append_jsonl(out / "games.jsonl", rows)
        if log:
            append_jsonl(Path(log), rows)


class Incomplete(ArenaError):
    """play() stopped at its deadline; rerunning with the same `out` continues where it stopped."""
    exit_code = 3


def _resume_key(a_key: str, b_key: str, map_name: str, tags: dict) -> str:
    return json.dumps([a_key, b_key, map_name, tags or {}], sort_keys=True)


@dataclass(frozen=True)
class OwnerRun:
    """An explicit prepared-input CLI run; verification grants no native-runner access."""
    owner: str
    fingerprint: str
    verify: object

    def output(self, path: Path) -> None:
        owner = ROOT / ".cache" / self.owner
        if (not path.is_absolute() or not path.is_relative_to(owner) or path == owner
                or path.is_relative_to(owner / "snap") or path.resolve() != path):
            raise ArenaError("owner CLI output is outside its direct output namespace")

    def check(self, games, out: Path, log: Path | None = None) -> None:
        if type(self.owner) is not str or self.owner.casefold() == "arena":
            raise ArenaError("owner CLI requires an independent owner namespace")
        snap = core.resolve_snapshot_root(ROOT / ".cache" / self.owner / "snap", self.owner)
        if not isinstance(self.fingerprint, str) or not re.fullmatch(r"[0-9a-f]{64}", self.fingerprint):
            raise ArenaError("owner input fingerprint is invalid")
        if not callable(self.verify):
            raise ArenaError("owner input verifier is required")
        for path in [out, out / "games", out / "games.jsonl"] + ([Path(log)] if log is not None else []):
            self.output(path)
        keys = {}
        for game in games:
            for e in (game.a, game.b):
                match = re.fullmatch(r"([A-Za-z0-9_-][A-Za-z0-9_.-]*)/([A-Za-z0-9_-][A-Za-z0-9_.-]*)@([0-9a-f]{12})", e.key)
                if not match or not e.ok:
                    raise ArenaError("owner CLI requires valid prepared entrants")
                suite, name, version = match.groups()
                expected = snap / version / "bots" / suite / name
                if e.path != expected or expected.resolve() != expected:
                    raise ArenaError("owner CLI entrant is outside its exact snapshot root")
                if e.key in keys and keys[e.key] != e.path:
                    raise ArenaError("owner CLI key has conflicting roots")
                keys[e.key] = e.path
        self.verify()


def _owner_digest(row: dict) -> str:
    value = {key: value for key, value in row.items() if key != "owner_transport"}
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def _owner_desc(path: Path) -> dict:
    data = path.read_bytes()
    return {"path": path.relative_to(ROOT).as_posix(), "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def _owner_rows(path: Path, fingerprint: str, expected: set[str], rounds: int) -> dict[str, dict]:
    """Strict owner checkpoint parsing: ordinary or torn rows never gain gate credit."""
    if not path.is_file():
        return {}
    raw = path.read_bytes()
    if raw and not raw.endswith(b"\n"):
        raise ArenaError("owner CLI checkpoint has a partial final row")
    done = {}
    for number, line in enumerate(raw.splitlines(), 1):
        try:
            row = json.loads(line)
            key = _resume_key(row["a"], row["b"], row["map"], row["tags"])
        except (ValueError, KeyError, TypeError) as exc:
            raise ArenaError(f"owner CLI checkpoint row {number} is invalid") from exc
        if (not isinstance(row, dict) or row.get("owner_input_fingerprint") != fingerprint or key not in expected
                or row.get("engine") != ENGINE or row.get("winner") not in WINNERS
                or bool(row.get("truncated")) != (rounds > 0) or (rounds and row.get("rounds") != rounds)):
            raise ArenaError(f"owner CLI checkpoint row {number} has different inputs or mode")
        receipt = row.get("owner_transport")
        if (not isinstance(receipt, dict) or set(receipt) != {"path", "size", "sha256"}
                or type(receipt["path"]) is not str or type(receipt["size"]) is not int
                or type(receipt["sha256"]) is not str):
            raise ArenaError("owner CLI checkpoint lacks its original transport receipt")
        receipt_path = ROOT / receipt["path"]
        if (not receipt_path.is_relative_to(path.parent) or receipt_path.resolve() != receipt_path
                or not receipt_path.is_file()):
            raise ArenaError("owner CLI checkpoint transport receipt is outside its output")
        data = receipt_path.read_bytes()
        if len(data) != receipt["size"] or hashlib.sha256(data).hexdigest() != receipt["sha256"]:
            raise ArenaError("owner CLI checkpoint transport receipt changed")
        try:
            binding = json.loads(data)
            if (binding["schema"] != "arena-owner-cli-transport-v1" or binding["owner_input_fingerprint"] != fingerprint
                    or binding["rows_sha256"].get(key) != _owner_digest(row)):
                raise ValueError("parsed row does not match its transport receipt")
        except (ValueError, KeyError, TypeError, AttributeError) as exc:
            raise ArenaError("owner CLI checkpoint row differs from its transport receipt") from exc
        if row["winner"] in ("A", "B", "draw"):
            if key in done and done[key] != row:
                raise ArenaError("owner CLI checkpoint has conflicting completed rows")
            done[key] = row
        elif key in done:
            raise ArenaError("owner CLI checkpoint continues an already completed key")
    return done


def _owner_batch(entrants, directory: Path, manifest: Path, log_path: Path, rounds: int, jobs: int,
                 owner_run: OwnerRun, argv_factory) -> dict:
    """Only this helper owns Slot admission; private Job cleanup precedes Slot release."""
    import compiled_runner
    versions = sorted({e.version for e in entrants})
    with ExitStack() as held:
        owner_run.verify()
        for path in (directory, manifest, log_path):
            owner_run.output(path)
        directory.mkdir(parents=True, exist_ok=False)
        game_keys = []

        def argv(count):
            nonlocal game_keys
            # Match ordinary CLI work: admission to the shared Slots comes first.
            # Acquiring VERSION while waiting for Slots can deadlock a normal batch.
            for version in versions:
                held.enter_context(FileLock(CACHE / "snap" / f"{version}.lock"))
            owner_run.verify()
            for path in (directory, manifest, log_path):
                owner_run.output(path)
            game_keys = argv_factory(count) or []
            command = [str(CAMBC), "run-batch", str(manifest), "--release", "--jobs", str(count), "--color", "never"]
            if rounds > 0:
                command += ["--rounds", str(rounds)]
            if GAME_TIMEOUT > 0:
                command += ["--timeout", str(GAME_TIMEOUT)]
            return command

        try:
            record = compiled_runner._slotted_command(argv, ROOT, directory,
                lambda count: max(120, (GAME_TIMEOUT or 1800) * max(1, (len(game_keys) + count - 1) // count) + 120),
                jobs=jobs)
        except BaseException as exc:
            process = getattr(exc, "compiled_process", None)
            if process is not None and not process.empty_confirmed:
                locks = held.pop_all()
                pending = next((item for item in compiled_runner._PENDING_CLEANUP if item["process"] is process), None)
                if pending is None:
                    exc.owner_version_locks = locks  # Never release locks while an owned tree may remain.
                else:
                    slots = pending["slots"]
                    class Admission:
                        def __exit__(self, *args):
                            locks.close()
                            slots.__exit__(*args)
                    pending["slots"] = Admission()
            try:
                owner_run.verify()
            except BaseException as drift:
                exc.add_note(f"Owner input verification after interruption failed: {drift!r}")
            raise
        owner_run.verify()
        if not record.get("tree_exit_confirmed") or record.get("timed_out"):
            raise ArenaError("owner CLI process did not finish with an empty private Job")
        text = "".join((directory / name).read_bytes().decode("utf-8", errors="replace")
                       for name in ("stdout.bin", "stderr.bin"))
        log_path.write_text(text, encoding="utf-8")
        receipt = {"schema": "arena-owner-cli-transport-v1", "owner": owner_run.owner,
                   "owner_input_fingerprint": owner_run.fingerprint, "game_keys": game_keys,
                   "rows_sha256": {}, "manifest": _owner_desc(manifest), "dispatch": _owner_desc(directory / "dispatch.json"),
                   "process": _owner_desc(directory / "process-end.json")}
        receipt_path = directory / "owner-run.json"
        receipt_path.write_text(json.dumps(receipt, sort_keys=True, indent=1) + "\n", encoding="utf-8")
        record["owner_transport"] = _owner_desc(receipt_path)
        return record


def _owner_bind_rows(record: dict, rows: list[dict], owner_run: OwnerRun) -> None:
    path = ROOT / record["owner_transport"]["path"]
    owner_run.output(path)
    if _owner_desc(path) != record["owner_transport"]:
        raise ArenaError("owner CLI transport receipt changed before checkpoint")
    receipt = json.loads(path.read_bytes())
    receipt["rows_sha256"] = {_resume_key(r["a"], r["b"], r["map"], r["tags"]): _owner_digest(r) for r in rows}
    path.write_text(json.dumps(receipt, sort_keys=True, indent=1) + "\n", encoding="utf-8")
    descriptor = _owner_desc(path)
    for row in rows:
        row["owner_transport"] = descriptor


def _owner_warm(entrants, map_file: Path, out: Path, owner_run: OwnerRun) -> None:
    """CLI reconfiguration is allowed only if every bound input remains byte-identical."""
    unique = {e.key: e for e in entrants}
    for e in unique.values():
        tmp = out / "warm" / f"{e.version}-{os.getpid()}-{next(_CALLS)}"
        owner_run.output(tmp)
        owner_run.verify()
        tmp.mkdir(parents=True, exist_ok=False)
        manifest, replay = tmp / "manifest.txt", tmp / "warm.repl26a"
        def make_manifest(count):
            manifest.write_text(f"{e.path} {e.path} {map_file} {replay}\n", encoding="utf-8")
            return []
        record = _owner_batch([e], tmp / "transport", manifest, tmp / "run-batch.log", 1, 1, owner_run, make_manifest)
        if record["returncode"] != 0 or not replay.is_file():
            raise ArenaError("owner CLI warmup produced no replay")
        _remove(replay)


def play(games: list[GameSpec], out: Path, jobs: int = 0, rounds: int = 0, keep_replays: str = "flagged",
         log: Path | None = None, quiet: bool = False, resume: bool = False,
         deadline: float | None = None, owner_run: OwnerRun | None = None) -> list[dict]:
    """Play every game (in chunks of at most max(8, 4 * slots) per run-batch call, holding machine
    slots per chunk) and return their rows in input order. Rows are appended to out/games.jsonl
    (and to `log`) as chunks finish.

    Games are deterministic, so with `resume` (the play/sweep/gate commands set it) a finished row
    already in out/games.jsonl for the same (A, B, map, tags) is reused instead of replayed. `deadline` (a time.monotonic() value)
    stops before the next chunk and raises Incomplete; calling again with the same `out` resumes."""
    if keep_replays not in KEEP:
        raise ArenaError(f"keep_replays must be one of {', '.join(KEEP)}")
    out = Path(out).absolute() if owner_run is not None else Path(out).resolve()
    expected = set()
    if owner_run is not None:
        owner_run.check(games, out, log)
        for game in games:
            key = _resume_key(game.a.key, game.b.key, map_path(game.map).stem, game.tags)
            if key in expected:
                raise ArenaError("owner CLI request has duplicate game keys")
            expected.add(key)
        done_rows = _owner_rows(out / "games.jsonl", owner_run.fingerprint, expected, rounds) if resume else {}
        if not resume and (out / "games.jsonl").exists():
            raise ArenaError("owner CLI output already has rows; resume is required")
    gdir = out / "games"
    if re.search(r"\s", str(gdir)):
        raise ArenaError(f"output path {gdir} contains whitespace (run-batch manifests are space-separated)")
    gdir.mkdir(parents=True, exist_ok=True)
    if owner_run is None:
        done_rows: dict[str, dict] = {}
    if resume and owner_run is None:
        for r in read_jsonl(out / "games.jsonl"):
            same_kind = bool(r.get("truncated")) == (rounds > 0) and (not rounds or r.get("rounds") == rounds)
            if (r.get("winner") in ("A", "B", "draw") and isinstance(r.get("a"), str) and same_kind
                    and r.get("engine") in (None, ENGINE)):  # a full rerun never reuses --rounds smoke rows
                done_rows.setdefault(_resume_key(r["a"], r["b"], r.get("map", ""), r.get("tags")), r)
    rows: list[dict | None] = [None] * len(games)
    plans: list[_Planned] = []
    used: set[str] = set()
    for i, g in enumerate(games):
        try:
            mfile = map_path(g.map)
        except ArenaError as exc:
            rows[i] = dict(_base_row(g, str(g.map)), error=str(exc))
            continue
        prior = done_rows.get(_resume_key(g.a.key, g.b.key, mfile.stem, g.tags))
        if prior is not None:
            rows[i] = prior  # already played into this out dir (deterministic: no need to replay)
            continue
        bad = {e.key: e for e in (g.a, g.b) if not e.ok}  # Entrant is unhashable; one entry per key
        if bad:
            rows[i] = dict(_base_row(g, mfile.stem), winner="skipped",
                           error="\n".join(f"build failed: {k}\n{_tail(e.log)}" for k, e in bad.items()))
            continue
        spaced = [str(p) for p in (g.a.path, g.b.path, mfile) if re.search(r"\s", str(p))]
        if spaced:
            if owner_run is not None:
                raise ArenaError(f"owner CLI path contains whitespace: {spaced[0]}")
            rows[i] = dict(_base_row(g, mfile.stem), error=f"path contains whitespace: {spaced[0]}")
            continue
        h = hashlib.sha256(f"{g.a.key}|{g.b.key}|{mfile.stem}|{rounds}".encode()).hexdigest()[:8]
        name = f"{_slug(mfile.stem)}_{_slug(g.a.bot.name)}_{_slug(g.b.bot.name)}_{h}"
        k = 1
        while name.lower() in used:
            k += 1
            name = f"{name.rsplit('~', 1)[0]}~{k}"
        used.add(name.lower())
        plan = _Planned(i, g, mfile.stem, mfile, gdir / f"{name}.repl26a", gdir / f"{name}.json")
        stale = [f for f in plan.outputs if not _remove(f)]  # run-batch refuses to overwrite outputs
        if stale:
            if owner_run is not None:
                raise ArenaError(f"owner CLI cannot remove stale output {stale[0]}")
            rows[i] = dict(_base_row(g, mfile.stem), error=f"cannot remove stale output {stale[0]}")
            continue
        plans.append(plan)
    reused = {id(r) for r in done_rows.values()}
    initial = [r for r in rows if r is not None and id(r) not in reused]
    if owner_run is not None:
        for row in initial:
            row["owner_input_fingerprint"] = owner_run.fingerprint
    _record(out, log, initial)
    if not plans:
        return rows  # type: ignore[return-value]
    entrants = [e for p in plans for e in (p.spec.a, p.spec.b)]
    if owner_run is not None:
        _owner_warm(entrants, plans[0].map_file, out, owner_run)
    else:
        _warm(entrants, plans[0].map_file)
    stamp = f"{time.strftime('%Y%m%dT%H%M%S')}-{os.getpid()}-{next(_CALLS)}"
    done, chunk_no = 0, 0
    while done < len(plans):
        if deadline is not None and time.monotonic() >= deadline:
            total = len(games)
            missing = len(plans) - done
            raise Incomplete(f"stopped at the time limit: {total - missing}/{total} games done; rerun the same "
                             f"command with --out {_rel(out)} to continue")
        chunk_no += 1
        manifest = gdir / f"batch-{stamp}-{chunk_no}.txt"
        batch_log = manifest.with_suffix(".log")
        def prepare_chunk(count):
            nonlocal chunk
            chunk = plans[done:done + max(8, 4 * count)]
            lines = [f"{p.spec.a.path} {p.spec.b.path} {p.map_file} {p.replay} --statistics {p.stats} --json"
                     for p in chunk]
            manifest.write_text("\n".join(lines) + "\n", encoding="utf-8")
            if not quiet:
                print(f"games {done + 1}-{done + len(chunk)} of {len(plans)} on {count} slot(s)",
                      file=sys.stderr, flush=True)
            return [_resume_key(p.spec.a.key, p.spec.b.key, p.map_name, p.spec.tags) for p in chunk]
        chunk = []
        if owner_run is not None:
            record = _owner_batch(entrants, manifest.with_suffix(".transport"), manifest, batch_log, rounds,
                                  min(jobs or slot_count(), len(plans) - done), owner_run, prepare_chunk)
            code = record["returncode"]
        else:
            with Slots(min(jobs or slot_count(), len(plans) - done), quiet=quiet) as s:  # never idle extra slots
                prepare_chunk(s.count)
                code = _run_batch(manifest, s.count, rounds, batch_log,
                                  entrants=[e.path for p in chunk for e in (p.spec.a, p.spec.b)])
        text = batch_log.read_text(encoding="utf-8", errors="replace") if batch_log.is_file() else ""
        status = batch_status(text)
        chunk_rows = []
        for p in chunk:
            row = game_row(p.spec, p.map_name, p.replay, p.stats, status.get(p.replay.name.lower(), ""), rounds,
                           text, code, **({"fallback_roster": {s: (MAX_PICKS, MAX_PICKS + 1) for s in "AB"}}
                                          if owner_run is not None else {}))
            if owner_run is not None:
                row["owner_input_fingerprint"] = owner_run.fingerprint
                row["owner_transport"] = record["owner_transport"]
            if keep_replays != "all":
                _remove(p.stats)
            if p.replay.is_file() and (keep_replays == "none" or (keep_replays == "flagged" and not flagged(row))):
                if _remove(p.replay):
                    row["replay"] = None
            rows[p.index] = row
            chunk_rows.append(row)
        if not any(r["winner"] == "error" for r in chunk_rows):
            if owner_run is None and not any(r.get("replay") for r in chunk_rows):  # owner receipts bind the manifest
                _remove(manifest)
            _remove(batch_log)
        if owner_run is not None:
            _owner_bind_rows(record, chunk_rows, owner_run)
        _record(out, log, chunk_rows)
        done += len(chunk)
    return rows  # type: ignore[return-value]


# ----------------------------------------------------------------------------- command
def _brief(text: str, limit: int = 200) -> str:
    """First and last non-empty line of an error text, on one line."""
    lines = [l.strip() for l in text.splitlines() if l.strip()] or ["?"]
    s = lines[0] if len(lines) == 1 else f"{lines[0]} ... {lines[-1]}"
    return s if len(s) <= limit else s[:limit - 3] + "..."


def _mean(values: list[int]) -> float | None:
    return sum(values) / len(values) if values else None


def _summary_line(title: str, ps: list[dict]) -> str:
    res = [p["result"] for p in ps]
    played = [p for p in ps if p["result"] in ("win", "loss", "draw")]
    full = [p for p in played if not p["truncated"]]
    g, og = _mean([p["gems"] for p in full]), _mean([p["opp_gems"] for p in full])
    gems = f"gems {g:.0f} vs {og:.0f}" if full else "gems n/a"
    line = (f"{title:<34} {res.count('win')}-{res.count('loss')}-{res.count('draw')}  {gems}  "
            f"faults {sum(p['faults'] for p in played)} exhausted {sum(p['exhausted'] for p in played)} "
            f"errors {sum(p['errors'] for p in played)} stalls {sum(p['stall'] for p in played)}")
    suspended = sum(p["fuel_at_budget"] for p in played)
    if suspended:
        line += f" suspended {suspended}"
    line += f"  fuel max {max((p['fuel_max'] for p in played), default=0)}"
    extra = [f"{n} {k}" for k, n in (("truncated", sum(p["truncated"] for p in ps)),
                                     ("error", res.count("error")), ("skipped", res.count("skipped"))) if n]
    return line + (f"  ({', '.join(extra)})" if extra else "")


def deadline_from(minutes: float) -> float | None:
    """A play() deadline from a --max-minutes value (0 = none)."""
    return time.monotonic() + minutes * 60 if minutes and minutes > 0 else None


def add_resume_args(p) -> None:
    """--max-minutes for commands that play games (shared by play, sweep and gate)."""
    p.add_argument("--max-minutes", type=float, default=0, metavar="M",
                   help="stop starting new games after M minutes and exit 3; rerun the same command with the "
                        "same --out to continue (finished games are reused)")


def _out_dir(arg: str, name: str) -> Path:
    if arg:
        return Path(arg).resolve()
    base = CACHE / "runs" / f"{time.strftime('%Y%m%d-%H%M%S')}-{_slug(name, 48)}"
    out, k = base, 1
    while True:
        try:
            out.mkdir(parents=True, exist_ok=False)
            return out
        except FileExistsError:
            k += 1
            out = base.with_name(f"{base.name}-{k}")


def _cmd_play(args) -> int:
    subject_bot = find_bot(args.bot)
    opponents = select(args.opponents)
    if not opponents:
        raise ArenaError(f"opponent selector {args.opponents!r} leaves no bot")
    maps = map_set(args.maps)
    if not maps:
        raise ArenaError("no maps selected")
    overrides = {}
    for item in args.overrides:
        k, sep, v = item.partition("=")
        if not sep or not k.strip():
            raise ArenaError(f"--set expects NAME=VALUE, got {item!r}")
        overrides[k.strip()] = v.strip()
    sides = ["A", "B"] if args.sides == "both" else [args.sides]
    ents = prepare_many([(subject_bot, overrides, args.label)] + [(o, None, "") for o in opponents])
    subject, opps = ents[0], ents[1:]
    games = [GameSpec(*((subject, o) if side == "A" else (o, subject)), m, {"subject": side, "opponent": o.bot.id})
             for o in opps for m in maps for side in sides]
    out = _out_dir(args.out, args.label or subject_bot.id)
    print(f"play {subject.name} ({subject.key}) vs {len(opps)} opponent(s) x {len(maps)} map(s) x side "
          f"{'/'.join(sides)} = {len(games)} games" + (f", {args.rounds} rounds" if args.rounds else ""), flush=True)
    for key, e in {e.key: e for e in ents if not e.ok}.items():
        print(f"FAILED build {key}\n    " + _tail(e.log, 8).replace("\n", "\n    "), flush=True)
    rows = play(games, out, jobs=args.jobs, rounds=args.rounds, keep_replays=args.keep_replays,
                resume=True, deadline=deadline_from(args.max_minutes))
    ps = [(r, perspective(r)) for r in rows]
    for o in opps:
        mine = [p for r, p in ps if r["tags"].get("opponent") == o.bot.id]
        print(_summary_line(f"vs {o.name}", mine))
    if len(opps) > 1:
        print(_summary_line("total", [p for _, p in ps]))
    notes = []
    for r, p in ps:
        where = f"{r['map']} as {p['side']} vs {p['opponent_name']}"
        if p["result"] in ("error", "skipped"):
            notes.append(f"  {where}: {p['result']}: {_brief(r.get('error') or '')}")
        elif health(r, p["side"]):
            notes.append(f"  {where}: {', '.join(health(r, p['side']))}" + (f"  {r['replay']}" if r.get("replay") else ""))
    for line in notes[:12]:
        print(line)
    if len(notes) > 12:
        print(f"  ... {len(notes) - 12} more in games.jsonl")
    print(f"out {_rel(out)}")
    return 1 if any(r["winner"] in ("error", "skipped") for r in rows) else 0


def register(sub) -> None:
    p = sub.add_parser("play", help="play BOT against opponents on maps and sides; print W-L-D, gems, health")
    p.add_argument("bot", help="subject: a selector matching one bot")
    p.add_argument("opponents", help="opponent selectors, comma-separated; each may match several bots")
    p.add_argument("--maps", default="pool", help="map set: pool, legacy, labs, small, large or names (default pool)")
    p.add_argument("--sides", default="both", choices=["both", "A", "B"],
                   type=lambda s: "both" if s.lower() == "both" else s.upper(), help="sides the subject plays")
    p.add_argument("--set", dest="overrides", action="append", default=[], metavar="NAME=VALUE",
                   help="knob override for BOT (repeatable)")
    p.add_argument("--label", default="", help="display name of the subject")
    p.add_argument("--jobs", type=int, default=0, help="machine slots to use (default: all free)")
    p.add_argument("--rounds", type=int, default=0, help="stop games after N rounds (no statistics; smoke runs)")
    p.add_argument("--out", default="", help="output dir (default .cache/arena/runs/<time>-<bot>/)")
    p.add_argument("--keep-replays", default="flagged", choices=KEEP, help="replays to keep (default flagged)")
    add_resume_args(p)
    p.set_defaults(func=_cmd_play)
