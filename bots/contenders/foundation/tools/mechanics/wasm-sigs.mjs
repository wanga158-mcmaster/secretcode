// Print the signatures of a wasm module's exported functions: node wasm-sigs.mjs FILE.wasm [regex]
// Used to learn the map editor engine's C ABI (MapBuilder_*, Game_*) before calling it.
import fs from 'node:fs';

const bytes = fs.readFileSync(process.argv[2]);
const filter = process.argv[3] ? new RegExp(process.argv[3]) : null;
let pos = 8;
const u = () => { let r = 0, s = 0, b; do { b = bytes[pos++]; r |= (b & 0x7f) << s; s += 7; } while (b & 0x80); return r >>> 0; };
const name = () => { const n = u(); const s = bytes.subarray(pos, pos + n).toString(); pos += n; return s; };
const VT = {0x7f: 'i32', 0x7e: 'i64', 0x7d: 'f32', 0x7c: 'f64', 0x7b: 'v128', 0x70: 'funcref', 0x6f: 'externref'};
const types = [], funcTypes = [], exports = [];
let importedFuncs = 0;
while (pos < bytes.length) {
  const id = bytes[pos++], size = u(), end = pos + size;
  if (id === 1) {
    for (let i = 0, n = u(); i < n; ++i) {
      pos++; // 0x60
      const params = Array.from({length: u()}, () => VT[bytes[pos++]]);
      const results = Array.from({length: u()}, () => VT[bytes[pos++]]);
      types.push({params, results});
    }
  } else if (id === 2) {
    for (let i = 0, n = u(); i < n; ++i) {
      name(); name(); const kind = bytes[pos++];
      if (kind === 0) { funcTypes.push(u()); ++importedFuncs; }
      else if (kind === 1) { pos++; const f = u(); u(); if (f & 1) u(); }
      else if (kind === 2) { const f = u(); u(); if (f & 1) u(); }
      else if (kind === 3) { pos += 2; }
    }
  } else if (id === 3) {
    for (let i = 0, n = u(); i < n; ++i) funcTypes.push(u());
  } else if (id === 7) {
    for (let i = 0, n = u(); i < n; ++i) { const nm = name(); const kind = bytes[pos++]; const idx = u(); if (kind === 0) exports.push([nm, idx]); }
  }
  pos = end;
}
for (const [nm, idx] of exports) {
  if (filter && !filter.test(nm)) continue;
  const t = types[funcTypes[idx]];
  console.log(`${nm}(${t.params.join(', ')}) -> ${t.results.join(', ') || 'void'}`);
}
