#!/usr/bin/env bash
# Build fastrun, the native game runner every `arena exp` uses (C++ on the wasmtime C API, the official
# engine WASM; whole games identical to the CLI), with the local llvm-mingw toolchain in .tools/toolchain.
# Output: .tools/fastrun/${NAME:-fastrun}.exe (+ wasmtime.dll). Build NAME=fastrun-dev to test changes
# while experiments use the installed binary, then rerun without NAME (or copy) once it passes
# tools/fastrun/validate.py.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TC="$ROOT/.tools/toolchain"
CXX="$TC/llvm-mingw-20261020-ucrt-x86_64/bin/clang++.exe"
WT="$TC/wasmtime-v50.0.0-x86_64-mingw-c-api"
OUT="$ROOT/.tools/fastrun"
NAME="${NAME:-fastrun}"
mkdir -p "$OUT"
"$CXX" -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
  -I"$WT/include" "$ROOT/tools/fastrun/fastrun.cpp" -o "$OUT/$NAME.exe" \
  -static "$WT/lib/libwasmtime.dll.a"
[ -f "$OUT/wasmtime.dll" ] || cp -f "$WT/lib/wasmtime.dll" "$OUT/wasmtime.dll"
echo "built $OUT/$NAME.exe"
