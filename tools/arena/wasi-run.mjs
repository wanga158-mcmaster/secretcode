// Run one WASI unit fixture with no game, file or network access: node wasi-run.mjs FIXTURE.wasm [args...]
import fs from 'node:fs';
import {WASI} from 'node:wasi';

const module = await WebAssembly.compile(fs.readFileSync(process.argv[2]));
for (const item of WebAssembly.Module.imports(module)) {
  if (item.module !== 'wasi_snapshot_preview1') throw Error(`unexpected import ${item.module}.${item.name}`);
}
const wasi = new WASI({version: 'preview1', args: process.argv.slice(2), env: {}, preopens: {}, returnOnExit: true});
const instance = await WebAssembly.instantiate(module, {wasi_snapshot_preview1: wasi.wasiImport});
process.exitCode = wasi.start(instance);
