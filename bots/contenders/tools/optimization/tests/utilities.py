"""Exercise the real executable's replay, read-only status and offline verifier."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--runtime-helper", type=Path, required=True)
    parser.add_argument("--scratch", type=Path, required=True)
    args = parser.parse_args()
    executable = args.executable.resolve()
    args.scratch.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="utilities-", dir=args.scratch.resolve()))
    checks = 0

    def run(arguments, success=True):
        nonlocal checks
        process = subprocess.run([str(executable), *map(str, arguments)], capture_output=True, text=True, encoding="utf-8", timeout=30)
        assert (process.returncode == 0) == success, (arguments, process.returncode, process.stdout, process.stderr)
        checks += 1
        return process

    request = {"schema": "contenders-eval/v1", "kind": "good", "candidate": {"knobs": {"value": 7}}}
    input_path = root / "reproduce.json"
    write(input_path, request)
    output = root / "replay"
    evaluator = ["--evaluator", args.runtime_helper.resolve(), "--evaluator-arg", "--eval-helper"]
    run(["evaluate", "--input", input_path, "--out", output, *evaluator])
    assert json.loads((output / "request.json").read_text()) == request
    response = json.loads((output / "response.json").read_text())
    assert response["healthy"] and response["objective"] == 1
    assert json.loads((output / "report.json").read_text())["status"] == "reproduced_healthy"
    assert "response.json" in (output / "report.md").read_text()
    checks += 4
    before = {p.relative_to(output).as_posix(): p.read_bytes() for p in output.rglob("*") if p.is_file()}
    status = run(["status", "--out", output])
    assert "reproduced_healthy" in status.stdout
    assert before == {p.relative_to(output).as_posix(): p.read_bytes() for p in output.rglob("*") if p.is_file()}
    checks += 2
    wrapped = root / "wrapped.json"
    write(wrapped, {"request": request})
    run(["evaluate", "--input", wrapped, "--out", root / "wrapped", *evaluator])
    original = (output / "checkpoint.json").read_bytes()
    damaged = json.loads(original)
    damaged["iteration"] += 1
    write(output / "checkpoint.json", damaged)
    run(["status", "--out", output], success=False)
    (output / "checkpoint.json").write_bytes(original)

    slow = root / "slow.json"
    write(slow, {"schema": "contenders-eval/v1", "kind": "slow"})
    cancelled = root / "deadline"
    started = time.monotonic()
    run(["evaluate", "--input", slow, "--out", cancelled, "--seconds", "0.15", *evaluator])
    assert time.monotonic() - started < 2
    assert json.loads((cancelled / "report.json").read_text())["status"] == "time_limit_checkpointed"
    assert not (cancelled / "response.json").exists()
    checks += 3
    run(["status", "--out", cancelled])
    run(["evaluate", "--input", slow, "--out", root / "timeout", "--evaluation-timeout", "0.05", *evaluator], success=False)
    assert json.loads((root / "timeout" / "run-status.json").read_text())["status"] == "failed_checkpoint_preserved"
    checks += 1
    run(["evaluate", "--input", slow, "--out", cancelled, "--seconds", "5", "--resume", *evaluator])
    assert json.loads((cancelled / "report.json").read_text())["status"] == "reproduced_healthy"
    checks += 1

    packet = root / "packet with spaces"
    payload = packet / "payload" / "unicode-λ.txt"
    payload.parent.mkdir(parents=True)
    payload.write_text("portable verified payload λ\n", encoding="utf-8")
    files = [{"path": payload.relative_to(packet).as_posix(), "size": payload.stat().st_size,
              "sha256": hashlib.sha256(payload.read_bytes()).hexdigest()}]

    def manifest(entries=files):
        value = {"schema": "optimizer-packet/v1", "kind": "model", "files": entries,
                 "payload_bytes": sum(e["size"] for e in entries),
                 "payload_identity": hashlib.sha256(canonical(entries)).hexdigest()}
        write(packet / "packet-manifest.json", value)

    manifest()
    run(["verify-packet", "--repo", packet, "--strict"])
    (packet / "unlisted.txt").write_text("unexpected")
    run(["verify-packet", "--repo", packet])
    run(["verify-packet", "--repo", packet, "--strict"], success=False)
    (packet / "unlisted.txt").unlink()
    for name in (".cache/generated.txt", "results/output.txt"):
        target = packet / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text("writable output")
    run(["verify-packet", "--repo", packet, "--strict"])
    (packet / ".packet-incomplete").write_text("incomplete")
    run(["verify-packet", "--repo", packet], success=False)
    (packet / ".packet-incomplete").unlink()
    original = payload.read_bytes()
    payload.write_bytes(original + b"tampered")
    run(["verify-packet", "--repo", packet], success=False)
    payload.write_bytes(original)
    bad = [dict(files[0], path="../outside.txt")]
    manifest(bad)
    run(["verify-packet", "--repo", packet], success=False)
    manifest(files + files)
    run(["verify-packet", "--repo", packet], success=False)
    manifest()
    value = json.loads((packet / "packet-manifest.json").read_text())
    value["payload_identity"] = "0" * 64
    write(packet / "packet-manifest.json", value)
    run(["verify-packet", "--repo", packet], success=False)
    manifest()
    run(["verify-packet", "--repo", packet, "--strict"])
    print(json.dumps({"checks": checks, "scratch": str(root), "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest()}))


if __name__ == "__main__":
    main()
