"""C3 deterministic restart and actual current-map / C4-frontier integration smoke.

Run with the compiled optimization executable; no game-server or native-game claim.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[5])
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    repo, out, exe = args.repo.resolve(), args.out.resolve(), args.exe.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if any(out.iterdir()):
        raise RuntimeError("campaign evidence directory must be new/empty")
    examples = repo / "bots/contenders/tools/optimization/examples"
    commands = []

    def run(name: str, config: str, iterations: int, resume: bool = False):
        command = [str(exe), "c3", "--repo", str(repo), "--input", str(examples/config), "--out", str(out/name), "--iterations", str(iterations)]
        if resume:
            command.append("--resume")
        commands.append(command)
        result = subprocess.run(command, cwd=repo, capture_output=True, text=True, check=False)
        with (out/(name+".log")).open("a", newline="\n") as stream:
            stream.write(result.stdout+result.stderr)
        if result.returncode:
            raise RuntimeError(f"{name} failed ({result.returncode}); see its log")
        return json.loads((out/name/"checkpoint.json").read_text())

    run("split", "c3-small.json", 31)
    split = run("split", "c3-small.json", 500, True)
    fresh = run("fresh", "c3-small.json", 500)
    assert all(split[key] == fresh[key] for key in ["iteration", "rng", "state"]), "restart changed search continuation"
    real = run("real-map", "c3-real-map.json", 100)
    mapped = run("atlas-mapped", "c3-real-map-atlas.json", 100)
    mapped_plan = json.loads((out/"atlas-mapped/best-plan.json").read_text())
    atlas_actions = [a for a in mapped_plan["actions"] if a.get("mode", "").startswith("atlas:")]
    assert atlas_actions, "actual atlas mode did not appear in the model-validated incumbent"
    assert mapped_plan["model_replay"]["valid"], "mapped incumbent failed model replay"
    for name in ["split", "fresh", "real-map", "atlas-mapped"]:
        plan = json.loads((out/name/"best-plan.json").read_text())
        assert plan["model_replay"]["valid"], f"{name} incumbent failed model replay"
    summary = {"schema":"contenders-c3-campaign/v1", "qualification":"restricted macro-model verification; no native game execution or strength claim", "restart_exact":{"keys":["iteration","rng","state"],"iteration":split["iteration"]}, "real_map_expanded":real["iteration"],"mapped_atlas_expanded":mapped["iteration"],"mapped_atlas_actions":len(atlas_actions),"commands":commands}
    (out/"campaign.json").write_text(json.dumps(summary,indent=2)+"\n",newline="\n")
    print(json.dumps({key:value for key,value in summary.items() if key!="commands"},indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
