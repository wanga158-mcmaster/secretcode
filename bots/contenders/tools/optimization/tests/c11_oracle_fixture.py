"""Adversarial subprocess fixture; this is never a native evaluator."""
import argparse
import hashlib
import json
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument("--request", type=Path, required=True)
p.add_argument("--response", type=Path, required=True)
p.add_argument("--repo")
a = p.parse_args()
request = json.loads(a.request.read_text(encoding="utf-8"))
if request.get("behavior") != "no_write":
    body = {"schema": "contenders-native-oracle/v1", "native": False,
            "request_sha256": hashlib.sha256(a.request.read_bytes()).hexdigest()}
    if request.get("behavior") == "wrong_hash":
        body["request_sha256"] = "0" * 64
    a.response.write_text(json.dumps(body), encoding="utf-8")
