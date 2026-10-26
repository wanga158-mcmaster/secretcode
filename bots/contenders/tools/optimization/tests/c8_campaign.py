"""Reproduce a substantive C8 full/split campaign and verify exact continuation."""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--optimizer', type=Path, required=True)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--iterations', type=int, default=16)
    parser.add_argument('--split', type=int, default=5)
    args = parser.parse_args()
    assert 0 < args.split < args.iterations
    repo, executable, output = args.repo.resolve(), args.optimizer.resolve(), args.out.resolve()
    source = args.input.resolve()
    output.mkdir(parents=True, exist_ok=True)

    def invoke(name: str, cap: int, resume: bool = False) -> None:
        destination = output / name
        log = output / f'{name}-{cap}.log'
        command = [str(executable), 'c8', '--repo', str(repo), '--input', str(source),
                   '--out', str(destination), '--iterations', str(cap), '--seconds', '300']
        if resume:
            command.append('--resume')
        with log.open('w', encoding='utf-8') as stream:
            subprocess.run(command, cwd=repo, stdout=stream, stderr=subprocess.STDOUT, check=True)
        print(f'C8 {name} completed cumulative cap {cap}', flush=True)

    def split() -> None:
        invoke('resumed', args.split)
        checkpoint = json.loads((output / 'resumed/checkpoint.json').read_text())
        assert checkpoint['iteration'] == args.split
        assert checkpoint['state'].get('pending') is None
        invoke('resumed', args.iterations, True)

    with ThreadPoolExecutor(max_workers=2) as workers:
        tasks = [workers.submit(invoke, 'complete', args.iterations), workers.submit(split)]
        for task in tasks:
            task.result()

    def read(directory: str, name: str):
        return json.loads((output / directory / name).read_text(encoding='utf-8'))

    whole, resumed = read('complete', 'checkpoint.json'), read('resumed', 'checkpoint.json')
    for field in ('identity', 'iteration', 'rng', 'state'):
        assert whole[field] == resumed[field], f'exact continuation differs in {field}'
    curriculum = read('complete', 'curriculum.json')
    assert curriculum == read('resumed', 'curriculum.json')
    assert read('complete', 'unresolved-cases.json') == read('resumed', 'unresolved-cases.json')
    summary = read('complete', 'curriculum-summary.json')
    assert summary['coverage']
    assert len(summary['coverage']) >= 8, 'readiness test must cover the broad family'
    assert sum(row['evaluated'] for row in summary['coverage'].values()) == args.iterations
    requests_checked = 0
    for entry in summary['native_requests']:
        request = read('complete', entry['request'])
        if request['supported']:
            assert len(request['actions']) == len(request['predicted_states'])
            assert request['qualification'] == 'request_only_not_native_validation'
            assert request['comparison_tolerance']['integer_and_boolean'] == 'exact'
            requests_checked += 1
    assert requests_checked > 0
    result = dict(status='passed', attempts=args.iterations, split_after=args.split,
                  exact_resume_fields=['identity', 'iteration', 'rng', 'state'],
                  curriculum_exact=True, unresolved_exact=True,
                  families=len(summary['coverage']), archive_cases=summary['archive_cases'],
                  native_request_projections_checked=requests_checked,
                  native_qualification=False)
    (output / 'verification.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result), flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
