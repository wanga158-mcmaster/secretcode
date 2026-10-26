"""C10's real WASM-fuel evaluator, invoked internally by the C++ optimizer.

The generated integer kernel is first checked as generated C++ under the pinned
WASI compiler/runtime. A short native game then measures it and a reference on the
same input in the same owner turn. These are mechanism games, not release health
or competitive evidence. No CPU timing is called game fuel.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import sys
import zipfile

VERSION = "c10-native-nearest/1"
class CandidateInvalid(RuntimeError):
    """Only observed semantic/fuel invalidity, never a missing runtime or timeout."""

LAYOUTS = ("aos32", "aos16", "soa32", "soa16", "packed16")
DISTANCES = ("ternary", "stdabs", "lut", "rotated")
PATTERNS = ("uniform", "clustered", "ties", "zero_front", "zero_back", "axis_stripes")
ROW = re.compile(r"FUEL_KERNEL sample=(\d+) n=(\d+) pattern=(\d+) raw=(\d+) reference=(\d+) checksum=(\d+) setup=(\d+)")


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def atomic_text(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + f".tmp-{os.getpid()}")
    with temporary.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(text)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def normalize_candidate(value):
    if not isinstance(value, dict):
        raise ValueError("candidate must be an object")
    c = dict(layout=value.get("layout", "aos32"), distance=value.get("distance", "ternary"),
             reduction=value.get("reduction", "branch"), unroll=value.get("unroll", 1),
             early_zero=value.get("early_zero", False), axis_prune=value.get("axis_prune", False))
    if c["layout"] not in LAYOUTS or c["distance"] not in DISTANCES or c["reduction"] not in ("branch", "mask"):
        raise ValueError("candidate outside C10 implementation grammar")
    if isinstance(c["unroll"], bool) or not isinstance(c["unroll"], int) or c["unroll"] not in (1, 2, 4, 8):
        raise ValueError("unroll must be 1,2,4,8")
    if not isinstance(c["early_zero"], bool) or not isinstance(c["axis_prune"], bool):
        raise ValueError("early_zero and axis_prune must be booleans")
    return c


def normalize_benchmark(value):
    if value is not None and not isinstance(value, dict):
        raise ValueError("benchmark must be an object")
    b = dict(map="01big-world", sides=["A", "B"], point_counts=[16, 64, 128],
             patterns=list(PATTERNS), samples_per_case=2, seed=17)
    b.update(value or {})
    if set(b) - {"map","sides","point_counts","patterns","samples_per_case","seed"}:
        raise ValueError("unknown benchmark field")
    if not isinstance(b["map"], str) or not re.fullmatch(r"[A-Za-z0-9_.-]+", b["map"]):
        raise ValueError("benchmark map must be one installed map name")
    if not isinstance(b["sides"], list) or not b["sides"] or len(set(b["sides"])) != len(b["sides"]) or any(x not in ("A", "B") for x in b["sides"]):
        raise ValueError("sides must contain A and/or B without duplicates")
    if not isinstance(b["point_counts"], list) or not b["point_counts"] or len(set(b["point_counts"])) != len(b["point_counts"]):
        raise ValueError("point_counts must be a nonempty unique list")
    if any(isinstance(n, bool) or not isinstance(n, int) or not 1 <= n <= 256 for n in b["point_counts"]):
        raise ValueError("point_counts must be integers in [1,256]")
    if not isinstance(b["patterns"], list) or not b["patterns"] or len(set(b["patterns"])) != len(b["patterns"]) or any(x not in PATTERNS for x in b["patterns"]):
        raise ValueError("unsupported or duplicate input pattern")
    if isinstance(b["samples_per_case"], bool) or not isinstance(b["samples_per_case"], int) or not 1 <= b["samples_per_case"] <= 8:
        raise ValueError("samples_per_case must be in [1,8]")
    if isinstance(b["seed"], bool) or not isinstance(b["seed"], int) or not 0 <= b["seed"] <= 0xffffffff:
        raise ValueError("seed must be uint32")
    return b


def kernel_header(candidate):
    c = normalize_candidate(candidate)
    layout = c["layout"]
    if layout.startswith("aos"):
        scalar = "std::int16_t" if layout == "aos16" else "int"
        data = f"struct Data {{ struct Point {{ {scalar} x,y; }} p[256]; void set(int i,int x,int y) {{ p[i]={{static_cast<{scalar}>(x),static_cast<{scalar}>(y)}}; }} }};"
        x, y = "data.p[i].x", "data.p[i].y"
    elif layout.startswith("soa"):
        scalar = "std::int16_t" if layout == "soa16" else "int"
        data = f"struct Data {{ {scalar} x[256],y[256]; void set(int i,int a,int b) {{ x[i]=static_cast<{scalar}>(a); y[i]=static_cast<{scalar}>(b); }} }};"
        x, y = "data.x[i]", "data.y[i]"
    else:
        data = "struct Data { std::uint16_t p[256]; void set(int i,int x,int y) { p[i]=static_cast<std::uint16_t>(x|(y<<8)); } };"
        x, y = "static_cast<int>(data.p[i]&255u)", "static_cast<int>(data.p[i]>>8)"
    lut = ",".join(str(abs(x)) for x in range(-99, 100))
    if c["distance"] == "ternary":
        distance = "const int ax=dx<0?-dx:dx, ay=dy<0?-dy:dy; return ax>ay?ax:ay;"
    elif c["distance"] == "stdabs":
        distance = "return std::max(std::abs(dx),std::abs(dy));"
    elif c["distance"] == "lut":
        distance = "return std::max(static_cast<int>(absolute[dx+99]),static_cast<int>(absolute[dy+99]));"
    else:
        distance = "const int a=dx+dy,b=dx-dy; return ((a<0?-a:a)+(b<0?-b:b))/2;"
    reduction = ("if(d<best) { best=d; encoded=i+1; }" if c["reduction"] == "branch" else
        "const std::uint32_t m=0u-static_cast<std::uint32_t>(d<best); best=static_cast<int>((static_cast<std::uint32_t>(best)&~m)|(static_cast<std::uint32_t>(d)&m)); encoded=static_cast<int>((static_cast<std::uint32_t>(encoded)&~m)|(static_cast<std::uint32_t>(i+1)&m));")
    prune = "const int ax=dx<0?-dx:dx; if(ax>=best) return;" if c["axis_prune"] else ""
    early = "if(best==0) return packed(best,encoded);" if c["early_zero"] else ""
    u = c["unroll"]
    body = " ".join(f"visit(i+{k}); {early}" for k in range(u))
    return f"""#pragma once
// C10 generated kernel. Preconditions: 0<=n<=256; all coordinates in [0,99].
// Result high16=Chebyshev distance, low16=first nearest index+1 (0 for n=0).
// Empty distance=10000. Integer-only; no gameplay or SDK calls.
// Candidate: {canonical(c)}
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#if defined(__GNUC__) || defined(__clang__)
#define C10_NOINLINE __attribute__((noinline))
#else
#define C10_NOINLINE
#endif
namespace fuel_kernel {{
{data}
inline constexpr unsigned char absolute[199]={{{lut}}};
inline std::uint32_t packed(int best,int encoded) {{ return (static_cast<std::uint32_t>(best)<<16)|static_cast<std::uint32_t>(encoded); }}
inline int distance(int dx,int dy) {{ {distance} }}
C10_NOINLINE inline std::uint32_t nearest(const Data& data,int n,int qx,int qy) {{
    int best=10000,encoded=0;
    auto visit=[&](int i) {{ const int dx={x}-qx,dy={y}-qy; {prune} const int d=distance(dx,dy); {reduction} }};
    int i=0;
    for(;i+{u}<=n;i+={u}) {{ {body} }}
    for(;i<n;++i) {{ visit(i); {early} }}
    return packed(best,encoded);
}}
}} // namespace fuel_kernel
#undef C10_NOINLINE
"""


REFERENCE = r"""
struct RefPoint { int x,y; };
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static std::uint32_t reference(const RefPoint* points,int n,int qx,int qy) {
    int best=10000,encoded=0;
    for(int i=0;i<n;++i) { const int dx=points[i].x-qx,dy=points[i].y-qy;
        const int ax=dx<0?-dx:dx,ay=dy<0?-dy:dy,d=ax>ay?ax:ay;
        if(d<best) { best=d; encoded=i+1; }
    }
    return (static_cast<std::uint32_t>(best)<<16)|static_cast<std::uint32_t>(encoded);
}
static std::uint32_t next_random(std::uint32_t& x) { x^=x<<13; x^=x>>17; x^=x<<5; return x; }
"""


def validator_source():
    return '#include "kernel.hpp"\n#include <cstdio>\n' + REFERENCE + r"""
static fuel_kernel::Data data;
static RefPoint points[256];
static std::uint64_t checks=0;
static int inequivalent(int stage) {
    std::printf("C10_INEQUIVALENT stage=%d\n",stage);
    return stage;
}
static bool check(int n,int qx,int qy) {
    ++checks;
    return fuel_kernel::nearest(data,n,qx,qy)==reference(points,n,qx,qy);
}
int main() {
    for(int dx=-99;dx<=99;++dx) for(int dy=-99;dy<=99;++dy) {
        const int ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
        if(fuel_kernel::distance(dx,dy)!=(ax>ay?ax:ay)) return inequivalent(1);
    }
    // Exhaust all sequences up to length five from seven boundary/tie points,
    // crossed with 36 query positions. This includes empty and stable tie cases.
    const RefPoint alphabet[]={{0,0},{99,99},{0,99},{99,0},{49,50},{50,49},{50,50}};
    const int queries[]={0,1,49,50,98,99};
    int combinations=1;
    for(int n=0;n<=5;++n) {
        for(int code=0;code<combinations;++code) {
            int v=code;
            for(int i=0;i<n;++i) { points[i]=alphabet[v%7]; v/=7; data.set(i,points[i].x,points[i].y); }
            for(int x:queries) for(int y:queries) if(!check(n,x,y)) return inequivalent(2);
        }
        combinations*=7;
    }
    // All lengths and unroll remainders through 256, random/boundary/duplicate
    // arrays and zero distances at the first and final index.
    std::uint32_t seed=0x9e3779b9u;
    for(int n=0;n<=256;++n) for(int pattern=0;pattern<8;++pattern) {
        const int qx=static_cast<int>(next_random(seed)%100),qy=static_cast<int>(next_random(seed)%100);
        for(int i=0;i<n;++i) {
            int x=static_cast<int>(next_random(seed)%100),y=static_cast<int>(next_random(seed)%100);
            if(pattern==1) { x=i%2?0:99; y=(i/2)%2?0:99; }
            if(pattern==2) { x=50; y=50; }
            if(pattern==3) { x=qx; y=i%100; }
            if(pattern==4) { x=i%100; y=qy; }
            if((pattern==5&&i==0)||(pattern==6&&i==n-1)) { x=qx;y=qy; }
            if(pattern==7) { x=static_cast<int>((qx+3)%100); y=static_cast<int>((qy+3)%100); }
            points[i]={x,y}; data.set(i,x,y);
        }
        if(!check(n,qx,qy)) return inequivalent(3);
    }
    std::printf("C10_VALIDATED distance_pairs=39601 nearest_arrays=%llu\n",static_cast<unsigned long long>(checks));
    return 0;
}
"""


def require_validation(valid, validation_log):
    if valid and "C10_VALIDATED " in validation_log:
        return
    if "C10_INEQUIVALENT " in validation_log:
        raise CandidateInvalid("Generated kernel equivalence failed: " + validation_log[-2000:])
    raise RuntimeError("Generated kernel validation infrastructure failed: " + validation_log[-2000:])


def probe_source(benchmark):
    b = normalize_benchmark(benchmark)
    cases = [(n, PATTERNS.index(p)) for n in b["point_counts"] for p in b["patterns"] for _ in range(b["samples_per_case"])]
    ns = ",".join(str(n) for n, _ in cases)
    patterns = ",".join(str(p) for _, p in cases)
    return '#include <cambc.h>\n#include <algorithm>\n#include <cstdio>\n#include <cstdint>\n#include "kernel.hpp"\n' + REFERENCE + f"""
static fuel_kernel::Data data;
static RefPoint points[256];
static volatile std::uint32_t sink;
static int sample=0;
static bool selected=false,initialized=false;
static constexpr int counts[]={{{ns}}};
static constexpr int patterns[]={{{patterns}}};
static constexpr int total={len(cases)};
static void draft_turn() {{
    const cambc::champion_draft picks[]={{cambc::champion_draft::ryth,cambc::champion_draft::marcella,cambc::champion_draft::varg,cambc::champion_draft::tavi,cambc::champion_draft::stellar,cambc::champion_draft::vesper_vale}};
    const auto mine=cambc::draft::current_draft(cambc::team::ally);
    for(auto p:picks) if(std::find(mine.begin(),mine.end(),p)==mine.end()) {{ cambc::draft::pick(p); return; }}
}}
static void play_turn() {{
    auto& me=cambc::self();
    if(!initialized) {{
        const auto roster=cambc::champions(cambc::team::ally);
        auto first=static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(me));
        for(const auto& c:roster) first=std::min(first,static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(c)));
        selected=first==static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(me)); initialized=true;
    }}
    if(!selected||!me.is_alive()||cambc::round_number()<3||sample>=total) return;
    if(cambc::fuel_remaining()<85000) {{ std::printf("fuel optimizer error insufficient fresh reserve\\n"); return; }}
    const auto setup_start=cambc::fuel_remaining();
    const int n=counts[sample],pattern=patterns[sample];
    std::uint32_t random={b['seed']}u^(static_cast<std::uint32_t>(sample+1)*2654435761u)^cambc::map::width();
    const int qx=static_cast<int>(next_random(random)%100),qy=static_cast<int>(next_random(random)%100);
    for(int i=0;i<n;++i) {{
        int x=static_cast<int>(next_random(random)%100),y=static_cast<int>(next_random(random)%100);
        if(pattern==1) {{ x=std::clamp(qx+static_cast<int>(next_random(random)%9)-4,0,99); y=std::clamp(qy+static_cast<int>(next_random(random)%9)-4,0,99); }}
        if(pattern==2) {{ x=50;y=50; }}
        if((pattern==3&&i==0)||(pattern==4&&i==n-1)) {{ x=qx;y=qy; }}
        if(pattern==5) {{ x=qx;y=i%100; }}
        points[i]={{x,y}}; data.set(i,x,y);
    }}
    const auto setup=setup_start-cambc::fuel_remaining();
    std::uint64_t raw=0,ref=0; std::uint32_t actual=0,expected=0;
    if(sample%2==0) {{
        auto start=cambc::fuel_remaining(); expected=reference(points,n,qx,qy); sink=expected; ref=start-cambc::fuel_remaining();
        start=cambc::fuel_remaining(); actual=fuel_kernel::nearest(data,n,qx,qy); sink=actual; raw=start-cambc::fuel_remaining();
    }} else {{
        auto start=cambc::fuel_remaining(); actual=fuel_kernel::nearest(data,n,qx,qy); sink=actual; raw=start-cambc::fuel_remaining();
        start=cambc::fuel_remaining(); expected=reference(points,n,qx,qy); sink=expected; ref=start-cambc::fuel_remaining();
    }}
    if(actual!=expected) std::printf("fuel optimizer error native checksum mismatch sample=%d\\n",sample);
    else std::printf("FUEL_KERNEL sample=%d n=%d pattern=%d raw=%llu reference=%llu checksum=%u setup=%llu\\n",sample,n,pattern,static_cast<unsigned long long>(raw),static_cast<unsigned long long>(ref),actual,static_cast<unsigned long long>(setup));
    ++sample;
}}
int main() {{ while(true) {{
    try {{ if(cambc::is_draft()) draft_turn(); else play_turn(); }}
    catch(const std::exception& e) {{ std::printf("fuel optimizer error: %s\\n",e.what()); }}
    catch(...) {{ std::printf("fuel optimizer error: unknown\\n"); }}
    cambc::end_turn();
}} }}
"""


def bindings(repo, core):
    files = [Path(__file__).resolve(), repo / "docs/reference/current-version.json", repo / "docs/reference/runtime-catalog.json",
             repo / "sdk/cpp/cambc.h", repo / "cmake/CMakeLists.txt", repo / "cambc.toml",
             repo / "tools/arena/core.py", repo / "tools/arena/games.py", repo / "tools/arena/wasi.py",
             repo / "tools/arena/wasi-run.mjs", Path(core.CAMBC), Path(core.CLANG), Path(core.NODE)]
    return {str(p.relative_to(repo)) if p.is_relative_to(repo) else p.name: digest(p) for p in files}


def export_base(work):
    """The same campaign root is found from nested responses and best_measurement.json."""
    work = Path(work).resolve()
    for parent in (work, *work.parents):
        if (parent / "run.json").is_file():
            return parent, "campaign_root"
    return work, "response_parent"


def resolve_artifact(manifest, descriptor):
    """Resolve an exported body after moving the complete output directory."""
    manifest = Path(manifest).resolve()
    relative = Path(descriptor["path"])
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError("C10 artifact path must stay relative to its declared base")
    if descriptor["path_base"] == "response_parent":
        base = manifest.parent
    elif descriptor["path_base"] == "campaign_root":
        base = next((parent for parent in (manifest.parent, *manifest.parent.parents)
                     if (parent / "run.json").is_file()), None)
        if base is None:
            raise ValueError("C10 campaign artifact base requires its enclosing run.json")
    else:
        raise ValueError("Unknown C10 artifact path_base")
    target = (base / relative).resolve()
    if not target.is_relative_to(base):
        raise ValueError("C10 artifact escaped its declared output base")
    return target


def publish(result, work):
    """Carry reproducible bodies with the result, not only cache locators."""
    exported = json.loads(json.dumps(result))
    work = Path(work).resolve()
    base, path_base = export_base(work)
    target = base / ("fuel-artifacts" if path_base == "campaign_root" else "artifacts")
    target.mkdir(parents=True, exist_ok=True)
    exported["durable_artifacts"] = []
    for body in result.get("artifacts", []):
        source = Path(body["path"])
        destination = target / (body["sha256"][:16] + "-" + source.name)
        if destination.exists():
            if digest(destination) != body["sha256"]:
                raise RuntimeError("Existing C10 export body changed: " + str(destination))
        else:
            temporary = destination.with_name(destination.name + f".tmp-{os.getpid()}")
            shutil.copyfile(source, temporary)
            os.replace(temporary, destination)
        exported["durable_artifacts"].append(dict(path=destination.relative_to(base).as_posix(),
                                                   path_base=path_base, sha256=body["sha256"]))
    # Cached absolute source locators are provenance, never the frozen output manifest.
    exported["source_artifacts"] = exported.get("artifacts", [])
    exported["artifacts"] = exported["durable_artifacts"]
    exported["artifact_path_bases"] = dict(
        campaign_root="Nearest enclosing directory containing run.json; preserve the complete campaign when moving it.",
        response_parent="Directory containing response.json; preserve that evaluation directory when moving it.")
    atomic_text(work / "response.json", json.dumps(exported, indent=2))
    return exported


def _evaluate_request(request: dict, repo: Path, work: Path) -> dict:
    repo, work = Path(repo).resolve(), Path(work).resolve()
    os.environ["CAMBC_OFFLINE"] = "1"
    os.environ["CAMBC_NO_UPDATE_CHECK"] = "1"
    if request.get("kind") != "fuel":
        raise ValueError("fuel_evaluator accepts only kind=fuel")
    candidate, benchmark = normalize_candidate(request.get("candidate", {})), normalize_benchmark(request.get("benchmark", {}))
    sys.path.insert(0, str(repo / "tools/arena"))
    import core
    import games
    import wasi
    map_file = core.map_path(benchmark["map"])
    environment = dict(version=VERSION, benchmark=benchmark, bindings=bindings(repo, core), map_sha256=digest(map_file), offline=True)
    if request.get("operation") == "environment":
        return dict(schema="contenders-eval/v1",kind="fuel",healthy=True,
                    measurement_backend="native_wasm_fuel",environment=environment)
    identity = dict(environment=environment, candidate=candidate)
    identity_hash = hashlib.sha256(canonical(identity).encode()).hexdigest()
    cache = repo / ".cache/contenders/optimization/c10/evaluations" / identity_hash
    cache.mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)
    with core.FileLock(cache / "evaluation.lock"):
        result_path = cache / "result.json"
        if result_path.is_file():
            result = json.loads(result_path.read_text(encoding="utf-8"))
            if result.get("identity_hash") != identity_hash or result.get("identity") != identity:
                raise RuntimeError("C10 cache identity mismatch")
            for body in result.get("artifacts", []):
                path = Path(body["path"])
                if not path.is_file() or digest(path) != body["sha256"]:
                    raise RuntimeError(f"C10 cache artifact changed: {path}")
            result["cache_hit"] = True
            return publish(result, work)
        header = kernel_header(candidate)
        (cache / "kernel.hpp").write_text(header, encoding="utf-8", newline="\n")
        validator = cache / "verify.cpp"
        validator.write_text(validator_source(), encoding="utf-8", newline="\n")
        with core.Slots(1, quiet=True):
            valid, validation_log = wasi.run_case("c10-equivalence", validator, includes=[cache], flags=["-O3"])
        (cache / "validation.log").write_text(validation_log, encoding="utf-8", newline="\n")
        require_validation(valid, validation_log)
        bot_root = (repo / "bots/contenders/_optimizer-fuel").resolve()
        if bot_root.parent != (repo / "bots/contenders").resolve():
            raise RuntimeError("generated probe path escaped owned suite")
        with core.FileLock(repo / ".cache/contenders/optimization/c10/materialize.lock"):
            source = bot_root / "src"
            source.mkdir(parents=True, exist_ok=True)
            (source / "kernel.hpp").write_text(header, encoding="utf-8", newline="\n")
            probe = probe_source(benchmark)
            (source / "main.cpp").write_text(probe, encoding="utf-8", newline="\n")
            (cache / "native-probe.cpp").write_text(probe, encoding="utf-8", newline="\n")
            entrant = core.prepare(core.Bot("contenders", "_optimizer-fuel"))
        if not entrant.ok:
            raise RuntimeError("native probe build failed: " + entrant.log[-2000:])
        total = len(benchmark["point_counts"]) * len(benchmark["patterns"]) * benchmark["samples_per_case"]
        modules = [{"path": str(p), "sha256": digest(p)} for p in sorted(entrant.path.rglob("bot*.wasm"))]
        if not modules: raise RuntimeError("native module artifact missing")
        game_hash = hashlib.sha256(canonical(dict(modules=[x["sha256"] for x in modules],
            map_sha256=digest(map_file), engine=games.ENGINE, rounds=total+5, protocol=VERSION)).encode()).hexdigest()
        rows = games.play([games.GameSpec(entrant, entrant, benchmark["map"], {"probe": "optimization-c10", "measurement": game_hash})],
                          repo / ".cache/contenders/optimization/c10/native-games" / game_hash,
                          rounds=total + 5, keep_replays="all", quiet=True, resume=True)
        if len(rows) != 1 or not rows[0].get("replay"):
            raise RuntimeError("native fuel probe did not produce one replay")
        row = rows[0]
        replay = Path(row["replay"])
        if not replay.is_absolute(): replay = repo / replay
        observations = []
        with zipfile.ZipFile(replay) as archive:
            for side in benchmark["sides"]:
                stdout = archive.read(f"team-{side.lower()}.stdout").replace(b"\0", b"").decode("utf-8", "replace")
                (cache / f"team-{side.lower()}.log").write_text(stdout, encoding="utf-8", newline="\n")
                if "native checksum mismatch" in stdout:
                    raise CandidateInvalid("native fuel kernel checksum mismatch")
                if "insufficient fresh reserve" in stdout:
                    raise CandidateInvalid("native fuel kernel could not retain its declared turn reserve")
                if re.search(r"\berror\b", stdout, re.I):
                    raise RuntimeError("native fuel probe logged an error")
                found = [dict(zip(("sample", "n", "pattern", "raw", "reference", "checksum", "setup"), map(int, m.groups())), side=side) for m in ROW.finditer(stdout)]
                if len(found) != total or sorted(x["sample"] for x in found) != list(range(total)):
                    raise RuntimeError(f"native probe missing/duplicating measurements on side {side}: {len(found)}/{total}")
                health = row.get(side) or {}
                if health.get("fuel_at_budget"):
                    raise CandidateInvalid("native fuel kernel reached the turn budget")
                if health.get("errors"):
                    raise RuntimeError("native fuel probe error/fuel suspension")
                observations.extend(found)
        ratios = [x["raw"] / x["reference"] for x in observations if x["reference"] > 0]
        if len(ratios) != len(observations) or any(x["raw"] <= 0 for x in observations):
            raise RuntimeError("invalid native fuel measurement")
        # Equal weight per declared input. Two sides test deterministic context;
        # their duplicate input observations are not independent game samples.
        mean_ratio = statistics.mean(ratios)
        by_pattern = {name: statistics.mean(x["raw"]/x["reference"] for x in observations if x["pattern"]==PATTERNS.index(name)) for name in benchmark["patterns"]}
        by_count = {str(n): statistics.mean(x["raw"]/x["reference"] for x in observations if x["n"]==n) for n in benchmark["point_counts"]}
        artifacts = [{"path": str(p), "sha256": digest(p)} for p in (cache / "kernel.hpp", validator, cache / "validation.log", cache / "native-probe.cpp", replay)] + modules
        result = dict(schema="contenders-eval/v1", kind="fuel", measurement_backend="native_wasm_fuel", healthy=True, objective=-mean_ratio,
                      fuel_ratio=mean_ratio, worst_case_ratio=max(ratios), by_pattern=by_pattern, by_count=by_count,
                      identity=identity, identity_hash=identity_hash, cache_hit=False,
                      candidate=candidate, kernel_header=header, observations=observations, cases=[row],
                      validation=dict(status="passed", log=validation_log, domain="Integer coordinate differences [-99,99]^2; exhaustive length<=5 sequences from seven boundary/tie points x36 queries; all lengths0..256 across eight adversarial/random families."),
                      measurement_scope="Truncated native mechanism self-game; fresh ordinary turns; same-turn reference; setup and logging excluded from kernel metric. No competitive outcome or full-game release-health claim.",
                      health_scope="All expected measurements, native checksum equality, no logged error or budget-reaching fuel. Truncated native fault/exhaustion statistics are not asserted complete.",
                      aggregation="Arithmetic mean of per-query candidate/reference fuel ratios over declared inputs; side duplicates are not independent statistical evidence.",
                      limitations=["Kernel-only fuel includes fixed fuel-query/call/sink overhead; no SDK or whole-bot speedup claim.",
                                   "Data preparation/conversion costs are reported separately, not optimized.",
                                   "No float arithmetic or coordinates outside declared integer domain.",
                                   "Best in enumerated family/benchmark distribution, not globally optimal machine code."], artifacts=artifacts)
        atomic_text(result_path, json.dumps(result, indent=2))
        return publish(result, work)


def evaluate_request(request: dict, repo: Path, work: Path) -> dict:
    try:
        return _evaluate_request(request, repo, work)
    except CandidateInvalid as error:
        return dict(schema="contenders-eval/v1",kind="fuel",healthy=False,
                    failure_kind="candidate_invalid",error=str(error))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request", type=Path, required=True)
    parser.add_argument("--response", type=Path, required=True)
    parser.add_argument("--repo", type=Path, required=True)
    args = parser.parse_args()
    try:
        request = json.loads(args.request.read_text(encoding="utf-8"))
        result = evaluate_request(request, args.repo, args.response.parent)
        status = 0
    except Exception as error:
        result = dict(schema="contenders-eval/v1", kind="fuel", healthy=False, failure_kind="infrastructure", error=str(error))
        status = 1
    args.response.parent.mkdir(parents=True, exist_ok=True)
    atomic_text(args.response, json.dumps(result, indent=2))
    print(json.dumps({key: result.get(key) for key in ("kind", "healthy", "objective", "fuel_ratio", "cache_hit", "error")}))
    return status


if __name__ == "__main__":
    raise SystemExit(main())
