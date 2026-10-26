"""Experiments: one question answered with paired, reproducible game rows on the exact fast engine.

  arena exp run OWNER/NAME -q "QUESTION" -s LABEL=BOT[+KNOB=V...][@WEIGHTS] [-s ...] --vs OPP[,OPP...]
                [--maps pool,heldout] [--sides both|A|B] [--keep flagged|none] [--rounds N]
  arena exp resume OWNER/NAME [--progress-from PATH]         (exact cached inputs; missing keys only)
  arena exp show OWNER/NAME [--by opponent|map]
  arena exp audit OWNER/NAME [--win-floor-vs SUITE/BOT] [--out PATH]
  arena exp close OWNER/NAME --note "the answer, and where the decision is recorded"
  arena exp list [OWNER] [--open]
  arena exp replay OWNER/NAME MAP SIDE SUBJECT OPPONENT [--native-role raw|injected]
                                                           (one game's replay and logs, for diagnosis)

Every subject plays the same games: each opponent, map and side (game keys). The first subject is
the baseline; the others are compared with it on identical keys (wins with an exact McNemar test on
the flips, gem margin and rounds-to-win with paired standard errors), by opponent and overall, with
health (fuel exhaustion, faults, error lines) for both sides. Games run on fastrun (the native runner,
identical to the CLI on whole games; tools/fastrun) sharded over single-job processes that hold
arena pool slots. A row is a recipe: games are deterministic, so `replay` regenerates any game's
replay and logs on demand and nothing is stored "just in case".

Layout: .cache/<owner>/exp/<name>/ spec.json (question, subjects, opponents, maps), rows.jsonl (one
perspective row per game), summary.json, flagged/ (bundles of unhealthy games, with --keep flagged).
`close` records the note in spec.json and the owner's index.jsonl and deletes everything else.
"""
from __future__ import annotations

import json
import math
from contextlib import contextmanager, ExitStack
import hashlib
import os
import re
import shutil
import sys
import threading
import time
import uuid
from pathlib import Path

import core
import stats
import compiled_runner as native
from core import ArenaError, ROOT

CHUNK = 2000  # games per fastrun call: slots are held per call, so other work interleaves
CHUNK_TIMEOUT_SECONDS = 7200  # process guard only; missing game keys remain resumable
_NATIVE_RECOVERY_LOCK = native.__dict__.setdefault("_replay_inspector_cleanup_lock", threading.RLock())
FASTRUN = ROOT / ".tools" / "fastrun" / "fastrun.exe"
SUBJECT = re.compile(r"^(?P<label>[A-Za-z0-9_.-]+)=(?P<bot>[^+@]+)(?P<sets>(\+[A-Za-z0-9_]+=[^+@]+)*)(@(?P<weights>.+))?$")
HEALTH = ("exhausted", "faults", "errors")
DEVELOPMENT = "all,-main,-anchors,-surprise,-contenders"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def runner_libraries() -> dict[str, str]:
    library = fastrun().with_name("wasmtime.dll")
    return {library.name: sha256(library)} if library.is_file() else {}


def provenance(subjects: list[dict], maps: list[Path], entrants: dict[str, object]) -> dict:
    """Bind inputs omitted by the source snapshot key, without retaining raw game output."""
    return {"bundle": sha256(ROOT / "docs/reference/current-version.json"),
            "engine": sha256(engine_wasm()), "runner": sha256(fastrun()), "toolchain": core.toolchain(),
            "runner_source": sha256(ROOT / "tools/fastrun/fastrun.cpp"),
            "runner_libraries": runner_libraries(),
            "maps": {m.stem: sha256(m) for m in maps},
            "wasm": {name: sha256(Path(wasm(ent))) for name, ent in entrants.items()},
            "weights": {s["label"]: {"path": str(s["weights"]),
                                     "sha256": sha256(Path(s["weights"]) if Path(s["weights"]).is_absolute()
                                                       else ROOT / s["weights"])}
                        for s in subjects if s.get("weights")}}


def provenance_gaps(spec: dict) -> list[str]:
    """Legacy experiments remain inspectable, with their missing bindings explicit."""
    pinned = spec.get("provenance")
    if not isinstance(pinned, dict):
        return ["legacy experiment: engine, runner, map, toolchain, WASM and weight bytes were not pinned"]
    gaps = []
    try:
        if spec.get("private_snapshots") or "snapshot_roots" in pinned:
            if not spec.get("entrants"):
                raise ArenaError("private experiment entrants are missing")
            for label in spec["entrants"]:
                recorded_snapshot_root(spec, label)
        actual = {"bundle": sha256(ROOT / "docs/reference/current-version.json"),
                  "engine": sha256(engine_wasm()), "runner": sha256(fastrun()), "toolchain": core.toolchain(),
                  "runner_libraries": runner_libraries()}
        for key, value in actual.items():
            if pinned.get(key, {} if key == "runner_libraries" else None) != value:
                gaps.append(f"{key} identity missing or changed")
        if pinned.get("runner_source") and pinned["runner_source"] != sha256(ROOT / "tools/fastrun/fastrun.cpp"):
            gaps.append("runner source identity changed")
        if not pinned.get("maps"):
            gaps.append("map identities missing")
        for name, digest in pinned.get("maps", {}).items():
            if sha256(core.map_path(name)) != digest:
                gaps.append(f"map identity changed: {name}")
        for label, record in pinned.get("weights", {}).items():
            path = Path(record["path"])
            if sha256(path if path.is_absolute() else ROOT / path) != record["sha256"]:
                gaps.append(f"weight identity changed: {label}")
        weighted = {s["label"] for s in spec["subjects"] if s.get("weights")}
        weighted |= {o for o in spec.get("opponent_ids") or [] if opponent_weights(spec, o)}
        if weighted != set(pinned.get("weights", {})):
            gaps.append("weight identities missing or unexpected")
        if set(pinned.get("wasm", {})) != set(spec.get("entrants", {})):
            gaps.append("WASM identities missing or unexpected")
        for label, digest in pinned.get("wasm", {}).items():
            ident, version = spec["entrants"][label].rsplit("@", 1)
            binary = recorded_snapshot_root(spec, label) / version / "bots" / ident / ".cambc/build-release/bot.wasm"
            if sha256(binary) != digest:
                gaps.append(f"WASM identity changed: {label}")
    except (OSError, KeyError, TypeError, ArenaError) as exc:
        gaps.append(f"provenance unavailable: {exc}")
    return gaps


def replay_provenance_gaps(spec: dict, subject: str, opponent: str, map_names) -> list[str]:
    """Validate one retained physical pair/map set without requiring unrelated inputs.

    Shared runtime pins keep their global semantics. This ephemeral projection
    never fills missing selected pins or narrows experiment/resume/audit checks.
    """
    pinned = spec.get("provenance")
    if not isinstance(pinned, dict) or not pinned:
        return provenance_gaps(spec)
    try:
        selected = [s for s in spec["subjects"] if s["label"] == subject]
        if len(selected) != 1:
            raise ValueError("selected subject must be unique")
        names = [map_names] if isinstance(map_names, str) else list(map_names)
        if not names or any(type(name) is not str for name in names):
            raise ValueError("selected maps must be recorded map names")
        scoped = dict(spec)
        scoped["subjects"] = selected
        scoped["entrants"] = {label: spec["entrants"][label] for label in (subject, opponent)}
        inputs = dict(pinned)
        inputs["maps"] = {name: pinned["maps"][name] for name in names}
        inputs["wasm"] = {label: pinned["wasm"][label] for label in (subject, opponent)}
        if "snapshot_roots" in pinned:
            inputs["snapshot_roots"] = {label: pinned["snapshot_roots"][label] for label in (subject, opponent)}
        weights = pinned.get("weights", {})
        if not isinstance(weights, dict):
            raise TypeError("recorded weight pins must be an object")
        inputs["weights"] = {subject: weights[subject]} if subject in weights else {}
        if selected[0].get("weights"):
            # The game consumes the subject path, which must match the pinned
            # bytes even if a different old recorded path is still available.
            weight = dict(weights[subject])
            weight["path"] = selected[0]["weights"]
            inputs["weights"] = {subject: weight}
        scoped["opponent_ids"] = [opponent]
        if opponent_weights(spec, opponent):
            inputs["weights"][opponent] = weights[opponent]
        scoped["provenance"] = inputs
    except (KeyError, TypeError, ValueError) as exc:
        return [f"selected replay provenance unavailable: {exc}"]
    return provenance_gaps(scoped)


def read_rows(path: Path) -> list[dict]:
    """Evidence reads must not silently skip torn or malformed JSONL lines."""
    if not path.is_file():
        raise ArenaError(f"missing experiment rows: {path}")
    rows = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except ValueError as exc:
            raise ArenaError(f"invalid experiment row {number}: {exc}") from exc
        if not isinstance(row, dict):
            raise ArenaError(f"experiment row {number} is not an object")
        rows.append(row)
    return rows


# ----------------------------------------------------------------------------- engine and runner
def fastrun() -> Path:
    if FASTRUN.is_file():
        return FASTRUN
    raise ArenaError("fastrun is not built: bash tools/fastrun/build.sh")


def engine_wasm() -> Path:
    """The pinned engine (docs/reference/current-version.json) from the CLI's visualiser assets."""
    pinned = json.loads((ROOT / "docs" / "reference" / "current-version.json").read_text(encoding="utf-8"))
    engine = str((pinned.get("engine") or {}).get("version") or "").strip()
    if not engine:
        raise ArenaError("docs/reference/current-version.json pins no engine version")
    tag = "v" + engine.replace(".", "_")
    base = Path(os.environ.get("LOCALAPPDATA", "")) / "cambc" / "assets"
    found = sorted(base.glob(f"*/visualiser/assets/{tag}-*.wasm"), key=lambda p: p.stat().st_mtime, reverse=True)
    if not found:
        raise ArenaError(f"no visualiser engine {tag}-*.wasm under {base} (run the pinned CLI once: arena env)")
    return found[0]


def exp_dir(name: str) -> Path:
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", name):
        raise ArenaError(f"experiment names are OWNER/NAME (letters, digits, - _ .), not {name!r}")
    owner, short = name.split("/")
    if owner in (".", "..") or short in (".", ".."):
        raise ArenaError("experiment OWNER and NAME cannot be '.' or '..'")
    if any(part.endswith(".") for part in (owner, short)):
        raise ArenaError("experiment OWNER and NAME cannot end with '.' (Windows path aliases)")
    if any(re.fullmatch(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", part, re.IGNORECASE)
           for part in (owner, short)):
        raise ArenaError("experiment OWNER and NAME cannot be reserved Windows device names")
    directory = ROOT / ".cache" / owner / "exp" / short
    expected = ROOT.resolve() / ".cache" / owner / "exp" / short
    try:
        resolved = directory.resolve()
    except OSError as exc:
        raise ArenaError(f"cannot resolve experiment path: {name}: {exc}") from exc
    # Refuse redirects at every component: an owner/name junction must not make
    # run --force or close remove another experiment or an owner's entire cache.
    if resolved != expected:
        raise ArenaError(f"experiment path redirects outside its OWNER/exp/NAME location: {name}")
    return directory


# ----------------------------------------------------------------------------- spec
def parse_subject(text: str) -> dict:
    m = SUBJECT.match(text.strip())
    if not m:
        raise ArenaError(f"subject {text!r}: expected LABEL=BOT[+KNOB=V...][@WEIGHTS]")
    sets = {}
    for item in filter(None, m.group("sets").split("+")):
        k, v = item.split("=", 1)
        sets[k] = v
    weights = m.group("weights")
    if weights:  # absolute, so games find it whatever the caller's directory (fastrun runs from the root)
        found = next((p for p in (Path(weights), ROOT / weights) if p.is_file()), None)
        if found is None:
            raise ArenaError(f"subject {m.group('label')}: no weights file {weights}")
        weights = str(found.resolve())
    return {"label": m.group("label"), "bot": m.group("bot").strip(), "set": sets, "weights": weights}


def opponent_label(bot: str, knobs: dict, weights: str | None) -> str:
    """Plain opponents keep their bot id; overrides and weights (root-relative) are spelled out."""
    label = bot + "".join(f"+{k}={v}" for k, v in knobs.items())
    if weights:
        p = Path(weights)
        label += "@" + (p.relative_to(ROOT).as_posix() if p.is_relative_to(ROOT) else p.as_posix())
    return label


def ename(e) -> str:
    return getattr(e, "label", "") or e.bot.id


def opponent_descriptor(label: str) -> dict:
    o = parse_subject("x=" + label)
    return {"bot": o["bot"], "set": o["set"], "weights": o["weights"]}


def opponent_weights(spec: dict, label: str) -> str | None:
    return ((spec.get("opponent_descriptors") or {}).get(label) or {}).get("weights")


def build(spec: dict) -> tuple[list, list]:
    """Entrants for the subjects and opponents (snapshots built once, by content hash)."""
    items = []
    for s in spec["subjects"]:
        found = core.select(s["bot"])
        if len(found) != 1:
            raise ArenaError(f"subject {s['label']}: {s['bot']!r} matches {len(found)} bots")
        items.append((found[0], s["set"], s["label"]))
    opponents = []
    named = json.loads((ROOT / "bots" / "gauntlet.json").read_text(encoding="utf-8")) if (ROOT / "bots" / "gauntlet.json").is_file() else {}
    selectors = []
    for sel in spec["opponents"]:  # named sets (bots/gauntlet.json) expand to their bots
        selectors += named[sel] if isinstance(named.get(sel), list) else [sel]
    for sel in selectors:  # BOT[+KNOB=V...][@WEIGHTS], as for subjects
        o = parse_subject("x=" + sel)
        found = core.select(o["bot"])
        if not found:
            raise ArenaError(f"opponent {sel!r} matches no bot")
        for b in found:
            item = (b, o["set"], opponent_label(b.id, o["set"], o["weights"]))
            if item not in opponents:
                opponents.append(item)
    items += opponents
    built = (_private_prepare(items, spec["name"].split("/")[0], spec.get("reuse_shared_snapshots", False))
             if spec.get("private_snapshots") else core.prepare_many(items))
    bad = [e for e in built if not e.ok]
    if bad:
        raise ArenaError("build failed: " + ", ".join(ename(e) for e in bad) + "\n" + bad[0].log[-2000:])
    return built[:len(spec["subjects"])], built[len(spec["subjects"]):]


def wasm(e) -> str:
    return str(e.path / ".cambc" / "build-release" / "bot.wasm")


def recorded_snapshot_root(spec: dict, label: str) -> Path:
    pinned = spec.get("provenance") or {}
    if not isinstance(pinned, dict):
        raise ArenaError("private snapshot provenance must be an object")
    if "reuse_shared_snapshots" in spec and type(spec["reuse_shared_snapshots"]) is not bool:
        raise ArenaError("shared snapshot reuse opt-in must be boolean")
    if "snapshot_roots" not in pinned:
        if spec.get("private_snapshots"):
            raise ArenaError("private experiment snapshot roots are missing")
        return core.CACHE / "snap"
    roots = pinned["snapshot_roots"]
    if (spec.get("private_snapshots") is not True or not isinstance(roots, dict) or not isinstance(spec.get("entrants"), dict) or
            set(roots) != set(spec.get("entrants", {})) or label not in roots or
            not all(type(value) is str for value in roots.values())):
        raise ArenaError("private experiment requires explicit roots for every entrant")
    identities = {}
    for source, key in spec["entrants"].items():
        if identities.setdefault(key, roots[source]) != roots[source]:
            raise ArenaError("same snapshot key has conflicting roots")
    exp_dir(spec["name"])  # Validate OWNER and reject redirected experiment ancestry.
    owner = spec["name"].split("/")[0]
    if owner.casefold() == "arena":
        raise ArenaError("private snapshots require an independent owner")
    lexical = Path(roots[label])
    lexical = lexical if lexical.is_absolute() else ROOT / lexical
    if lexical == core.CACHE / "snap" and spec.get("reuse_shared_snapshots") is not True:
        raise ArenaError("shared snapshot roots require explicit reuse opt-in")
    root = core.resolve_snapshot_root(roots[label], owner)
    if roots[label] != root.relative_to(ROOT).as_posix():
        raise ArenaError("recorded snapshot root is not canonical")
    return root


def _private_prepare(items: list[tuple], owner: str, reuse_shared: bool = False) -> list:
    if type(reuse_shared) is not bool:
        raise ArenaError("shared snapshot reuse opt-in must be boolean")
    if owner.casefold() == "arena":
        raise ArenaError("private snapshots require an independent owner")
    root = core.resolve_snapshot_root(Path(".cache") / owner / "snap", owner)
    built, missing = [None] * len(items), []
    for index, item in enumerate(items):
        bot, settings, label = item[:3]
        contents = item[3] if len(item) > 3 else core.read_closure(bot)
        version = core.version_of(core.apply_overrides(bot, contents, settings) if settings else contents, settings)
        key = f"{bot.id}@{version}"
        if not reuse_shared:
            missing.append((index, (bot, settings, label, contents)))
            continue
        with core.FileLock(core.CACHE / "snap" / f"{version}.lock"):
            try:
                candidate = core.cached_snapshot(key)
                snap = core.CACHE / "snap" / version
                if dict(candidate.overrides) != settings:
                    raise ArenaError("global snapshot configuration/state differs")
                (snap / ".ready").touch()
                built[index] = core.Entrant(bot, version, snap / "bots" / bot.suite / bot.name, True,
                                           overrides=tuple(sorted(settings.items())), label=label)
            except (ArenaError, OSError, ValueError, KeyError):
                missing.append((index, (bot, settings, label, contents)))
    local = core.prepare_many([item for _, item in missing], snapshot_root=root, snapshot_owner=owner) if missing else []
    for (index, _), entry in zip(missing, local):
        if entry.ok:
            core.cached_snapshot(entry.key, snapshot_root=root, snapshot_owner=owner)
        built[index] = entry
    return built


# ----------------------------------------------------------------------------- running
def recover_native_cleanup() -> None:
    """Keep exact Job/process/Slot ownership through repeated cancellation until empty."""
    while True:
        try:
            _NATIVE_RECOVERY_LOCK.acquire()
            break
        except BaseException:
            continue
    try:
        while native._PENDING_CLEANUP:
            try:
                if native.confirm_pending_cleanup():
                    break
            except BaseException as error:
                try:
                    print("Owned experiment process cleanup still unconfirmed: " + repr(error), file=sys.stderr, flush=True)
                except BaseException:
                    pass
            try:
                time.sleep(1)
            except BaseException:
                continue
    finally:
        _NATIVE_RECOVERY_LOCK.release()


def native_chunk(argv: list[str], directory: Path, wanted: int, minimum: int, poll: float | None) -> dict:
    """The shared transport owns the sole slot acquisition and private Job."""
    try:
        record = native._slotted_command(lambda count: argv + ["--procs", str(max(1, count))],
                                         ROOT, directory, CHUNK_TIMEOUT_SECONDS,
                                         jobs=wanted, minimum=minimum, poll=poll)
    finally:
        # This runs inside the caller's run/replay operation guard. A second
        # interruption cannot discard retained handles or release that guard.
        recover_native_cleanup()
    if record.get("tree_exit_confirmed") is not True:
        raise ArenaError("fastrun process tree was not confirmed empty")
    return record


def perspective(r: dict, subject: str, opponent: str) -> dict:
    """A fastrun row from the subject's side (team a_side)."""
    a = 0 if r["a_side"] == "A" else 1
    out = {"map": r["map"], "side": r["a_side"], "subject": subject, "opponent": opponent,
           "result": "draw" if r["winner"] == "draw" else ("win" if r["winner"] == "a" else "loss"),
           "rounds": r["rounds"], "gems": r["gems"][a], "opp_gems": r["gems"][1 - a]}
    for k in ("kills", "deaths", "gold", "spent", "collected", "monster_damage", "champion_damage"):
        if k in r:
            out[k], out["opp_" + k] = r[k][a], r[k][1 - a]
    out["health"] = {k: r[k][a] for k in HEALTH if r[k][a]}
    out["opp_health"] = {k: r[k][1 - a] for k in HEALTH if r[k][1 - a]}
    if r.get("error"):
        out["health"]["runner_error"] = r["error"][:200]
    if r.get("truncated"):
        out["truncated"] = True
    return out


def run_games(games: list[str], bots: dict[str, str], out: Path, rounds: int = 0, bundles: Path | None = None,
              *, on_chunk=None) -> list[dict]:
    """Play game lines (MAP SIDE KEY_A W_A KEY_B W_B TAG) on fastrun in chunks holding pool slots."""
    rows: list[dict] = []
    engine = engine_wasm()
    for c in range(0, len(games), CHUNK):
        part = games[c:c + CHUNK]
        listing = out.with_suffix(f".games{c // CHUNK}.txt")
        part_out = out.with_suffix(f".rows{c // CHUNK}.jsonl")
        listing.write_text("\n".join(part) + "\n", encoding="utf-8")
        part_out.unlink(missing_ok=True)
        argv = [str(fastrun()), "--engine", str(engine), "--games", str(listing), "--out", str(part_out)]
        for key, path in bots.items():
            argv += ["--bot", f"{key}={path}"]
        if rounds:
            argv += ["--rounds", str(rounds)]
        if bundles:
            argv += ["--bundles", str(bundles)]
        # Small paired probes can run serially on a brief single-slot handoff.
        # Bulk batches retain their throughput-oriented four-slot minimum.
        wanted = min(core.slot_count(), len(part))
        small_probe = len(part) <= 4
        minimum = 1 if small_probe else min(4, wanted)
        transport = out.parent / ".native-commands" / uuid.uuid4().hex
        transport.mkdir(parents=True)
        process = native_chunk(argv, transport, wanted, minimum, .05 if small_probe else None)
        failed = (process.get("timed_out") is not False or process.get("forced_termination") or
                  type(process.get("returncode")) is not int or process["returncode"] != 0)
        got = []
        if not failed:
            try:
                got = read_rows(part_out) if part_out.is_file() else []
            except ArenaError as error:
                error.compiled_process_record = process
                raise
        if failed or len(got) != len(part):
            stderr = transport / "stderr.bin"
            detail = stderr.read_text(encoding="utf-8", errors="replace").strip()[-1500:] if stderr.is_file() else ""
            count = "unread" if failed else str(len(got))
            error = ArenaError(f"fastrun failed ({process.get('returncode')}, {count}/{len(part)} rows, "
                               f"timed_out={process.get('timed_out')}, forced_termination={process.get('forced_termination')}): {detail}")
            error.compiled_process_record = process
            raise error
        if on_chunk is not None:
            # Publish durable normalized progress before discarding any raw chunk.
            on_chunk(part, got)
        listing.unlink(missing_ok=True)
        part_out.unlink(missing_ok=True)
        # Retain compact containment receipts, not successful raw process streams.
        for name in ("stdout.bin", "stderr.bin"):
            (transport / name).unlink(missing_ok=True)
        rows += got
    return rows


def write_spec(directory: Path, spec: dict) -> None:
    """Publish complete metadata atomically for concurrent checkups and readers."""
    write_document(directory / "spec.json", spec)


def write_document(path: Path, value: dict | list | str) -> None:
    temporary = path.with_name("." + path.stem + "-" + uuid.uuid4().hex + ".tmp")
    try:
        with temporary.open("w", encoding="utf-8", newline="\n") as stream:
            if isinstance(value, str):
                stream.write(value)
            else:
                json.dump(value, stream, indent=1, allow_nan=False)
            stream.flush()
            os.fsync(stream.fileno())
        for attempt in range(20):
            try:
                temporary.replace(path)
                break
            except PermissionError:
                # Windows readers may briefly deny replacement while holding
                # the old file open. Keep the old complete document intact.
                if attempt == 19:
                    raise
                time.sleep(.01)
    finally:
        temporary.unlink(missing_ok=True)


def read_spec(path: Path) -> dict:
    """Read an atomic metadata document despite brief Windows sharing conflicts."""
    for attempt in range(20):
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(.01)


def cmd_run(args) -> int:
    with operation_lock(exp_dir(args.name)):
        return _run_experiment(args)


def _run_experiment(args) -> int:
    if getattr(args, "reuse_shared_snapshots", False) and not getattr(args, "private_snapshots", False):
        raise ArenaError("--reuse-shared-snapshots requires --private-snapshots")
    d = exp_dir(args.name)
    if (d / "spec.json").is_file() and not args.force:
        raise ArenaError(f"{args.name} exists (interrupted? `arena exp resume {args.name}` runs only missing games; else show it, close it, or --force to replace it)")
    with replay_close_lock(d):
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)
    subjects = [parse_subject(s) for s in args.subject or []]
    for sel in args.subjects or []:  # bulk subjects: every bot a selector matches, labelled by its id
        for b in core.select(sel):
            subjects.append({"label": b.id.replace("/", "."), "bot": b.id, "set": {}, "weights": None})
    spec = {"name": args.name, "question": args.question, "created": time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime()),
            "subjects": subjects, "opponents": [o for v in args.vs for o in v.split(",") if o],
            "maps": args.maps, "sides": args.sides, "rounds": args.rounds, "keep": args.keep, "status": "preparing"}
    if getattr(args, "private_snapshots", False):
        spec["private_snapshots"] = True
        spec["reuse_shared_snapshots"] = getattr(args, "reuse_shared_snapshots", False)
    if not spec["subjects"] or not spec["opponents"]:
        raise ArenaError("an experiment needs at least one --subject and one --vs opponent")
    labels = [s["label"] for s in spec["subjects"]]
    if len(set(labels)) != len(labels):
        raise ArenaError("subject labels must be unique")
    spec["preparation"] = {"state": "started", "started": spec["created"],
                           "notice": "Preparation metadata records intent and outcomes; it is not proof of a live worker."}
    write_spec(d, spec)
    try:
        subjects, opponents = build(spec)
        spec["entrants"] = {s["label"]: e.key for s, e in zip(spec["subjects"], subjects)}
        spec["entrants"].update({ename(e): e.key for e in opponents})
        spec["opponent_ids"] = [ename(e) for e in opponents]
        spec["opponent_descriptors"] = {ename(e): {"bot": e.bot.id, "set": dict(getattr(e, "overrides", ())),
                                                 "weights": opponent_descriptor(ename(e))["weights"]
                                                 if ename(e) != e.bot.id else None} for e in opponents}
        maps = [core.map_path(m) for m in core.map_set(args.maps)]
        weighted = spec["subjects"] + [{"label": k, "weights": v["weights"]}
                                       for k, v in spec["opponent_descriptors"].items() if v["weights"]]
        spec["provenance"] = provenance(weighted, maps,
                                         dict(zip([s["label"] for s in spec["subjects"]], subjects)) |
                                         {ename(e): e for e in opponents})
        if spec.get("private_snapshots"):
            entries = dict(zip([s["label"] for s in spec["subjects"]], subjects)) | {ename(e): e for e in opponents}
            spec["provenance"]["snapshot_roots"] = {label: entry.path.parents[3].relative_to(ROOT).as_posix()
                                                    for label, entry in entries.items()}
            for label in entries:
                recorded_snapshot_root(spec, label)
        sides = ["A", "B"] if args.sides == "both" else [args.sides]
        bots = {f"s{i}": wasm(e) for i, e in enumerate(subjects)}
        bots.update({f"o{j}": wasm(e) for j, e in enumerate(opponents)})
        games = []
        for i, s in enumerate(spec["subjects"]):
            for j, o in enumerate(spec["opponent_ids"]):
                for m in maps:
                    for side in sides:
                        games.append(f"{m} {side} s{i} {s['weights'] or '-'} o{j} "
                                     f"{opponent_weights(spec, o) or '-'} s{i}.o{j}")
        spec["games"] = len(games)
        spec["status"] = "open"
        spec["preparation"].update(state="complete", finished=time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime()))
        write_spec(d, spec)
    except BaseException as error:
        interrupted = isinstance(error, KeyboardInterrupt)
        spec["status"] = "preparation-interrupted" if interrupted else "preparation-failed"
        spec["preparation"].update(state="interrupted" if interrupted else "failed",
                                   finished=time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime()),
                                   error={"type": type(error).__name__, "message": str(error)[:4000]})
        try:
            write_spec(d, spec)
        except OSError as recording_error:
            print(f"could not record preparation failure: {recording_error}", file=sys.stderr)
        raise
    return execute_prepared(d, spec, games, bots)


def operation_lock(directory: Path):
    """Run/resume/force/close exclude one another; cleanup never removes this lock."""
    return core.FileLock(directory.parent / ".operation-locks" / (directory.name + ".lock"), poll=.05, timeout=0)


def progress_fingerprint(spec: dict) -> str:
    """Stable import binding; mutable lifecycle/question fields do not change game inputs."""
    fields = ("subjects", "entrants", "opponent_ids", "opponents", "maps", "sides", "rounds", "games", "provenance")
    try:
        inputs = {k: spec.get(k) for k in fields}
        if "opponent_descriptors" in spec:
            inputs["opponent_descriptors"] = spec["opponent_descriptors"]
        if "reuse_shared_snapshots" in spec:
            inputs["reuse_shared_snapshots"] = spec["reuse_shared_snapshots"]
        if "private_snapshots" in spec and type(spec["private_snapshots"]) is not bool:
            raise ValueError("private snapshot mode must be boolean")
        if spec.get("private_snapshots") is True:
            inputs["private_snapshots"] = True
        data = json.dumps(inputs, sort_keys=True, separators=(",", ":"), allow_nan=False)
    except (TypeError, ValueError) as exc:
        raise ArenaError(f"invalid experiment input fingerprint: {exc}") from exc
    return hashlib.sha256(data.encode("utf-8")).hexdigest()


def row_key(row: dict) -> tuple:
    return tuple(row.get(k) for k in ("subject", "opponent", "map", "side"))


def game_plan(spec: dict) -> tuple[list[str], list[tuple]]:
    """Expand recorded identities only, never current selectors or source versions."""
    subjects, opponents = spec.get("subjects"), spec.get("opponent_ids")
    pinned = spec.get("provenance")
    if not isinstance(pinned, dict):
        raise ArenaError("invalid recorded experiment provenance")
    maps = pinned.get("maps", {})
    if (not isinstance(subjects, list) or not all(isinstance(s, dict) and
            type(s.get("label")) is str and type(s.get("bot")) is str and
            isinstance(s.get("set", {}), dict) and (s.get("weights") is None or type(s["weights"]) is str)
            for s in subjects) or not isinstance(opponents, list) or not all(type(o) is str for o in opponents)
            or not isinstance(maps, dict) or not all(type(m) is str and type(h) is str for m, h in maps.items())
            or not isinstance(spec.get("entrants"), dict) or type(spec.get("games")) is not int):
        raise ArenaError("invalid recorded experiment matrix types")
    labels = [s["label"] for s in subjects]
    if (not subjects or len(set(labels)) != len(labels) or not opponents or len(set(opponents)) != len(opponents)
            or set(labels) & set(opponents) or not maps
            or set(spec.get("entrants", {})) != set(labels) | set(opponents)
            or spec.get("sides") not in ("both", "A", "B")
            or type(spec.get("rounds")) is not int or spec["rounds"] < 0):
        raise ArenaError("invalid recorded experiment game matrix")
    sides = ["A", "B"] if spec["sides"] == "both" else [spec["sides"]]
    paths = {name: core.map_path(name) for name in maps}
    games, keys = [], []
    for i, subject in enumerate(subjects):
        for j, opponent in enumerate(opponents):
            for name in maps:
                for side in sides:
                    games.append(f"{paths[name]} {side} s{i} {subject.get('weights') or '-'} o{j} "
                                 f"{opponent_weights(spec, opponent) or '-'} s{i}.o{j}")
                    keys.append((subject["label"], opponent, name, side))
    if spec.get("games") != len(keys):
        raise ArenaError("recorded experiment game count does not match its matrix")
    return games, keys


def validate_progress_rows(rows: list, expected: set[tuple], *, spec: dict | None = None) -> dict[tuple, dict]:
    if not isinstance(rows, list):
        raise ArenaError("progress rows must be a list")
    result = {}
    telemetry = ("kills", "deaths", "gold", "spent", "collected", "monster_damage", "champion_damage")
    allowed = {"subject", "opponent", "map", "side", "result", "rounds", "gems", "opp_gems", "health", "opp_health", "truncated"}
    private = spec is not None and spec.get("private_snapshots") is True
    if private:
        allowed.add("diagnostic_only")
    allowed.update(telemetry); allowed.update("opp_" + name for name in telemetry)
    for index, row in enumerate(rows, 1):
        valid = isinstance(row, dict)
        if valid:
            valid = (not set(row) - allowed and all(type(row.get(k)) is str for k in ("subject", "opponent", "map", "side"))
                     and row_key(row) in expected and row.get("result") in ("win", "loss", "draw")
                     and all(type(row.get(k)) is int and row[k] >= 0 for k in ("rounds", "gems", "opp_gems"))
                     and all(isinstance(row.get(k), dict) and all(
                         (type(v) is str if n == "runner_error" else type(v) is int and v >= 0)
                         for n, v in row[k].items()) for k in ("health", "opp_health"))
                     and ("truncated" not in row or type(row["truncated"]) is bool))
            valid = valid and (row.get("diagnostic_only") is True if private else "diagnostic_only" not in row)
            for name in telemetry:
                for key in (name, "opp_" + name):
                    numeric = (int, float) if name.endswith("_damage") else (int,)
                    if key in row and not (type(row[key]) in numeric and math.isfinite(row[key]) and row[key] >= 0):
                        valid = False
        if not valid:
            raise ArenaError(f"invalid or unexpected progress row {index}")
        key = row_key(row)
        if key in result:
            raise ArenaError(f"duplicate progress game key: {key}")
        result[key] = row
    return result


def read_progress(path: Path, spec: dict, expected: set[tuple]) -> dict[tuple, dict]:
    try:
        document = read_spec(path)
    except (OSError, ValueError) as exc:
        raise ArenaError(f"invalid progress document {path}: {exc}") from exc
    if (not isinstance(document, dict) or document.get("schema") != "arena-experiment-progress/v1"
            or document.get("fingerprint") != progress_fingerprint(spec)):
        raise ArenaError(f"progress input fingerprint/schema mismatch: {path}")
    return validate_progress_rows(document.get("rows"), expected, spec=spec)


def normalized_chunk(spec: dict, part: list[str], raw: list[dict]) -> list[dict]:
    wanted = set()
    for line in part:
        fields = line.split()
        if len(fields) != 7:
            raise ArenaError("invalid recorded game line")
        wanted.add((Path(fields[0]).stem, fields[1], fields[6]))
    seen, rows = set(), []
    for row in raw:
        try:
            if "diagnostic_only" in row:
                raise ArenaError("runner output cannot supply the diagnostic progress marker")
            physical = (row["map"], row["a_side"], row["tag"])
            if physical not in wanted or physical in seen:
                raise ArenaError("unexpected or duplicate returned game key")
            si, oj = row["tag"].split(".")
            if not re.fullmatch(r"s[0-9]+", si) or not re.fullmatch(r"o[0-9]+", oj) or row.get("winner") not in ("a", "b", "draw"):
                raise ArenaError("invalid returned game identity/result")
            if "truncated" in row and type(row["truncated"]) is not bool:
                raise ArenaError("invalid returned truncation marker")
            for name in ("gems", *HEALTH):
                if not isinstance(row[name], list) or len(row[name]) != 2 or any(type(v) is not int or v < 0 for v in row[name]):
                    raise ArenaError(f"invalid returned {name}")
            rows.append(perspective(row, spec["subjects"][int(si[1:])]["label"], spec["opponent_ids"][int(oj[1:])]))
            if spec.get("private_snapshots") is True:
                rows[-1]["diagnostic_only"] = True
            seen.add(physical)
        except (KeyError, TypeError, ValueError, IndexError) as exc:
            raise ArenaError(f"invalid returned game row: {exc}") from exc
    if seen != wanted or len(part) != len(wanted):
        raise ArenaError("returned game matrix incomplete or duplicated")
    return rows


def execute_prepared(directory: Path, spec: dict, games: list[str], bots: dict[str, str],
                     saved: dict[tuple, dict] | None = None, *, collect_flagged: bool = True) -> int:
    t0 = time.time()
    canonical_games, keys = game_plan(spec)
    if games != canonical_games:
        raise ArenaError("prepared game inputs do not match recorded matrix")
    expected = set(keys)
    completed = validate_progress_rows(list((saved or {}).values()), expected, spec=spec)
    def checkpoint(part, raw):
        gaps = provenance_gaps(spec)
        if gaps:
            raise ArenaError("experiment inputs changed during execution: " + "; ".join(gaps))
        group = validate_progress_rows(normalized_chunk(spec, part, raw), expected, spec=spec)
        for key, row in group.items():
            if key in completed:
                raise ArenaError(f"duplicate completed game key: {key}")
            completed[key] = row
        write_document(directory / "progress.json", {"schema": "arena-experiment-progress/v1",
                       "fingerprint": progress_fingerprint(spec), "rows": [completed[k] for k in keys if k in completed]})
    missing = [game for game, key in zip(games, keys) if key not in completed]
    if missing:
        raw = run_games(missing, bots, directory / ("run-" + uuid.uuid4().hex), rounds=spec["rounds"], on_chunk=checkpoint)
        # Compatibility with ordinary run_games substitutes that return once without callbacks.
        if len(completed) != len(keys):
            checkpoint(missing, raw)
    gaps = provenance_gaps(spec)
    if gaps:
        raise ArenaError("experiment inputs changed during execution: " + "; ".join(gaps))
    if set(completed) != expected:
        raise ArenaError("experiment remains incomplete")
    rows = [completed[k] for k in keys]
    write_document(directory / "rows.jsonl", "".join(json.dumps(r, sort_keys=True, separators=(",", ":")) + "\n" for r in rows))
    flagged = [r for r in rows if r["health"] or r["opp_health"]]
    if collect_flagged and flagged and spec["keep"] == "flagged":
        again = [g for g, r in zip(games, rows) if r["health"] or r["opp_health"]][:20]
        run_games(again, bots, directory / "flagged-run", rounds=spec["rounds"], bundles=directory / "flagged")
    summary = summarize(spec, rows)
    write_document(directory / "summary.json", summary)
    print(f"{spec['name']}: {len(rows)} games in {time.time() - t0:.0f} s; {len(flagged)} with health flags")
    census(rows) if len(spec["subjects"]) > 6 else show(spec, summary)
    return 0


def cmd_resume(args) -> int:
    directory = exp_dir(args.name)
    with operation_lock(directory):
        directory, spec = load(args.name)
        if (not isinstance(spec, dict) or spec.get("status") != "open" or
                not isinstance(spec.get("preparation"), dict) or spec["preparation"].get("state") != "complete"):
            raise ArenaError("resume requires a prepared open experiment")
        pinned = spec.get("provenance") or {}
        if (not isinstance(pinned, dict) or
                not all(type(pinned.get(k)) is str and pinned[k] for k in ("bundle", "engine", "runner", "runner_source", "toolchain")) or
                not all(isinstance(pinned.get(k), dict) for k in ("maps", "wasm", "runner_libraries", "weights")) or
                not pinned["maps"] or not pinned["wasm"]):
            raise ArenaError("resume requires complete pinned runtime/source provenance")
        games, keys = game_plan(spec)
        gaps = provenance_gaps(spec)
        if gaps:
            raise ArenaError("cannot resume changed inputs: " + "; ".join(gaps))
        entries = [replay_entrant(spec, s["label"], s, cached_only=True) for s in spec["subjects"]]
        opponents = [replay_entrant(spec, o, None, cached_only=True) for o in spec["opponent_ids"]]
        bots = {f"s{i}": wasm(e) for i, e in enumerate(entries)} | {f"o{j}": wasm(e) for j, e in enumerate(opponents)}
        saved, canonical = {}, {}
        sources = []
        if (directory / "rows.jsonl").is_file():
            canonical = validate_progress_rows(read_rows(directory / "rows.jsonl"), set(keys), spec=spec)
            sources.append(canonical)
        for path in (directory / "progress.json", Path(args.progress_from) if getattr(args, "progress_from", None) else None):
            if path is not None and path.is_file():
                sources.append(read_progress(path, spec, set(keys)))
            elif path is not None and path != directory / "progress.json":
                raise ArenaError(f"missing imported progress: {path}")
        for source in sources:
            for key, row in source.items():
                if key in saved and saved[key] != row:
                    raise ArenaError(f"conflicting progress for game key: {key}")
                saved[key] = row
        try:
            summary_ready = read_spec(directory / "summary.json") == summarize(spec, [canonical[k] for k in keys])
        except (OSError, ValueError, KeyError):
            summary_ready = False
        gaps = provenance_gaps(spec)
        if gaps:
            raise ArenaError("cannot resume changed inputs: " + "; ".join(gaps))
        if len(canonical) == len(keys) and summary_ready:
            print(f"{args.name}: complete; no games replayed")
            return 0
        # Persist an observer import before launching anything; interrupted missing work remains recoverable.
        if saved:
            write_document(directory / "progress.json", {"schema": "arena-experiment-progress/v1",
                           "fingerprint": progress_fingerprint(spec), "rows": [saved[k] for k in keys if k in saved]})
        return execute_prepared(directory, spec, games, bots, saved, collect_flagged=False)


# ----------------------------------------------------------------------------- reporting
def summarize(spec: dict, rows: list[dict]) -> dict:
    by_subject: dict[str, dict] = {}
    for r in rows:
        by_subject.setdefault(r["subject"], {})[(r["opponent"], r["map"], r["side"])] = r
    base_label = spec["subjects"][0]["label"]
    base = by_subject.get(base_label, {})
    opponents = sorted({r["opponent"] for r in rows})
    out = {"games": len(rows), "subjects": {}}
    for label, games in by_subject.items():
        entry = {}
        for opp in opponents + ["all"]:
            keys = [k for k in games if opp == "all" or k[0] == opp]
            if not keys:
                continue
            sel = [games[k] for k in keys]
            w = sum(r["result"] == "win" for r in sel) + 0.5 * sum(r["result"] == "draw" for r in sel)
            lo, hi = stats.wilson(w, len(sel))
            won = [r["rounds"] for r in sel if r["result"] == "win"]
            e = {"n": len(sel), "score": w / len(sel), "ci": [lo, hi],
                 "margin": sum(r["gems"] - r["opp_gems"] for r in sel) / len(sel),
                 "rounds_to_win": sum(r["rounds"] if r["result"] == "win" else 2000 for r in sel) / len(sel),
                 "mean_win_rounds": sum(won) / len(won) if won else None,
                 "unhealthy": sum(bool(r["health"]) for r in sel), "opp_unhealthy": sum(bool(r["opp_health"]) for r in sel)}
            if label != base_label:
                sub_b = {k: base[k] for k in keys if k in base}
                sub_v = {k: games[k] for k in keys if k in base}
                p = stats.paired(sub_b, sub_v)
                if p.get("n"):
                    speed = stats.paired(sub_b, sub_v, margin=lambda r: -(r["rounds"] if r["result"] == "win" else 2000))
                    e["vs_base"] = {"n": p["n"], "wins": [p["wins_base"], p["wins_var"]], "gained": len(p["gained"]),
                                    "lost": len(p["lost"]), "mcnemar_p": p["mcnemar_p"], "margin_diff": p["diff_mean"],
                                    "margin_se": p["diff_se"], "speed_gain": speed["diff_mean"], "speed_se": speed["diff_se"]}
            entry[opp] = e
        out["subjects"][label] = entry
    return out


def show(spec: dict, summary: dict, by: str = "opponent") -> None:
    print(f"Q: {spec['question']}")
    print(f"maps {spec['maps']}, sides {spec['sides']}, opponents {', '.join(spec['opponents'])}; "
          f"baseline {spec['subjects'][0]['label']}")
    for label, entry in summary["subjects"].items():
        for opp, e in entry.items():
            line = (f"  {label:12s} vs {opp:28s} n {e['n']:5d}  score {100 * e['score']:5.1f}% "
                    f"[{100 * e['ci'][0]:.0f}-{100 * e['ci'][1]:.0f}]  margin {e['margin']:+6.0f}  to-win {e['rounds_to_win']:6.0f}")
            if e["unhealthy"] or e["opp_unhealthy"]:
                line += f"  UNHEALTHY {e['unhealthy']} (opp {e['opp_unhealthy']})"
            print(line)
            if "vs_base" in e:
                v = e["vs_base"]
                print(f"  {'':12s}    paired vs base: wins {v['wins'][0]}->{v['wins'][1]} (+{v['gained']}/-{v['lost']}, "
                      f"p={v['mcnemar_p']:.3f})  margin {v['margin_diff']:+.0f}+-{v['margin_se']:.0f}  "
                      f"rounds {-v['speed_gain']:+.0f}+-{v['speed_se']:.0f}")
    if spec.get("status") == "closed":
        print(f"closed {spec.get('closed')}: {spec.get('note')}")


def load(name: str) -> tuple[Path, dict]:
    d = exp_dir(name)
    if not (d / "spec.json").is_file():
        raise ArenaError(f"no experiment {name}")
    spec = read_spec(d / "spec.json")
    if (spec.get("private_snapshots") or "snapshot_roots" in (spec.get("provenance") or {})) and spec.get("name") != name:
        raise ArenaError("private experiment owner/name disagrees with its directory")
    return d, spec


def result_counts(rows: list[dict]) -> dict:
    """Strict wins stay separate from draws and wins against unhealthy opponents."""
    wins = sum(r["result"] == "win" for r in rows)
    return {"games": len(rows), "wins": wins,
            "draws": sum(r["result"] == "draw" for r in rows),
            "losses": sum(r["result"] == "loss" for r in rows),
            "strict_win_rate": wins / len(rows) if rows else None,
            "healthy_wins": sum(r["result"] == "win" and not r["health"] and not r["opp_health"]
                                and not r.get("truncated") for r in rows),
            "own_flagged_games": sum(bool(r["health"]) for r in rows),
            "opponent_flagged_games": sum(bool(r["opp_health"]) for r in rows),
            "truncated_games": sum(bool(r.get("truncated")) for r in rows)}


def win_floor(report: dict, spec: dict, opponent: str) -> dict:
    """Assess only the measured complete current-pool win floor, never strategic admission."""
    reasons = []
    if spec.get("private_snapshots") or "snapshot_roots" in (spec.get("provenance") or {}):
        reasons.append("private snapshot experiments are diagnostic; qualification retention is unsupported")
    recorded = spec.get("entrants", {}).get(opponent)
    current = None
    if report["coverage"]["opponents"].count(opponent) != 1:
        reasons.append("requested opponent is not a unique recorded opponent")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", opponent):
        reasons.append("win-floor opponent must be an exact suite/bot ID")
    else:
        try:
            target = core.bot(opponent)
            if target.id != opponent:
                raise ArenaError("opponent resolved to a different bot")
            current = f"{target.id}@{core.version(target)}"
            if current != recorded:
                reasons.append("recorded opponent is not its exact current snapshot")
        except (ArenaError, OSError) as exc:
            reasons.append(f"current opponent identity unavailable: {exc}")
    pool = core.map_set("pool")
    maps = list((spec.get("provenance") or {}).get("maps", {})) or core.map_set(spec["maps"])
    pool_count = len(pool)
    required_games = 2 * pool_count
    if not pool_count or len(set(pool)) != pool_count or len(maps) != pool_count or set(maps) != set(pool):
        reasons.append(f"win floor requires exactly all {pool_count} current pool maps (nonempty and unique)")
    if spec.get("sides") != "both":
        reasons.append("win floor requires both sides")
    if spec.get("rounds"):
        reasons.append("win floor requires complete games")
    if not report["integrity_pass"]:
        reasons.append("experiment row integrity failed")
    if report["provenance_gaps"]:
        reasons.append("experiment input provenance is missing or changed")
    states = {}
    for account in report["bots"]:
        gaps = list(reasons)
        if account["source_error"] or not account["current_snapshot"]:
            gaps.append("current subject identity unavailable")
        eligible = [m for m in account["observations"] if m["source_current"] and m["default_configuration"]]
        panel = None
        if len(eligible) != 1:
            gaps.append("requires one uniquely measured current default subject panel")
        else:
            if eligible[0]["snapshot"] != account["current_snapshot"]:
                gaps.append("subject identity changed during audit")
            panel = eligible[0]["by_opponent"].get(opponent)
            if panel is None or panel["games"] != required_games or not eligible[0]["matrix_complete"]:
                gaps.append(f"requires {required_games} unique complete target game keys")
            if panel is not None:
                if panel["own_flagged_games"]:
                    gaps.append("subject has recorded health flags in the target panel")
                if panel["truncated_games"]:
                    gaps.append("target panel contains truncated games")
                if panel["healthy_wins"] < 1:
                    gaps.append("no strict win with both teams healthy")
        status = "passed" if not gaps else "blocked"
        states[status] = states.get(status, 0) + 1
        account["win_floor"] = {"opponent": opponent, "opponent_snapshot": recorded,
                                "required_games": required_games, "status": status, "qualified": not gaps,
                                "wins": panel["wins"] if panel else None,
                                "draws": panel["draws"] if panel else None,
                                "losses": panel["losses"] if panel else None,
                                "healthy_wins": panel["healthy_wins"] if panel else None,
                                "reasons": gaps}
    return {"opponent": opponent, "opponent_snapshot": recorded, "current_opponent_snapshot": current,
            "required_games_per_bot": required_games, "required_maps": pool, "sides": ["A", "B"],
            "status_counts": states,
            "all_passed": bool(report["bots"]) and all(a["win_floor"]["qualified"] for a in report["bots"]),
            "limits": ["Passing establishes only the measured strict-win floor on this exact pool panel.",
                       "Strategic distinctness, mechanism witnesses, stalls and unseen-map strength remain separate requirements."]}


def audit(spec: dict, rows: list[dict], selector: str = DEVELOPMENT, win_floor_vs: str | None = None) -> dict:
    """Account for current bots from finite experiment rows; style never establishes contribution."""
    from collections import Counter, defaultdict
    subjects = {s["label"]: s for s in spec["subjects"]}
    entrants = spec.get("entrants", {})
    opponents = spec.get("opponent_ids") or [k for k in entrants if k not in subjects]
    maps = list((spec.get("provenance") or {}).get("maps", {})) or core.map_set(spec["maps"])
    sides = ["A", "B"] if spec["sides"] == "both" else [spec["sides"]]
    expected = {(o, m, s) for o in opponents for m in maps for s in sides}
    issues, by_subject = [], defaultdict(list)
    if len(subjects) != len(spec["subjects"]):
        issues.append("duplicate subject labels in spec")
    if not expected:
        issues.append("empty expected game matrix")
    for label, subject in subjects.items():
        if not str(entrants.get(label, "")).startswith(subject["bot"] + "@"):
            issues.append(f"missing or mismatched subject identity: {label}")
    for index, row in enumerate(rows, 1):
        label = row.get("subject")
        key = (row.get("opponent"), row.get("map"), row.get("side"))
        valid = (all(type(row.get(k)) is str for k in ("subject", "opponent", "map", "side"))
                 and label in subjects and key in expected and row.get("result") in ("win", "loss", "draw")
                 and all(type(row.get(k)) is int and row[k] >= 0 for k in ("rounds", "gems", "opp_gems"))
                 and all(isinstance(row.get(k), dict) and
                         all(type(v) is int and v >= 0 if n != "runner_error" else isinstance(v, str)
                             for n, v in row[k].items()) for k in ("health", "opp_health")))
        if not valid:
            issues.append(f"invalid or unexpected game row {index}")
        if valid:
            by_subject[label].append(row)
    expected_count = len(expected) * len(subjects)
    if len(rows) != expected_count or spec.get("games") != expected_count:
        issues.append(f"matrix count mismatch: {len(rows)} rows, spec {spec.get('games')}, expected {expected_count}")
    observations = {}
    signatures = defaultdict(list)
    for label, subject in subjects.items():
        selected = by_subject[label]
        keys = Counter((r.get("opponent"), r.get("map"), r.get("side")) for r in selected)
        missing = expected - set(keys)
        duplicate = [k for k, count in keys.items() if count > 1]
        if duplicate:
            issues.append(f"duplicate game keys: {label} ({len(duplicate)})")
        own = Counter(); other = Counter()
        for row in selected:
            for role, totals in (("health", own), ("opp_health", other)):
                for name, value in (row.get(role) or {}).items():
                    if type(value) is int:
                        totals[name] += value
        complete = not missing and not duplicate and len(selected) == len(expected)
        observation = {"label": label, "snapshot": entrants.get(label), "configuration": subject.get("set") or {},
                       "weights": subject.get("weights"), "games": len(selected), "expected_games": len(expected),
                       "missing_keys": len(missing), "duplicate_keys": len(duplicate), "matrix_complete": complete,
                       "own_flagged_games": sum(bool(r.get("health")) for r in selected),
                       "opponent_flagged_games": sum(bool(r.get("opp_health")) for r in selected),
                       "own_health": dict(own), "opponent_health": dict(other),
                       "truncated_games": sum(bool(r.get("truncated")) for r in selected),
                       **result_counts(selected),
                       "by_opponent": {o: result_counts([r for r in selected if r["opponent"] == o])
                                       for o in opponents},
                       "contribution": "unverified: experiment rows do not witness a named strategic contribution"}
        observations[label] = observation
        if complete:
            signature = [{k: v for k, v in r.items() if k != "subject"}
                         for r in sorted(selected, key=lambda r: (r["opponent"], r["map"], r["side"]))]
            signatures[hashlib.sha256(json.dumps(signature, sort_keys=True).encode()).hexdigest()].append(label)
    accounts = []
    for bot in core.select(selector):
        matching = []
        try:
            current = f"{bot.id}@{core.version(bot)}"
            source_error = None
        except (ArenaError, OSError) as exc:
            current, source_error = None, str(exc)
        for label, subject in subjects.items():
            if subject["bot"] != bot.id:
                continue
            item = dict(observations[label])
            try:
                variant = f"{bot.id}@{core.version(bot, subject.get('set') or None)}"
                item["source_current"] = variant == item["snapshot"]
            except (ArenaError, OSError) as exc:
                item["source_current"], item["source_error"] = False, str(exc)
            item["default_configuration"] = not subject.get("set") and not subject.get("weights")
            matching.append(item)
        eligible = [m for m in matching if m["source_current"] and m["default_configuration"]]
        status = ("missing" if not matching else "stale" if not eligible else
                  "incomplete" if not all(m["matrix_complete"] for m in eligible) else
                  "unhealthy" if any(m["own_flagged_games"] or m["truncated_games"] for m in eligible) else
                  "observed-clean")
        if source_error:
            status = "source-unavailable"
        accounts.append({"bot": bot.id, "current_snapshot": current, "status": status,
                         "suite_charter": f"bots/{bot.suite}/README.md",
                         "source_error": source_error, "observations": matching,
                         "contribution_adapter": (f"bots/{bot.suite}/tests/population.py" if
                                                  (ROOT / "bots" / bot.suite / "tests/population.py").is_file() else None),
                         "contribution": "unverified", "next_check":
                         "Repair measured health first, then witness the declared interaction and a distinct control on both sides."
                         if status == "unhealthy" else
                         "Measure the current default on matching keys." if status in ("missing", "stale", "incomplete") else
                         "Witness the declared interaction, its continuation and a distinct control in applicable games."})
    report = {"schema": "experiment-coverage-audit/v1", "experiment": spec["name"], "selector": selector,
            "entrants": dict(entrants),
            "integrity_pass": not issues, "integrity_issues": issues,
            "provenance_gaps": provenance_gaps(spec), "games": len(rows), "expected_games": expected_count,
            "coverage": {"maps": len(maps), "opponents": opponents, "sides": sides,
                         "full_matches": not bool(spec.get("rounds"))},
            "status_counts": dict(Counter(a["status"] for a in accounts)), "bots": accounts,
            "identical_observed_rows": [labels for labels in signatures.values() if len(labels) > 1],
            "limits": ["Clean means only faults, fuel exhaustion, logged errors and runner truncation measured in these rows; stalls are not measured.",
                       "Identical rows identify an investigation lead; aggregate telemetry does not prove identical choices or a unique contribution.",
                       "No retention, admission or strength claim is established by this audit."]}
    if win_floor_vs is not None:
        report["win_floor"] = win_floor(report, spec, win_floor_vs)
    return report


def cmd_audit(args) -> int:
    directory, spec = load(args.name)
    evidence = {name: {"path": path.relative_to(ROOT).as_posix(), "sha256": sha256(path)}
                for name, path in (("spec", directory / "spec.json"), ("rows", directory / "rows.jsonl"))}
    report = audit(spec, read_rows(directory / "rows.jsonl"), args.subjects, getattr(args, "win_floor_vs", None))
    if any(sha256(ROOT / item["path"]) != item["sha256"] for item in evidence.values()):
        raise ArenaError("experiment spec or rows changed during audit; retry at a stable boundary")
    report["evidence"] = evidence
    if args.out:
        target = Path(args.out)
        target = target if target.is_absolute() else ROOT / target
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"{spec['name']}: {report['games']}/{report['expected_games']} rows; {len(report['bots'])} current bots")
        print("states: " + ", ".join(f"{key}={value}" for key, value in report["status_counts"].items()))
        for gap in report["integrity_issues"] + report["provenance_gaps"]:
            print("gap: " + gap)
        print(f"{len(report['identical_observed_rows'])} identical-row groups; all strategic contributions remain unverified by rows")
        if "win_floor" in report:
            floor = report["win_floor"]
            print(f"strict win floor vs {floor['opponent']}: " +
                  ", ".join(f"{key}={value}" for key, value in floor["status_counts"].items()) +
                  "; contribution remains separate")
    return 0 if report["integrity_pass"] and ("win_floor" not in report or report["win_floor"]["all_passed"]) else 2


def census(rows: list[dict]) -> None:
    """One line per subject: strict W-D-L/win rate, health, and style means per game."""
    opponents = sorted({r["opponent"] for r in rows})
    by: dict[str, list[dict]] = {}
    for r in rows:
        by.setdefault(r["subject"], []).append(r)
    head = "".join(f"{o.split('/')[-1][:16] + ' W-D-L win%':>29s}" for o in opponents)
    print(f"{'subject':40s}{head}  unhealthy  kills deaths   gold  gems  to-win")
    for label, rs in sorted(by.items(), key=lambda kv: -sum(r["result"] == "win" for r in kv[1]) / len(kv[1])):
        cells = ""
        for o in opponents:
            sel = [r for r in rs if r["opponent"] == o]
            counts = result_counts(sel)
            cell = (f"{counts['wins']}-{counts['draws']}-{counts['losses']} "
                    f"{100 * counts['strict_win_rate']:.1f}%") if sel else "-"
            cells += f"{cell:>29s}"
        n = len(rs)
        mean = lambda k: sum(r.get(k, 0) for r in rs) / n
        to_win = sum(r["rounds"] if r["result"] == "win" else 2000 for r in rs) / n
        print(f"{label:40s}{cells}  {sum(bool(r['health']) for r in rs):4d}/{n:<4d} {mean('kills'):5.1f} {mean('deaths'):6.1f} "
              f"{mean('gold'):6.0f} {mean('collected'):5.0f} {to_win:7.0f}")


def cmd_show(args) -> int:
    d, spec = load(args.name)
    rows = core.read_jsonl(d / "rows.jsonl")
    if args.census:
        census(rows)
        return 0
    summary = summarize(spec, rows)
    show(spec, summary, args.by)
    if args.by == "map":
        per: dict[str, dict] = {}
        for r in rows:
            per.setdefault(r["map"], {}).setdefault(r["subject"], []).append(r)
        for m, subs in sorted(per.items()):
            print(f"  {m:28s} " + "  ".join(f"{s} {sum(x['result'] == 'win' for x in v)}/{len(v)}" for s, v in subs.items()))
    return 0


def cmd_close(args) -> int:
    with operation_lock(exp_dir(args.name)), replay_close_lock(exp_dir(args.name)):
        return _close_experiment(args)


def _close_experiment(args) -> int:
    d, spec = load(args.name)
    if not args.note.strip():
        raise ArenaError("close needs --note: the answer and where the decision is recorded")
    spec.update(status="closed", closed=time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime()), note=args.note.strip())
    write_spec(d, spec)
    freed = 0
    for p in list(d.iterdir()):
        if p.name in ("spec.json", "rows.jsonl", "summary.json"):
            continue
        freed += sum(f.stat().st_size for f in p.rglob("*") if f.is_file()) if p.is_dir() else p.stat().st_size
        shutil.rmtree(p) if p.is_dir() else p.unlink()
    index = d.parent / "index.jsonl"
    core.append_jsonl(index, [{"name": spec["name"], "question": spec["question"], "created": spec["created"],
                               "closed": spec["closed"], "games": spec.get("games"), "note": spec["note"]}])
    print(f"closed {spec['name']} ({freed / 1e6:.1f} MB removed); recorded in {index.relative_to(ROOT)}")
    return 0


def cmd_list(args) -> int:
    now = time.time()
    found = sorted((ROOT / ".cache").glob("*/exp/*/spec.json"))
    for p in found:
        spec = read_spec(p)
        if args.owner and not spec["name"].startswith(args.owner + "/"):
            continue
        if args.open and spec.get("status") == "closed":
            continue
        size = sum(f.stat().st_size for f in p.parent.rglob("*") if f.is_file())
        idle = (now - max(f.stat().st_mtime for f in p.parent.rglob("*") if f.is_file())) / 3600
        flag = "  STALE: close it" if spec.get("status") != "closed" and idle > 24 else ""
        print(f"{spec['name']:40s} {spec.get('status', 'open'):7s} {spec.get('games', 0):6d} games {size / 1e6:7.1f} MB "
              f"idle {idle:5.1f} h  {spec['question'][:60]}{flag}")
    return 0


def replay_lock_root(directory: Path) -> Path:
    # Close removes experiment children; coordination locks must outlive that cleanup.
    return directory.parent / ".replay-locks" / directory.name


@contextmanager
def replay_case_lock(directory: Path, recipe_id: str):
    root = replay_lock_root(directory)
    with core.FileLock(root / "gate.lock", poll=.05):
        lock = core.FileLock(root / "cases" / f"{recipe_id}.lock", poll=.05)
        lock.__enter__()
    try:
        yield
    finally:
        lock.__exit__(None, None, None)


@contextmanager
def replay_close_lock(directory: Path):
    root = replay_lock_root(directory)
    with core.FileLock(root / "gate.lock", poll=.05), ExitStack() as cases:
        for path in sorted((root / "cases").glob("*.lock")):
            cases.enter_context(core.FileLock(path, poll=.05))
        yield


def replay_recipe(spec: dict, label: str, opponent: str, map_path: Path, side: str,
                  prepared: dict[str, object] | None = None) -> tuple[str, dict]:
    """Bind one physical recipe, independent of display labels and scratch paths."""
    pinned = spec.get("provenance") or {}
    subject = next(s for s in spec["subjects"] if s["label"] == label)
    runtime = {name: pinned[name] for name in
               ("bundle", "engine", "runner", "runner_source", "runner_libraries", "toolchain") if name in pinned}
    if not pinned:  # Legacy inputs are unpinned in the experiment, but cannot share output across runtime changes.
        runtime = {"bundle": sha256(ROOT / "docs/reference/current-version.json"),
                   "engine": sha256(engine_wasm()), "runner": sha256(fastrun()),
                   "runner_libraries": runner_libraries(), "toolchain": core.toolchain()}
    weight = subject.get("weights")
    if weight:
        path = Path(weight)
        weight = sha256(path if path.is_absolute() else ROOT / path)
    recipe = {"schema": "arena-experiment-replay-recipe/v1",
              "subject": spec.get("entrants", {}).get(label),
              "opponent": spec.get("entrants", {}).get(opponent),
              "map": {"name": map_path.stem, "sha256": sha256(map_path)}, "side": side,
              "rounds": spec.get("rounds") or 0, "weights": weight,
              "wasm": {"subject": pinned.get("wasm", {}).get(label),
                       "opponent": pinned.get("wasm", {}).get(opponent)}, "runtime": runtime}
    if opponent_weights(spec, opponent):  # absent otherwise, so older recipe ids are unchanged
        recipe["opponent_weights"] = sha256(Path(opponent_weights(spec, opponent)))
    if "snapshot_roots" in pinned:
        recipe["snapshot_roots"] = {name: recorded_snapshot_root(spec, source).relative_to(ROOT).as_posix()
                                     for name, source in (("subject", label), ("opponent", opponent))}
        recipe["reuse_shared_snapshots"] = spec.get("reuse_shared_snapshots", False)
    if prepared is not None:
        recipe["wasm"] = {name: sha256(Path(wasm(ent))) for name, ent in prepared.items()}
    encoded = json.dumps(recipe, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()[:32], recipe


def replay_files(directory: Path) -> dict[str, str]:
    return {p.relative_to(directory).as_posix(): sha256(p) for p in directory.rglob("*") if p.is_file()}


def native_replay_recipe(spec: dict, label: str, opponent: str, map_path: Path,
                         side: str, role: str) -> tuple[str, dict]:
    """Bind a cached-only CLI reproduction without changing the experiment's RAW recipe."""
    if role not in ("raw", "injected"):
        raise ArenaError("native replay payload role must be raw or injected")
    subject = next(s for s in spec["subjects"] if s["label"] == label)
    if spec.get("rounds") or subject.get("weights") or opponent_weights(spec, opponent):
        raise ArenaError("native replay requires a full game without subject or opponent weights")
    for source in (label, opponent):
        native._key(spec.get("entrants", {}).get(source, ""))  # Native scope before generic cached reads.
    entries = {"subject": replay_entrant(spec, label, subject, cached_only=True),
               "opponent": replay_entrant(spec, opponent, None, cached_only=True)}
    names = {"subject": label, "opponent": opponent}
    roots = {name: recorded_snapshot_root(spec, source) for name, source in names.items()}
    private = "snapshot_roots" in (spec.get("provenance") or {})
    if entries["subject"].key == entries["opponent"].key and roots["subject"] != roots["opponent"]:
        raise ArenaError("same native snapshot key has conflicting roots")
    bindings = {name: native._capture(ent.key, role, **({"snapshot_root": roots[name],
                "snapshot_owner": spec["name"].split("/")[0]} if private else {})).record() for name, ent in entries.items()}
    pinned = spec.get("provenance") or {}
    if pinned:
        actual = {"bundle": sha256(ROOT / "docs/reference/current-version.json"),
                  "engine": sha256(engine_wasm()), "toolchain": core.toolchain()}
        if any(pinned.get(name) != value for name, value in actual.items()):
            raise ArenaError("native replay bundle, engine or toolchain differs from the experiment")
        if pinned.get("maps", {}).get(map_path.stem) != sha256(map_path):
            raise ArenaError("native replay selected map differs from the experiment")
        if any(pinned.get("wasm", {}).get(source) != bindings[name]["raw"]["sha256"]
               for name, source in (("subject", label), ("opponent", opponent))):
            raise ArenaError("native replay RAW module differs from the experiment")
    _, engine, dependencies = native._dependencies()
    recipe = {"schema": "arena-experiment-native-replay-recipe/v1", "transport": "native-cli",
              "selected_role": role, "bindings": bindings,
              "map": {"name": map_path.stem, "sha256": sha256(map_path)}, "side": side,
              "rounds": 0, "weights": None, "source_experiment_pinned": bool(pinned),
              "runtime": {"engine": engine, "dependencies": dependencies,
                          "adapter_sha256": sha256(Path(__file__))}}
    if private:
        recipe["snapshot_roots"] = {name: root.relative_to(ROOT).as_posix() for name, root in roots.items()}
        recipe["reuse_shared_snapshots"] = spec.get("reuse_shared_snapshots", False)
    encoded = json.dumps(recipe, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()[:32], recipe


def validate_native_replay(directory: Path, recipe: dict) -> None:
    """A completed selected bundle is immutable; transport logs contain volatile paths/PIDs."""
    try:
        if json.loads((directory / "recipe.json").read_text(encoding="utf-8")) != recipe:
            raise ArenaError("native replay recipe disagrees with the completed bundle")
        files = json.loads((directory / "files.json").read_text(encoding="utf-8"))
        expected = {"output.zip", "statistics.json", "native-completion.json", "recipe.json"}
        if set(files) != expected or any(sha256(directory / name) != files[name] for name in expected):
            raise ArenaError("native replay completed bundle bytes changed")
        if {p.name for p in directory.iterdir()} != expected | {"files.json"}:
            raise ArenaError("native replay completed bundle contains unexpected files")
    except (OSError, ValueError, TypeError) as exc:
        raise ArenaError(f"native replay completed bundle unavailable: {exc}") from exc


def cmd_native_replay(args, directory: Path, spec: dict, map_path: Path) -> int:
    role = args.native_role
    recipe_id, recipe = native_replay_recipe(spec, args.subject, args.opponent, map_path, args.side, role)
    out = directory / "replays" / f"{map_path.stem}_{args.side}_{recipe_id}"
    with replay_case_lock(directory, recipe_id):
        if progress_fingerprint(read_spec(directory / "spec.json")) != progress_fingerprint(spec):
            raise ArenaError("experiment inputs changed while waiting for the replay case lock")
        if native_replay_recipe(spec, args.subject, args.opponent, map_path, args.side, role)[1] != recipe:
            raise ArenaError("native replay inputs changed while waiting for the case lock")
        if out.exists():
            validate_native_replay(out, recipe)
        else:
            scratch_root = directory / "replay-scratch"
            scratch = scratch_root / uuid.uuid4().hex
            scratch.mkdir(parents=True)
            published = False
            try:
                keys = [recipe["bindings"][name]["snapshot"] for name in ("subject", "opponent")]
                if args.side == "B":
                    keys.reverse()
                try:
                    options = ({"snapshot_roots": {recipe["bindings"][name]["snapshot"]: root
                                                  for name, root in recipe["snapshot_roots"].items()},
                                "snapshot_owner": spec["name"].split("/")[0]} if "snapshot_roots" in recipe else {})
                    result = native.play(*keys, map_path, scratch / "native", role=role, jobs=1,
                                         timeout=CHUNK_TIMEOUT_SECONDS, **options)
                finally:
                    # Do not release the case/close guard while a retained private Job owns workers.
                    recover_native_cleanup()
                rows = result.get("rows", [])
                if (result.get("execution_pass") is not True or result.get("stopped") is not True or
                        result.get("jobs") is not False or result.get("unconfirmed_process_pid") is not None or
                        len(rows) != 1 or rows[0].get("execution_complete") is not True or
                        rows[0].get("measurement_complete") is not True):
                    raise ArenaError("native replay did not complete with confirmed worker cleanup")
                expected = {binding["snapshot"]: binding for binding in recipe["bindings"].values()}
                native_match = rows[0].get("native_match") or {}
                if (result.get("selected_role") != role or result.get("engine") != recipe["runtime"]["engine"] or
                        result.get("dependencies") != recipe["runtime"]["dependencies"] or
                        {binding["snapshot"]: binding for binding in result.get("bindings", [])} != expected or
                        rows[0].get("a") != keys[0] or rows[0].get("b") != keys[1] or
                        rows[0].get("map") != map_path.stem or
                        native_match.get("mapHash") != recipe["map"]["sha256"] or
                        native_match.get("engineBuildHash") != recipe["runtime"]["engine"]["buildHash"] or
                        native_match.get("engineVersion") != recipe["runtime"]["engine"]["version"] or
                        native_replay_recipe(spec, args.subject, args.opponent, map_path, args.side, role)[1] != recipe):
                    raise ArenaError("native replay execution disagrees with its bound recipe")
                staged = scratch / "bundle"
                staged.mkdir()
                native_job = scratch / "native/jobs/game-000000"
                for source, name, field in ((native_job / "output.zip", "output.zip", "archive"),
                                            (native_job / "summary.json", "statistics.json", "statistics")):
                    if not source.is_file() or not source.stat().st_size:
                        raise ArenaError(f"native replay missing selected output: {name}")
                    descriptor = rows[0].get(field) or {}
                    recorded_path = Path(descriptor.get("path", ""))
                    if not recorded_path.is_absolute():
                        recorded_path = ROOT / recorded_path
                    if (recorded_path.resolve() != source.resolve() or
                            descriptor.get("sha256") != sha256(source) or
                            descriptor.get("size") != source.stat().st_size):
                        raise ArenaError(f"native replay selected output differs from its completed job: {name}")
                    shutil.copyfile(source, staged / name)
                    if sha256(staged / name) != descriptor["sha256"]:
                        raise ArenaError(f"native replay selected output changed while copying: {name}")
                # Retain only the selected proof; native input packages/transport streams are regenerable.
                completion = {"schema": "arena-experiment-native-replay-completion/v1",
                              "execution_pass": True, "stopped": True, "jobs": False,
                              "selected_role": role, "health_pass": result.get("health_pass"),
                              "row": {name: value for name, value in rows[0].items()
                                      if name not in ("replay", "archive", "statistics")}}
                (staged / "native-completion.json").write_text(json.dumps(completion, indent=2) + "\n", encoding="utf-8", newline="\n")
                (staged / "recipe.json").write_text(json.dumps(recipe, indent=2) + "\n", encoding="utf-8", newline="\n")
                (staged / "files.json").write_text(json.dumps(replay_files(staged), indent=2) + "\n", encoding="utf-8", newline="\n")
                validate_native_replay(staged, recipe)
                out.parent.mkdir(parents=True, exist_ok=True)
                staged.rename(out)
                published = True
            finally:
                if scratch.resolve().parent != scratch_root.resolve():
                    raise ArenaError("native replay scratch escaped its invocation directory")
                if published:
                    shutil.rmtree(scratch)
                else:
                    print(f"native replay diagnostic scratch retained in {scratch.relative_to(ROOT)}; `close` removes it",
                          file=sys.stderr)
    print(f"native {role} bundle in {out.relative_to(ROOT)} (output.zip, statistics.json); `close` removes it")
    return 0


def cmd_replay(args) -> int:
    d, spec = load(args.name)
    labels = [s["label"] for s in spec["subjects"]]
    if args.subject not in labels:
        raise ArenaError(f"subject {args.subject} not in {labels}")
    i = labels.index(args.subject)
    opponent_ids = spec.get("opponent_ids") or [k for k in spec.get("entrants", {}) if k not in labels]
    if args.opponent not in opponent_ids:
        raise ArenaError(f"opponent {args.opponent} not in {opponent_ids}")
    maps = list((spec.get("provenance") or {}).get("maps", {})) or core.map_set(spec["maps"])
    map_path = core.map_path(args.map)
    selected_map = map_path.stem
    if selected_map not in maps or args.side not in (["A", "B"] if spec["sides"] == "both" else [spec["sides"]]):
        raise ArenaError("requested map/side was not part of the experiment")
    if getattr(args, "native_role", None) is not None:
        return cmd_native_replay(args, d, spec, map_path)
    try:
        recipe_id, recipe = replay_recipe(spec, args.subject, args.opponent, map_path, args.side)
    except OSError as exc:
        raise ArenaError(f"cannot bind replay recipe inputs: {exc}") from exc
    # Same recipes serialize before snapshot rebuilding or any game CPU slots.
    # Different recipes never share staging files or published bundle paths.
    with replay_case_lock(d, recipe_id):
        if progress_fingerprint(read_spec(d / "spec.json")) != progress_fingerprint(spec):
            raise ArenaError("experiment inputs changed while waiting for the replay case lock")
        gaps = replay_provenance_gaps(spec, args.subject, args.opponent, [selected_map])
        if spec.get("provenance") and gaps:
            raise ArenaError("cannot replay changed inputs: " + "; ".join(gaps))
        if gaps:
            print("warning: " + "; ".join(gaps), file=sys.stderr)
        if replay_recipe(spec, args.subject, args.opponent, map_path, args.side)[1] != recipe:
            raise ArenaError("replay recipe inputs changed while waiting for the case lock")
        expected_map = (spec.get("provenance") or {}).get("maps", {}).get(selected_map)
        if expected_map and recipe["map"]["sha256"] != expected_map:
            raise ArenaError("cannot replay changed selected map bytes")
        subject = replay_entrant(spec, args.subject, spec["subjects"][i])
        opponent = replay_entrant(spec, args.opponent, None)
        prepared = {"subject": subject, "opponent": opponent}
        bound_id, bound = replay_recipe(spec, args.subject, args.opponent, map_path, args.side, prepared)
        if any(bound[name] != value for name, value in recipe.items() if name != "wasm"):
            raise ArenaError("replay recipe inputs changed while preparing recorded snapshots")
        recipe_id, recipe = bound_id, bound
        leaf = f"{selected_map}_{args.side}_{recipe_id}"
        out = d / "replays" / leaf
        scratch_root = d / "replay-scratch"
        scratch = scratch_root / uuid.uuid4().hex
        scratch.mkdir(parents=True)
        try:
            w = spec["subjects"][i]["weights"] or "-"
            wo = opponent_weights(spec, args.opponent) or "-"
            run_games([f"{map_path} {args.side} s {w} o {wo} {recipe_id}"],
                      {"s": wasm(subject), "o": wasm(opponent)}, scratch / "run",
                      rounds=spec.get("rounds") or 0, bundles=scratch / "bundles")
            staged = scratch / "bundles" / leaf
            required = ("replay.repl26a", "team-a.stdout", "team-b.stdout", "team-a.fuel", "team-b.fuel")
            missing = [name for name in required if not (staged / name).is_file() or
                       (name.endswith((".repl26a", ".fuel")) and not (staged / name).stat().st_size)]
            if missing:
                raise ArenaError(f"replay produced an incomplete bundle for recipe {recipe_id}: {', '.join(missing)}")
            if (replay_recipe(spec, args.subject, args.opponent, map_path, args.side, prepared)[1] != recipe or
                    (spec.get("provenance") and replay_provenance_gaps(spec, args.subject, args.opponent, [selected_map]))):
                raise ArenaError("replay inputs changed during execution; bundle was not published")
            (staged / "recipe.json").write_text(json.dumps(recipe, indent=2) + "\n", encoding="utf-8", newline="\n")
            out.parent.mkdir(parents=True, exist_ok=True)
            if out.exists():
                if not out.is_dir() or replay_files(staged) != replay_files(out):
                    raise ArenaError(f"same replay recipe produced different bundle bytes: {out}")
            else:
                staged.rename(out)
        finally:
            if scratch.resolve().parent != scratch_root.resolve():
                raise ArenaError("replay scratch escaped its invocation directory")
            shutil.rmtree(scratch)
    print(f"bundle in {out.relative_to(ROOT)} (replay.repl26a, team-a/b.stdout, .fuel); `close` removes it")
    return 0


def recorded_opponent(spec: dict, label: str) -> tuple[dict, bool]:
    """Resolve a recorded recipe; legacy inference is confined to its exact cache entry."""
    def overrides(value):
        if not isinstance(value, dict) or not all(type(k) is str and type(v) is str for k, v in value.items()):
            raise ArenaError(f"invalid recorded opponent overrides: {label}")
        return dict(value)

    descriptors = spec.get("opponent_descriptors")
    legacy = spec.get("legacy_opponent_overrides")
    if legacy is not None and not isinstance(legacy, dict):
        raise ArenaError(f"invalid legacy opponent overrides: {label}")
    inferred = False
    if "opponent_descriptors" in spec:
        descriptor = descriptors.get(label) if isinstance(descriptors, dict) else None
        if (not isinstance(descriptor, dict) or "set" not in descriptor
                or (descriptor.get("bot") != label or descriptor.get("weights") is not None)
                and (not isinstance(descriptor.get("set"), dict) or type(descriptor.get("bot")) is not str
                     or descriptor != opponent_descriptor(label))):
            raise ArenaError(f"invalid recorded opponent descriptor: {label}")
        settings = overrides(descriptor["set"])
        if legacy is not None and label in legacy and overrides(legacy[label]) != settings:
            raise ArenaError(f"conflicting recorded opponent overrides: {label}")
    elif legacy is not None and label in legacy:
        settings = overrides(legacy[label])
    else:
        key = spec.get("entrants", {}).get(label, "")
        match = (re.fullmatch(r"([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)@([0-9a-f]{12})", key)
                 if type(key) is str else None)
        if not match or "/".join(match.groups()[:2]) != label:
            raise ArenaError(f"recorded opponent snapshot identity missing or invalid: {label}")
        version = match.group(3)
        try:
            meta = json.loads((recorded_snapshot_root(spec, label) / version / "meta.json").read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            raise ArenaError(f"recorded opponent recipe unavailable: {key}: {exc}") from exc
        if not isinstance(meta, dict) or meta.get("bot") != label or meta.get("version") != version:
            raise ArenaError(f"recorded opponent metadata identity mismatch: {key}")
        settings = overrides(meta.get("overrides", {}))
        inferred = True
    pinned = spec.get("provenance")
    binaries = pinned.get("wasm") if isinstance(pinned, dict) else None
    digest = binaries.get(label) if isinstance(binaries, dict) else None
    if settings and (type(digest) is not str or not re.fullmatch(r"[0-9a-f]{64}", digest)):
        raise ArenaError(f"recorded overridden opponent requires pinned WASM identity: {label}")
    if "opponent_descriptors" in spec:
        return {"bot": descriptor["bot"], "set": settings, "weights": descriptor.get("weights")}, inferred
    return {"bot": label, "set": settings, "weights": None}, inferred


def replay_entrant(spec: dict, label: str, subject: dict | None, *, cached_only: bool = False):
    """Use the recorded cached closure, or rebuild only if current source matches exactly."""
    if subject is None:
        subject, inferred = recorded_opponent(spec, label)
        cached_only = cached_only or inferred  # Inference never authorizes rebuilding a missing recipe.
    key = spec.get("entrants", {}).get(label, "")
    match = re.fullmatch(r"([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)@([0-9a-f]{12})", key)
    if not match or "/".join(match.groups()[:2]) != subject["bot"]:
        raise ArenaError(f"recorded snapshot identity missing or invalid: {label}")
    suite, name, version = match.groups()
    bot = core.Bot(suite, name)
    root = recorded_snapshot_root(spec, label)
    if spec.get("private_snapshots") or "snapshot_roots" in (spec.get("provenance") or {}):
        ent = core.cached_snapshot(key, snapshot_root=root, snapshot_owner=spec["name"].split("/")[0])
        if dict(ent.overrides) != (subject.get("set") or {}):
            raise ArenaError("recorded snapshot configuration differs")
        pinned = (spec.get("provenance") or {}).get("wasm", {}).get(label)
        if pinned and sha256(Path(wasm(ent))) != pinned:
            raise ArenaError(f"recorded snapshot WASM changed: {key}")
        return ent
    snap = root / version
    if not snap.is_dir():
        if cached_only:
            raise ArenaError(f"recorded snapshot unavailable; resume never builds: {key}")
        if f"{bot.id}@{core.version(bot, subject.get('set') or None)}" != key:
            raise ArenaError(f"recorded snapshot unavailable and current sources drifted: {key}")
        ent = core.prepare(bot, subject.get("set") or None)
        if not ent.ok or ent.key != key:
            raise ArenaError(f"cannot rebuild exact recorded snapshot: {key}")
        return ent
    try:
        meta = json.loads((snap / "meta.json").read_text(encoding="utf-8"))
        if (not isinstance(meta, dict) or meta.get("bot") != bot.id or meta.get("version") != version or
                meta.get("overrides", {}) != (subject.get("set") or {}) or
                not (snap / ".ready").is_file() or
                (snap / ".toolchain").read_text(encoding="utf-8").strip() != core.toolchain()):
            raise ArenaError(f"recorded snapshot metadata/toolchain mismatch: {key}")
        target = snap / "bots" / suite / name
        files = {p.resolve() for p in (target / "src").rglob("*") if p.is_file() and ".cambc" not in p.parts}
        stack = [p for p in files if p.suffix in core.SOURCE_SUFFIXES]
        while stack:
            source = stack.pop()
            for include in core.INCLUDE.findall(source.read_text(encoding="utf-8", errors="surrogateescape")):
                candidate = (source.parent / include).resolve()
                if candidate.is_file() and candidate not in files and candidate.is_relative_to(snap.resolve()):
                    files.add(candidate)
                    if candidate.suffix in core.SOURCE_SUFFIXES:
                        stack.append(candidate)
        contents = {ROOT / p.relative_to(snap.resolve()): p.read_bytes() for p in files}
        if not contents or core.version_of(contents, subject.get("set") or None) != version:
            raise ArenaError(f"recorded snapshot source digest mismatch: {key}")
        ent = core.Entrant(bot, version, target, True, overrides=tuple(sorted(meta.get("overrides", {}).items())))
        if not Path(wasm(ent)).is_file():
            raise ArenaError(f"recorded snapshot WASM missing: {key}")
        pinned = (spec.get("provenance") or {}).get("wasm", {}).get(label)
        if pinned and sha256(Path(wasm(ent))) != pinned:
            raise ArenaError(f"recorded snapshot WASM changed: {key}")
        return ent
    except (OSError, ValueError) as exc:
        raise ArenaError(f"recorded snapshot unavailable: {key}: {exc}") from exc


def register(sub) -> None:
    p = sub.add_parser("exp", help="experiments: paired game rows on the fast native runner, with a lifecycle",
                       description=__doc__, formatter_class=__import__("argparse").RawDescriptionHelpFormatter)
    es = p.add_subparsers(dest="exp_command", required=True, metavar="action")
    r = es.add_parser("run", help="play every subject against every opponent, map and side")
    r.add_argument("name", help="OWNER/NAME (stored under .cache/OWNER/exp/NAME)")
    r.add_argument("-q", "--question", required=True)
    r.add_argument("-s", "--subject", action="append", help="LABEL=BOT[+KNOB=V...][@WEIGHTS]; first = baseline")
    r.add_argument("--subjects", action="append", help="bulk subjects: every bot a selector matches (census)")
    r.add_argument("--vs", action="append", required=True,
                   help="opponent selectors or named sets from bots/gauntlet.json (gauntlet, gauntlet-wide, passive)")
    r.add_argument("--maps", default="pool", help="map sets: pool, legacy, labs, heldout, train, names (default pool)")
    r.add_argument("--sides", default="both", choices=["both", "A", "B"])
    r.add_argument("--rounds", type=int, default=0, help="stop games after N rounds (smoke tests only)")
    r.add_argument("--keep", default="flagged", choices=["flagged", "none"], help="bundles for unhealthy games (default)")
    r.add_argument("--force", action="store_true", help="replace an existing experiment of this name")
    r.add_argument("--private-snapshots", action="store_true",
                   help="diagnostic only: prepare all inputs in OWNER/snap; shared Slots/locks remain global")
    r.add_argument("--reuse-shared-snapshots", action="store_true",
                   help="with --private-snapshots only: explicitly permit validated global input reuse and ready refresh")
    r.set_defaults(func=cmd_run)
    rs = es.add_parser("resume", help="resume missing games of an open prepared experiment using exact cached inputs")
    rs.add_argument("name")
    rs.add_argument("--progress-from", metavar="PATH", help="import a separately observed arena-experiment-progress/v1 document")
    rs.set_defaults(func=cmd_resume)
    s = es.add_parser("show", help="the summary (recomputed from the rows)")
    s.add_argument("name")
    s.add_argument("--by", default="opponent", choices=["opponent", "map"])
    s.add_argument("--census", action="store_true", help="one line per subject: strict W-D-L and win percentage, health, style")
    s.set_defaults(func=cmd_show)
    au = es.add_parser("audit", help="account for current bots, exact source freshness and finite health/contribution gaps")
    au.add_argument("name")
    au.add_argument("--subjects", default=DEVELOPMENT, help="current bot selector (default excludes main, anchors, surprise, contenders)")
    au.add_argument("--json", action="store_true")
    au.add_argument("--out", help="write the compact audit as JSON")
    au.add_argument("--win-floor-vs", metavar="SUITE/BOT",
                    help="require a healthy strict win against this exact current opponent on all current pool maps, both sides; not contribution admission")
    au.set_defaults(func=cmd_audit)
    c = es.add_parser("close", help="record the answer and delete everything but spec, rows and summary")
    c.add_argument("name")
    c.add_argument("--note", required=True)
    c.set_defaults(func=cmd_close)
    li = es.add_parser("list", help="experiments with status, size and idle time")
    li.add_argument("owner", nargs="?")
    li.add_argument("--open", action="store_true")
    li.set_defaults(func=cmd_list)
    rp = es.add_parser("replay", help="regenerate one game's replay and logs")
    rp.add_argument("name")
    rp.add_argument("map")
    rp.add_argument("side", choices=["A", "B"])
    rp.add_argument("subject")
    rp.add_argument("opponent")
    rp.add_argument("--native-role", choices=["raw", "injected"],
                    help="cached-only full CLI replay of this payload role; no weights or round cap")
    rp.set_defaults(func=cmd_replay)
