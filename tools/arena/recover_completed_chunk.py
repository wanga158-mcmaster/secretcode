"""Owner-invoked adapter for an already completed, terminal fastrun chunk.

Writes a NEW observer progress envelope outside the experiment. Does not alter
spec/raw/listing/canonical rows, resume, rebuild or play. The owner must first
confirm the original transport process is terminal and its Job empty.
"""
from pathlib import Path
import argparse
import hashlib
import json
import sys

ROOT = Path("D:/files/battlecode")
sys.path.insert(0, str(ROOT / "tools/arena"))
import exp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("name")
    parser.add_argument("--rows", required=True)
    parser.add_argument("--games", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    directory = exp.exp_dir(args.name)
    with exp.operation_lock(directory):
        directory, spec = exp.load(args.name)
        if spec.get("status") != "open" or spec.get("preparation", {}).get("state") != "complete":
            raise exp.ArenaError("recovery requires the original prepared open experiment")
        paths = [Path(value).absolute() for value in (args.rows, args.games)]
        out = Path(args.out).absolute()
        owner_root = directory.parent.parent
        if any(path.resolve() != path or not path.is_relative_to(directory) or not path.is_file() for path in paths):
            raise exp.ArenaError("completed chunk files must be direct files in this experiment")
        if out.resolve() != out or not out.is_relative_to(owner_root) or out.is_relative_to(directory) or out.exists():
            raise exp.ArenaError("observer output must be a NEW owner-cache file outside the experiment")
        inputs = [*paths, directory / "spec.json"]
        digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
        before = {str(path): digest(path) for path in inputs}
        gaps = exp.provenance_gaps(spec)
        if gaps:
            raise exp.ArenaError("cannot import changed inputs: " + "; ".join(gaps))
        canonical, keys = exp.game_plan(spec)
        part = paths[1].read_text(encoding="utf-8").splitlines()
        if len(part) != len(set(part)) or not set(part).issubset(set(canonical)):
            raise exp.ArenaError("completed game listing is not an exact subset of the recorded plan")
        rows = exp.normalized_chunk(spec, part, exp.read_rows(paths[0]))
        validated = exp.validate_progress_rows(rows, set(keys), spec=spec)
        if before != {str(path): digest(path) for path in inputs} or exp.provenance_gaps(spec):
            raise exp.ArenaError("source chunk or pinned inputs changed during import")
        exp.write_document(out, {"schema": "arena-experiment-progress/v1",
                                "fingerprint": exp.progress_fingerprint(spec),
                                "rows": [validated[key] for key in keys if key in validated]})
        print(json.dumps({"observer_progress": str(out), "games_preserved": len(validated),
                          "recorded_games": len(keys), "inputs_unchanged": True, "new_games": 0}))


if __name__ == "__main__":
    main()
