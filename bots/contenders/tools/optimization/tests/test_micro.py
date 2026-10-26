"""Independent C12 search checks. The finite oracle verifies algorithms, never physics.

The real native mechanism cases are retained separately under
results/contenders/optimization/micro/scout. Run this file with --exe PATH after
building tests/micro.cpp plus common.cpp and micro.cpp with arena's slot manager.
"""
from __future__ import annotations
import argparse
import copy
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace

ROOT=Path(__file__).resolve().parents[5]


def canonical(value):
    return json.dumps(value,sort_keys=True,separators=(",",":"),allow_nan=False)


def host_peak_rss(process):
    if os.name!="nt":
        return None
    class Counters(ctypes.Structure):
        _fields_=[("cb",ctypes.c_ulong),("PageFaultCount",ctypes.c_ulong),
            *[(name,ctypes.c_size_t) for name in ("PeakWorkingSetSize","WorkingSetSize","QuotaPeakPagedPoolUsage",
                "QuotaPagedPoolUsage","QuotaPeakNonPagedPoolUsage","QuotaNonPagedPoolUsage","PagefileUsage","PeakPagefileUsage")]]
    counters=Counters(); counters.cb=ctypes.sizeof(counters)
    ok=ctypes.windll.psapi.GetProcessMemoryInfo(ctypes.c_void_p(int(process._handle)),ctypes.byref(counters),counters.cb)
    return counters.PeakWorkingSetSize if ok else None


END={"type":"end"}
A={"type":"move","x":10,"y":0}
B={"type":"move","x":20,"y":0}
HIT={"type":"attack","target":{"kind":1,"id":0}}


def finite_tree(prefix):
    """An explicitly enumerated trap: maximin chooses B, optimistic/yield chooses A."""
    owner=0
    actions=[A,B,END]
    score=0
    terminal=False
    valid=True
    if prefix:
        if prefix[0]==END:
            terminal=len(prefix)==1
            valid=terminal
            actions=[]
        elif prefix[0] in (A,B):
            actions=[END]
            if len(prefix)>=2:
                valid=prefix[1]==END
                owner=1
                actions=[HIT,END]
            if len(prefix)>=3:
                valid=valid and prefix[2] in (HIT,END) and len(prefix)==3
                terminal=True
                actions=[]
                score=((-10 if prefix[2]==HIT else 20) if prefix[0]==A else (3 if prefix[2]==HIT else 4))
        else:
            valid=False
    hp=[100,100]
    if score<0: hp[0]+=score
    else: hp[1]-=score
    champions=[]
    for i in (0,1):
        champions.append(dict(id=i,team=i,type=0,x=0,y=0,health=hp[i],maxHealth=100,alive=True,
            kills=0,gold=0,gems=0,shield=0,strength=10,focus=0,moveSpeed=1,range=1,
            items=[],consumables=[],cooldowns=[0,0],buffs=[],stunned=False,
            channel=dict(kind="none"),availability=dict(move=True,attack=True,cast=True),abilities=[]))
    state=dict(current_owner=owner,round=1,moment=dict(currentChampion=owner,team=owner,round=1,
        phase=10,awaitingInput=True),champions=champions,camps=[],groups=[],scores=[0,0],
        observation=dict(champion_ids=[0,1],camp_ids=[]),fingerprint=hashlib.sha256(canonical(prefix).encode()).hexdigest(),
        fingerprint_scope="Finite verification tree; no native state or physics claim")
    return dict(state=state,actions=actions,terminal=dict(reason="finite-tree-leaf" if terminal else None),
                prefix_valid=valid,supported=True,finite_score=score)


def fixture_oracle():
    parser=argparse.ArgumentParser()
    parser.add_argument("--fixture-oracle",action="store_true")
    parser.add_argument("--fixture-control",type=Path)
    parser.add_argument("--request",type=Path,required=True)
    parser.add_argument("--response",type=Path,required=True)
    parser.add_argument("--repo")
    args=parser.parse_args()
    raw=args.request.read_bytes()
    request=json.loads(raw)
    control=json.loads(args.fixture_control.read_text()) if args.fixture_control and args.fixture_control.is_file() else {}
    identity=dict(backend="finite-maxmin-verification/v1",identity_sha256=hashlib.sha256(b"finite-maxmin-verification/v1").hexdigest(),
        root_fingerprint=finite_tree([])["state"]["fingerprint"],scope="Algorithm fixture only; not native evidence")
    response=dict(schema="contenders-micro/v1",native=False,supported=True,identity=identity,
        request_sha256=hashlib.sha256(raw).hexdigest(),branch_proof=dict(native_branch_equal=False,test_fixture=True))
    if request["kind"]=="identity":
        response.update(state=finite_tree([])["state"],actions=finite_tree([])["actions"])
    elif request["kind"]=="baseline":
        response.update(state=finite_tree([A,END,END])["state"],prefix=[A,END,END],prefix_valid=True,
                        actions=[],trace=[],policy="finite-fixture-optimistic-trap-control")
    elif request["kind"]=="evaluate":
        if control.get("fail_next"):
            control.pop("fail_next")
            args.fixture_control.write_text(json.dumps(control))
            raise SystemExit(19)
        if control.get("sleep"):
            time.sleep(control["sleep"])
        results=[]
        for job in request["jobs"]:
            item=finite_tree(job["prefix"])
            item.update(id=job["id"],trace=[])
            for index,action in enumerate(job["prefix"]):
                prior=finite_tree(job["prefix"][:index])
                after=finite_tree(job["prefix"][:index+1])
                item["trace"].append(dict(index=index,owner=prior["state"]["current_owner"],action=action,
                    code=0 if after["prefix_valid"] else 22,applied=after["prefix_valid"],events=[],
                    before_fingerprint=prior["state"]["fingerprint"],after_fingerprint=after["state"]["fingerprint"],effects={}))
            results.append(item)
        response.update(root_state=finite_tree([])["state"],results=results)
    else:
        raise ValueError("Finite fixture accepts identity/evaluate/baseline only")
    args.response.parent.mkdir(parents=True,exist_ok=True)
    args.response.write_text(json.dumps(response),encoding="utf-8")


def exact_fixture_result():
    # Independent, complete enumeration. Every enemy reply is included rather
    # than freezing the opponent to end. Same-owner move and end precede MIN.
    values={}
    for root_action in (A,B):
        values[canonical(root_action)]=min(finite_tree([root_action,END,reply])["finite_score"] for reply in (HIT,END))
    values[canonical(END)]=0
    winner=max(values,key=values.get)
    assert winner==canonical(B) and values[winner]==3
    assert max(finite_tree([A,END,reply])["finite_score"] for reply in (HIT,END))==20
    return json.loads(winner),values


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe",type=Path,required=True)
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args()
    out=args.out.resolve(); out.mkdir(parents=True,exist_ok=True)
    control=out/"fixture-control.txt"; control.write_text("{}")
    config=out/"fixture-input.json"
    config.write_text(json.dumps(dict(scenario=dict(replay="finite-verification-tree",actor=0,input=0,participants=[0,1]),
        objective=dict(kind="combat"),search=dict(horizon_turns=2,max_nodes=100,batch_size=2,max_actions_per_turn=8))),encoding="utf-8")
    base=[str(args.exe.resolve()),"micro","--repo",str(ROOT),"--input",str(config),"--seed","17",
        "--allow-test-fixture","--evaluator",sys.executable,"--evaluator-arg",str(Path(__file__).resolve()),
        "--evaluator-arg=--fixture-oracle","--evaluator-arg=--fixture-control","--evaluator-arg",str(control)]
    checks=[]
    timings=[]
    strict=[argument for argument in base if argument!="--allow-test-fixture"]+["--out",str(out/"strict-native"),"--iterations","1"]
    refused=subprocess.run(strict,cwd=ROOT,capture_output=True,text=True,timeout=30)
    assert refused.returncode!=0 and "native" in refused.stderr.lower(),refused.stderr
    checks.append(dict(name="diagnostic-oracle-requires-explicit-fixture-opt-in"))
    def run(name,iterations,resume=False,extra=(),success=True):
        folder=out/name
        command=base+["--out",str(folder),"--iterations",str(iterations),*extra]+(["--resume"] if resume else [])
        started=time.monotonic()
        with subprocess.Popen(command,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True) as child:
            stdout,stderr=child.communicate(timeout=120)
            peak=host_peak_rss(child)
            process=SimpleNamespace(returncode=child.returncode,stdout=stdout,stderr=stderr)
        timings.append(dict(case=name,iterations_target=iterations,resume=resume,
            elapsed_seconds=time.monotonic()-started,host_worker_peak_rss_bytes=peak))
        (out/(name+"-last.stdout.txt")).write_text(process.stdout,encoding="utf-8")
        (out/(name+"-last.stderr.txt")).write_text(process.stderr,encoding="utf-8")
        if success is not None:
            assert (process.returncode==0)==success,(name,process.returncode,process.stdout,process.stderr)
        return process,time.monotonic()-started
    # The complete finite game is small enough to enumerate independently. The
    # production search must reject optimistic trap A once both enemy replies exist.
    expected,values=exact_fixture_result()
    run("continuous",200)
    report=json.loads((out/"continuous/report.json").read_text())
    checkpoint=json.loads((out/"continuous/checkpoint.json").read_text())
    assert report["backend_authority"]=="test_fixture"
    assert report["root_action"]==expected,(report["root_action"],expected)
    assert report["status"]=="exhausted_declared_tree",report["status"]
    assert report["nodes"]==10,report["nodes"]
    assert [step["action"] for step in report["principal_variation"]]==[B,END,HIT],report["principal_variation"]
    assert [step["owner"] for step in report["principal_variation"]]==[0,0,1]
    assert abs(report["best"]["metrics"]["health_swing"]-.03)<1e-12
    assert report["baseline"]["value"]>report["best"]["value"],"The fixture baseline must stay explicitly optimistic rather than become a minimax guarantee"
    checks.append(dict(name="finite-independent-enumeration",expected_root_action=expected,leaf_minima=values))
    run("split",3)
    split_before=json.loads((out/"split/checkpoint.json").read_text())
    run("split",3,True)
    split_same=json.loads((out/"split/checkpoint.json").read_text())
    assert split_before["iteration"]==split_same["iteration"]
    assert split_before["state"]==split_same["state"]
    run("split",200,True)
    resumed=json.loads((out/"split/checkpoint.json").read_text())
    assert resumed["iteration"]==checkpoint["iteration"]
    assert resumed["state"]==checkpoint["state"],"Split and continuous tree/search work differ"
    checks.append(dict(name="same-cap-and-exact-resume",iteration=checkpoint["iteration"]))
    corrupt=copy.deepcopy(resumed); corrupt["iteration"]+=1
    (out/"split/checkpoint.json").write_text(json.dumps(corrupt))
    process,_=run("split",201,True,success=False)
    assert "checksum" in process.stderr.lower(),process.stderr
    checks.append(dict(name="corrupted-checkpoint-fails-closed"))
    control.write_text(json.dumps(dict(fail_next=True)))
    run("failure",200,success=False)
    failed=json.loads((out/"failure/checkpoint.json").read_text())
    control.write_text("{}")
    run("failure",200,True)
    recovered=json.loads((out/"failure/checkpoint.json").read_text())
    assert recovered["state"]==checkpoint["state"]
    checks.append(dict(name="infrastructure-failure-retries-same-work",committed_iteration=failed["iteration"]))
    control.write_text(json.dumps(dict(sleep=2)))
    process,elapsed=run("deadline",200,extra=("--seconds","0.5"),success=None)
    assert elapsed<3,elapsed
    stopped=json.loads((out/"deadline/run-status.json").read_text())
    assert stopped["time_limit_reached"],stopped
    control.write_text("{}")
    run("deadline",200,True)
    after_deadline=json.loads((out/"deadline/checkpoint.json").read_text())
    assert after_deadline["state"]==checkpoint["state"]
    checks.append(dict(name="deadline-and-restart",elapsed_seconds=elapsed))
    limited=out/"fixture-low-memory.json"
    limited_config=json.loads(config.read_text());limited_config["search"]["max_state_memory_mb"]=16
    limited.write_text(json.dumps(limited_config))
    limited_command=[str(limited) if argument==str(config) else argument for argument in base]
    limited_run=subprocess.run(limited_command+["--out",str(out/"low-memory"),"--iterations","200"],
        cwd=ROOT,capture_output=True,text=True,timeout=30)
    assert limited_run.returncode==0,limited_run.stderr
    limited_report=json.loads((out/"low-memory/report.json").read_text())
    assert limited_report["status"]=="resource_limited",limited_report["status"]
    assert limited_report["native_evaluations"]==0
    assert (out/"low-memory/checkpoint.json").is_file()
    checks.append(dict(name="declared-memory-guard-stops-before-expansion",native_evaluations=0))
    no_seed=out/"fixture-no-seed.json"
    no_seed_config=json.loads(config.read_text());no_seed_config["search"]["seed_baseline"]=False
    no_seed.write_text(json.dumps(no_seed_config))
    no_seed_command=[str(no_seed) if argument==str(config) else argument for argument in base]
    no_seed_run=subprocess.run(no_seed_command+["--out",str(out/"no-seed"),"--iterations","200"],
        cwd=ROOT,capture_output=True,text=True,timeout=30)
    assert no_seed_run.returncode==0,no_seed_run.stderr
    no_seed_report=json.loads((out/"no-seed/report.json").read_text())
    no_seed_checkpoint=json.loads((out/"no-seed/checkpoint.json").read_text())
    def prefix_multiset(saved):
        nodes=saved["state"]["nodes"]
        prefixes=[]
        for index in range(len(nodes)):
            actions=[];node=index
            while node:
                actions.append(nodes[node][1]);node=nodes[node][0]
            prefixes.append(canonical(list(reversed(actions))))
        return sorted(prefixes)
    assert no_seed_report["root_action"]==expected and no_seed_report["nodes"]==10
    assert prefix_multiset(no_seed_checkpoint)==prefix_multiset(checkpoint),"Baseline seeding pruned, invented, or duplicated finite action prefixes"
    checks.append(dict(name="seeded-and-unseeded-exhaustive-domain-equivalence",nodes=10,root_action=expected))
    summary=dict(passed=True,checks=checks,scope="Finite algorithm fixture, not native combat or search-strength evidence",
                 executable_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),report=report,timings=timings,
                 checkpoint=dict(bytes=(out/"continuous/checkpoint.json").stat().st_size,nodes=report["nodes"],
                     interpretation="Serialized checkpoint size on the finite fixture; host RSS includes runtime/JSON allocations and excludes evaluator child processes."))
    (out/"verification.json").write_text(json.dumps(summary,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(dict(passed=True,checks=len(checks),output=str(out))))


if __name__=="__main__":
    if "--fixture-oracle" in sys.argv:
        fixture_oracle()
    else:
        main()
