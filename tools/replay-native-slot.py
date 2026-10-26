"""Contained read-only replay inspector; no game or server command.

The reviewed compiled runner owns one Slot and its private Windows Job. Raw
streams are forwarded unchanged only after the exact owned process tree is empty.
"""
from pathlib import Path
import os
import re
import sys
import uuid
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/arena"))
import core
import compiled_runner as shared

INSPECTOR = ROOT / "tools/inspect-strategy-replay.mjs"
RAW_ROOT = ROOT / ".cache/arena/replay-inspector"
WALL_TIMEOUT_SECONDS = 120
CHILD_SLOT_IMPORT = "--import=data:text/javascript,process.env.ARENA_REPLAY_SLOT%3D%271%27%3B"
_RECOVERY_LOCK = shared.__dict__.setdefault("_replay_inspector_cleanup_lock", threading.RLock())
SAFE_NODE_FLAGS = {"--no-warnings", "--trace-warnings", "--no-deprecation",
                   "--trace-deprecation", "--throw-deprecation", "--enable-source-maps",
                   "--experimental-wasi-unstable-preview1"}


def _same_path(left, right):
    return os.path.normcase(str(Path(left).resolve())) == os.path.normcase(str(Path(right).resolve()))


def _inspector_path(value):
    path = Path(value)
    return _same_path(path, INSPECTOR) and path.is_file()


def validate_argv(argv):
    """Keep exact argv; refuse evaluators, preloaders and unrelated executables."""
    argv = list(argv)
    if len(argv) < 2 or not Path(argv[0]).is_file() or not _same_path(argv[0], core.NODE):
        raise ValueError("Expected the configured arena Node executable")
    for index in range(1, len(argv)):
        value = argv[index]
        if _inspector_path(value):
            return argv
        if value == "--" and index + 1 < len(argv) and _inspector_path(argv[index + 1]):
            return argv
        if value not in SAFE_NODE_FLAGS and re.fullmatch(r"--disable-warning=[A-Za-z0-9_-]+", value) is None:
            raise ValueError("Expected the owned replay inspector after supported diagnostic Node flags")
    raise ValueError("Expected Node executable, inspector path and inspector arguments")


def recover_retained_cleanup():
    """Retain exact shared Job/process/Slot objects until confirmed empty."""
    with _RECOVERY_LOCK:
        while shared._PENDING_CLEANUP:
            try:
                if shared.confirm_pending_cleanup():
                    break
            except BaseException as error:
                try:
                    print("Owned replay inspector cleanup still unconfirmed: " + repr(error), file=sys.stderr, flush=True)
                except BaseException:
                    pass
            try:
                time.sleep(1)
            except BaseException:
                # Caught interruptions cannot discard the exact handles or Slot.
                continue


def _forward(directory, stdout, stderr):
    forwarded = set()
    for name, destination in [("stdout.bin", stdout), ("stderr.bin", stderr)]:
        path = directory / name
        if path.exists():
            with path.open("rb") as stream:
                while chunk := stream.read(64 * 1024):
                    remaining = memoryview(chunk)
                    while remaining:
                        written = destination.write(remaining)
                        if type(written) is not int or not 0 < written <= len(remaining):
                            raise OSError("Replay stream writer did not accept the output bytes")
                        remaining = remaining[written:]
            destination.flush()
            forwarded.add(name)
    return forwarded == {"stdout.bin", "stderr.bin"}


def run(argv, stdout=None, stderr=None):
    return _run_validated(validate_argv(argv), stdout, stderr)


def _child_argv(argv):
    # This fixed internal marker runs only in the child. The public ingress
    # still rejects caller-supplied --import/evaluator/preloader arguments.
    return [argv[0], CHILD_SLOT_IMPORT, *argv[1:]]


def _run_validated(argv, stdout=None, stderr=None):
    """Shared transport; the test-only native oracle supplies its own strict scope."""
    argv = list(argv)
    launched_argv = _child_argv(argv)
    stdout = sys.stdout.buffer if stdout is None else stdout
    stderr = sys.stderr.buffer if stderr is None else stderr
    RAW_ROOT.mkdir(parents=True, exist_ok=True)
    # Inherit the workspace ACL. tempfile's private Windows ACL denies the
    # sandboxed launcher access to its own dispatch and output files on this host.
    directory = RAW_ROOT / ("replay-" + uuid.uuid4().hex)
    directory.mkdir()
    record, primary = None, None
    retained = []
    try:
        # _slotted_command acquires the sole real Slot. Its finite wait starts
        # after acquisition; do not wrap it in another Slots context.
        record = shared._slotted_command(lambda count: launched_argv, ROOT, directory, WALL_TIMEOUT_SECONDS, jobs=1)
    except BaseException as error:
        primary = error
        record = getattr(error, "compiled_process_record", None)
    finally:
        # Shared recovery can touch any retained Job. Serialize only cleanup,
        # never launches or slot waits, across imported bridge instances.
        with _RECOVERY_LOCK:
            retained = [item for item in shared._PENDING_CLEANUP if item["directory"] == directory]
            recover_retained_cleanup()
    # The shared API releases Slot ownership only for a confirmed empty Job,
    # or when process creation failed without leaving an owned process alive.
    owned_process = getattr(primary, "compiled_process", None)
    cleanup_recovered = ((bool(retained) and all(item["process"].empty_confirmed for item in retained))
                         or (owned_process is not None and owned_process.empty_confirmed))
    confirmed = (record is None or record.get("tree_exit_confirmed") is True
                 or record.get("empty_confirmed") is True or cleanup_recovered)
    if not confirmed:
        if primary is None:
            primary = RuntimeError("Replay inspector process tree was not confirmed empty")
        else:
            primary.add_note("Replay process receipt did not establish confirmed cleanup")
    result = {
            "schema": "arena-replay-inspector-bridge/v1", "argv": argv,
            "launchedArgv": launched_argv,
            "timeout_seconds": WALL_TIMEOUT_SECONDS, "slot": 1,
            "process": record, "error": repr(primary) if primary else None,
            "jobs": not confirmed, "stopped": confirmed,
            "cleanupRecovered": cleanup_recovered,
            "streamsForwarded": False,
        }
    try:
        shared._write(directory / "bridge-result.json", result)
    except BaseException as error:
        if primary is None:
            primary = error
        else:
            primary.add_note("Bridge receipt publication failed: " + repr(error))
    try:
        if confirmed:
            result["streamsForwarded"] = _forward(directory, stdout, stderr)
            result["error"] = repr(primary) if primary is not None else None
            # Publication failure retains both streams just like any other
            # primary error. A reader must not infer forwarding from Job exit.
            updated = directory / "bridge-result.forwarded.tmp"
            shared._write(updated, result)
            os.replace(updated, directory / "bridge-result.json")
    except BaseException as error:
        if primary is None:
            raise
        primary.add_note("Replay stream forwarding failed: " + repr(error))
    if primary is not None:
        raise primary
    if (confirmed and isinstance(record, dict) and record.get("returncode") == 0
            and record.get("timed_out") is False and record.get("tree_exit_confirmed") is True
            and result["streamsForwarded"]):
        # Exact staging leaves only: never follow paths from a process record,
        # scan other invocations, or remove the retained compact metadata.
        for name in ("stdout.bin", "stderr.bin"):
            try:
                (directory / name).unlink(missing_ok=True)
            except OSError as error:
                # Cache maintenance must not fail an already delivered result.
                result["streamCleanupError"] = {"stream": name, "error": repr(error)}
                try:
                    updated = directory / "bridge-result.cleanup.tmp"
                    shared._write(updated, result)
                    os.replace(updated, directory / "bridge-result.json")
                except OSError:
                    pass  # The previously published process/forwarding facts remain.
                break
    return 124 if record["timed_out"] else record["returncode"]


def main():
    return run(sys.argv[1:])


if __name__ == "__main__":
    raise SystemExit(main())
