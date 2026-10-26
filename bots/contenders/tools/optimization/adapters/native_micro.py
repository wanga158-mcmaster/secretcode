"""Run the actual gameplay micro branch backend under the shared native slot pool."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--request', type=Path, required=True)
    p.add_argument('--response', type=Path, required=True)
    p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[5])
    p.add_argument('--node', type=Path)
    p.add_argument('--engine', type=Path)
    p.add_argument('--sampler', type=Path)
    p.add_argument('--profiles', type=Path)
    p.add_argument('--timeout', type=float, default=600)
    a = p.parse_args()
    repo = a.repo.resolve()
    sys.path.insert(0, str(repo / 'tools/arena'))
    from core import NODE, Slots
    candidates = [a.node, os.environ.get('OPT_NODE'), os.environ.get('ARENA_NODE'),
                  Path(__file__).resolve().parents[1] / 'runtime/node/node.exe', NODE,
                  shutil.which('node')]
    node = next((Path(v) for v in candidates if v and Path(v).is_file()), None)
    if node is None:
        raise RuntimeError('Node missing; use --node or include the native packet runtime')
    request = a.request.resolve()
    response = a.response.resolve()
    if request == response:
        raise RuntimeError('Request and response must be distinct files')
    request_hash = hashlib.sha256(request.read_bytes()).hexdigest()
    response.parent.mkdir(parents=True, exist_ok=True)
    # An old response must never be usable after a no-write or failed invocation.
    if response.exists():
        raise RuntimeError('Response already exists; use a fresh call directory')
    command = [str(node), str(Path(__file__).with_suffix('.mjs')), '--request', str(request),
               '--response', str(response), '--repo', str(repo)]
    for name in ('engine', 'sampler', 'profiles'):
        if getattr(a, name):
            command += ['--' + name, str(getattr(a, name).resolve())]
    with Slots(1, quiet=True), response.with_suffix('.stdout.log').open('wb') as out, response.with_suffix('.stderr.log').open('wb') as err:
        run = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=out, stderr=err,
                             timeout=a.timeout, cwd=repo)
    if not response.is_file():
        raise RuntimeError('Native backend did not write a response; inspect stderr log')
    body = json.loads(response.read_text(encoding='utf-8'))
    if body.get('schema') != 'contenders-micro/v1' or body.get('request_sha256') != request_hash:
        raise RuntimeError('Native response schema/request binding mismatch')
    if run.returncode:
        raise RuntimeError('Native backend failed: ' + str(body.get('reason', response.with_suffix('.stderr.log'))))
    print(json.dumps({'response': str(response), 'native': body.get('native'),
                      'results': len(body.get('results', []))}))
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print('native micro error: ' + str(error), file=sys.stderr)
        raise SystemExit(1)
