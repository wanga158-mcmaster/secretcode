"""Exercise the real C2 loop using an explicit non-game evaluator fixture.

Usage: python c2_resume.py --executable PATH --out NEW_SCRATCH_DIRECTORY
"""
import argparse
import json
from pathlib import Path
import subprocess
import shutil
import sys


def native_projection(checkpoint):
    state = checkpoint["state"]
    entries = {}
    for identity, entry in state["entries"].items():
        entries[identity] = {key:value for key,value in entry.items() if key not in {"train","validation","qualification"}}
        entries[identity]["train"] = {n:{key:summary[key] for key in ("score","objective","healthy","cases")}
                                      for n,summary in entry["train"].items()}
    return {"iteration":checkpoint["iteration"],"rng":checkpoint["rng"],"best":state["best"],
            "pending":state["pending"],"entries":entries,"audits":state["audits"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--contextual", action="store_true")
    args = parser.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    fixture_repo = root / "repo"
    fixture_repo.mkdir()
    evaluator = Path(__file__).with_name("c2_fixture_evaluator.py").resolve()
    config = {
        "controller":"fixture-only", "evaluation":{"fixture_fail_once":True},
        "space":[{"name":"x","type":"integer","min":0,"max":7,"default":0},
                 {"name":"y","type":"integer","min":0,"max":4,"default":0},
                 {"name":"z","values":[10,20],"default":10,"active_if":{"x":[3,7]}}],
        "cases":{
            "train":{"maps":["train-a","train-b"],"opponents":["fixture"]},
            "validation":{"maps":["validation-a"],"opponents":["fixture"]},
            "qualification":{"maps":["qualification-a"],"opponents":["fixture"]}},
        "search":{"population":2,"elites":2,"stages":[2,4],"audit_every":2,
                  "validation_every":2,"qualification_on_finish":False}}
    if args.contextual:
        config["policy_space"]={"max_depth":2,"max_leaves":3,"features":[
            {"name":"width","thresholds":[48]}, {"name":"enemy_has_ryth","thresholds":[0.5]}]}
    input_file = root / "config.json"
    input_file.write_text(json.dumps(config), encoding="utf-8")
    command = [str(args.executable.resolve()),"c2","--input",str(input_file),"--repo",str(fixture_repo),
               "--seed","71","--evaluator",sys.executable,"--evaluator-arg",str(evaluator)]
    def launch(name, iterations, resume=False, okay=True):
        out = root/name
        invocation = command+["--out",str(out),"--iterations",str(iterations)]+(["--resume"] if resume else [])
        result = subprocess.run(invocation, capture_output=True, text=True)
        (root/(name+f"-{iterations}-{'resume' if resume else 'start'}.stdout")).write_text(result.stdout+result.stderr,encoding="utf-8")
        assert (result.returncode==0)==okay, (result.returncode,result.stdout,result.stderr)
        return out
    for name in ("uninterrupted","split"):
        (root/name).mkdir()
        # Run refuses nonempty new outputs. The first fixture invocation creates this
        # marker; precreating it is intentionally done after Run starts via first failure.
    # Each initial failure happens after C2 checkpoints its pending candidate/RNG.
    for name in ("uninterrupted","split"):
        launch(name,12 if name=="uninterrupted" else 5,okay=False)
        checkpoint=json.loads((root/name/"checkpoint.json").read_text())
        assert checkpoint["iteration"]==0 and checkpoint["state"]["pending"] is not None
    launch("uninterrupted",12,resume=True)
    launch("split",5,resume=True)
    launch("split",12,resume=True)
    a=json.loads((root/"uninterrupted/checkpoint.json").read_text())
    b=json.loads((root/"split/checkpoint.json").read_text())
    assert native_projection(a)==native_projection(b), "C2 resume changed sampled configurations, racing decisions or RNG"
    assert b["iteration"]==12 and b["state"]["pending"] is None
    best=b["state"]["best"]
    assert best and (root/"split/best/configuration.hpp").is_file()
    assert "qualification" not in b["state"]["entries"][best], "qualification was evaluated during selection"
    # Replaying a finished run reuses exact stored responses; no duplicate training evidence.
    before=len(list((root/"split/evaluations").glob("*.json")))
    launch("split",12,resume=True)
    assert before==len(list((root/"split/evaluations").glob("*.json")))
    # Incumbents can replace artifacts sharing a basename. Once preserved, those
    # files also remain usable without the original evaluator cache paths.
    relocated = root/"relocated"
    shutil.copytree(root/"split",relocated,ignore=shutil.ignore_patterns("work","fixture-artifacts"))
    (root/"split/fixture-artifacts").rename(root/"split/fixture-artifacts-unavailable")
    launch("relocated",12,resume=True)
    # A changed evaluator environment is caught even with unchanged config and cached
    # fitness, because the campaign queries its whole context before resuming.
    (fixture_repo/"fixture-context-version").write_text("v2",encoding="utf-8")
    launch("split",12,resume=True,okay=False)
    (fixture_repo/"fixture-context-version").write_text("v1",encoding="utf-8")
    # The executable runtime rejects a changed semantic input before any evaluation.
    config["space"][0]["max"]=8
    input_file.write_text(json.dumps(config),encoding="utf-8")
    launch("split",13,resume=True,okay=False)
    # A winning but unhealthy candidate never becomes the incumbent.
    config["space"]=[{"name":"x","values":[7],"default":7}]
    config["evaluation"]["fixture_fail_once"]=False
    input_file.write_text(json.dumps(config),encoding="utf-8")
    unhealthy=launch("unhealthy",1)
    rejected=json.loads((unhealthy/"checkpoint.json").read_text())["state"]
    assert rejected["best"]=="" and all(entry["rejected"] for entry in rejected["entries"].values())
    # Terminal duplicate plateaus do not consume fresh randomness when reopened.
    if not args.contextual:
        launch("unhealthy",5,resume=True)
        stopped=json.loads((unhealthy/"checkpoint.json").read_text())
        launch("unhealthy",6,resume=True)
        reopened=json.loads((unhealthy/"checkpoint.json").read_text())
        assert stopped["rng"]==reopened["rng"] and stopped["state"]==reopened["state"]
    # A finished campaign must verify raw evidence even when no evaluation is due.
    config["space"][0]["values"]=[0]
    config["space"][0]["default"]=0
    input_file.write_text(json.dumps(config),encoding="utf-8")
    corruption=launch("corrupt",1)
    result_file=next((corruption/"evaluations").glob("*.json"))
    result_file.write_text(result_file.read_text()+"\n",encoding="utf-8")
    launch("corrupt",1,resume=True,okay=False)
    print("C2 interruption, pending recovery, exact split/resume, cache reuse, changed input/context rejection, unhealthy-winner exclusion and evidence integrity passed")
    return 0


if __name__=="__main__":
    sys.exit(main())
