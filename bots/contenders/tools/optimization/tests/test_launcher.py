"""Windows launcher integration: real executable, checkpoints and isolated Ctrl+C."""
from __future__ import annotations

import ctypes
import datetime
from ctypes import wintypes
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

HERE = Path(__file__).resolve().parent
ENGINE = HERE.parent
REPO = HERE.parents[4]
SPEC = importlib.util.spec_from_file_location("optimizer_packet", ENGINE / "packet.py")
packet = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(packet)


def fixture() -> None:
    if sys.argv[1] == "--fixture-grandchild":
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        if hasattr(signal, "SIGBREAK"):
            signal.signal(signal.SIGBREAK, signal.SIG_IGN)
        Path(sys.argv[2]).write_text(json.dumps({'pid': os.getpid(), 'ignores_interrupt': True}), encoding='utf-8')
        time.sleep(45)
        return
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--request", required=True)
    parser.add_argument("--response", type=Path, required=True)
    parser.add_argument("--repo", required=True)
    args = parser.parse_args(sys.argv[2:])
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    if hasattr(signal, "SIGBREAK"):
        signal.signal(signal.SIGBREAK, signal.SIG_IGN)
    args.response.parent.mkdir(parents=True, exist_ok=True)
    ready = args.response.parent / 'launcher-fixture-grandchild-ready.json'
    child = subprocess.Popen([sys.executable, "-B", "-I", str(Path(__file__).resolve()), "--fixture-grandchild", str(ready)])
    deadline = time.monotonic() + 5
    while not ready.exists() and child.poll() is None and time.monotonic() < deadline:
        time.sleep(0.02)
    if not ready.exists():
        child.terminate()
        child.wait(timeout=5)
        raise RuntimeError('fixture grandchild did not install its interrupt handlers')
    (args.response.parent / "launcher-fixture-pids.json").write_text(json.dumps({"evaluator": os.getpid(), "grandchild": child.pid}), encoding="utf-8")
    try:
        time.sleep(45)
        args.response.write_text('{"healthy":true,"objective":0}', encoding="utf-8")
    finally:
        child.terminate()
        child.wait(timeout=5)


def read(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def plain_environment():
    env = os.environ.copy()
    system = Path(os.environ["SystemRoot"])
    env["PATH"] = os.pathsep.join(map(str, (system / "System32", system, system / "System32/WindowsPowerShell/v1.0")))
    return env


if os.name == "nt":
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateJobObjectW.restype = wintypes.HANDLE
    kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.AttachConsole.argtypes = [wintypes.DWORD]
    kernel.GenerateConsoleCtrlEvent.argtypes = [wintypes.DWORD, wintypes.DWORD]
    kernel.SetConsoleCtrlHandler.argtypes = [ctypes.c_void_p, wintypes.BOOL]

    class BasicLimit(ctypes.Structure):
        _fields_ = [("process_time", ctypes.c_longlong), ("job_time", ctypes.c_longlong),
                    ("flags", wintypes.DWORD), ("min_ws", ctypes.c_size_t), ("max_ws", ctypes.c_size_t),
                    ("active", wintypes.DWORD), ("affinity", ctypes.c_size_t),
                    ("priority", wintypes.DWORD), ("scheduling", wintypes.DWORD)]

    class JobLimit(ctypes.Structure):
        _fields_ = [("basic", BasicLimit), ("io", ctypes.c_ulonglong * 6),
                    ("process_memory", ctypes.c_size_t), ("job_memory", ctypes.c_size_t),
                    ("peak_process_memory", ctypes.c_size_t), ("peak_job_memory", ctypes.c_size_t)]


def alive(pid: int) -> bool:
    handle = kernel.OpenProcess(0x00100000, False, pid)
    if not handle:
        return False
    try:
        return kernel.WaitForSingleObject(handle, 0) == 0x102
    finally:
        kernel.CloseHandle(handle)


class HiddenConsole:
    """A separate hidden console/job; control events cannot reach the user's app."""
    def __init__(self, command: str, cwd: Path, log: Path):
        environment = plain_environment()
        self.output = log.open("wb")
        startup = subprocess.STARTUPINFO()
        startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        self.process = subprocess.Popen(command, cwd=cwd, env=environment, stdin=subprocess.DEVNULL,
            stdout=self.output, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup)
        self.job = kernel.CreateJobObjectW(None, None)
        limits = JobLimit()
        limits.basic.flags = 0x2000  # kill only this test's descendants when its job closes
        if not self.job or not kernel.SetInformationJobObject(self.job, 9, ctypes.byref(limits), ctypes.sizeof(limits)) or not kernel.AssignProcessToJobObject(self.job, wintypes.HANDLE(int(self.process._handle))):
            self.process.kill()
            self.process.wait()
            if self.job:
                kernel.CloseHandle(self.job)
            self.output.close()
            raise ctypes.WinError(ctypes.get_last_error())

    def ctrl_c(self):
        kernel.FreeConsole()
        if not kernel.AttachConsole(self.process.pid):
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            if not kernel.SetConsoleCtrlHandler(None, True) or not kernel.GenerateConsoleCtrlEvent(0, 0):
                raise ctypes.WinError(ctypes.get_last_error())
            time.sleep(0.15)
        finally:
            kernel.FreeConsole()
            kernel.SetConsoleCtrlHandler(None, False)

    def close(self):
        kernel.CloseHandle(self.job)
        if self.process.poll() is None:
            self.process.wait(timeout=5)
        self.output.close()


@unittest.skipUnless(os.name == "nt", "Windows launcher integration")
class LauncherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ENGINE / "bin/optimizer.exe").is_file():
            raise unittest.SkipTest("build the optimizer first")

    def setUp(self):
        self.scratch = REPO / ".cache/contenders/optimization/launcher-tests"
        self.scratch.mkdir(parents=True, exist_ok=True)
        self.temp = Path(tempfile.mkdtemp(prefix="case-", dir=self.scratch))
        self.root = self.temp / "packet with spaces"
        packet.export_packet(REPO, self.root, "model")
        binary_override = os.environ.get("OPTIMIZER_TEST_BINARY")
        if binary_override:
            target = self.root / "bots/contenders/tools/optimization/bin/optimizer.exe"
            shutil.copy2(binary_override, target)
            manifest = read(self.root / packet.MANIFEST)
            entry = next(item for item in manifest["files"] if item["path"] == manifest["optimizer"])
            old_size = entry["size"]
            entry.update(size=target.stat().st_size, sha256=packet.digest(target))
            manifest["payload_bytes"] += entry["size"] - old_size
            manifest["payload_identity"] = hashlib.sha256(packet.canonical(manifest["files"])).hexdigest()
            (self.root / packet.MANIFEST).write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        self.launcher = self.root / "bots/contenders/tools/optimization/run.ps1"
        self.cwd = self.temp / "unrelated cwd"
        self.cwd.mkdir()

    def tearDown(self):
        self.assertTrue(self.temp.resolve().is_relative_to(self.scratch.resolve()))
        # Retain small failures for diagnosis; successful generated packets are disposable.
        result = self._outcome.result
        failed = any(test is self for test, _ in result.failures + result.errors)
        if not failed:
            proof = self.scratch.parent / 'launcher-audit/proofs' / self.temp.name
            proof.mkdir(parents=True, exist_ok=True)
            artifacts = []
            selected_names = {'run-status.json', 'run.json', 'checkpoint.json', 'report.json', 'launcher-fixture-pids.json', 'launcher-fixture-grandchild-ready.json'}
            files = [path for path in self.temp.rglob('*') if path.is_file() and (path.name in selected_names or path.parent == self.temp and path.suffix == '.log')]
            for index, source in enumerate(sorted(files)):
                target = proof / f'artifact-{index:03d}{source.suffix}'
                shutil.copy2(source, target)
                artifacts.append({'source_relative': source.relative_to(self.temp).as_posix(), 'path': target.name,
                                  'sha256': packet.digest(target), 'size': target.stat().st_size})
            summary = {'schema': 'contenders-launcher-proof/v1', 'test': self._testMethodName, 'passed': True,
                'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                'binary_sha256': packet.digest(self.root / 'bots/contenders/tools/optimization/bin/optimizer.exe'),
                'utility_audit_binary': bool(os.environ.get('OPTIMIZER_TEST_BINARY')), 'artifacts': artifacts,
                'source_sha256': {name: packet.digest(ENGINE / name) for name in ('run.ps1', 'run.cmd', 'packet.py', 'tests/test_launcher.py')}}
            (proof / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
            shutil.rmtree(self.temp)

    def run_launcher(self, *args, launcher=None, timeout=20):
        powershell = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
        result = subprocess.run([str(powershell), "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(launcher or self.launcher), *map(str, args)],
            cwd=self.cwd, env=plain_environment(), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout, text=True)
        self.assertEqual(result.returncode, 0, result.stdout)
        return result

    def test_default_output_resumes_from_unrelated_cwd(self):
        args = ("c4", "-Input", "examples/c4-smoke.json", "-Hours", "0", "-Seed", "73")
        self.run_launcher(*args, "-Iterations", "1")
        output = self.root / "results/contenders/optimization/campaigns/c4"
        identity = read(output / "run.json")["identity"]
        self.run_launcher(*args, "-Iterations", "2", "-Resume")
        self.assertEqual(read(output / "checkpoint.json")["iteration"], 2)
        self.assertEqual(read(output / "run.json")["identity"], identity)

    def test_worker_uses_shipped_profile_without_portfolio_argument(self):
        output = self.temp / "worker c output"
        self.run_launcher("-Worker", "C", "-Out", output, "-Hours", "0", "-Iterations", "1")
        run = read(output / "run.json")
        self.assertEqual(run["engine"], "c4-atlas")
        self.assertEqual(run["seed"], 303)

    def test_model_packet_checkpoint_resumes_after_packet_move(self):
        args = ("c4", "-Input", "examples/c4-smoke.json", "-Hours", "0", "-Seed", "73")
        self.run_launcher(*args, "-Iterations", "1")
        old_root = self.root
        self.root = self.temp / "relocated packet with spaces"
        old_root.rename(self.root)
        self.assertFalse(old_root.exists())
        self.launcher = self.root / "bots/contenders/tools/optimization/run.ps1"
        self.run_launcher(*args, "-Iterations", "2", "-Resume")
        checkpoint = self.root / "results/contenders/optimization/campaigns/c4/checkpoint.json"
        self.assertEqual(read(checkpoint)["iteration"], 2)

    def test_repository_checkpoint_resumes_in_model_packet(self):
        original = self.temp / "repository output"
        args = ("c4", "-Input", "examples/c4-smoke.json", "-Hours", "0", "-Seed", "73")
        self.run_launcher(*args, "-Out", original, "-Iterations", "1", launcher=ENGINE / "run.ps1")
        moved = self.root / ".cache/moved output"
        shutil.copytree(original, moved)
        self.run_launcher(*args, "-Out", moved, "-Iterations", "2", "-Resume")
        self.assertEqual(read(moved / "checkpoint.json")["iteration"], 2)
        self.assertEqual(read(original / "run.json")["identity"], read(moved / "run.json")["identity"])

    def fixture_process(self, name, hours):
        request = self.temp / (name + ".json")
        request.write_text('{"schema":"contenders-eval/v1","kind":"config","candidate":{}}', encoding="utf-8")
        output = self.root / ".cache" / name
        args = [str(self.root / "bots/contenders/tools/optimization/run.cmd"), "evaluate", "-Input", str(request), "-Out", str(output),
            "-Hours", str(hours), "-Iterations", "1", "--evaluator", sys.executable, "--evaluator-arg", str(Path(__file__).resolve()), "--evaluator-arg", "--fixture-evaluator"]
        cmd = Path(os.environ["SystemRoot"]) / "System32/cmd.exe"
        # This is a fixed test command in a private temp directory, deliberately
        # including spaces so the actual .cmd -> PowerShell -> C++ chain is tested.
        command = f'"{cmd}" /d /s /c "{subprocess.list2cmdline(args)}"'
        log = self.temp / (name + ".log")
        process = HiddenConsole(command, self.cwd, log)
        return output, log, process

    def wait_fixture_started(self, output, log, process):
        deadline = time.monotonic() + 12
        marker = None
        while time.monotonic() < deadline and process.process.poll() is None:
            markers = list(output.rglob("launcher-fixture-pids.json")) if output.exists() else []
            if markers:
                marker = markers[0]
                break
            time.sleep(0.05)
        self.assertIsNotNone(marker, log.read_text(encoding="utf-8", errors="replace"))
        ready = read(marker.parent / 'launcher-fixture-grandchild-ready.json')
        pids = read(marker)
        self.assertTrue(ready['ignores_interrupt'])
        self.assertEqual(ready['pid'], pids['grandchild'])
        return pids

    def assert_fixture_stopped(self, pids):
        deadline = time.monotonic() + 3
        while any(alive(pid) for pid in pids.values()) and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertFalse(any(alive(pid) for pid in pids.values()), "evaluator descendant survived stop")

    def test_ctrl_c_cleans_ignored_evaluator_descendants_and_reports(self):
        output, log, process = self.fixture_process("interrupt output", 0)
        try:
            pids = self.wait_fixture_started(output, log, process)
            process.ctrl_c()
            process.process.wait(timeout=10)
            self.assert_fixture_stopped(pids)
            self.assertTrue((output / "report.json").is_file(), "interrupted first evaluation needs a readable report; " + str(log))
            report = read(output / "report.json")
            self.assertTrue(report.get("interrupted") or "interrupt" in report.get("status", "").lower(), report)
            status = read(output / "run-status.json")
            self.assertEqual(status["status"], "interrupted_checkpointed")
            self.assertFalse(status["checkpoint_available"], "first uncommitted evaluation must not create a state checkpoint")
            self.assertEqual(status["committed_iteration"], 0)
        finally:
            process.close()

    def test_hours_deadline_cleans_descendants_and_reports_time_limit(self):
        output, log, process = self.fixture_process("time limit output", 0.0005)
        try:
            pids = self.wait_fixture_started(output, log, process)
            self.assertEqual(process.process.wait(timeout=8), 0, log.read_text(encoding="utf-8", errors="replace"))
            self.assert_fixture_stopped(pids)
            status = read(output / "run-status.json")
            self.assertEqual(status["status"], "time_limit_checkpointed")
            self.assertEqual(status["committed_iteration"], 0)
            self.assertFalse(status["checkpoint_available"])
            self.assertEqual(read(output / "report.json")["status"], "time_limit_checkpointed")
        finally:
            process.close()


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1].startswith("--fixture-"):
        fixture()
    else:
        unittest.main(verbosity=2)
