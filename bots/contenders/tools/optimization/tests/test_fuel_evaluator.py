"""C10 grammar/export guards; opt-in generated-code WASI and native smoke checks.

Run the substantial generated-source validation with C10_RUN_WASI_TESTS=1.
Native complete roundtrip can be enabled separately with C10_RUN_NATIVE_TEST=1.
"""
from pathlib import Path
import importlib.util
import json
import os
import subprocess
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[5]
SPEC = importlib.util.spec_from_file_location("fuel_evaluator", Path(__file__).resolve().parents[1] / "adapters/fuel_evaluator.py")
fuel = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fuel)


class FuelTests(unittest.TestCase):
    def test_grammar_rejects_unsafe_domain(self):
        for candidate in ({"layout":"unknown"},{"unroll":True},{"unroll":1.0},{"unroll":3},{"distance":"float"},{"axis_prune":1}):
            with self.assertRaises(ValueError): fuel.normalize_candidate(candidate)
        for benchmark in ({"point_counts":[257]},{"point_counts":[True]},{"patterns":["missing"]},
                          {"sides":["A","A"]},{"sides":"A"},{"samples_per_case":9},{"map":"../00eight"},{"typo":1}):
            with self.assertRaises(ValueError): fuel.normalize_benchmark(benchmark)
        fuel.require_validation(True,"C10_VALIDATED distance_pairs=39601 nearest_arrays=1")
        with self.assertRaises(fuel.CandidateInvalid): fuel.require_validation(False,"C10_INEQUIVALENT stage=2")
        for valid,log in ((False,"COMPILE FAILED"),(False,"timed out after 600s"),
                          (False,"Node runtime trapped before the validator completed"),(True,"")):
            with self.assertRaisesRegex(RuntimeError,"infrastructure"): fuel.require_validation(valid,log)

    def test_all_640_headers_are_reproducible(self):
        headers=set()
        for layout in fuel.LAYOUTS:
            for distance in fuel.DISTANCES:
                for reduction in ("branch","mask"):
                    for unroll in (1,2,4,8):
                        for early in (False,True):
                            for prune in (False,True):
                                c=dict(layout=layout,distance=distance,reduction=reduction,unroll=unroll,early_zero=early,axis_prune=prune)
                                header=fuel.kernel_header(c)
                                self.assertEqual(header,fuel.kernel_header(c))
                                self.assertIn("Preconditions: 0<=n<=256",header)
                                headers.add(header)
        self.assertEqual(len(headers),640)

    def test_probe_has_bounded_work_and_error_boundary(self):
        source=fuel.probe_source(dict(point_counts=[16],patterns=["uniform"],samples_per_case=1))
        self.assertIn("sample>=total",source)
        self.assertIn("fuel_remaining()<85000",source)
        self.assertIn("actual!=expected",source)
        self.assertIn("catch(const std::exception&",source)
        self.assertIn("cambc::end_turn()",source)

    def test_export_carries_bodies_and_rejects_tampering(self):
        with tempfile.TemporaryDirectory(dir=ROOT/".cache") as scratch:
            path=Path(scratch); source=path/"kernel.hpp"; source.write_text("test\n",encoding="utf-8")
            result=dict(artifacts=[dict(path=str(source),sha256=fuel.digest(source))])
            exported=fuel.publish(result,path/"out")
            body=fuel.resolve_artifact(path/"out/response.json",exported["durable_artifacts"][0])
            self.assertEqual(body.read_text(),"test\n")
            self.assertFalse(Path(exported["durable_artifacts"][0]["path"]).is_absolute())
            body.write_text("changed\n",encoding="utf-8")
            with self.assertRaises(RuntimeError): fuel.publish(result,path/"out")
            campaign=path/"campaign"; campaign.mkdir(); (campaign/"run.json").write_text("{}")
            work=campaign/"evaluations/kernel/calls/1"
            portable=fuel.publish(result,work)
            (campaign/"best_measurement.json").write_text(json.dumps(portable),encoding="utf-8")
            moved=path/"moved"; shutil.copytree(campaign,moved)
            for manifest in (moved/"best_measurement.json",moved/"evaluations/kernel/calls/1/response.json"):
                saved=json.loads(manifest.read_text())
                self.assertEqual(saved["artifacts"],saved["durable_artifacts"])
                for artifact in saved["durable_artifacts"]:
                    resolved=fuel.resolve_artifact(manifest,artifact)
                    self.assertTrue(resolved.is_relative_to(moved))
                    self.assertEqual(fuel.digest(resolved),artifact["sha256"])
            with self.assertRaises(ValueError): fuel.resolve_artifact(moved/"best_measurement.json",dict(path="../escaped",path_base="campaign_root"))

    @unittest.skipUnless(os.environ.get("C10_RUN_WASI_TESTS")=="1","set C10_RUN_WASI_TESTS=1 for actual generated C++ validation")
    def test_generated_wasi_variants_and_bad_prediction(self):
        sys.path.insert(0,str(ROOT/"tools/arena"))
        import core
        import wasi
        variants=[dict(layout="aos32",distance="ternary",reduction="branch",unroll=1),
                  dict(layout="aos16",distance="stdabs",reduction="mask",unroll=2),
                  dict(layout="soa32",distance="lut",reduction="branch",unroll=4,early_zero=True),
                  dict(layout="soa16",distance="rotated",reduction="mask",unroll=8,axis_prune=True),
                  dict(layout="packed16",distance="lut",reduction="mask",unroll=8,axis_prune=True,early_zero=True)]
        with tempfile.TemporaryDirectory(dir=ROOT/".cache") as scratch:
            path=Path(scratch); source=path/"verify.cpp"; source.write_text(fuel.validator_source(),encoding="utf-8")
            for i,candidate in enumerate(variants):
                (path/"kernel.hpp").write_text(fuel.kernel_header(candidate),encoding="utf-8")
                with core.Slots(1,quiet=True): ok,log=wasi.run_case(f"c10-generated-{i}",source,includes=[path],flags=["-O3"])
                self.assertTrue(ok,log)
                self.assertIn("C10_VALIDATED",log)
            bad=fuel.kernel_header({}).replace("return ax>ay?ax:ay;","return (ax>ay?ax:ay)+1;")
            (path/"kernel.hpp").write_text(bad,encoding="utf-8")
            with core.Slots(1,quiet=True): ok,log=wasi.run_case("c10-bad-generated",source,includes=[path],flags=["-O3"])
            self.assertFalse(ok)
            with self.assertRaises(fuel.CandidateInvalid): fuel.require_validation(ok,log)

    @unittest.skipUnless(os.environ.get("C10_RUN_NATIVE_TEST")=="1","set C10_RUN_NATIVE_TEST=1 for a native probe and cache roundtrip")
    def test_native_measurement_cache_and_durable_export(self):
        work=ROOT/".cache/contenders/optimization/c10/unittest-native"
        request=dict(kind="fuel",candidate=dict(layout="soa16",unroll=4,early_zero=True,axis_prune=True),
                     benchmark=dict(point_counts=[16,64],patterns=["uniform","zero_back"],samples_per_case=1))
        result=fuel.evaluate_request(request,ROOT,work)
        again=fuel.evaluate_request(request,ROOT,work/"again")
        self.assertTrue(result["healthy"])
        self.assertEqual(result["measurement_backend"],"native_wasm_fuel")
        self.assertTrue(again["cache_hit"])
        self.assertEqual(result["observations"],again["observations"])
        self.assertEqual(len(result["observations"]),8)
        for body in again["durable_artifacts"]:
            self.assertEqual(fuel.digest(fuel.resolve_artifact(work/"again/response.json",body)),body["sha256"])

    @unittest.skipUnless(os.environ.get("C10_RUN_CPP_TESTS")=="1","set C10_RUN_CPP_TESTS=1 for C++ engine restart/error tests")
    def test_cpp_resume_failure_environment_and_reference(self):
        sys.path.insert(0,str(ROOT/"tools/arena"))
        import core
        compiler=Path(os.environ.get("CXX",r"C:\msys64\ucrt64\bin\g++.exe"))
        self.assertTrue(compiler.is_file(),str(compiler))
        engine=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(dir=ROOT/".cache") as scratch:
            path=Path(scratch); exe=path/"c10-test.exe"
            command=[str(compiler),"-std=c++20","-O2","-static","-pthread","-I",str(engine/"include"),
                     str(engine/"src/common.cpp"),str(engine/"src/c10.cpp"),str(engine/"tests/c10-driver.cpp"),"-o",str(exe)]
            environment=dict(os.environ,PATH=str(compiler.parent)+os.pathsep+os.environ.get("PATH",""))
            with core.Slots(1,quiet=True): compiled=subprocess.run(command,env=environment,capture_output=True,text=True)
            self.assertEqual(compiled.returncode,0,compiled.stdout+compiled.stderr)
            config=path/"input.json"
            config.write_text(json.dumps(dict(search=dict(layouts=["aos32","packed16"],distances=["ternary","lut"],
                reductions=["branch"],unrolls=[1],early_zero=[False],axis_prune=[False]))),encoding="utf-8")
            state=path/"evaluator-state"; state.mkdir(); (state/"fail_next").write_text("1")
            out=path/"resumed"
            base=[str(exe),"c10","--repo",str(ROOT),"--input",str(config),"--seed","71","--out",str(out),
                  "--allow-test-evaluator","--evaluator",sys.executable,"--evaluator-arg",str(engine/"tests/c10_fixture_evaluator.py"),
                  "--evaluator-arg=--state","--evaluator-arg",str(state)]
            failed=subprocess.run(base+["--iterations","1"],capture_output=True,text=True)
            self.assertNotEqual(failed.returncode,0)
            checkpoint=json.loads((out/"checkpoint.json").read_text())
            self.assertEqual(checkpoint["iteration"],0)
            self.assertEqual(checkpoint["state"]["cursor"],0)
            resumed=subprocess.run(base+["--iterations","2","--resume"],capture_output=True,text=True)
            self.assertEqual(resumed.returncode,0,resumed.stdout+resumed.stderr)
            checkpoint=json.loads((out/"checkpoint.json").read_text())
            self.assertEqual(checkpoint["state"]["cursor"],2)
            self.assertEqual(checkpoint["state"]["failed"],0)
            (state/"environment.txt").write_text("changed")
            changed=subprocess.run(base+["--iterations","4","--resume"],capture_output=True,text=True)
            self.assertNotEqual(changed.returncode,0)
            self.assertIn("environment changed",changed.stderr)
            (state/"environment.txt").write_text("first")
            finished=subprocess.run(base+["--iterations","4","--resume"],capture_output=True,text=True)
            self.assertEqual(finished.returncode,0,finished.stdout+finished.stderr)
            final=json.loads((out/"checkpoint.json").read_text())["state"]
            self.assertEqual(final["cursor"],4)
            self.assertEqual(len(final["archive"]),4)
            report=json.loads((out/"report.json").read_text())
            self.assertFalse(report["native_wasm_fuel"])
            continuous=path/"continuous"
            straight=list(base); straight[straight.index(str(out))]=str(continuous)
            process=subprocess.run(straight+["--iterations","4"],capture_output=True,text=True)
            self.assertEqual(process.returncode,0,process.stdout+process.stderr)
            expected=json.loads((continuous/"checkpoint.json").read_text())["state"]
            self.assertEqual(final["order"],expected["order"])
            self.assertEqual([(x["id"],x["objective"]) for x in final["archive"]],[(x["id"],x["objective"]) for x in expected["archive"]])
            # A proven rejection is the sole class allowed to consume work.
            rejected=path/"rejected"; reject_base=list(base); reject_base[reject_base.index(str(out))]=str(rejected)
            (state/"reject_next").write_text("1")
            process=subprocess.run(reject_base+["--iterations","1"],capture_output=True,text=True)
            self.assertEqual(process.returncode,2)
            rejection=json.loads((rejected/"checkpoint.json").read_text())["state"]
            self.assertEqual(rejection["cursor"],1)
            self.assertEqual(rejection["failed"],1)


if __name__=="__main__": unittest.main()
