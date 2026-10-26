"""Explicit diagnostic evaluator for C10 restart/error tests; never native fuel."""
import argparse
import json
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument("--request",type=Path,required=True)
p.add_argument("--response",type=Path,required=True)
p.add_argument("--repo")
p.add_argument("--state",type=Path,required=True)
a=p.parse_args()
a.state.mkdir(parents=True,exist_ok=True)
r=json.loads(a.request.read_text(encoding="utf-8"))
token=(a.state/"environment.txt").read_text() if (a.state/"environment.txt").exists() else "first"
if r.get("operation")=="environment":
    result=dict(kind="fuel",healthy=True,environment=dict(backend="diagnostic",token=token))
elif (a.state/"fail_next").exists():
    (a.state/"fail_next").unlink()
    raise SystemExit(17)
elif (a.state/"reject_next").exists():
    (a.state/"reject_next").unlink()
    result=dict(kind="fuel",healthy=False,failure_kind="candidate_invalid",error="deliberately rejected test candidate")
else:
    c=r["candidate"]
    ratio=1.0 + 0.03*["aos32","aos16","soa32","soa16","packed16"].index(c["layout"]) - 0.02*["ternary","stdabs","lut","rotated"].index(c["distance"])
    result=dict(kind="fuel",healthy=True,measurement_backend="diagnostic",objective=-ratio,
                fuel_ratio=ratio,worst_case_ratio=ratio+0.01,identity=dict(test=True),
                kernel_header="// Diagnostic fixture only; no native kernel.\n")
a.response.parent.mkdir(parents=True,exist_ok=True)
a.response.write_text(json.dumps(result),encoding="utf-8")
