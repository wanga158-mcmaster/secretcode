#!/usr/bin/env sh
set -eu
engine_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$engine_root/../../../.." && pwd)
cxx=${CXX:-g++}
output=${1:-"$engine_root/bin/optimizer"}
if ! command -v "$cxx" >/dev/null 2>&1; then
    printf '%s\n' 'A native C++20 compiler is required; set CXX to its executable path.' >&2
    exit 1
fi
mkdir -p -- "$(dirname -- "$output")"
if [ -f "$repo_root/arena.cmd" ]; then
    staging_parent="$repo_root/.cache/contenders/optimization/runtime-build"
else
    staging_parent="$engine_root/.cache/runtime-build"
fi
mkdir -p -- "$staging_parent"
staging_directory=$(mktemp -d "$staging_parent/build.XXXXXXXX")
staging_executable="$staging_directory/optimizer"
[ -n "$(find "$engine_root/src" -type f -name '*.cpp' -print -quit)" ] || {
    printf '%s\n' 'No engine sources found in src/.' >&2; exit 1;
}
# The modest source tree fits one compiler invocation. find supplies separate path
# arguments, so whitespace in the checkout path never becomes shell syntax.
if [ -f "$repo_root/tools/arena/core.py" ]; then
    python=${OPTIMIZER_PYTHON:-python3}
    command -v "$python" >/dev/null 2>&1 || {
        printf '%s\n' 'Python is required to use the repository build slot pool; set OPTIMIZER_PYTHON.' >&2; exit 1;
    }
    find "$engine_root/src" -type f -name '*.cpp' -exec "$python" -B -I -X utf8 \
        "$engine_root/adapters/build_host.py" --repo "$repo_root" -- "$cxx" \
        -std=c++20 -O2 -DNDEBUG -Wall -Wextra -Wpedantic -pthread \
        -static-libgcc -static-libstdc++ -I "$engine_root/include" -o "$staging_executable" {} +
else
    # A standalone source packet has no arena installation or shared slot pool.
    find "$engine_root/src" -type f -name '*.cpp' -exec "$cxx" \
        -std=c++20 -O2 -DNDEBUG -Wall -Wextra -Wpedantic -pthread \
        -static-libgcc -static-libstdc++ -I "$engine_root/include" -o "$staging_executable" {} +
fi
cp -- "$staging_executable" "$output"
chmod +x -- "$output"
printf 'Built %s\n' "$output"
