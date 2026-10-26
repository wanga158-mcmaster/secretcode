"""Foundation of the arena toolkit: paths, bot discovery, map sets, source snapshots, builds,
machine-wide slots and locked JSONL logs. Standard library only.

A bot is any directory bots/<suite>/<bot>/ containing src/main.cpp; its id is "<suite>/<bot>".
Its version is a hash of its build closure: every file under src/ plus every file it reaches
through relative #include paths (shared suite modules). Matches never run the working tree:
`prepare()` copies the closure into .cache/arena/snap/<version>/ (repo-relative layout), applies
knob overrides there, and builds it once under a lock, so parallel agents can keep editing.
"""
from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BOTS = ROOT / "bots"
MAPS = ROOT / "maps"
CACHE = ROOT / ".cache" / "arena"
RESULTS = ROOT / "results"
CAMBC = ROOT / ".tools" / ("cambc.exe" if os.name == "nt" else "cambc")
FROZEN_SUITES = {"anchors"}
HOME = Path.home()
NODE = Path(os.environ.get("ARENA_NODE") or
            HOME / ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe")
if not NODE.is_file() and shutil.which("node"):
    NODE = Path(shutil.which("node"))
CLANG = Path(os.environ.get("ARENA_CLANG") or
             Path(os.environ.get("LOCALAPPDATA", HOME / "AppData/Local")) / "cambc/wasi-sdk-33/bin/clang++.exe")
SOURCE_SUFFIXES = {".cpp", ".cc", ".h", ".hpp", ".hh", ".inc", ".ipp", ".def"}
INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"\n]+)"', re.M)


class ArenaError(RuntimeError):
    pass


# ----------------------------------------------------------------------------- discovery
@dataclass(frozen=True, order=True)
class Bot:
    suite: str
    name: str

    @property
    def id(self) -> str:
        return f"{self.suite}/{self.name}"

    @property
    def path(self) -> Path:
        return BOTS / self.suite / self.name

    @property
    def frozen(self) -> bool:
        return self.suite in FROZEN_SUITES

    def __str__(self) -> str:
        return self.id


def suites() -> list[str]:
    return sorted(d.name for d in BOTS.iterdir() if d.is_dir() and not d.name.startswith((".", "_")))


def all_bots() -> list[Bot]:
    return sorted(Bot(p.parent.parent.parent.name, p.parent.parent.name) for p in BOTS.glob("*/*/src/main.cpp"))


def bot(ident: str) -> Bot:
    found = select(ident)
    if len(found) != 1:
        raise ArenaError(f"{ident!r} matches {len(found)} bots; use <suite>/<bot>")
    return found[0]


def select(spec: str | list[str] | None = None) -> list[Bot]:
    """Comma-separated selectors: all | <suite> | <suite>/<bot> | globs (main/*, */tempo).
    A leading '-' excludes (e.g. "all,-anchors"). Unknown selectors raise."""
    items = spec if isinstance(spec, list) else [s for s in (spec or "all").split(",")]
    items = [s.strip() for s in items if s.strip()]
    every = all_bots()
    bots = [b for b in every if not b.name.startswith("_")]  # scratch bots: only an exact id selects them
    chosen: list[Bot] = []
    for item in items:
        neg = item.startswith("-")
        pat = item[1:] if neg else item
        if pat == "all":
            hit = bots
        elif "/" in pat:
            hit = [b for b in every if b.id == pat] or [b for b in bots if fnmatch.fnmatchcase(b.id, pat)]
        else:
            hit = [b for b in bots if fnmatch.fnmatchcase(b.suite, pat)] or \
                  [b for b in bots if fnmatch.fnmatchcase(b.name, pat)]
        if not hit and not neg:
            raise ArenaError(f"selector {pat!r} matches no bot (see `arena bots`)")
        if neg:
            chosen = [b for b in chosen if b not in hit]
        else:
            chosen += [b for b in hit if b not in chosen]
    return chosen


# ----------------------------------------------------------------------------- maps
def _pool_index() -> list[dict]:
    """Official pool in index order: {id, file stem, pool name, sha256} (stems are canonical map names)."""
    files = json.loads((ROOT / ".cambc" / "map-pool.json").read_text(encoding="utf-8"))["maps"]
    names = {m["id"]: m["name"] for m in json.loads((MAPS / "pool.json").read_text(encoding="utf-8"))["maps"]}
    return [{"id": f["id"], "stem": Path(f["file"]).stem, "name": names.get(f["id"], Path(f["file"]).stem),
             "file": f["file"], "sha256": f["sha256"]} for f in files]


_GEOMETRY: tuple[float, dict] | None = None


def _geometry() -> dict:
    """Geometry export keyed by canonical map name (file stem); parsed once per file version."""
    global _GEOMETRY
    path = MAPS / "geometry.json"
    if not path.is_file():
        return {}
    mtime = path.stat().st_mtime
    if _GEOMETRY is None or _GEOMETRY[0] != mtime:
        by_pool_name = {m["id"]: m for m in json.loads(path.read_text(encoding="utf-8"))["maps"]}
        _GEOMETRY = (mtime, {e["stem"]: by_pool_name[e["name"]] for e in _pool_index() if e["name"] in by_pool_name})
    return _GEOMETRY[1]


def pool_maps() -> list[str]:
    return [e["stem"] for e in _pool_index()]


def map_info(name: str) -> dict:
    """width, height, area, draft (picks) for pool maps; {} for maps outside the geometry export."""
    g = _geometry().get(name) or _geometry().get(name.replace("_", "-"))
    if not g:
        return {}
    w, h = int(g["width"]), int(g["height"])
    return {"width": w, "height": h, "area": w * h, "draft": int(g.get("draftCount") or 0)}


GENERATED = ("heldout", "train", "fresh")  # maps/generated/<set>: fresh layouts fitted to the pool (fastrun --genmap)


def map_set(spec: str) -> list[str]:
    """Comma-separated: pool|all (the official pool) | legacy (17 older versions) | labs |
    heldout (60 generated maps for unseen-map claims) | train (200 generated maps for tuning) |
    fresh (150 generated maps, seed 777, claim-only: never tune or probe on them) |
    small|large (pool split at the median area) | map names."""
    out: list[str] = []
    for item in [s.strip() for s in spec.split(",") if s.strip()]:
        if item in ("pool", "all"):
            names = pool_maps()
        elif item in ("legacy", "labs"):
            names = sorted(p.stem for p in (MAPS / item).glob("*.map26a"))
        elif item in GENERATED:
            names = sorted(p.stem for p in (MAPS / "generated" / item).glob("*.map26a"))
        elif item in ("small", "large"):
            pool = pool_maps()
            areas = sorted(map_info(m).get("area", 0) for m in pool)
            median = areas[len(areas) // 2]
            names = [m for m in pool if (map_info(m).get("area", 0) < median) == (item == "small")]
        else:
            p = map_path(item)  # canonical name (file stem), whatever spelling was typed; a path outside maps/ stays a path
            names = [p.stem if any(c == p for c in _stem_hits(p.stem)) else str(p)]
        out += [n for n in names if n not in out]
    return out


def _stem_hits(stem: str) -> list[Path]:
    try:
        return [map_path(stem)]
    except ArenaError:
        return []


def map_path(name: str) -> Path:
    if name.endswith(".map26a") and Path(name).is_file():
        return Path(name).resolve()
    for stem in dict.fromkeys((name, name.replace("_", "-"))):
        for cand in (MAPS / f"{stem}.map26a", MAPS / "legacy" / f"{stem}.map26a", MAPS / "labs" / f"{stem}.map26a",
                     *(MAPS / "generated" / g / f"{stem}.map26a" for g in GENERATED)):
            if cand.is_file():
                return cand.resolve()
    if Path(name).is_file():
        return Path(name).resolve()
    raise ArenaError(f"unknown map {name!r}")


# ----------------------------------------------------------------------------- closures and versions
def _read_text(p: Path) -> str:
    return p.read_text(encoding="utf-8", errors="surrogateescape")


def closure(b: Bot) -> list[Path]:
    """Files that determine the build: src/** plus relative includes reached from them (SDK excluded)."""
    src = b.path / "src"
    files = {p.resolve() for p in src.rglob("*") if p.is_file() and ".cambc" not in p.parts}
    stack = [p for p in files if p.suffix in SOURCE_SUFFIXES]
    sdk = (ROOT / "sdk").resolve()
    while stack:
        f = stack.pop()
        for inc in INCLUDE.findall(_read_text(f)):
            cand = (f.parent / inc).resolve()
            if cand.is_file() and cand not in files and not cand.is_relative_to(sdk) and cand.is_relative_to(ROOT):
                files.add(cand)
                if cand.suffix in SOURCE_SUFFIXES:
                    stack.append(cand)
    return sorted(files)


# any declarator of `constexpr T a = 1, b = 2;` (the value ends at its own ',' or ';'; braces and
# parentheses keep their commas)
KNOB_CONSTEXPR = (r"(\b(?:inline\s+)?(?:static\s+)?constexpr\s+[\w:<>,\s]+?\s+"
                  r"(?:[A-Za-z_]\w*\s*=\s*(?:\{{[^;]*?\}}|\([^;]*?\)|[^;,]+?)\s*,\s*)*{name}\s*=\s*)"
                  r"(\{{[^;]*?\}}|\([^;]*?\)|[^;,]+?)(\s*[;,])")
KNOB_DEFINE = r"(^[ \t]*#[ \t]*define[ \t]+{name}[ \t]+)([^\r\n]+?)([ \t]*\r?$)"


def apply_overrides(b: Bot, contents: dict[Path, bytes], overrides: dict[str, str]) -> dict[Path, bytes]:
    """Replace the value of `constexpr ... NAME = v;` or `#define NAME v` for each override.
    A constexpr knob must match exactly once (the bot's own src/ is searched before shared files).
    A macro knob may be defined in several headers (e.g. #ifndef-guarded defaults); every
    definition is replaced when they all carry the same value, since include order decides which
    one takes effect."""
    out = dict(contents)
    own = (b.path / "src").resolve()
    order = sorted(out, key=lambda p: (not p.is_relative_to(own), str(p)))
    for name, value in overrides.items():
        hits, kind = [], ""
        for kind, pattern in (("constexpr", KNOB_CONSTEXPR), ("define", KNOB_DEFINE)):
            rx = re.compile(pattern.format(name=re.escape(name)), re.M)
            for p in order:
                if p.suffix not in SOURCE_SUFFIXES:
                    continue
                text = out[p].decode("utf-8", errors="surrogateescape")
                hits += [(p, m) for m in rx.finditer(text)]
            if hits:
                break
        if kind == "constexpr":
            hits = [h for h in hits if h[0].is_relative_to(own)] or hits
            ok = len(hits) == 1
        else:
            ok = bool(hits) and len({m.group(2).strip() for _, m in hits}) == 1
        if not ok:
            where = ", ".join(sorted({str(h[0].relative_to(ROOT)) for h in hits})) or "nowhere"
            raise ArenaError(f"knob {name!r} for {b.id}: {len(hits)} matches ({where})")
        for p in sorted({h[0] for h in hits}):
            text = out[p].decode("utf-8", errors="surrogateescape")
            spans = [m.span(2) for q, m in hits if q == p]
            for start, end in sorted(spans, reverse=True):
                text = text[:start] + str(value) + text[end:]
            out[p] = text.encode("utf-8", errors="surrogateescape")
    return out


def version_of(contents: dict[Path, bytes], overrides: dict[str, str] | None = None) -> str:
    h = hashlib.sha256()
    for p in sorted(contents):
        h.update(p.relative_to(ROOT).as_posix().encode() + b"\0" + contents[p] + b"\0")
    for k, v in sorted((overrides or {}).items()):
        h.update(f"override {k}={v}\0".encode())
    return h.hexdigest()[:12]


def read_closure(b: Bot) -> dict[Path, bytes]:
    """The bot's build closure read once (pass to prepare(contents=...) to build several variants
    from one consistent read while other agents keep editing)."""
    return {p: p.read_bytes() for p in closure(b)}


def version(b: Bot, overrides: dict[str, str] | None = None) -> str:
    contents = read_closure(b)
    if overrides:
        contents = apply_overrides(b, contents, overrides)
    return version_of(contents, overrides)


# ----------------------------------------------------------------------------- locks, slots, logs
if os.name == "nt":
    import msvcrt

    def _try_lock(fd: int) -> bool:
        try:
            os.lseek(fd, 0, os.SEEK_SET)
            msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
            return True
        except OSError:
            return False

    def _unlock(fd: int) -> None:
        os.lseek(fd, 0, os.SEEK_SET)
        msvcrt.locking(fd, msvcrt.LK_UNLCK, 1)
else:
    import fcntl

    def _try_lock(fd: int) -> bool:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except OSError:
            return False

    def _unlock(fd: int) -> None:
        fcntl.flock(fd, fcntl.LOCK_UN)


class FileLock:
    """Exclusive inter-process lock on a lock file; released by the OS if the holder dies."""

    def __init__(self, path: Path, poll: float = 0.2, timeout: float | None = None):
        self.path, self.poll, self.timeout, self.fd = Path(path), poll, timeout, None

    def __enter__(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        fd = os.open(self.path, os.O_RDWR | os.O_CREAT, 0o666)
        try:
            start = time.monotonic()
            while not _try_lock(fd):
                if self.timeout is not None and time.monotonic() - start > self.timeout:
                    raise ArenaError(f"timed out waiting for {self.path}")
                time.sleep(self.poll)
        except BaseException:
            os.close(fd)
            raise
        self.fd = fd
        return self

    def __exit__(self, *exc):
        fd, self.fd = self.fd, None
        try:
            _unlock(fd)
        finally:
            os.close(fd)


def slot_count() -> int:
    return max(1, int(os.environ.get("ARENA_SLOTS") or os.cpu_count() or 1))


class Slots:
    """Machine-wide pool of CPU slots shared by every arena process; nothing is reserved per agent.
    Takes up to `want` free slots (at least `minimum`, waiting if necessary) for the `with` body.
    Long jobs should hold slots per chunk so concurrent work interleaves."""

    def __init__(self, want: int, minimum: int = 1, poll: float | None = None, quiet: bool = False):
        self.want = max(1, min(want, slot_count()))
        self.minimum = max(1, min(minimum, self.want))
        # Notice brief bulk handoffs for diagnostics and small native builds.
        # Bulk requests retain their old cadence; no capacity is reserved.
        small = self.want <= 2 and self.minimum == 1
        self.poll = (0.05 if small else 2.0) if poll is None else poll
        self.quiet, self.fds, self.count = quiet, [], 0

    def __enter__(self):
        d = CACHE / "slots"
        d.mkdir(parents=True, exist_ok=True)
        waited = False
        try:
            while True:
                for i in range(slot_count()):
                    if len(self.fds) >= self.want:
                        break
                    fd = os.open(d / f"{i}.lock", os.O_RDWR | os.O_CREAT, 0o666)
                    try:
                        locked = _try_lock(fd)
                    except BaseException:
                        os.close(fd)
                        raise
                    if locked:
                        self.fds.append(fd)
                    else:
                        os.close(fd)
                if len(self.fds) >= self.minimum:
                    break
                self.__exit__()
                if not waited and not self.quiet:
                    print(f"waiting for {self.minimum} free arena slot(s) of {slot_count()}", file=sys.stderr)
                    waited = True
                time.sleep(self.poll)
        except BaseException as error:
            try:
                self.__exit__()
            except BaseException as cleanup_error:
                error.add_note(f"Slot acquisition cleanup also failed: {cleanup_error!r}")
            raise
        self.count = len(self.fds)
        return self

    def __exit__(self, *exc):
        fds, self.fds, self.count = self.fds, [], 0
        first_error = None
        for fd in reversed(fds):
            try:
                try:
                    _unlock(fd)
                finally:
                    os.close(fd)
            except BaseException as error:
                if first_error is None:
                    first_error = error
        if first_error is not None:
            raise first_error


def busy_slots() -> int:
    """Slots currently held by any arena process (probe without keeping them)."""
    d = CACHE / "slots"
    busy = 0
    for i in range(slot_count()):
        p = d / f"{i}.lock"
        if not p.exists():
            continue
        fd = os.open(p, os.O_RDWR | os.O_CREAT, 0o666)
        if _try_lock(fd):
            _unlock(fd)
        else:
            busy += 1
        os.close(fd)
    return busy


def append_jsonl(path: Path, rows: list[dict]) -> None:
    """Append rows to a shared JSONL log under its lock (safe with many concurrent writers)."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with FileLock(path.with_name(path.name + ".lock")):
        torn = False
        if path.is_file() and path.stat().st_size:
            with open(path, "rb") as fh:
                fh.seek(-1, os.SEEK_END)
                torn = fh.read(1) != b"\n"
        with open(path, "a", encoding="utf-8", newline="\n") as fh:
            if torn:  # an interrupted writer left a partial line: end it so this row stays readable
                fh.write("\n")
            for r in rows:
                fh.write(json.dumps(r, separators=(",", ":"), sort_keys=True) + "\n")


def read_jsonl(path: Path) -> list[dict]:
    path = Path(path)
    if not path.is_file():
        return []
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line:
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                continue  # a torn final line from an interrupted writer
    return rows


# ----------------------------------------------------------------------------- snapshots and builds
@dataclass
class Entrant:
    """A built, immutable snapshot of a bot (optionally with knob overrides)."""
    bot: Bot
    version: str
    path: Path            # absolute bot directory inside the snapshot
    ok: bool
    log: str = ""
    overrides: tuple = ()  # sorted (name, value) pairs
    label: str = ""        # free-form display name, e.g. a sweep variant

    @property
    def key(self) -> str:
        """Ledger identity: <suite>/<bot>@<version>."""
        return f"{self.bot.id}@{self.version}"

    @property
    def name(self) -> str:
        return self.label or self.bot.id


TRANSIENT = "TRANSIENT:"  # prefix of build logs that must not be cached as a failure


def toolchain() -> str:
    """Identity of everything outside the closure that a snapshot build depends on (CLI, SDK headers,
    CMake, cambc.toml). Snapshots built with another toolchain are rebuilt, so upgrades take effect."""
    global _TOOLCHAIN
    if _TOOLCHAIN is None:
        h = hashlib.sha256()
        st = CAMBC.stat() if CAMBC.is_file() else None
        h.update(f"cli {st.st_size if st else 0} {st.st_mtime_ns if st else 0}\0".encode())
        for rel in ("sdk/cpp/cambc.h", "sdk/cpp/imports.h", "cmake/CMakeLists.txt", "cambc.toml"):
            f = ROOT / rel
            h.update(rel.encode() + b"\0" + (f.read_bytes() if f.is_file() else b"") + b"\0")
        _TOOLCHAIN = h.hexdigest()[:12]
    return _TOOLCHAIN


_TOOLCHAIN: str | None = None


def build(bot_dir: Path, timeout: int = 1800) -> tuple[bool, str]:
    """`cambc build <dir> --release` (release mode matches the platform). Returns (ok, output tail)."""
    try:
        proc = subprocess.run([str(CAMBC), "build", str(bot_dir), "--release"], cwd=ROOT, capture_output=True,
                              text=True, errors="replace", timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, f"{TRANSIENT} build timed out after {timeout}s"
    out = (proc.stdout or "") + (proc.stderr or "")
    if proc.returncode < 0 or proc.returncode in (0xC000013A, -1073741510):  # killed or Ctrl-C
        return False, f"{TRANSIENT} build interrupted (exit {proc.returncode})\n" + out[-6000:]
    return proc.returncode == 0 and "built" in out, out[-6000:]


def _link_sdk(snap: Path) -> None:
    link = snap / "sdk"
    if link.exists():
        return
    if os.name == "nt":
        import _winapi
        _winapi.CreateJunction(str(ROOT / "sdk"), str(link))
    else:
        os.symlink(ROOT / "sdk", link, target_is_directory=True)


def resolve_snapshot_root(value=None, owner: str | None = None) -> Path:
    """Only the global namespace or an explicitly named owner's direct snap root."""
    if value is None:
        if owner is not None:
            raise ArenaError("snapshot owner requires an explicit root")
        return CACHE / "snap"
    root = Path(value)
    if not root.is_absolute():
        root = ROOT / root
    if root != CACHE / "snap":
        if (type(owner) is not str or not re.fullmatch(r"[A-Za-z0-9_.-]+", owner) or owner in (".", "..")
                or owner.endswith(".") or re.fullmatch(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", owner, re.I)):
            raise ArenaError("invalid private snapshot owner")
        if root != ROOT / ".cache" / owner / "snap" or owner.casefold() == "arena":
            raise ArenaError("snapshot root is not this owner's namespace")
    if root.resolve() != root:
        raise ArenaError("snapshot namespace is redirected")
    return root


def cached_snapshot(key: str, *, snapshot_root=None, snapshot_owner=None) -> Entrant:
    """Scope-neutral immutable input validation; this grants no native execution permission."""
    match = re.fullmatch(r"([A-Za-z0-9_-][A-Za-z0-9_.-]*)/([A-Za-z0-9_-][A-Za-z0-9_.-]*)@([0-9a-f]{12})", key)
    if not match:
        raise ArenaError("invalid cached snapshot identity")
    suite, name, version = match.groups()
    if any(part.endswith(".") or re.fullmatch(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", part, re.I)
           for part in (suite, name)):
        raise ArenaError("invalid cached snapshot path identity")
    snap = resolve_snapshot_root(snapshot_root, snapshot_owner) / version
    target = snap / "bots" / suite / name
    def direct(path):
        if path.resolve() != path or _is_reparse(path):
            raise ArenaError("cached snapshot input is redirected")
        return path
    direct(snap)
    for filename in ("meta.json", ".ready", ".toolchain"):
        if not direct(snap / filename).is_file():
            raise ArenaError("cached snapshot metadata is unavailable")
    meta = json.loads((snap / "meta.json").read_text(encoding="utf-8"))
    if not isinstance(meta, dict):
        raise ArenaError("cached snapshot metadata must be an object")
    settings = meta.get("overrides")
    if (meta.get("bot") != f"{suite}/{name}" or meta.get("version") != version or (snap / ".failed").exists()
            or not isinstance(settings, dict) or not all(type(k) is str and type(v) is str for k, v in settings.items())
            or (snap / ".toolchain").read_text().strip() != toolchain()):
        raise ArenaError("cached snapshot identity/configuration/toolchain differs")
    files, pending = set(), [target / "src"]
    while pending:
        with os.scandir(direct(pending.pop())) as entries:
            for entry in entries:
                path = direct(Path(entry.path))
                if path.name == ".cambc":
                    continue
                if entry.is_dir(follow_symlinks=False):
                    pending.append(path)
                elif entry.is_file(follow_symlinks=False):
                    files.add(path)
    stack = [path for path in files if path.suffix in SOURCE_SUFFIXES]
    while stack:
        source = stack.pop()
        for include in INCLUDE.findall(_read_text(source)):
            lexical = Path(os.path.abspath(source.parent / include))
            resolved = lexical.resolve()
            if not resolved.is_relative_to(snap):
                if not resolved.is_relative_to((ROOT / "sdk").resolve()):
                    raise ArenaError("cached source include escapes its snapshot and pinned SDK")
                continue  # External SDK is pinned by toolchain, as in ordinary closure validation.
            if resolved == lexical and not lexical.exists():
                continue  # Quoted SDK/system headers can be found through compiler include dirs.
            candidate = direct(lexical)
            if candidate.is_file() and candidate not in files:
                files.add(candidate)
                if candidate.suffix in SOURCE_SUFFIXES:
                    stack.append(candidate)
    contents = {ROOT / path.relative_to(snap): path.read_bytes() for path in files}
    if not contents or meta.get("files") != len(contents) or version_of(contents, settings) != version:
        raise ArenaError("cached snapshot source digest differs")
    for filename in ("bot.wasm", "bot-injected.wasm"):
        path = direct(target / ".cambc/build-release" / filename)
        if not path.is_file() or not 8 <= path.stat().st_size <= 256 * 1024 * 1024:
            raise ArenaError("cached snapshot RAW/SDK module is missing or invalid")
        with path.open("rb") as stream:
            if stream.read(8) != b"\0asm\x01\0\0\0":
                raise ArenaError("cached snapshot RAW/SDK module is not WASM version1")
    return Entrant(Bot(suite, name), version, target, True, overrides=tuple(sorted(settings.items())))


def prepare(b: Bot, overrides: dict[str, str] | None = None, label: str = "", rebuild_failed: bool = False,
            contents: dict[Path, bytes] | None = None, *, snapshot_root=None, snapshot_owner=None) -> Entrant:
    """Snapshot `b` (plus overrides) by content hash and build it once; safe under concurrency.
    `contents` (from read_closure) pins the sources instead of reading the working tree now."""
    overrides = {k: str(v) for k, v in (overrides or {}).items()}
    contents = dict(contents) if contents is not None else read_closure(b)
    if overrides:
        contents = apply_overrides(b, contents, overrides)
    ver = version_of(contents, overrides)
    root = resolve_snapshot_root(snapshot_root, snapshot_owner)
    snap = root / ver
    if snapshot_root is not None and snap.resolve() != snap:
        raise ArenaError("snapshot directory is redirected")
    target = snap / b.path.relative_to(ROOT)
    ent = Entrant(b, ver, target, False, "", tuple(sorted(overrides.items())), label)
    with FileLock(CACHE / "snap" / f"{ver}.lock"):
        if snapshot_root is not None:
            if resolve_snapshot_root(snapshot_root, snapshot_owner) != root or snap.resolve() != snap:
                raise ArenaError("snapshot namespace changed while waiting for its version lock")
        if root != CACHE / "snap":
            root.mkdir(parents=True, exist_ok=True)
            (root / ".keep").touch()  # Preserve diagnostic inputs, not this owner's experiment output.
        stamp = snap / ".toolchain"
        current = stamp.is_file() and stamp.read_text(encoding="utf-8").strip() == toolchain()
        if (snap / ".ready").is_file() and current:
            (snap / ".ready").touch()
            ent.ok = True
            return ent
        if snap.exists() and not current:
            shutil.rmtree(snap, ignore_errors=True)  # built by another CLI/SDK: rebuild with this one
        if (snap / ".failed").is_file() and not rebuild_failed:
            ent.log = (snap / ".failed").read_text(encoding="utf-8", errors="replace")
            return ent
        if snap.exists():
            shutil.rmtree(snap, ignore_errors=True)  # an interrupted earlier attempt
        for p, data in contents.items():
            dst = snap / p.relative_to(ROOT)
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(data)
        _link_sdk(snap)
        (snap / "meta.json").write_text(json.dumps({"bot": b.id, "version": ver, "overrides": overrides,
                                                     "files": len(contents)}, indent=1), encoding="utf-8")
        with Slots(2, quiet=True):
            ok, log = build(target)
        if not ok and log.startswith(TRANSIENT):
            shutil.rmtree(snap, ignore_errors=True)  # retried next time instead of cached as failed
        else:
            (snap / ".toolchain").write_text(toolchain() + "\n", encoding="utf-8")
            (snap / (".ready" if ok else ".failed")).write_text(log, encoding="utf-8", errors="replace")
        ent.ok, ent.log = ok, log
    return ent


# ----------------------------------------------------------------------------- cold owner builds
COLD_BUILD_SCHEMA = "arena.owner-cold-build-pair.v1"


def _cold_hash(value) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def _cold_direct(path: Path) -> Path:
    if not path.is_absolute() or path.resolve() != path or _is_reparse(path):
        raise ArenaError(f"cold build input/output is redirected: {path}")
    return path


def _cold_file(path: Path) -> dict:
    import stat
    _cold_direct(path)
    if not path.is_file() or not stat.S_ISREG(path.stat().st_mode):
        raise ArenaError(f"cold build regular file is missing: {path}")
    digest, size = hashlib.sha256(), 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
            size += len(block)
    return {"path": str(path), "size": size, "sha256": digest.hexdigest()}


def _cold_tree_leaf(path: Path) -> dict:
    """Hash every byte below a walk-validated direct directory; never follow a leaf redirect."""
    import stat
    info = path.lstat()
    if (stat.S_ISLNK(info.st_mode) or path.is_symlink() or getattr(info, "st_file_attributes", 0) & 0x400
            or not stat.S_ISREG(info.st_mode)):
        raise ArenaError("cold build runtime contains a redirected or nonregular leaf")
    digest, size = hashlib.sha256(), 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
            size += len(block)
    return {"size": size, "sha256": digest.hexdigest()}


def _cold_tree(path: Path) -> dict:
    _cold_direct(path)
    if not path.is_dir():
        raise ArenaError(f"cold build runtime directory is missing: {path}")
    files, pending = [], [path]
    while pending:
        parent = pending.pop()
        if parent != path:
            _cold_direct(parent)
        with os.scandir(parent) as entries:
            for entry in entries:
                child = Path(entry.path)
                if entry.is_dir(follow_symlinks=False):
                    pending.append(child)
                elif entry.is_file(follow_symlinks=False):
                    descriptor = _cold_tree_leaf(child)
                    files.append([child.relative_to(path).as_posix(), descriptor["size"], descriptor["sha256"]])
                else:
                    raise ArenaError("cold build runtime contains a nonregular input")
    files.sort()
    return {"path": str(path), "files": len(files), "bytes": sum(row[1] for row in files), "sha256": _cold_hash(files)}


def _cold_sources(bot: Bot, contents: dict[Path, bytes], expected_key: str) -> tuple[dict, dict]:
    if (not isinstance(bot, Bot) or not re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9_.-]*/[A-Za-z0-9_-][A-Za-z0-9_.-]*", bot.id)
            or any(part.endswith(".") or re.fullmatch(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", part, re.I)
                   for part in (bot.suite, bot.name))):
        raise ArenaError("cold build logical bot identity is invalid")
    own = ROOT / "bots" / bot.suite / bot.name / "src"
    copied, descriptors = {}, {}
    for path, data in dict(contents).items():
        if not isinstance(path, Path) or not path.is_absolute() or type(data) is not bytes or not path.is_relative_to(own):
            raise ArenaError("cold build requires immutable original-ID src bytes")
        relative = path.relative_to(own)
        if not relative.parts or any(not re.fullmatch(r"[A-Za-z0-9_.-]+", part) or part in (".", "..", ".cambc")
                                     or part.endswith((".", " ")) or re.fullmatch(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", part, re.I)
                                     for part in relative.parts):
            raise ArenaError("cold build source path is unsafe")
        logical = path.relative_to(ROOT).as_posix()
        if logical.casefold() in {key.casefold() for key in descriptors}:
            raise ArenaError("cold build source names collide")
        copied[path] = data
        descriptors[logical] = {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    if own / "main.cpp" not in copied or expected_key != f"{bot.id}@{version_of(copied)}":
        raise ArenaError("cold build original source identity differs")
    for path, data in copied.items():
        if path.suffix not in SOURCE_SUFFIXES:
            continue
        for include in INCLUDE.findall(data.decode("utf-8", errors="surrogateescape")):
            if "\\" in include or "\0" in include or ":" in include:
                raise ArenaError("cold build source include is unsafe")
            lexical = Path(os.path.abspath(path.parent / include))
            if lexical in copied or lexical.is_relative_to(ROOT / "sdk"):
                continue
            sdk_candidate = Path(os.path.abspath(ROOT / "sdk/cpp" / include))
            if not sdk_candidate.is_relative_to(ROOT / "sdk") or not sdk_candidate.is_file():
                raise ArenaError("cold build source reaches outside its released src and pinned SDK")
    return copied, dict(sorted(descriptors.items()))


def _cold_roots(owner: str, roots, *, fresh=False) -> tuple[Path, Path]:
    # Reuse the established owner-name admission without admitting a cache namespace.
    if type(owner) is not str:
        raise ArenaError("cold build owner must be explicit")
    resolve_snapshot_root(ROOT / ".cache" / owner / "snap", owner)
    base = ROOT / ".cache" / owner
    values = tuple(Path(path) for path in roots)
    if len(values) != 2 or values[0] == values[1]:
        raise ArenaError("cold build requires two distinct roots")
    if values[0].parent != values[1].parent:
        raise ArenaError("cold build roots must share one admitted owner output parent")
    for path in values:
        if (not path.is_absolute() or path.parent not in (base, base / "qualification-tooling") or path.name in ("snap", "exp")
                or not re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9_.-]*", path.name)
                or path.name.endswith(".") or re.fullmatch(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", path.name, re.I)
                or path.resolve() != path):
            raise ArenaError("cold build root is not a direct unredirected owner child")
        if fresh and path.exists():
            raise ArenaError("cold build refuses existing roots or cached outputs")
        if not fresh:
            _cold_direct(path)
    return values


def _cold_runtime_installation(configured: Path, anchor: Path) -> tuple[Path, dict]:
    """Pin one reviewed tool installation through Windows per-file virtualization.

    A logical installation directory may resolve to itself while its files resolve into
    the packaged app's LocalCache. Every configured file must bind the same relative
    member of the physical installation selected by its actual executable.
    """
    if not configured.is_absolute() or anchor.is_absolute() or any(part == ".." for part in anchor.parts):
        raise ArenaError("cold runtime installation path is invalid")
    executable = (configured / anchor).resolve()
    physical = executable
    for _ in anchor.parts:
        physical = physical.parent
    tree = _cold_tree(physical)
    files, pending = 0, [configured]
    while pending:
        parent = pending.pop()
        relative = parent.relative_to(configured)
        expected = physical / relative
        if parent.resolve() not in (parent, expected) or not expected.is_dir():
            raise ArenaError("cold runtime directory reaches outside its pinned installation")
        with os.scandir(parent) as entries:
            for entry in entries:
                logical = Path(entry.path)
                expected = physical / logical.relative_to(configured)
                if entry.is_dir(follow_symlinks=False):
                    pending.append(logical)
                elif entry.is_file(follow_symlinks=False):
                    if logical.resolve() != expected or not expected.is_file():
                        raise ArenaError("cold runtime file differs from its pinned installation identity")
                    files += 1
                else:
                    raise ArenaError("cold runtime configured view contains a nonregular input")
    if files != tree["files"]:
        raise ArenaError("cold runtime configured and physical inventories differ")
    return physical, {**tree, "configured_path": str(configured)}


def cold_build_runtime() -> dict:
    """Actual byte pins, not the ordinary cached toolchain() identity. No credential/env scan."""
    host = {name: os.environ.get(name) for name in ("LOCALAPPDATA", "USERPROFILE", "SystemRoot")}
    if any(type(value) is not str or not value or "\0" in value for value in host.values()):
        raise ArenaError("cold build requires named Windows runtime directories")
    local, system = Path(host["LOCALAPPDATA"]), Path(host["SystemRoot"])
    # Installed runtime aliases can legitimately resolve into this packaged app's LocalCache.
    # Pin their physical files; owner source/output paths never receive this relaxation.
    wasi, wasi_tree = _cold_runtime_installation(local / "cambc/wasi-sdk-33", Path("bin/clang++.exe"))
    cmake, cmake_tree = _cold_runtime_installation(local / "cambc/cmake-3.31.8-windows-x86_64", Path("bin/cmake.exe"))
    ninja, ninja_tree = _cold_runtime_installation(local / "cambc/ninja-1.12.1-windows-x86_64", Path("ninja.exe"))
    paths = {"cli": CAMBC, "compiler": wasi / "bin/clang++.exe", "linker": wasi / "bin/wasm-ld.exe",
             "cmake": cmake / "bin/cmake.exe", "ninja": ninja / "ninja.exe", "node": NODE,
             "python": Path(sys.executable), "toolchain": wasi / "share/cmake/wasi-sdk-p1.cmake"}
    configured = {name: str(path) for name, path in paths.items()}
    files = {name: _cold_file(path.resolve()) for name, path in paths.items()}
    for name in (f"python{sys.version_info.major}{sys.version_info.minor}.dll", "python3.dll"):
        path = Path(sys.executable).resolve().parent / name
        if path.is_file():
            files[name] = _cold_file(path.resolve())
    for name in ("cambc.toml", "docs/reference/current-version.json", "docs/reference/engine-identity.json",
                 "tools/arena/core.py", "tools/arena/compiled_runner.py"):
        files[name] = _cold_file(ROOT / name)
    bundle = json.loads((ROOT / "docs/reference/current-version.json").read_bytes())
    identity = json.loads((ROOT / "docs/reference/engine-identity.json").read_bytes())
    if files["cli"]["sha256"] != bundle["cli"]["windowsX64"]["sha256"] or files["cli"]["sha256"] != identity["nativeCli"]["sha256"]:
        raise ArenaError("cold build CLI differs from the current pinned runtime")
    return {"schema": "arena.cold-build-runtime.v1", "host": host, "configured": configured, "files": files,
            "trees": {"sdk": _cold_tree(ROOT / "sdk"), "project_cmake": _cold_tree(ROOT / "cmake"),
                      "wasi": wasi_tree, "cmake": cmake_tree, "ninja": ninja_tree},
            "system_directory": str(_cold_direct((system / "System32").resolve())), "python_version": sys.version}


def _cold_environment(runtime: dict, root: Path, count: int) -> dict:
    if type(count) is not int or not 1 <= count <= 2:
        raise ArenaError("cold build slot count is invalid")
    host, files = runtime["host"], runtime["files"]
    system = Path(host["SystemRoot"])
    path = [Path(files[name]["path"]).parent for name in ("compiler", "cmake", "ninja")]
    path += [system / "System32", system]
    return {"LOCALAPPDATA": host["LOCALAPPDATA"], "USERPROFILE": host["USERPROFILE"],
            "SystemRoot": host["SystemRoot"], "WINDIR": host["SystemRoot"],
            "PATH": os.pathsep.join(map(str, path)), "TEMP": str(root / "tmp"), "TMP": str(root / "tmp"),
            "CMAKE_BUILD_PARALLEL_LEVEL": str(count), "LANG": "C", "LC_ALL": "C", "TZ": "UTC"}


def _cold_source_check(root: Path, bot: Bot, contents: dict, version: str) -> Path:
    snapshot = root / version
    target = snapshot / "bots" / bot.suite / bot.name
    source = target / "src"
    _cold_direct(source)
    actual, pending = set(), [source]
    while pending:
        with os.scandir(_cold_direct(pending.pop())) as entries:
            for entry in entries:
                path = _cold_direct(Path(entry.path))
                if entry.is_dir(follow_symlinks=False):
                    pending.append(path)
                elif entry.is_file(follow_symlinks=False):
                    actual.add(path)
                else:
                    raise ArenaError("cold build source has a nonregular member")
    expected = {snapshot / logical.relative_to(ROOT): data for logical, data in contents.items()}
    if actual != set(expected) or any(path.read_bytes() != data for path, data in expected.items()):
        raise ArenaError("cold build released source bytes changed")
    if (snapshot / "sdk").resolve() != (ROOT / "sdk").resolve():
        raise ArenaError("cold build SDK link differs from the pinned SDK")
    return target


def _cold_generated(target: Path, runtime: dict) -> dict:
    build = target / ".cambc/build-release"
    cache_path = build / "CMakeCache.txt"
    cache = {}
    for line in _cold_direct(cache_path).read_text(encoding="utf-8", errors="replace").splitlines():
        if line and not line.startswith(("#", "//")) and "=" in line:
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    paths = {"BOT_DIR": target, "BC_SDK": ROOT / "sdk", "CMAKE_HOME_DIRECTORY": ROOT / "cmake",
             "CMAKE_COMMAND": Path(runtime["files"]["cmake"]["path"]),
             "CMAKE_MAKE_PROGRAM": Path(runtime["files"]["ninja"]["path"]),
             "CMAKE_TOOLCHAIN_FILE": Path(runtime["files"]["toolchain"]["path"])}
    if cache.get("CMAKE_BUILD_TYPE") != "Release" or any(not cache.get(key) or Path(cache[key]).resolve() != value.resolve() for key, value in paths.items()):
        raise ArenaError("cold build selected an unpinned configuration/tool path")
    compilers = list(build.glob("CMakeFiles/*/CMakeCXXCompiler.cmake"))
    if len(compilers) != 1:
        raise ArenaError("cold build compiler selection is ambiguous")
    compiler_text = _cold_direct(compilers[0]).read_text(encoding="utf-8", errors="replace")
    selected = re.search(r'^set\(CMAKE_CXX_COMPILER "([^"\n]+)"\)', compiler_text, re.M)
    if not selected or Path(selected.group(1)).resolve() != Path(runtime["files"]["compiler"]["path"]):
        raise ArenaError("cold build actual compiler differs from its runtime pin")
    names = {"cache": cache_path, "compiler": compilers[0], "commands": build / "compile_commands.json",
             "ninja": build / "build.ninja", "rules": build / "CMakeFiles/rules.ninja"}
    return {name: _cold_file(path) for name, path in names.items()}


def _cold_build_record(root: Path, bot: Bot, contents: dict, runtime: dict, version: str, count: int, timeout: int) -> dict:
    target = _cold_source_check(root, bot, contents, version)
    directory = root / "process"
    _cold_file(directory / "dispatch.json")
    dispatch = json.loads((directory / "dispatch.json").read_bytes())
    environment = _cold_environment(runtime, root, count)
    command = [str(CAMBC), "build", str(target), "--release"]
    if dispatch != {"argv": command, "cwd": str(ROOT), "timeout": timeout, "environment": environment}:
        raise ArenaError("cold build dispatch differs from the actual bound command/environment")
    _cold_file(directory / "process-end.json")
    process = json.loads((directory / "process-end.json").read_bytes())
    containment = process.get("containment") or {}
    if (type(process.get("returncode")) is not int or process.get("returncode") != 0 or process.get("timed_out") is not False or process.get("tree_exit_confirmed") is not True
            or process.get("forced_termination") is not False or containment.get("kind") != "windows-private-job"
            or containment.get("empty_confirmed") is not True or containment.get("assigned_before_resume") is not True
            or containment.get("resumed") is not True or containment.get("kill_on_close") is not True
            or containment.get("forced_termination") is not False):
        raise ArenaError("cold build did not finish cleanly in an empty private Job")
    for name in ("stdout", "stderr"):
        expected = _cold_file(directory / (name + ".bin"))
        expected["path"] = Path(expected["path"]).relative_to(ROOT).as_posix()
        if process.get(name) != expected:
            raise ArenaError("cold build process output bytes changed")
    outputs = {}
    for role, name in (("raw", "bot.wasm"), ("sdk", "bot-injected.wasm")):
        path = target / ".cambc/build-release" / name
        descriptor = _cold_file(path)
        with path.open("rb") as stream:
            if not 8 <= descriptor["size"] <= 256 * 1024 * 1024 or stream.read(8) != b"\0asm\x01\0\0\0":
                raise ArenaError("cold build RAW/SDK output is not a valid WASM1 module")
        outputs[role] = descriptor
    return {"root": str(root), "target": str(target), "environment": environment, "granted_slots": count, "timeout": timeout,
            **outputs, "generated": _cold_generated(target, runtime),
            "transport": {name: _cold_file(directory / filename) for name, filename in
                          (("dispatch", "dispatch.json"), ("process", "process-end.json"), ("stdout", "stdout.bin"), ("stderr", "stderr.bin"))}}


def validate_cold_build_pair(pair: dict, bot: Bot, contents: dict[Path, bytes], *, owner: str, expected_key: str) -> dict:
    """Rehash actual cold inputs/process/generated outputs; no builds, warming or cache reads."""
    copied, sources = _cold_sources(bot, contents, expected_key)
    if (type(pair) is not dict or set(pair) != {"schema", "bot", "key", "owner", "roots", "sources", "runtime", "builds", "fingerprint"}
            or pair.get("schema") != COLD_BUILD_SCHEMA or pair.get("bot") != bot.id or pair.get("key") != expected_key
            or pair.get("owner") != owner or pair.get("sources") != sources or type(pair.get("roots")) is not list):
        raise ArenaError("cold build pair input identity differs")
    roots = _cold_roots(owner, pair["roots"])
    runtime = cold_build_runtime()
    if pair["runtime"] != runtime or pair["fingerprint"] != _cold_hash({key: value for key, value in pair.items() if key != "fingerprint"}):
        raise ArenaError("cold build pair runtime/fingerprint differs")
    if type(pair["builds"]) is not list or len(pair["builds"]) != 2:
        raise ArenaError("cold build pair lacks two actual builds")
    version = expected_key.rsplit("@", 1)[1]
    for root, build in zip(roots, pair["builds"]):
        if type(build) is not dict or type(build.get("timeout")) is not int or not 1 <= build["timeout"] <= 24 * 3600:
            raise ArenaError("cold build timeout is invalid")
        expected = _cold_build_record(root, bot, copied, runtime, version, build.get("granted_slots"), build["timeout"])
        if build != expected:
            raise ArenaError("cold build pair actual artifacts differ")
    return pair


def cold_build_pair(bot: Bot, contents: dict[Path, bytes], *, owner: str, roots, expected_key: str, timeout: int = 1800) -> dict:
    """Two fresh owner-local builds under the original ID. No prepare(), ready reuse or warming.

    This produces current-toolchain reproduction inputs, not historical server-byte proof.
    The release consumer must independently bind the ZIP/release and compare both roles to its reference.
    """
    from contextlib import ExitStack
    import compiled_runner as native
    copied, sources = _cold_sources(bot, contents, expected_key)
    roots = _cold_roots(owner, roots, fresh=True)
    if type(timeout) is not int or not 1 <= timeout <= 24 * 3600:
        raise ArenaError("cold build timeout must be bounded and positive")
    version = expected_key.rsplit("@", 1)[1]
    slots, locks = Slots(2, quiet=True), ExitStack()
    slots.__enter__()  # Match owner CLI work: never wait for admission while holding VERSION.
    release = True
    directory = None
    try:
        locks.enter_context(FileLock(CACHE / "snap" / f"{version}.lock"))
        roots = _cold_roots(owner, roots, fresh=True)
        runtime = cold_build_runtime()
        for root in roots:
            root.mkdir(parents=True, exist_ok=False)
            (root / "tmp").mkdir()
            (root / "process").mkdir()
            snapshot = root / version
            for path, data in copied.items():
                destination = snapshot / path.relative_to(ROOT)
                destination.parent.mkdir(parents=True, exist_ok=True)
                with destination.open("xb") as output:
                    output.write(data)
            _link_sdk(snapshot)
        builds = []
        for root in roots:
            target = _cold_source_check(root, bot, copied, version)
            directory = root / "process"
            environment = _cold_environment(runtime, root, slots.count)
            record = native._command([str(CAMBC), "build", str(target), "--release"], ROOT, directory, timeout, env=environment)
            if not record.get("tree_exit_confirmed"):
                raise ArenaError("cold build transport returned without confirmed private Job exit")
            if cold_build_runtime() != runtime:
                raise ArenaError("cold build runtime changed during execution")
            builds.append(_cold_build_record(root, bot, copied, runtime, version, slots.count, timeout))
        if builds != [_cold_build_record(root, bot, copied, runtime, version, slots.count, timeout) for root in roots]:
            raise ArenaError("cold build earlier artifacts changed during the second build")
        pair = {"schema": COLD_BUILD_SCHEMA, "bot": bot.id, "key": expected_key, "owner": owner,
                "roots": list(map(str, roots)), "sources": sources, "runtime": runtime, "builds": builds}
        pair["fingerprint"] = _cold_hash(pair)
        with (roots[0] / "cold-build-pair.json").open("x", encoding="utf-8", newline="\n") as output:
            json.dump(pair, output, sort_keys=True, indent=1, allow_nan=False)
            output.write("\n")
        return pair
    except BaseException as error:
        process = getattr(error, "compiled_process", None)
        if process is not None and not process.empty_confirmed:
            release = False
            class Admission:
                def __exit__(self, *args):
                    try:
                        locks.close()
                    finally:
                        slots.__exit__(*args)
            native._PENDING_CLEANUP.append({"process": process, "slots": Admission(), "directory": directory, "error": error})
        raise
    finally:
        if release:
            try:
                locks.close()
            finally:
                slots.__exit__(None, None, None)


TRIMMED = ".trimmed"  # marker: the snapshot's build directories hold only bot.wasm


def trim_snapshot(snap: Path) -> int:
    """Reduce a built snapshot to what fastrun needs: sources, meta and each build's bot.wasm. The CLI
    path (games.py) re-warms a trimmed snapshot, which rebuilds its build directory on first use.
    Returns the bytes freed; 0 when the snapshot is in use, not ready, or already trimmed."""
    if snap.parent != CACHE / "snap":  # Private diagnostic inputs retain both payload roles.
        return 0
    if not (snap / ".ready").is_file() or (snap / TRIMMED).is_file():
        return 0
    freed = 0
    try:
        with FileLock(snap.with_name(snap.name + ".lock"), poll=0.05, timeout=0.2):
            for build in snap.glob("bots/*/*/.cambc/build*"):  # build (cambc build) and build-release
                if not build.is_dir() or _is_reparse(build):
                    continue
                for item in list(build.iterdir()):
                    if item.name == "bot.wasm" or _is_reparse(item):
                        continue
                    if item.is_dir():
                        freed += sum(f.stat().st_size for f in item.rglob("*") if f.is_file())
                        shutil.rmtree(item, ignore_errors=True)
                    else:
                        freed += item.stat().st_size
                        item.unlink(missing_ok=True)
            (snap / ".batch-ready").unlink(missing_ok=True)  # the CLI path must rebuild before use
            (snap / TRIMMED).write_text("build directories reduced to bot.wasm\n", encoding="utf-8")
    except ArenaError:
        return 0
    return freed


def _is_reparse(p: Path) -> bool:
    """Symlinks and Windows junctions (snapshots link sdk/): never entered or deleted through."""
    try:
        st = p.lstat()
    except OSError:
        return True
    return p.is_symlink() or bool(getattr(st, "st_file_attributes", 0) & 0x400)  # FILE_ATTRIBUTE_REPARSE_POINT


def prepare_many(items: list[tuple], workers: int | None = None, rebuild_failed: bool = False,
                 *, snapshot_root=None, snapshot_owner=None) -> list[Entrant]:
    """prepare() for (bot, overrides, label[, contents]) items in parallel threads (builds share the slot pool)."""
    from concurrent.futures import ThreadPoolExecutor
    workers = workers or max(1, min(8, slot_count() // 2))
    with ThreadPoolExecutor(max_workers=workers) as pool:
        return list(pool.map(lambda it: prepare(it[0], it[1], it[2], rebuild_failed=rebuild_failed,
                                                contents=it[3] if len(it) > 3 else None,
                                                **({"snapshot_root": snapshot_root, "snapshot_owner": snapshot_owner}
                                                   if snapshot_root is not None else {})), items))


# ----------------------------------------------------------------------------- commands
def _cmd_bots(args) -> int:
    rows = []
    for b in select(args.select):
        rows.append({"id": b.id, "frozen": b.frozen, "version": version(b) if args.versions else ""})
    if args.json:
        print(json.dumps(rows, indent=1))
        return 0
    by_suite: dict[str, list[str]] = {}
    for r in rows:
        s, n = r["id"].split("/", 1)
        by_suite.setdefault(s, []).append(n + (f"@{r['version']}" if r["version"] else ""))
    for s, names in by_suite.items():
        tag = " (frozen)" if s in FROZEN_SUITES else ""
        print(f"{s}{tag} [{len(names)}]: {' '.join(names)}")
    print(f"{len(rows)} bots in {len(by_suite)} suites")
    return 0


def parse_sets(items) -> dict[str, str]:
    """`--set k=v` arguments (repeatable, or several pairs in one argument) as an overrides dict."""
    out: dict[str, str] = {}
    for item in items or []:
        for pair in item.split():
            if "=" not in pair:
                raise ArenaError(f"--set expects name=value, got {pair!r}")
            k, v = pair.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def _cmd_build(args) -> int:
    chosen = select(args.select)
    overrides = parse_sets(args.set)
    if overrides and args.in_place:
        raise ArenaError("--set builds a snapshot variant; it cannot be combined with --in-place")
    if args.in_place:
        from concurrent.futures import ThreadPoolExecutor

        def one(b):
            with FileLock(CACHE / "inplace" / f"{b.suite}__{b.name}.lock"), Slots(2, quiet=True):
                return b, *build(b.path)
        with ThreadPoolExecutor(max_workers=args.workers or max(1, slot_count() // 2)) as pool:
            results = [(b, ok, log) for b, ok, log in pool.map(one, chosen)]
    else:
        ents = prepare_many([(b, overrides or None, "") for b in chosen], args.workers, args.rebuild_failed)
        results = [(e.bot, e.ok, e.log) for e in ents]
    failed = [(b, log) for b, ok, log in results if not ok]
    for b, log in failed:
        tail = "\n    ".join(l for l in log.strip().splitlines()[-12:])
        print(f"FAILED {b.id}\n    {tail}")
    print(f"built {len(results) - len(failed)}/{len(results)}" + ("" if args.in_place else " (snapshots)"))
    return 1 if failed else 0


def _cmd_test(args) -> int:
    extra = [a for a in (args.extra or []) if a != "--"][:]
    chosen = select(args.select)
    by_suite: dict[str, list[str]] = {}
    for b in chosen:
        by_suite.setdefault(b.suite, []).append(b.name)
    whole = {s for s in by_suite if len(by_suite[s]) == len([b for b in all_bots() if b.suite == s])}
    failed, ran = [], 0
    for s, names in by_suite.items():
        runner = BOTS / s / "tests" / "run.py"
        if not runner.is_file():
            print(f"{s}: no tests/run.py")
            continue
        cmd = [sys.executable, "-B", str(runner)] + ([] if s in whole else names) + extra
        print(f"== {s}: {' '.join(cmd[2:])}", flush=True)
        ran += 1
        if subprocess.run(cmd, cwd=ROOT).returncode != 0:
            failed.append(s)
    print(f"suites tested {ran}, failed {len(failed)}" + (f": {', '.join(failed)}" if failed else ""))
    return 1 if failed else 0


def _cmd_env(args) -> int:
    manifest = json.loads((ROOT / "docs/reference/current-version.json").read_text(encoding="utf-8"))

    def sha(p: Path) -> str:
        return hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else "missing"
    problems = []

    def pins(o):
        if isinstance(o, dict):
            if isinstance(o.get("path"), str) and isinstance(o.get("sha256"), str):
                yield o["path"], o["sha256"]
            for v in o.values():
                yield from pins(v)
        elif isinstance(o, list):
            for v in o:
                yield from pins(v)
    checked = dict(pins(manifest))
    for rel, want in checked.items():
        if sha(ROOT / rel) != want:
            problems.append(f"{rel} differs from docs/reference/current-version.json")
    for m in _pool_index():
        if sha(MAPS / m["file"]) != m["sha256"]:
            problems.append(f"map {m['file']} missing or changed")
    print(f"bundle   cli/sdk {manifest['cli']['version']}, engine {manifest.get('engine', {}).get('version', '?')}"
          f", {len(checked)} pinned files + pool maps ({'ok' if not problems else str(len(problems)) + ' problems'})")
    for p in problems[:20]:
        print("  ", p)
    print(f"python   {sys.executable}")
    print(f"node     {NODE} ({'ok' if NODE.is_file() else 'missing'})")
    print(f"clang    {CLANG} ({'ok' if CLANG.is_file() else 'missing'})")
    print(f"slots    {busy_slots()}/{slot_count()} busy (ARENA_SLOTS overrides)")
    print(f"bots     {len(all_bots())} in {len(suites())} suites")
    snaps = list((CACHE / "snap").glob("*/meta.json")) if (CACHE / "snap").is_dir() else []
    print(f"cache    {len(snaps)} snapshots in {CACHE}")
    return 1 if problems else 0


def register(sub) -> None:
    p = sub.add_parser("env", help="check the pinned CLI/SDK/maps and show runtimes, slots and cache")
    p.set_defaults(func=_cmd_env)
    p = sub.add_parser("bots", help="list bots (all bots/<suite>/<bot>/src/main.cpp)")
    p.add_argument("select", nargs="?", default="all")
    p.add_argument("--versions", action="store_true", help="show the content version of each bot")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=_cmd_bots)
    p = sub.add_parser("build", help="build bots as cached snapshots (default) or in place")
    p.add_argument("select", nargs="?", default="all")
    p.add_argument("--in-place", action="store_true", help="build the working directories instead")
    p.add_argument("--set", action="append", metavar="K=V", help="knob override for every selected bot (repeatable)")
    p.add_argument("--workers", type=int, default=0)
    p.add_argument("--rebuild-failed", action="store_true", help="retry snapshots whose last build failed")
    p.set_defaults(func=_cmd_build)
    p = sub.add_parser("test", help="run bots/<suite>/tests/run.py for the selected suites ([-- ARGS] for run.py)")
    p.add_argument("select", nargs="?", default="all")
    p.add_argument("extra", nargs=argparse.REMAINDER, help="after --: arguments for each run.py (e.g. --fixtures)")
    p.set_defaults(func=_cmd_test)
