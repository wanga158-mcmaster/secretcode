// Run one decision-suite fixture under WASI: node run.mjs FIXTURE.wasm [ENV_JSON] [strict|stub]
// The fixture gets only the given environment (no files, network or game). "strict" rejects any
// non-WASI import; "stub" lets unresolved SDK imports link but throws if one is ever called.
import fs from 'node:fs';
import {WASI} from 'node:wasi';

const [file, envJson = '{}', mode = 'strict'] = process.argv.slice(2);
const module = await WebAssembly.compile(fs.readFileSync(file));
const imports = {};
for (const item of WebAssembly.Module.imports(module)) {
  if (item.module === 'wasi_snapshot_preview1') continue;
  if (mode !== 'stub' || item.kind !== 'function') throw Error(`unexpected import ${item.module}.${item.name}`);
  (imports[item.module] ??= {})[item.name] = () => { throw Error(`unexpected SDK import ${item.module}.${item.name}`); };
}
const wasi = new WASI({version: 'preview1', args: [file], env: JSON.parse(envJson), preopens: {}, returnOnExit: true});
imports.wasi_snapshot_preview1 = wasi.wasiImport;
process.exitCode = wasi.start(await WebAssembly.instantiate(module, imports));
