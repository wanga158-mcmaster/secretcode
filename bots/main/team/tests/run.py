"""Build and run the SDK-independent joint protocol/planner tests.

Use the repository's configured native Python/C++ runtime under the owner's
contained, shared-slot launch wrapper. No games or SDK are needed. This small
runner does not create a second slot pool. The executable goes in the bot owner's
regenerable cache, not beside sources.
"""
from pathlib import Path
import os
import shutil
import subprocess


def main():
    root = Path(__file__).resolve().parents[4]
    cache = root / ".cache" / "main" / "med" / "team-tests"
    cache.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).with_name("joint_model.cpp")
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("Set CXX to the configured native clang++ or g++ compiler")
    output = cache / ("joint-model.exe" if os.name == "nt" else "joint-model")
    subprocess.run([compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-pedantic", str(source), "-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)


if __name__ == "__main__":
    main()
