"""Bounded paired-statistics, health, identity and restart tests; no native games.

The fixture deliberately controls terminal outcomes. It exercises the production
qualification utility and runtime, without presenting fixture gains as game evidence.
Use QUALIFICATION_TEST_EXE to reuse an already compiled harness.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[5]
PACKAGE=ROOT/"bots/contenders/tools/optimization"
TESTS=PACKAGE/"tests"
SCRATCH=ROOT/".cache/contenders/optimization/qualification-tests"


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def write(path,value):
    Path(path).parent.mkdir(parents=True,exist_ok=True)
    Path(path).write_text(json.dumps(value,sort_keys=True,allow_nan=False)+"\n",encoding="utf-8",newline="\n")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fixture():
    parser=argparse.ArgumentParser()
    parser.add_argument("--request",type=Path,required=True)
    parser.add_argument("--response",type=Path,required=True)
    parser.add_argument("--repo",type=Path,required=True)
    parser.add_argument("--fixture",action="store_true")
    parser.add_argument("--fixture-control",type=Path,required=True)
    args=parser.parse_args()
    request=read(args.request)
    control=read(args.fixture_control)
    if control.get("fail"):
        raise RuntimeError("intentional diagnostic infrastructure failure")
    result=dict(context_identity=control.get("context","fixed-context"),
                identity=dict(test_fixture=True),healthy=True,request_id=request["request_id"])
    if request["kind"]!="identity":
        rows=[]
        for case in request["cases"]:
            path=args.repo/"maps"/case["map"]
            row=dict(map=path.stem,side=case["side"],opponent=case["opponent"],
                     identity=dict(map_sha256=digest(path)),complete=True,payoff=1,
                     baseline_payoff=-1,margin=20,baseline_margin=-10,
                     health=[],baseline_health=[],opponent_health=[],baseline_opponent_health=[])
            if control.get("unhealthy"):
                row["opponent_health"]=["fixture opponent fault"]
                result["healthy"]=False
            rows.append(row)
        if control.get("omit"):
            rows=rows[:-1]
        if control.get("duplicate") and rows:
            rows.append(copy.deepcopy(rows[0]))
        if control.get("wrong_map") and rows:
            rows[0]["identity"]["map_sha256"]="wrong"
        body=(args.fixture_control.parent/"sources"/("a"*128+".candidate.json")
              if control.get("hashed_source_basename") else args.response.parent/"fixture-artifact.bin")
        body.parent.mkdir(parents=True,exist_ok=True)
        body.write_bytes(b"diagnostic qualification artifact\x00\xff")
        result.update(cases=rows,candidate_identity=dict(key=control.get("candidate","frozen-candidate")),
                      games_played=len(rows)*2,cache_hits=0,
                      artifacts=[dict(path=str(body),sha256=digest(body),size=body.stat().st_size,role="test_fixture")])
    write(args.response,result)


class QualificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True,exist_ok=True)
        supplied=os.environ.get("QUALIFICATION_TEST_EXE")
        cls.executable=Path(supplied).resolve() if supplied else SCRATCH/("qualification-tests.exe" if os.name=="nt" else "qualification-tests")
        if supplied:
            if not cls.executable.is_file():
                raise RuntimeError("QUALIFICATION_TEST_EXE does not exist")
            return
        compiler=os.environ.get("CXX") or shutil.which("g++")
        if not compiler and os.name=="nt" and Path("C:/msys64/ucrt64/bin/g++.exe").is_file():
            compiler="C:/msys64/ucrt64/bin/g++.exe"
        if not compiler:
            raise unittest.SkipTest("C++20 compiler unavailable")
        command=[sys.executable,"-B","-I",str(PACKAGE/"adapters/build_host.py"),"--repo",str(ROOT),"--",
                 compiler,"-std=c++20","-O1","-pthread","-Wall","-Wextra","-Wpedantic","-I",str(PACKAGE/"include"),
                 str(PACKAGE/"src/common.cpp"),str(PACKAGE/"src/qualification.cpp"),
                 str(TESTS/"qualification_tests.cpp"),"-o",str(cls.executable)]
        if os.name=="nt":
            command += ["-static","-static-libgcc","-static-libstdc++"]
        result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=240)
        if result.returncode:
            raise RuntimeError(result.stdout[-12000:]+result.stderr[-12000:])

    def setUp(self):
        self.directory=Path(tempfile.mkdtemp(prefix="case-",dir=SCRATCH))
        self.repo=self.directory/"repo"
        (self.repo/"maps").mkdir(parents=True)
        for i in range(3):
            (self.repo/"maps"/f"m{i}.map26a").write_bytes(f"diagnostic map {i}".encode())
        self.control=self.directory/"control.json"
        write(self.control,{})
        self.config=dict(request=dict(schema="contenders-eval/v1",kind="config",
                            candidate=dict(test_fixture=True),controller="fixture/controller"),
                         batch_cases=8,bootstrap_samples=2000,
                         stages=[dict(name="diagnostic",maps=[f"m{i}" for i in range(3)],
                                      opponents=["fixture/opponent"],sides=["A","B"])])
        self.input=self.directory/"input.json"
        write(self.input,self.config)
        self.out=self.directory/"run"

    def invoke(self,mode="qualify",target=6,resume=False,out=None,ok=True):
        command=[str(self.executable),mode,"--repo",str(self.repo),"--input",str(self.input),
                 "--out",str(out or self.out),"--iterations",str(target),"--seed","73",
                 "--evaluator",sys.executable,"--evaluator-arg=-B","--evaluator-arg=-I",
                 "--evaluator-arg",str(Path(__file__).resolve()),"--evaluator-arg=--fixture",
                 "--evaluator-arg=--fixture-control","--evaluator-arg",str(self.control)]
        if resume:
            command.append("--resume")
        result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=30)
        if ok:
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        else:
            self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
        return result

    def summary(self,rows,samples=2000):
        write(self.input,dict(rows=rows,samples=samples))
        self.invoke(mode="summary")
        return read(self.out/"summary.json")

    @staticmethod
    def row(a,b,map_name="one",side="A",**changes):
        row=dict(payoff=a,baseline_payoff=b,margin=a*10,baseline_margin=b*10,
                 map=map_name,side=side,opponent="fixture/opponent",stage="s",family="f",
                 health=[],baseline_health=[],opponent_health=[],baseline_opponent_health=[],complete=True)
        row.update(changes)
        return row

    def test_exact_pairing_health_and_mcnemar(self):
        rows=[self.row(1,-1,"a"),self.row(-1,1,"a","B"),self.row(0,-1,"b")]
        rows[0]["baseline_opponent_health"]=["fault"]
        stats=self.summary(rows)
        self.assertEqual(stats["paired_mean_payoff"],1/3)
        self.assertEqual(stats["paired_win_gains"],1)
        self.assertEqual(stats["paired_win_losses"],1)
        self.assertEqual(stats["raw_key_mcnemar_p"],1)
        self.assertEqual(stats["unhealthy_pairs"],1)
        self.assertEqual(stats["healthy_pair_sensitivity"]["keys"],2)
        self.assertEqual(stats["healthy_pair_sensitivity"]["paired_mean_payoff"],-.5)
        many=self.summary([self.row(1,-1,str(i)) for i in range(5)])
        self.assertAlmostEqual(many["raw_key_mcnemar_p"],.0625)
        for field in ("health","baseline_health","opponent_health","baseline_opponent_health"):
            dirty=self.row(1,-1)
            dirty[field]=["fault"]
            self.assertEqual(self.summary([dirty])["unhealthy_pairs"],1)
        self.assertEqual(self.summary([self.row(1,-1,complete=False)])["incomplete_pairs"],1)
        missing=self.row(1,-1)
        del missing["baseline_health"]
        self.assertEqual(self.summary([missing])["unhealthy_pairs"],1)

    def test_physical_map_clusters_determinism_and_resample_precision(self):
        rows=[self.row(1,-1,"alias-one",identity=dict(map_sha256="same")),
              self.row(1,-1,"alias-two",identity=dict(map_sha256="same"))]
        stats=self.summary(rows)
        self.assertEqual(stats["paired_payoff_interval"]["map_clusters"],1)
        self.assertFalse(stats["paired_payoff_interval"]["sufficient_clusters"])
        rows[1]["identity"]["map_sha256"]="other"
        self.assertEqual(self.summary(rows),self.summary(rows))
        self.assertFalse(self.summary(rows,samples=1)["paired_payoff_interval"]["sufficient_resamples"])
        self.assertNotIn("lower95",self.summary(rows,samples=0)["paired_payoff_interval"])
        paired=[self.row(1,-1,"good",str(i)) for i in range(8)]
        paired += [self.row(-1,1,"bad",str(i)) for i in range(8)]
        interval=self.summary(paired)["paired_payoff_interval"]
        self.assertEqual((interval["lower95"],interval["upper95"]),(-2,2))

    def test_plan_sides_and_duplicate_physical_keys(self):
        self.invoke(mode="plan")
        self.assertEqual(len(read(self.out/"plan.json")),6)
        self.config["stages"][0]["sides"]=[]
        write(self.input,self.config)
        self.invoke(mode="plan",ok=False)
        self.config["stages"][0]["sides"]=["A"]
        self.config["stages"][0]["maps"]=["m0","copy"]
        shutil.copyfile(self.repo/"maps/m0.map26a",self.repo/"maps/copy.map26a")
        write(self.input,self.config)
        self.invoke(mode="plan",ok=False)

    def test_cumulative_cap_same_cap_resume_and_continuous_outputs(self):
        self.invoke(target=1)
        before=read(self.out/"checkpoint.json")
        self.assertEqual(before["iteration"],1)
        self.assertEqual(len(before["state"]["rows"]),1)
        self.invoke(target=1,resume=True)
        self.assertEqual(read(self.out/"checkpoint.json")["state"],before["state"])
        self.invoke(target=3,resume=True)
        self.assertEqual(read(self.out/"checkpoint.json")["iteration"],3)
        self.invoke(target=6,resume=True)
        fresh=self.directory/"fresh"
        self.invoke(target=6,out=fresh)
        a,b=read(self.out/"report.json"),read(fresh/"report.json")
        self.assertEqual(a["statistics"],b["statistics"])
        self.assertEqual(read(self.out/"checkpoint.json")["state"]["rows"],read(fresh/"checkpoint.json")["state"]["rows"])
        self.assertEqual(a["decision"],"supported_gain_in_requested_distribution")

    def test_health_blocks_raw_qualification_and_small_bootstrap_is_uncertain(self):
        write(self.control,dict(unhealthy=True))
        self.invoke()
        report=read(self.out/"report.json")
        self.assertEqual(report["decision"],"rejected_health")
        self.assertEqual(report["statistics"]["healthy_pair_sensitivity"]["keys"],0)
        write(self.control,{})
        self.config["bootstrap_samples"]=1
        write(self.input,self.config)
        other=self.directory/"uncertain"
        self.invoke(out=other)
        self.assertEqual(read(other/"report.json")["decision"],"positive_but_uncertain")

    def test_missing_duplicate_wrong_identity_and_infrastructure_do_not_commit(self):
        for i,control in enumerate((dict(omit=True),dict(duplicate=True),dict(wrong_map=True),dict(fail=True))):
            with self.subTest(control=control):
                write(self.control,control)
                out=self.directory/f"bad-{i}"
                self.invoke(target=1,out=out,ok=False)
                self.assertEqual(read(out/"checkpoint.json")["iteration"],0)
                write(self.control,{})
                self.invoke(target=1,out=out,resume=True)
                self.assertEqual(read(out/"checkpoint.json")["iteration"],1)

    def test_resume_refuses_environment_candidate_and_artifact_corruption(self):
        self.invoke(target=1)
        write(self.control,dict(context="changed-context"))
        self.assertIn("context changed",self.invoke(target=2,resume=True,ok=False).stderr)
        write(self.control,dict(candidate="changed-candidate"))
        self.assertIn("candidate changed",self.invoke(target=2,resume=True,ok=False).stderr)
        self.assertEqual(read(self.out/"checkpoint.json")["iteration"],1)
        write(self.control,{})
        manifest=read(self.out/"artifacts.json")
        self.assertEqual(manifest[0]["path_base"],"run_output")
        body=self.out/manifest[0]["path"]
        self.assertEqual(digest(body),manifest[0]["sha256"])
        body.write_bytes(b"corrupted")
        self.assertIn("artifact missing or changed",self.invoke(target=2,resume=True,ok=False).stderr)

    def test_deep_output_does_not_repeat_long_hashed_source_basename(self):
        write(self.control,dict(hashed_source_basename=True))
        out=self.directory/"deep-output"/"portable-qualification-campaign"
        self.invoke(target=1,out=out)
        first=read(out/"checkpoint.json")
        manifest=read(out/"artifacts.json")
        self.assertEqual(len(manifest),1)
        artifact=manifest[0]
        source=self.directory/"sources"/("a"*128+".candidate.json")
        legacy_destination=out/"artifacts"/(artifact["sha256"]+"-"+source.name)
        self.assertGreater(len(str(legacy_destination)),260,
                           "Regression must exceed MAX_PATH under the former filename scheme")
        retained=out/artifact["path"]
        self.assertEqual(retained.name,artifact["sha256"]+".json")
        self.assertLess(len(str(retained))+40,260,
                        "Reasonable output path leaves room for atomic temporary suffix")
        self.assertEqual(artifact["path_base"],"run_output")
        self.assertEqual(digest(retained),digest(source))
        self.assertEqual(retained.read_bytes(),source.read_bytes())
        self.assertEqual(retained.stat().st_size,artifact["size"])
        self.assertEqual(first["iteration"],1)
        self.invoke(target=1,out=out,resume=True)
        self.assertEqual(read(out/"checkpoint.json")["state"],first["state"])
        self.invoke(target=2,out=out,resume=True)
        resumed=read(out/"checkpoint.json")
        self.assertEqual(resumed["iteration"],2)
        self.assertEqual(resumed["state"]["candidate_identity"],first["state"]["candidate_identity"])
        self.assertEqual(resumed["state"]["context_identity"],first["state"]["context_identity"])
        self.assertEqual(read(out/"artifacts.json"),manifest)
        self.assertEqual(digest(retained),artifact["sha256"])


if __name__=="__main__":
    if "--fixture" in sys.argv:
        fixture()
    else:
        unittest.main(verbosity=2)
