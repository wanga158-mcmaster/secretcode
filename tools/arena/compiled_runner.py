"""Fresh local matches from existing exact WASM snapshots, without building projects.

The installed API consumes a ZIP containing manifest.json and bot.wasm. Full matches
are required: its statistics export rejects requested truncation. All native output
is retained. These records measure execution and existing health, never admission
or contribution coverage. File bindings do not attest internal memory/JIT behavior.
Run directly with Python: compiled_runner.py play ... or regress ... .
"""
from __future__ import annotations

import argparse
from collections import Counter
import ctypes
from ctypes import wintypes
from dataclasses import dataclass
import hashlib
import json
import math
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import sys
import time
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import core
import games
import health_context
import panel_executables
import population
from core import ArenaError

ROOT = core.ROOT
PRODUCER = Path(__file__).resolve()
EXCLUDED = {"main", "surprise", "contenders"}
KEY = re.compile(r"([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)@([0-9a-f]{12})")
PORTABLE = {"apiVersion": 1, "status": "ready", "language": "cpp"}
MEMBERS = {"result.json", "replay.repl26a", "summary.camstat"} | {
    f"team-{side}.{extension}" for side in "ab" for extension in ("stdout", "debug", "fuel", "cli.log")}
MAX_FILE = 512 * 1024 * 1024
MANIFEST_CHUNK = 128  # subprocess size only; all planned games continue in subsequent chunks
# Directly observed portable-output pairing. Unknown values fail closed, not guessed.
BRIEF_OUTCOMES = {1: "teamA", 2: "teamB"}  # official visualiser bw(); exact draw enum unproved
_PENDING_CLEANUP = []  # retained Job/process handles AND acquired slots, never a launch reservation


def _digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def _path(value) -> Path:
    p = Path(value)
    return Path(os.path.abspath(p if p.is_absolute() else ROOT / p))


def _deny_location(value) -> Path:
    """Normalize traversal/case and inspect aliases before any content read."""
    path = _path(value)
    for candidate in (path, path.resolve()):
        parts = [part.casefold() for part in candidate.parts]
        if any(part == "bots" and parts[index + 1] in EXCLUDED
               for index, part in enumerate(parts[:-1])):
            raise ArenaError("path reaches an excluded bot suite")
    return path


def _regular(path: Path) -> None:
    _deny_location(path)
    if path.resolve() != path or not path.is_file() or not stat.S_ISREG(path.stat().st_mode):
        raise ArenaError(f"missing/noncanonical regular file: {path}")


def _desc(path: Path) -> dict:
    _regular(path)
    key = "path" if path.is_relative_to(ROOT) else "file"
    return {key: path.relative_to(ROOT).as_posix() if key == "path" else str(path),
            "sha256": _digest(path), "size": path.stat().st_size}


def _inventory(directory: Path) -> list[dict]:
    result = []
    for path in sorted(directory.rglob("*")):
        if path.is_file() or path.is_symlink():
            try:
                result.append(_desc(path))
            except (ArenaError, OSError) as error:
                result.append({"path": path.relative_to(ROOT).as_posix(), "unhashed": str(error)})
    return result


def _descriptor_path(value: dict) -> Path:
    return _path(value["path"]) if "path" in value else Path(value["file"])


def _check(pins: list[dict]) -> None:
    for pin in pins:
        if _desc(_descriptor_path(pin)) != pin:
            raise ArenaError(f"input changed: {pin}")


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ArenaError(f"duplicate JSON field: {key}")
        result[key] = value
    return result


def _json(raw: bytes):
    return json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs,
                      parse_constant=lambda value: (_ for _ in ()).throw(ArenaError(f"invalid constant: {value}")))


def _frozen_json(path: Path):
    _regular(path)
    if path.stat().st_size > MAX_FILE:
        raise ArenaError("JSON input exceeds bounded reader")
    raw = path.read_bytes()
    key = "path" if path.is_relative_to(ROOT) else "file"
    pin = {key: path.relative_to(ROOT).as_posix() if key == "path" else str(path),
           "sha256": hashlib.sha256(raw).hexdigest(), "size": len(raw)}
    if _desc(path) != pin:
        raise ArenaError("JSON file changed during capture")
    return _json(raw), pin


def _load(path: Path):
    return _frozen_json(path)[0]


def _write(path: Path, value) -> None:
    with path.open("x", encoding="utf-8", newline="\n") as f:
        json.dump(value, f, sort_keys=True, indent=2, allow_nan=False)
        f.write("\n")
        f.flush()
        os.fsync(f.fileno())


def _key(value: str) -> tuple[str, str, str]:
    match = KEY.fullmatch(value) if isinstance(value, str) else None
    if not match or any(part in (".", "..") for part in match.groups()[:2]):
        raise ArenaError("an exact canonical suite/bot@12-lowercase-hex snapshot is required")
    suite, name, version = match.groups()
    if suite.casefold() in EXCLUDED:
        raise ArenaError("main, surprise and contenders are outside this runner's mandate")
    return suite, name, version


def _excluded_paths(value) -> None:
    """Preflight every panel entry before sequential old validation reads source."""
    if isinstance(value, dict):
        for k, v in value.items():
            _excluded_paths(k)
            _excluded_paths(v)
    elif isinstance(value, list):
        for v in value:
            _excluded_paths(v)
    elif isinstance(value, str):
        spelling = value.replace("\\", "/").casefold()
        if re.search(r"(^|/)bots/", spelling):
            _deny_location(value)


def _safe_files(directory: Path, *, measurement=False) -> list[Path]:
    directory = _deny_location(directory)
    if directory.resolve() != directory:
        raise ArenaError("redirected source/measurement directory")
    if not directory.exists():
        return []
    if directory.is_file():
        _regular(directory)
        return [directory]
    if not directory.is_dir():
        raise ArenaError("nonregular source/measurement root")
    found = []
    for child in directory.iterdir():
        if (not measurement and child.name == ".cambc") or (measurement and (child.name == "__pycache__" or child.suffix == ".pyc")):
            continue
        _deny_location(child)
        if child.resolve() != child:
            raise ArenaError("redirected source/measurement leaf")
        if child.is_dir():
            found.extend(_safe_files(child, measurement=measurement))
        else:
            _regular(child)
            found.append(child)
    return found


def _preflight_snapshot(key: str, *, snapshot_root=None, snapshot_owner=None) -> None:
    suite, name, version = _key(key)
    snap = core.resolve_snapshot_root(snapshot_root, snapshot_owner) / version
    directory = snap / "bots" / suite / name
    _deny_location(directory)
    if directory.resolve() != directory:
        raise ArenaError("redirected snapshot root")
    for filename in ("meta.json", ".ready", ".toolchain"):
        _regular(snap / filename)
    meta = _load(snap / "meta.json")
    if meta.get("bot") != f"{suite}/{name}" or meta.get("version") != version:
        raise ArenaError("snapshot metadata identity mismatch")
    files = set(_safe_files(directory / "src"))
    stack = [path for path in files if path.suffix in core.SOURCE_SUFFIXES]
    while stack:
        source = stack.pop()
        _regular(source)
        for include in core.INCLUDE.findall(source.read_text(encoding="utf-8", errors="surrogateescape")):
            candidate = _deny_location(source.parent / include)
            resolved = candidate.resolve()
            if not resolved.is_relative_to(snap):
                continue  # external SDK candidates are not read by the old closure walker
            if resolved != candidate:
                raise ArenaError("redirected relative source include")
            if candidate.is_file() and candidate not in files:
                _regular(candidate)
                files.add(candidate)
                if candidate.suffix in core.SOURCE_SUFFIXES:
                    stack.append(candidate)


def _preflight_measurements(contract: dict) -> None:
    bot = contract["bot"]
    suite, name = bot.split("/", 1)
    _key(f"{bot}@000000000000")
    paths = [f"bots/{suite}/tests", f"bots/{suite}/{name}/tests", "tools/arena/wasi.py",
             "tools/arena/wasi-run.mjs", "tools/arena/gate.py", "tools/arena/population.py", "tools/arena/games.py"]
    paths += list(contract.get("test_paths", [])) + list(contract.get("observer_paths", []))
    observer = contract.get("observer")
    if isinstance(observer, str):
        candidate = _deny_location(observer)
        if candidate.is_file():
            paths.append(observer)
    for value in paths:
        _safe_files(_deny_location(value), measurement=True)


def _preflight_descriptors(value) -> None:
    if isinstance(value, dict):
        if "path" in value and "sha256" in value:
            _regular(_deny_location(value["path"]))
        for child in value.values():
            _preflight_descriptors(child)
    elif isinstance(value, list):
        for child in value:
            _preflight_descriptors(child)


@dataclass
class Binding:
    key: str
    raw: dict
    injected: dict
    payload: bytes
    role: str
    source_files: list[dict]
    snapshot_root: str | None = None

    def record(self):
        record = {"snapshot": self.key, "raw": self.raw, "injected": self.injected,
                "selected_role": self.role, "inner_bot_sha256": hashlib.sha256(self.payload).hexdigest(),
                "source_files": self.source_files}
        if self.snapshot_root is not None:
            record["snapshot_root"] = self.snapshot_root
        return record


def _capture(key: str, role: str, expected: dict | None = None, *, snapshot_root=None, snapshot_owner=None) -> Binding:
    suite, name, version = _key(key)
    options = {"snapshot_root": snapshot_root, "snapshot_owner": snapshot_owner} if snapshot_root is not None else {}
    root = core.resolve_snapshot_root(snapshot_root, snapshot_owner)
    _preflight_snapshot(key, **options)
    directory = root / version / "bots" / suite / name
    if directory.resolve() != directory:
        raise ArenaError("snapshot directory is redirected")
    ent = population._snapshot(key, **options)  # source/meta/toolchain checks; never prepare/build
    if ent.path != directory or ent.key != key or not ent.ok:
        raise ArenaError("snapshot validator identity disagrees")
    files, selected = {}, None
    for label, filename in (("raw", "bot.wasm"), ("injected", "bot-injected.wasm")):
        path = directory / ".cambc/build-release" / filename
        _regular(path)
        if path.stat().st_size > MAX_FILE:
            raise ArenaError("WASM module exceeds bounded capture")
        data = path.read_bytes()  # these captured bytes, not a later path reopen, feed ZIP
        value = _desc(path)
        if hashlib.sha256(data).hexdigest() != value["sha256"]:
            raise ArenaError("cached module changed during capture")
        if data[:8] != b"\0asm\x01\0\0\0":
            raise ArenaError("cached executable is not a WASM version1 module")
        if expected and value["path"] != expected[label]["path"]:
            raise ArenaError("noncanonical executable binding")
        if expected and value["sha256"] != expected[label]["sha256"]:
            raise ArenaError("bound executable changed during capture")
        files[label] = value
        if label == role:
            selected = data
    snap = root / version
    source_files = [_desc(snap / rel) for rel in population._snapshot_files(ent, **options)]
    source_files += [_desc(snap / name) for name in ("meta.json", ".ready", ".toolchain")]
    return Binding(key, files["raw"], files["injected"], selected, role, source_files,
                   root.relative_to(ROOT).as_posix() if root != core.CACHE / "snap" else None)


def _fresh_out(value) -> Path:
    out = _path(value)
    cache = (ROOT / ".cache").resolve()
    relative = out.relative_to(cache) if out.is_relative_to(cache) else Path()
    if (out.resolve() != out or not out.is_relative_to(cache) or out == cache or
            len(relative.parts) >= 2 and relative.parts[1].casefold() == "snap" or
            out.is_relative_to(core.CACHE / "snap") or out.is_relative_to(core.CACHE / "slots")):
        raise ArenaError("fresh output must be a nonredirected directory under regenerable .cache, outside snapshots/slots")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.mkdir(exist_ok=False)
    return out


def _pack(path: Path, binding: Binding) -> dict:
    with zipfile.ZipFile(path, "x", compression=zipfile.ZIP_STORED) as z:
        for name, data in (("manifest.json", json.dumps(PORTABLE, separators=(",", ":")).encode()),
                           ("bot.wasm", binding.payload)):
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = 0o100600 << 16
            z.writestr(info, data)
    with zipfile.ZipFile(path) as z:
        return {"archive": _desc(path), "members": [{"name": info.filename, "size": info.file_size,
                "sha256": hashlib.sha256(z.read(info)).hexdigest()} for info in z.infolist()]}


class _BasicLimit(ctypes.Structure):
    _fields_ = [("PerProcessUserTimeLimit", ctypes.c_longlong), ("PerJobUserTimeLimit", ctypes.c_longlong),
                ("LimitFlags", wintypes.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD), ("SchedulingClass", wintypes.DWORD)]


class _IOCounters(ctypes.Structure):
    _fields_ = [(name, ctypes.c_ulonglong) for name in ("ReadOperationCount", "WriteOperationCount", "OtherOperationCount",
                                                     "ReadTransferCount", "WriteTransferCount", "OtherTransferCount")]


class _ExtendedLimit(ctypes.Structure):
    _fields_ = [("BasicLimitInformation", _BasicLimit), ("IoInfo", _IOCounters),
                ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]


class _Accounting(ctypes.Structure):
    _fields_ = [(name, ctypes.c_longlong) for name in ("TotalUserTime", "TotalKernelTime", "ThisPeriodTotalUserTime", "ThisPeriodTotalKernelTime")]
    _fields_ += [(name, wintypes.DWORD) for name in ("TotalPageFaultCount", "TotalProcesses", "ActiveProcesses", "TotalTerminatedProcesses")]


def _explicit_environment(env):
    """Validate only a caller-supplied mapping; None preserves inherited default behavior.

    _winapi.CreateProcess converts mappings to a Windows Unicode environment block.
    Never enumerate or copy the ambient process environment here.
    """
    if env is None:
        return None
    if type(env) is not dict:
        raise ArenaError("explicit process environment must be a dictionary")
    result, folded = {}, set()
    for key, value in env.items():
        if (type(key) is not str or not key or "=" in key or "\0" in key or type(value) is not str or "\0" in value
                or key.casefold() in folded):
            raise ArenaError("explicit process environment has an invalid or duplicate key/value")
        folded.add(key.casefold())
        result[key] = value
    return result


class _WinJobProcess:
    """Private Windows Job, assigned before resuming the initial thread. No PID kill.

    Caught creation/assignment/resume failures clean exact handles. Abrupt launcher
    death between suspended creation and assignment can leave a suspended, never
    resumed initial process; arbitrary-crash freedom in that window is not claimed.
    """
    drain_seconds = 1.0

    def __init__(self, argv, cwd, stdout, stderr, *, env=None):
        env = _explicit_environment(env)
        if os.name != "nt":
            raise ArenaError("compiled runner requires verified Windows Job containment")
        import _winapi
        import msvcrt
        self.argv, self.pid, self.returncode = argv, None, None
        self.job = self.process = self.thread = None
        self.assigned = self.resumed = self.empty_confirmed = self.forced = False
        self.seen = set()
        self.k = ctypes.WinDLL("kernel32", use_last_error=True)
        signatures = {
            "CreateJobObjectW": ([ctypes.c_void_p, wintypes.LPCWSTR], wintypes.HANDLE),
            "SetInformationJobObject": ([wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD], wintypes.BOOL),
            "AssignProcessToJobObject": ([wintypes.HANDLE, wintypes.HANDLE], wintypes.BOOL),
            "QueryInformationJobObject": ([wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p], wintypes.BOOL),
            "TerminateJobObject": ([wintypes.HANDLE, wintypes.UINT], wintypes.BOOL),
            "TerminateProcess": ([wintypes.HANDLE, wintypes.UINT], wintypes.BOOL),
            "ResumeThread": ([wintypes.HANDLE], wintypes.DWORD),
            "WaitForSingleObject": ([wintypes.HANDLE, wintypes.DWORD], wintypes.DWORD),
            "GetExitCodeProcess": ([wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)], wintypes.BOOL),
            "CloseHandle": ([wintypes.HANDLE], wintypes.BOOL),
        }
        for name, (args, result) in signatures.items():
            function = getattr(self.k, name)
            function.argtypes, function.restype = args, result
        try:
            self.job = self.k.CreateJobObjectW(None, None)
            self._require(self.job, "CreateJobObject")
            limits = _ExtendedLimit()
            limits.BasicLimitInformation.LimitFlags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            self._require(self.k.SetInformationJobObject(self.job, 9, ctypes.byref(limits), ctypes.sizeof(limits)), "SetJobLimits")
            with open(os.devnull, "rb") as stdin:
                handles = [msvcrt.get_osfhandle(f.fileno()) for f in (stdin, stdout, stderr)]
                inherited = [os.get_handle_inheritable(handle) for handle in handles]
                info = subprocess.STARTUPINFO()
                info.dwFlags |= subprocess.STARTF_USESTDHANDLES
                info.hStdInput, info.hStdOutput, info.hStdError = handles
                info.lpAttributeList = {"handle_list": handles}
                try:
                    for handle in handles:
                        os.set_handle_inheritable(handle, True)
                    self.process, self.thread, self.pid, _ = _winapi.CreateProcess(
                        str(argv[0]), subprocess.list2cmdline(argv), None, None, True,
                        subprocess.CREATE_NO_WINDOW | 0x00000004 | (0x00000400 if env is not None else 0),
                        env, str(cwd), info)  # CREATE_SUSPENDED; _winapi serializes explicit mappings as UTF-16.
                finally:
                    for handle, before in zip(handles, inherited):
                        os.set_handle_inheritable(handle, before)
            self.seen.add(self.pid)
            self._assign()
            self.assigned = True
            self._resume()
            self.resumed = True
        except BaseException as error:
            try:
                if self.process:
                    if self.assigned:
                        self.terminate_tree()
                    else:
                        self._require(self.k.TerminateProcess(self.process, 1), "TerminateSuspendedProcess")
                        self._require(self.k.WaitForSingleObject(self.process, 10000) == 0, "WaitSuspendedProcess")
                        self.empty_confirmed = self._active() == 0
                else:
                    self.empty_confirmed = True
            except BaseException as stop_error:
                error.compiled_live_pid = self.pid
                error.compiled_process = self
                error.add_note(f"Suspended-process cleanup unconfirmed: {stop_error!r}")
            error.compiled_process_record = self.record()
            if self.empty_confirmed:
                self.close()
            raise
        finally:
            if self.thread:
                self.k.CloseHandle(self.thread)
                self.thread = None

    def _require(self, ok, name):
        if not ok:
            raise ctypes.WinError(ctypes.get_last_error(), name)

    def _assign(self):
        self._require(self.k.AssignProcessToJobObject(self.job, self.process), "AssignProcessToJobObject")

    def _resume(self):
        previous = self.k.ResumeThread(self.thread)
        if previous != 1:
            raise ArenaError(f"ResumeThread expected suspended-count1, got {previous}")

    def _active(self):
        value = _Accounting()
        self._require(self.k.QueryInformationJobObject(self.job, 1, ctypes.byref(value), ctypes.sizeof(value), None), "QueryJobAccounting")
        return int(value.ActiveProcesses)

    def _pids(self):
        capacity = 64
        while capacity <= 8192:
            data = ctypes.create_string_buffer(8 + capacity * ctypes.sizeof(ctypes.c_size_t))
            if self.k.QueryInformationJobObject(self.job, 3, data, len(data), None):
                count = struct.unpack_from("<II", data.raw)[1]
                if count > capacity:
                    raise ArenaError("job PID response exceeds buffer")
                values = {ctypes.c_size_t.from_buffer(data, 8 + i * ctypes.sizeof(ctypes.c_size_t)).value for i in range(count)}
                self.seen.update(values)
                return values
            if ctypes.get_last_error() != 234:  # ERROR_MORE_DATA
                self._require(False, "QueryJobPIDs")
            capacity *= 2
        raise ArenaError("owned process telemetry exceeds bound")

    def _empty(self, timeout):
        deadline = time.monotonic() + timeout
        while True:
            self._pids()
            if self._active() == 0:
                self.empty_confirmed = True
                return
            if time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired(self.argv, timeout)
            time.sleep(0.02)

    def poll(self):
        status = self.k.WaitForSingleObject(self.process, 0)
        if status == 0:
            code = wintypes.DWORD()
            self._require(self.k.GetExitCodeProcess(self.process, ctypes.byref(code)), "GetExitCodeProcess")
            self.returncode = int(code.value)
        elif status != 258:
            self._require(False, "WaitForSingleObject")
        return self.returncode

    def wait(self, timeout):
        deadline = time.monotonic() + timeout
        while self.poll() is None:
            self._pids()
            if time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired(self.argv, timeout)
            time.sleep(0.02)
        try:
            self._empty(min(self.drain_seconds, max(0.0, deadline - time.monotonic())))
        except subprocess.TimeoutExpired:
            self.terminate_tree()
        return self.returncode

    def terminate_tree(self):
        self._pids()
        if self._active():
            self.forced = True
            self._require(self.k.TerminateJobObject(self.job, 1), "TerminateJobObject")
        self._empty(10)
        if self.process:
            self._require(self.k.WaitForSingleObject(self.process, 10000) == 0, "WaitTerminatedParent")
            self.poll()

    def record(self):
        return {"kind": "windows-private-job", "initial_pid": self.pid,
                "job_handle": int(self.job) if self.job else None,
                "process_handle": int(self.process) if self.process else None,
                "assigned_before_resume": self.assigned, "resumed": self.resumed,
                "observed_owned_pids": sorted(self.seen), "empty_confirmed": self.empty_confirmed,
                "forced_termination": self.forced, "kill_on_close": True,
                "limit": "Abrupt launcher death before job assignment can strand an unresumed initial process."}

    def close(self):
        if self.process:
            self.k.CloseHandle(self.process)
            self.process = None
        if self.job:
            self.k.CloseHandle(self.job)
            self.job = None


def _terminate(proc, directory):
    proc.terminate_tree()
    _write(directory / "containment-stop.json", proc.record())


def _slotted_command(argv_factory, cwd, directory, timeout, jobs=1, *, minimum=None, poll=None):
    """One Slot admission and private Job; omitted admission options retain old defaults."""
    admission = {"quiet": True}
    if minimum is not None:
        admission["minimum"] = minimum
        admission["poll"] = poll
    elif poll is not None:
        admission["poll"] = poll
    slots = core.Slots(jobs, **admission)
    slots.__enter__()
    release = True
    try:
        return _command(argv_factory(slots.count), cwd, directory, timeout(slots.count) if callable(timeout) else timeout)
    except BaseException as error:
        process = getattr(error, "compiled_process", None)
        if process is not None and not process.empty_confirmed:
            release = False
            _PENDING_CLEANUP.append({"process": process, "slots": slots, "directory": directory, "error": error})
        raise
    finally:
        if release:
            slots.__exit__(None, None, None)


def confirm_pending_cleanup() -> bool:
    """Explicit recovery of retained owned handles; no new game/worker or PID kill."""
    for item in list(_PENDING_CLEANUP):
        process = item["process"]
        try:
            if not process.empty_confirmed:
                if process.assigned:
                    process.terminate_tree()
                else:
                    process._require(process.k.TerminateProcess(process.process, 1), "TerminateSuspendedProcess")
                    process._require(process.k.WaitForSingleObject(process.process, 10000) == 0, "WaitSuspendedProcess")
                    process.empty_confirmed = process._active() == 0
            if not process.empty_confirmed:
                continue
            process.close()
            item["slots"].__exit__(None, None, None)
            _PENDING_CLEANUP.remove(item)
            _write(item["directory"] / "cleanup-confirmed.json", process.record())
        except BaseException as error:
            item["error"].add_note(f"Owned cleanup still unconfirmed: {error!r}")
    return not _PENDING_CLEANUP


def _command(argv, cwd, directory, timeout, *, env=None):
    """One subprocess, raw logs retained even on launch/timeout exceptions. No retries."""
    env = _explicit_environment(env)
    dispatch = {"argv": argv, "cwd": str(cwd), "timeout": timeout}
    if env is not None:
        dispatch["environment"] = env
    _write(directory / "dispatch.json", dispatch)
    start = time.monotonic()
    with (directory / "stdout.bin").open("xb") as out, (directory / "stderr.bin").open("xb") as err:
        try:
            proc = _WinJobProcess(argv, cwd, out, err, **({"env": env} if env is not None else {}))
        except BaseException as error:
            try:
                _write(directory / "containment-failure.json", getattr(error, "compiled_process_record", {"creation_failed": True}))
            except BaseException as publication_error:
                error.add_note(f"Containment failure publication also failed: {publication_error!r}")
            raise
        publication_error = None
        primary = None
        try:
            _write(directory / "process-start.json", {"pid": proc.pid})
        except BaseException as error:
            publication_error = error
        timed_out = False
        try:
            code = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            try:
                _terminate(proc, directory)
            except BaseException as stop_error:
                stop_error.compiled_process_record = proc.record()
                if not proc.empty_confirmed:
                    stop_error.compiled_live_pid = proc.pid
                    stop_error.compiled_process = proc
                    stop_error.add_note("Owned process-tree exit could not be confirmed")
                else:
                    proc.close()
                raise
            code = proc.returncode
        except BaseException as error:
            primary = error
            try:
                _terminate(proc, directory)
            except BaseException as stop_error:
                if not proc.empty_confirmed:
                    primary.compiled_live_pid = proc.pid
                    primary.compiled_process = proc
                primary.add_note(f"Process-tree stop also failed: {stop_error!r}; PID {proc.pid}")
            code = proc.returncode
    containment = proc.record()
    if proc.empty_confirmed:
        proc.close()
    record = {"pid": proc.pid, "returncode": code, "timed_out": timed_out,
              "containment": containment, "tree_exit_confirmed": containment["empty_confirmed"],
              "forced_termination": containment["forced_termination"],
              "elapsed_seconds": time.monotonic() - start,
              "stdout": _desc(directory / "stdout.bin"), "stderr": _desc(directory / "stderr.bin")}
    try:
        _write(directory / "process-end.json", record)
    except BaseException as error:
        if publication_error is None:
            publication_error = error
    if primary is not None:
        primary.compiled_process_record = record
        if publication_error is not None:
            primary.add_note(f"Process evidence publication also failed: {publication_error!r}")
        raise primary
    if publication_error is not None:
        publication_error.compiled_process_record = record
        raise publication_error
    return record


def _events(path: Path, planned: dict) -> dict:
    states = {key: {"started": False, "terminal": None, "errors": []} for key in planned}
    errors, counts, batch = [], Counter(), None
    fields = {"started": {"apiVersion", "event", "id", "pid"},
              "progress": {"apiVersion", "event", "id", "round", "maxRounds", "status"},
              "finished": {"apiVersion", "event", "id", "replay"},
              "error": {"apiVersion", "event", "id", "errorCode", "diagnostic"},
              "batchFinished": {"apiVersion", "event", "failed"}}
    if not path.is_file():
        return {"jobs": states, "errors": ["machine stdout missing"], "counts": {}, "batch": None}
    with path.open("rb") as f:
        for number, raw in enumerate(f, 1):
            try:
                if len(raw) > 1024 * 1024 or not raw.endswith(b"\n"):
                    raise ArenaError("oversize or partial machine line")
                value = _json(raw)
                event = value.get("event") if isinstance(value, dict) else None
                if event not in fields or set(value) != fields[event] or type(value["apiVersion"]) is not int or value["apiVersion"] != 1:
                    raise ArenaError("unknown/malformed machine event")
                if batch is not None:
                    raise ArenaError("event after batchFinished")
                counts[event] += 1
                if event == "batchFinished":
                    if type(value["failed"]) is not int or value["failed"] < 0:
                        raise ArenaError("invalid failed count")
                    batch = value
                    continue
                key = value["id"]
                if key not in states:
                    raise ArenaError("unknown job ID")
                state = states[key]
                if event == "started":
                    if state["started"] or type(value["pid"]) is not int or value["pid"] <= 0:
                        raise ArenaError("duplicate/invalid started")
                    state["started"] = True
                    state["worker_pid"] = value["pid"]
                elif not state["started"] or state["terminal"]:
                    raise ArenaError("event before started or after job terminal")
                elif event == "progress":
                    if (type(value["round"]) is not int or type(value["maxRounds"]) is not int or value["round"] < 0 or
                            value["maxRounds"] <= 0 or value["round"] < state.get("round", 0) or
                            value["status"] not in ("drafting", "running", "finished", "truncated")):
                        raise ArenaError("invalid progress")
                    state["round"] = value["round"]
                    state["progress"] = state.get("progress", 0) + 1
                    state["status"] = value["status"]
                    if value["status"] == "truncated":
                        state["errors"].append("full job reported truncated")
                elif event == "finished":
                    target = value["replay"]
                    if not isinstance(target, str) or _path(target.removeprefix("\\\\?\\")) != planned[key]:
                        raise ArenaError("finished output path disagrees")
                    # Progress is sampled; finished is the unique authoritative terminal event.
                    # Exit/batch success and complete native output are checked separately.
                    state["terminal"] = "finished"
                else:
                    if not isinstance(value["errorCode"], str) or not isinstance(value["diagnostic"], str):
                        raise ArenaError("invalid error event")
                    state["terminal"] = "error"
                    state["errors"].append(value)
            except (ValueError, TypeError, KeyError, UnicodeError, ArenaError) as exc:
                if len(errors) < 32:
                    errors.append(f"line {number}: {exc}")
                else:
                    counts["omitted_errors"] += 1
    if batch is None:
        errors.append("batchFinished missing")
    elif batch["failed"] != sum(s["terminal"] == "error" for s in states.values()):
        errors.append("batch failed count disagrees with terminal records")
    for key, state in states.items():
        if not state["started"] or state["terminal"] is None:
            state["errors"].append("job lifecycle incomplete")
    return {"jobs": states, "errors": errors, "counts": dict(counts), "batch": batch}


def _uint(value):
    if not ((type(value) is int and value >= 0) or (type(value) is str and re.fullmatch(r"[0-9]{1,20}", value))):
        raise ArenaError("missing/invalid unsigned native metric")
    return int(value)


def _statistics(stats, brief, map_sha, engine):
    match = stats["match"]
    diagnostics = stats["diagnostics"]
    if diagnostics["detailedCaptureComplete"] is not True:
        raise ArenaError("full CAMSTAT detailed capture is not complete")
    if not isinstance(diagnostics["codes"], list) or diagnostics["codes"]:
        raise ArenaError("native diagnostics codes are missing, malformed or unexplained")
    for key, expected in (("engineBuildHash", engine["buildHash"]), ("engineVersion", engine["version"]), ("mapHash", map_sha)):
        if match[key] != expected or brief["result"][key] != expected:
            raise ArenaError(f"native identity mismatch: {key}")
    if match["outcome"] not in ("teamA", "teamB", "draw"):
        raise ArenaError("native outcome missing")
    if type(brief["result"]["outcome"]) is not int or BRIEF_OUTCOMES.get(brief["result"]["outcome"]) != match["outcome"]:
        raise ArenaError("brief outcome disagrees or its numeric mapping is not yet verified")
    completed, started = _uint(match["roundsCompleted"]), _uint(match["roundsStarted"])
    if started - completed not in (0, 1) or _uint(brief["result"]["rounds"]) != started or type(match["finalRoundPartial"]) is not bool:
        raise ArenaError("native round counters disagree")
    if type(brief["result"]["detailedCaptureComplete"]) is not bool:
        raise ArenaError("brief capture field missing; it does not override full capture")
    if _uint(match["metricSemanticsVersion"]) != _uint(brief["result"]["metricSemanticsVersion"]):
        raise ArenaError("metric semantics disagree")
    for key in ("finalInput", "finalSubstep", "width", "height"):
        _uint(match[key])
    if type(match["seed"]) not in (str, int) or not str(match["seed"]).isascii() or not str(match["seed"]).isdigit():
        raise ArenaError("native seed unavailable")
    faults = diagnostics["botFaults"]
    if set(faults) != {"teamA", "teamB"} or not isinstance(brief["botFaults"], list) or len(brief["botFaults"]) != 2:
        raise ArenaError("native faults incomplete")
    for index, side in enumerate(("teamA", "teamB")):
        if _uint(faults[side]) != _uint(brief["botFaults"][index]):
            raise ArenaError("brief/full faults disagree")
    teams = stats["teams"]
    bt = brief["result"]["teams"]
    if not isinstance(teams, list) or len(teams) != 2 or {t["team"] for t in teams} != {"teamA", "teamB"}:
        raise ArenaError("native team totals incomplete")
    if not isinstance(bt, list) or len(bt) != 2 or any(type(t["team"]) is not int for t in bt) or {t["team"] for t in bt} != {0, 1}:
        raise ArenaError("brief team totals incomplete")
    for team in teams:
        numeric = 0 if team["team"] == "teamA" else 1
        other = next(t for t in bt if t["team"] == numeric)
        if _uint(team["submittedGems"]) != _uint(other["submittedGems"]):
            raise ArenaError("brief/full submitted gems disagree")
    roster, champions = stats["roster"]["champions"], stats["champions"]
    if not isinstance(roster, list) or not roster or not isinstance(champions, list):
        raise ArenaError("native roster missing")
    identities = {c["id"]: c for c in roster}
    if len(identities) != len(roster) or len(champions) != len(roster) or {c["id"] for c in champions} != set(identities):
        raise ArenaError("native roster duplicate/mismatch")
    for c in roster:
        _uint(c["id"])
        _uint(c["draftPick"])
        if c["team"] not in ("teamA", "teamB") or not isinstance(c["name"], str):
            raise ArenaError("invalid roster member")
    for c in champions:
        _uint(c["id"])
        source = identities[c["id"]]
        if c["team"] != source["team"] or c["name"] != source["name"]:
            raise ArenaError("champion/roster identity differs")
        for metric in ("earnedGold", "kills", "deaths"):
            _uint(c["metrics"][metric])
    if not isinstance(stats["events"], list):
        raise ArenaError("native event table missing")
    required_health = {(kind, actor) for kind in ("campClear", "fuelExhausted") for actor in identities}
    seen_health = set()
    for group in stats["events"]:
        if not isinstance(group, dict) or not isinstance(group["kind"], str) or not isinstance(group["entries"], list):
            raise ArenaError("native event group malformed")
        if group["kind"] in ("campClear", "fuelExhausted"):
            owner = group["owner"]
            identity = identities.get(owner.get("id"))
            if (identity is None or type(owner.get("id")) is not int or owner.get("kind") != "champion" or
                    owner.get("team") != identity["team"] or owner.get("name") != identity["name"]):
                raise ArenaError("health event owner unavailable")
            key = (group["kind"], owner["id"])
            if key in seen_health:
                raise ArenaError("duplicate native health kind/owner group")
            seen_health.add(key)
            for entry in group["entries"]:
                if not isinstance(entry, dict) or not isinstance(entry["timestamp"], dict):
                    raise ArenaError("health event timestamp unavailable")
                stamp = entry["timestamp"]
                if set(stamp) != set(health_context.STAMP):
                    raise ArenaError("health event timestamp fields incomplete")
                for field in health_context.STAMP:
                    value = stamp[field]
                    if field == "phase":
                        if type(value) is not str or not value or len(value) > 32 or "\0" in value:
                            raise ArenaError("health event phase malformed")
                    elif type(value) is not int or value < 0:
                        raise ArenaError("health event timestamp integer malformed")
    if seen_health != required_health:
        raise ArenaError("dense native health kind/owner groups incomplete")


def _consume(job, directory, cli, engine):
    archive = directory / "output.zip"
    _regular(archive)
    files, manifest = {}, []
    with zipfile.ZipFile(archive) as z:
        infos = z.infolist()
        if len(infos) != len(MEMBERS) or {i.filename for i in infos} != MEMBERS:
            raise ArenaError("missing/duplicate/unknown output ZIP member")
        for info in infos:
            if info.is_dir() or stat.S_ISLNK(info.external_attr >> 16) or info.file_size > MAX_FILE:
                raise ArenaError("invalid/bounded ZIP member")
            data = z.read(info)  # verifies each CRC, including the opaque replay payload
            manifest.append({"name": info.filename, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
            if info.filename != "replay.repl26a":
                files[info.filename] = data
    _write(directory / "archive-members.json", manifest)
    if files["summary.camstat"][:8] != b"CAMSTAT\0":
        raise ArenaError("invalid CAMSTAT header")
    camstat = directory / "summary.camstat"
    with camstat.open("xb") as f:
        f.write(files["summary.camstat"])
    summary = directory / "summary.json"
    convert = directory / "summary-conversion"
    convert.mkdir()
    pins = [_desc(archive), _desc(camstat), _desc(cli)]
    process = _slotted_command(lambda _: [str(cli), "summary", str(camstat), "-o", str(summary)], directory, convert, 120)
    _check(pins)
    if (process["returncode"] != 0 or process["timed_out"] or not process.get("tree_exit_confirmed") or
            process.get("forced_termination")):
        raise ArenaError("documented summary conversion failed")
    stats, stats_pin = _frozen_json(summary)
    brief = _json(files["result.json"])
    _statistics(stats, brief, job["map_sha256"], engine)
    context = health_context.extract(stats, stats_pin["sha256"])
    _write(directory / "native-context.json", context)
    if context["status"] != "complete":
        raise ArenaError("native exhaustion context incomplete")
    projection = games.parse_statistics(stats)
    for side in "AB":
        fuel = files[f"team-{side.lower()}.fuel"]
        if (fuel[:8] != games.FUEL_MAGIC or len(fuel) < games.FUEL_HEADER + 64 or
                (len(fuel) - games.FUEL_HEADER) % 8):
            raise ArenaError("missing/malformed fuel stream")
        values = struct.unpack_from(f"<{(len(fuel) - games.FUEL_HEADER) // 8}Q", fuel, games.FUEL_HEADER)
        picks, bodies = projection["roster"][side]
        if picks < 1 or bodies < picks or len(values) <= 8 + picks + bodies or not all(values[:8]):
            raise ArenaError("fuel/roster ordinary gameplay unavailable")
    channels, notes = games.parse_replay(archive, projection["roster"])
    if notes:
        raise ArenaError(f"native channel measurement incomplete: {notes}")
    for side in "AB":
        projection[side].update(channels[side])
    projection["truncated"] = False  # terminal outcome, not finalRoundPartial
    _check([stats_pin])
    return {**projection, "replay": archive.relative_to(ROOT).as_posix(),
            "health": {side: games.health(projection, side) for side in "AB"},
            "measurement_complete": True, "native_match": stats["match"],
            "brief_result": brief, "statistics": stats_pin, "archive": _desc(archive)}


def _dependencies():
    files = [PRODUCER, Path(core.__file__), Path(games.__file__), Path(population.__file__),
             Path(panel_executables.__file__), Path(health_context.__file__),
             ROOT / "docs/reference/current-version.json", ROOT / "docs/reference/engine-identity.json"]
    bundle, bundle_pin = _frozen_json(files[-2])
    cli = core.CAMBC
    _regular(cli)
    if _digest(cli) != bundle["cli"]["windowsX64"]["sha256"]:
        raise ArenaError("installed CLI disagrees with current bundle pin")
    identity, identity_pin = _frozen_json(files[-1])
    pins = [_desc(p) for p in files + [cli]]
    if pins[-3] != bundle_pin or pins[-2] != identity_pin:
        raise ArenaError("bundle/engine metadata changed during capture")
    return cli, identity["engine"], pins


def _run(specs, bindings, output, role, jobs, timeout, provenance):
    if role not in ("raw", "injected") or type(jobs) is not int or jobs < 1 or type(timeout) is not int or timeout < 1:
        raise ArenaError("invalid role/jobs/timeout")
    if (len({s["id"] for s in specs}) != len(specs) or not specs or
            any(not isinstance(s["id"], str) or re.fullmatch(r"game-[0-9]{6}", s["id"]) is None or
                s["a"] not in bindings or s["b"] not in bindings for s in specs)):
        raise ArenaError("empty/duplicate job plan")
    cli, engine, dependencies = _dependencies()
    output_path = _path(output)
    if any(_path(record["path"]).is_relative_to(output_path) for binding in bindings.values()
           for record in (binding.raw, binding.injected, *binding.source_files)):
        raise ArenaError("native output must not contain its source inputs")
    out = _fresh_out(output)
    (out / "inputs").mkdir()
    (out / "jobs").mkdir()
    (out / "batches").mkdir()
    originals = dependencies + [r for b in bindings.values() for r in (b.raw, b.injected, *b.source_files)]
    originals += [provenance[key] for key in ("executables", "panel") if key in provenance]
    owned, portable, packages = [], {}, []
    rows, failures, seen = [], [], set()
    primary = None
    record = {"schema": "arena-compiled-run/v1", "scope": "fresh compiled-payload execution and existing health; no admission",
              "engine": engine, "dependencies": dependencies, "bindings": [b.record() for b in bindings.values()],
              "selected_role": role, "settings": {"jobs_requested": jobs, "match_timeout": timeout, "release": True, "round_limit": None},
              "provenance": provenance, "planned_jobs": specs,
              "coverage": {"status": "unknown", "reason": "No native contribution observer executed."},
              "portable_manifest": PORTABLE,
              "limits": ["The recovered f6 portable proof used equal raw/injected bytes; selected-role support for unequal modules is not inferred from that experiment.",
                         "Captured/copied byte hashes and outputs do not attest internal memory/JIT transformations.",
                         "Source/cache owners must not mutate inputs concurrently; path preflight and rehashes are not filesystem locks.",
                         "Windows Job containment starts before resume; abrupt launcher death before assignment can strand an unresumed process.",
                         "Official frontend verifies numeric outcomes1=A and2=B; exact terminal draw enum is unproved and fails closed."]}
    _write(out / "run.json", record)
    try:
        for key, binding in bindings.items():
            path = out / "inputs" / (key.replace("/", "-").replace("@", "-") + ".zip")
            packages.append(_pack(path, binding))
            owned.append(_desc(path))
            portable[key] = path
        _write(out / "inputs/portable-members.json", packages)
        owned.append(_desc(out / "inputs/portable-members.json"))
        maps = {}
        for spec in specs:
            source = Path(spec["map_file"])
            original = _desc(source)
            if original["sha256"] != spec["map_sha256"]:
                raise ArenaError("planned map changed")
            originals.append(original)
            if spec["map_sha256"] not in maps:
                path = out / "inputs" / (spec["map_sha256"] + ".map26a")
                data = source.read_bytes()
                if hashlib.sha256(data).hexdigest() != original["sha256"]:
                    raise ArenaError("map changed during capture")
                with path.open("xb") as f:
                    f.write(data)
                maps[spec["map_sha256"]] = path
                owned.append(_desc(path))
        for index in range(0, len(specs), MANIFEST_CHUNK):
            chunk = specs[index:index + MANIFEST_CHUNK]
            batchdir = out / "batches" / f"{index // MANIFEST_CHUNK:04d}"
            batchdir.mkdir()
            planned = {}
            manifest = batchdir / "input.jsonl"
            with manifest.open("x", encoding="utf-8", newline="\n") as f:
                for spec in chunk:
                    directory = out / "jobs" / spec["id"]
                    directory.mkdir()
                    planned[spec["id"]] = directory / "output.zip"
                    job = {"apiVersion": 1, "id": spec["id"],
                           "teamA": {"path": str(portable[spec["a"]]), "language": "cpp", "name": "TeamA"},
                           "teamB": {"path": str(portable[spec["b"]]), "language": "cpp", "name": "TeamB"},
                           "map": str(maps[spec["map_sha256"]]), "replay": str(planned[spec["id"]])}
                    f.write(json.dumps(job, separators=(",", ":")) + "\n")
            owned.append(_desc(manifest))
            _check(originals + owned)
            process = None
            try:
                def arguments(count):
                    _check(originals + owned)
                    return [str(cli), "run-batch", "--jsonl", str(manifest), "--jobs", str(count),
                            "--release", "--color", "never", "--timeout", str(timeout)]
                process = _slotted_command(arguments, out, batchdir, lambda count: timeout * math.ceil(len(chunk) / count) + 45, jobs)
            except BaseException as error:
                primary = error
            if getattr(primary, "compiled_live_pid", None) is not None:
                raise primary  # retain raw prefix; never launch a converter beside unconfirmed owned workers
            protocol = _events(batchdir / "stdout.bin", planned)
            _write(batchdir / "machine-validation.json", protocol)
            batch_ok = (process is not None and process["returncode"] == 0 and not process["timed_out"] and
                        process.get("tree_exit_confirmed") is True and not process.get("forced_termination") and
                        not protocol["errors"] and protocol["batch"] is not None and protocol["batch"]["failed"] == 0)
            for spec in chunk:
                ident = spec["id"]
                if ident in seen:
                    raise ArenaError("internal duplicate row publication")
                state = protocol["jobs"][ident]
                row = {"id": ident, "a": spec["a"], "b": spec["b"], "map": spec["map"], "engine": games.ENGINE,
                       "tags": spec.get("tags", {}), "winner": "error", "rounds": None, "A": None, "B": None,
                       "measurement_complete": False, "execution_complete": False, "health": None}
                try:
                    if state["terminal"] != "finished" or state["errors"]:
                        raise ArenaError(f"native job did not complete: {state}")
                    row.update(_consume(spec, out / "jobs" / ident, cli, engine))
                    row["execution_complete"] = batch_ok
                except Exception as error:
                    if getattr(error, "compiled_live_pid", None) is not None:
                        raise
                    row["error"] = str(error)
                rows.append(row)
                seen.add(ident)
                with (out / "games.jsonl").open("a", encoding="utf-8", newline="\n") as f:
                    f.write(json.dumps(row, sort_keys=True, allow_nan=False) + "\n")
                    f.flush()
                    os.fsync(f.fileno())
            if not batch_ok or any(not row["execution_complete"] for row in rows[-len(chunk):]):
                failures.append({"batch": index // MANIFEST_CHUNK, "process": process, "protocol": protocol["errors"]})
                break
            _check(originals + owned)
        if primary is not None:
            raise primary
    except BaseException as error:
        primary = error
    finally:
        try:
            drift = None
            try:
                _check(originals + owned)
            except Exception as error:
                drift = str(error)
            execution = (primary is None and not failures and drift is None and len(rows) == len(specs) and
                         all(row["execution_complete"] for row in rows))
            result = {**record, "rows": rows, "expected_games": len(specs), "recorded_games": len(rows),
                      "execution_pass": execution,
                      "health_pass": execution and all(not any(row["health"].values()) for row in rows),
                      "failures": failures, "exception": repr(primary) if primary else None, "input_drift": drift,
                      "input_files": owned, "original_files": originals, "retained_files": _inventory(out),
                      "admitted": False, "stopped": not bool(getattr(primary, "compiled_live_pid", None)),
                      "jobs": bool(getattr(primary, "compiled_live_pid", None)),
                      "unconfirmed_process_pid": getattr(primary, "compiled_live_pid", None)}
            _write(out / "summary.json", result)
        except BaseException as final_error:
            if primary is not None:
                primary.add_note(f"Final evidence publication also failed: {final_error!r}; raw files retained at {out}")
            else:
                raise
    if primary is not None:
        raise primary
    return result


def _map_file(value):
    path = _path(value)
    _excluded_paths(path.as_posix())
    _regular(path)
    if path.suffix != ".map26a":
        raise ArenaError("an existing .map26a file is required")
    return path


def play(a_snapshot: str, b_snapshot: str, map_file, out, *, role="raw", jobs=1, timeout=120,
         snapshot_roots=None, snapshot_owner=None):
    """One controlled full match, existing snapshots only; no rebuild or resume."""
    _key(a_snapshot)
    _key(b_snapshot)
    if role not in ("raw", "injected"):
        raise ArenaError("payload role must be raw or injected")
    keys = tuple(dict.fromkeys((a_snapshot, b_snapshot)))
    if snapshot_roots is None and snapshot_owner is not None:
        raise ArenaError("native snapshot owner requires explicit roots")
    if snapshot_roots is not None and (not isinstance(snapshot_roots, dict) or set(snapshot_roots) != set(keys)
                                      or not all(isinstance(root, (str, Path)) for root in snapshot_roots.values())):
        raise ArenaError("native replay requires one explicit root per snapshot key")
    options = lambda key: ({"snapshot_root": snapshot_roots[key], "snapshot_owner": snapshot_owner}
                           if snapshot_roots is not None else {})
    for key in keys:
        _preflight_snapshot(key, **options(key))
    bindings = {key: _capture(key, role, **options(key)) for key in keys}
    path = _map_file(map_file)
    specs = [{"id": "game-000000", "a": a_snapshot, "b": b_snapshot, "map": path.stem,
              "map_file": str(path), "map_sha256": _digest(path), "tags": {}}]
    return _run(specs, bindings, out, role, jobs, timeout, {"kind": "controlled-full-match"})


def regress(subject_snapshot: str, executables, out, *, role="raw", jobs=1, timeout=120):
    """Old panel maps/both sides through pinned byte bindings; coverage stays unknown."""
    _key(subject_snapshot)
    manifest_path = _path(executables)
    raw_manifest, manifest_pin = _frozen_json(manifest_path)
    _excluded_paths(raw_manifest)
    _preflight_descriptors(raw_manifest)
    panel_path = panel_executables._record_path(raw_manifest["panel"], "panel")
    panel, panel_pin = _frozen_json(panel_path)
    _excluded_paths(panel)
    _preflight_descriptors(panel)
    for binding in raw_manifest["snapshots"]:
        _key(binding["snapshot"])
    for entry in panel["entries"]:
        _key(entry["snapshot"])
        if isinstance(entry.get("identity"), dict) and "snapshot" in entry["identity"]:
            _key(entry["identity"]["snapshot"])
        for key in (entry["bot"], entry.get("contract", {}).get("bot")):
            if not isinstance(key, str) or key.split("/", 1)[0].casefold() in EXCLUDED:
                raise ArenaError("panel has an excluded/malformed bot identity")
    if raw_manifest["panel"] != {key: panel_pin[key] for key in ("path", "sha256")}:
        raise ArenaError("captured panel bytes disagree with executable manifest")
    for key in dict.fromkeys([subject_snapshot] + [row["snapshot"] for row in raw_manifest["snapshots"]] +
                             [row["snapshot"] for row in panel["entries"]]):
        _preflight_snapshot(key)
    for entry in panel["entries"]:
        _preflight_measurements(entry["contract"])
    manifest = panel_executables.validate(manifest_path)
    if manifest != raw_manifest or _desc(manifest_path) != manifest_pin or _desc(panel_path) != panel_pin:
        raise ArenaError("manifest/panel changed during existing validation")
    if role not in ("raw", "injected"):
        raise ArenaError("payload role must be raw or injected")
    expected = {entry["snapshot"]: entry for entry in manifest["snapshots"]}
    bindings = {subject_snapshot: _capture(subject_snapshot, role)}
    bindings.update({key: _capture(key, role, binding) for key, binding in expected.items() if key not in bindings})
    if subject_snapshot in expected:
        for label in ("raw", "injected"):
            if bindings[subject_snapshot].record()[label]["sha256"] != expected[subject_snapshot][label]["sha256"]:
                raise ArenaError("subject/opponent shared snapshot differs from panel binding")
    specs = []
    for entry in panel["entries"]:
        for name, expected_sha in entry["maps"].items():
            path = core.map_path(name)
            if _digest(path) != expected_sha:
                raise ArenaError("old panel map changed")
            for side in entry["sides"]:
                a, b = ((subject_snapshot, entry["snapshot"]) if side == "A" else (entry["snapshot"], subject_snapshot))
                specs.append({"id": f"game-{len(specs):06d}", "a": a, "b": b, "map": name,
                              "map_file": str(path), "map_sha256": expected_sha,
                              "tags": {"subject": side, "population_behavior": entry["behavior"],
                                       "population_side": "B" if side == "A" else "A"}})
    return _run(specs, bindings, out, role, jobs, timeout,
                {"kind": "experimental-panel-regression", "executables": manifest_pin, "panel": panel_pin})


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("play", help="one full compiled-input match, existing snapshots only")
    p.add_argument("a_snapshot")
    p.add_argument("b_snapshot")
    p.add_argument("--map", required=True, dest="map_file")
    p = sub.add_parser("regress", help="old panel maps/both sides; native contribution coverage remains unknown")
    p.add_argument("subject_snapshot")
    p.add_argument("--executables", required=True)
    for p in sub.choices.values():
        p.add_argument("--out", required=True)
        p.add_argument("--role", choices=("raw", "injected"), default="raw")
        p.add_argument("--jobs", type=int, default=1)
        p.add_argument("--timeout", type=int, default=120)
    args = vars(parser.parse_args(argv))
    command = args.pop("command")
    try:
        result = play(**args) if command == "play" else regress(**args)
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        if _PENDING_CLEANUP:
            print("Owned process cleanup is unconfirmed; handles and slots remain held.", file=sys.stderr)
            while not confirm_pending_cleanup():
                time.sleep(1)
        return 1
    print(json.dumps({key: result[key] for key in ("execution_pass", "health_pass", "expected_games", "recorded_games", "coverage")}))
    return 0 if result["execution_pass"] and result["health_pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
