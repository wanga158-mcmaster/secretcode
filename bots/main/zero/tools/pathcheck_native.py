"""Check the production path cap's fuel inequality and compile the production runtime probe.

Run with `arena env`'s Python. Native processes use arena's shared slots and private Windows Jobs.
All generated sources, binaries and logs stay in .cache/main/zero/path-parity/native/.
"""
from pathlib import Path
import json
import re
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools/arena'))
import compiled_runner as native


def main():
    out = ROOT / '.cache/main/zero/path-parity/native'
    out.mkdir(parents=True, exist_ok=True)
    production = (ROOT / 'bots/main/zero/src/sdk.cpp').read_text()
    marker = 'std::uint16_t affordable_cap('
    start = production.index(marker)
    brace = production.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (production[end] == '{') - (production[end] == '}')
        end += 1
    helper = production[start:end]
    margin = re.search(r'constexpr std::uint64_t kPathMargin = (\d+);', production).group(1)
    source = out / 'cap.cpp'
    source.write_text('''#include <algorithm>
#include <cstdint>
#include <cstdio>
namespace cambc {
struct distance_field { static constexpr std::uint16_t unreachable = 65535; };
namespace detail {
struct metadata { std::uint32_t chunked_width, chunked_height, field_bytes; };
metadata current;
const metadata& layout() { return current; }
}}
constexpr std::uint64_t kPathMargin = ''' + margin + ';\n' + helper + '''
int main() {
  unsigned checks = 0;
  for (const auto chunks_w : {4u, 8u, 13u}) {
    const auto chunks = std::uint64_t(chunks_w) * chunks_w;
    cambc::detail::current = {chunks_w, chunks_w, std::uint32_t(chunks * 128)};
    for (const auto reserve : {0ull, 5000ull, 30000ull, 80000ull}) {
      std::uint16_t previous = 0;
      for (std::uint64_t remaining = 0; remaining <= 200000; remaining += 17) {
        const auto cap = affordable_cap(remaining, reserve);
        if (cap < previous) return 1;
        previous = cap;
        if (cap) {
          const auto charge = chunks * 16 + chunks * cap;
          if (remaining <= reserve + kPathMargin) return 2;
          const auto allowance = (remaining - reserve - kPathMargin) * 2 / 3;
          if (charge > allowance || charge + reserve + kPathMargin > remaining) return 3;
          ++checks;
        }
      }
    }
  }
  cambc::detail::current = {13, 13, 21632};
  if (affordable_cap(100000, 5000) <= 172 || affordable_cap(100000, 30000) <= 172) return 4;
  if (!affordable_cap(100000, 80000)) return 5;
  if (affordable_cap(1000000000, 0) != 65534) return 6;
  std::printf("path cap inequality: %u positive-cap cases; bootstrap/ordinary/partial boundaries pass\\n", checks);
}
''', encoding='utf-8', newline='\n')
    compiler = ROOT / '.tools/toolchain/llvm-mingw-20261020-ucrt-x86_64/bin/clang++.exe'
    wasi = Path.home() / 'AppData/Local/cambc/wasi-sdk-33/bin/clang++.exe'
    executable = out / 'cap.exe'
    commands = [
        ('probe-syntax', [str(wasi), '-std=c++20', '-fsyntax-only', '-I' + str(ROOT / 'sdk/cpp'), str(ROOT / 'bots/main/zero-pathcheck/src/main.cpp')]),
        ('cap-build', [str(compiler), '-std=c++20', '-O2', '-static', str(source), '-o', str(executable)]),
        ('cap-run', [str(executable)]),
    ]
    report = []
    for name, argv in commands:
        directory = out / name
        directory.mkdir(exist_ok=True)
        result = native._slotted_command(lambda _, args=argv: args, ROOT, directory, 120, jobs=1)
        record = {'name': name, 'returncode': result.get('returncode'), 'stdout': (directory / 'stdout.bin').read_text(errors='replace'), 'stderr': (directory / 'stderr.bin').read_text(errors='replace')}
        report.append(record)
        print(json.dumps(record))
        if result.get('returncode') != 0:
            (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
            return 1
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
