// Public settings/catalogues from the pinned editor; one unstarted metadata context only.
// No runner initialization, bot loading, drafts, champion turns or match launches.
// node tools/runtime-catalog/capture.mjs [--project DIR] [--out FRESH_DIR]
// Upgrade staging may override --engine FILE --engine-version X.Y.Z --engine-sha256 HASH.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {WASI} from 'node:wasi';
import {fileURLToPath} from 'node:url';
import {layouts, names as oldNames} from './public-config-layout-v2.mjs';
const sourceDirectory=path.dirname(fileURLToPath(import.meta.url));
const root=path.resolve(sourceDirectory,'../..');
const options={};
for(let index=2;index<process.argv.length;index+=2){
  const option=process.argv[index],value=process.argv[index+1];
  if(!['--project','--out','--engine','--engine-version','--engine-sha256','--compiler'].includes(option)||!value||value.startsWith('--')||option in options)
    throw Error('Unknown, repeated or incomplete option: '+option);
  options[option]=value;
}
const version=JSON.parse(fs.readFileSync(path.join(root,'docs/reference/current-version.json'),'utf8').replace(/^\uFEFF/,''));
const project=path.resolve(options['--project']??root);
const output=path.resolve(options['--out']??path.join(root,'.cache/runtime-catalog',new Date().toISOString().replace(/[:.]/g,'-')));
const engineVersion=options['--engine-version']??version.engine.version;
const engineSha256=options['--engine-sha256']??version.engine.mapEditorWasmSha256;
if(!/^\d+\.\d+\.\d+$/.test(engineVersion)||!/^[a-f0-9]{64}$/.test(engineSha256))throw Error('Invalid engine identity');
if(options['--engine']&&(!options['--engine-version']||!options['--engine-sha256']))throw Error('Explicit engine requires version and SHA256');
const localAppData=process.env.LOCALAPPDATA??path.join(os.homedir(),'AppData/Local');
function findEngine(){
  const assets=path.join(localAppData,'cambc/assets'),prefix='v'+engineVersion.replaceAll('.','_')+'-';
  for(const release of fs.readdirSync(assets).sort())for(const area of ['mapeditor','visualiser']){
    const directory=path.join(assets,release,area,'assets');
    if(!fs.existsSync(directory))continue;
    for(const name of fs.readdirSync(directory).sort())if(name.startsWith(prefix)&&name.endsWith('.wasm')){
      const candidate=path.join(directory,name);
      if(crypto.createHash('sha256').update(fs.readFileSync(candidate)).digest('hex')===engineSha256)return candidate;
    }
  }
  throw Error('Pinned editor engine not found in official asset cache');
}
const engine=path.resolve(options['--engine']??findEngine());
const compiler=path.resolve(options['--compiler']??process.env.ARENA_CLANG??path.join(localAppData,'cambc/wasi-sdk-33/bin/clang++.exe'));
if(fs.existsSync(output))throw Error('Capture output must be a fresh directory: '+output);
fs.mkdirSync(path.dirname(output),{recursive:true});fs.mkdirSync(output);
const check=(ok,message)=>{if(!ok)throw Error(message)};
const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const pin=file=>({path:file,sha256:digest(fs.readFileSync(file)),bytes:fs.statSync(file).size});
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const save=(name,value)=>fs.writeFileSync(path.join(output,name),JSON.stringify(value,null,2)+'\n',{flag:'wx'});
const begun=performance.now();
const manifestFile=path.join(project,'.cambc/map-pool.json');
const manifest=read(manifestFile);
check(manifest.origin==='https://2026a.battlecode.co.uk' && Array.isArray(manifest.maps) &&
  manifest.maps.length>0 && manifest.maps.length===version.mapPool.count &&
  new Set(manifest.maps.map(item=>item.id)).size===manifest.maps.length &&
  new Set(manifest.maps.map(item=>item.file)).size===manifest.maps.length,'Unexpected map manifest');
const maps=manifest.maps.map(item=>{const file=path.join(project,'maps',item.file),identity=pin(file);check(path.basename(item.file)===item.file&&identity.sha256===item.sha256,'Map identity mismatch');return {...item,name:path.basename(item.file,'.map26a'),file,size:identity.bytes,verified:true};});
const fixturePath=path.join(sourceDirectory,'abi.cpp');
const dependencies=[fileURLToPath(import.meta.url),engine,compiler,process.execPath,path.join(sourceDirectory,'public-config-layout-v2.mjs'),fixturePath,path.join(project,'sdk/cpp/cambc.h'),path.join(project,'sdk/cpp/imports.h'),manifestFile,...maps.map(x=>x.file)];
const before=dependencies.map(pin);
check(pin(engine).sha256===engineSha256,'Engine payload mismatch');
const compilerArgs=[fixturePath,'-std=c++23','-O3','-Wall','-Wextra','-Werror','-fsyntax-only','-I'+path.join(project,'sdk/cpp')];
const syntax=spawnSync(compiler,compilerArgs,{cwd:root,encoding:'utf8',windowsHide:true});
save('abi-command.json',{argv:[compiler,...compilerArgs],exitCode:syntax.status});
fs.writeFileSync(path.join(output,'abi.stdout'),syntax.stdout??'',{flag:'wx'});
fs.writeFileSync(path.join(output,'abi.stderr'),syntax.stderr??'',{flag:'wx'});
check(syntax.status===0,'New SDK ABI assertions failed');
const prepared=performance.now();
const module = await WebAssembly.compile(fs.readFileSync(engine));
check(WebAssembly.Module.imports(module).every(x => x.kind === 'function' && x.module === 'wasi_snapshot_preview1'),
  'Unexpected current editor import surface');
const wasi = new WASI({version: 'preview1', args: [], env: {}, preopens: {}});
const instance = await WebAssembly.instantiate(module, {wasi_snapshot_preview1: wasi.wasiImport});
wasi.initialize(instance);
const exports = instance.exports;
const allowed = new Map(Object.entries({Engine_Malloc: 1, Engine_Free: 1, GetLastErrorMsg: 0,
  Version: 0, Game_API_GetSettings: 1, Game_API_GetItemInfo: 2, Game_API_GetChampionPassiveInfo: 2,
  Game_FromMapFileBytes: 4, Game_API_Camp_Design: 3, Game_API_Champion_Design: 3, Game_Destroy: 1}));
const calls = [], resources = [];
for (const [name, arity] of allowed) check(typeof exports[name] === 'function' && exports[name].length === arity,
  'Unsupported public export: ' + name);
const printable = value => typeof value === 'bigint' ? value.toString() : value;
function call(name, ...values) {
  check(allowed.get(name) === values.length, 'Disallowed export');
  const row = {name, args: values.map(printable)}; calls.push(row);
  const result = exports[name](...values); row.result = printable(result) ?? null; return result;
}
function allocate(size) {
  const pointer = call('Engine_Malloc', size + 32);
  check(pointer > 0 && pointer % 8 === 0, 'Bad allocation');
  const row = {pointer, size, freed: false}; resources.push(row);
  new Uint8Array(exports.memory.buffer, pointer, size + 32).fill(0xa5);
  new Uint8Array(exports.memory.buffer, pointer + 16, size).fill(0);
  return row;
}
function bytes(row) {
  const all = new Uint8Array(exports.memory.buffer, row.pointer, row.size + 32);
  check(all.subarray(0, 16).every(x => x === 0xa5) && all.subarray(row.size + 16).every(x => x === 0xa5), 'ABI guard overwritten');
  return Uint8Array.from(all.subarray(16, row.size + 16));
}
function free(row) { if (!row.freed) { call('Engine_Free', row.pointer); row.freed = true; } }
function query(name, argumentsBeforePointer, size) {
  const row = allocate(size);
  try {
    check(call(name, ...argumentsBeforePointer, row.pointer + 16) === 0, 'Public query rejected: ' + name);
    return bytes(row);
  } finally { free(row); }
}
function fields(raw, layout, base = 0) {
  const view = new DataView(raw.buffer, raw.byteOffset, raw.byteLength), out = {};
  const reader = {i32: 'getInt32', u32: 'getUint32', f32: 'getFloat32', f64: 'getFloat64'};
  for (const [name, type, offset] of layout.fields) {
    const value = view[reader[type]](base + offset, true);
    check(Number.isFinite(value), 'Nonfinite ABI field'); out[name] = value;
  }
  return out;
}
function design(raw, layout) {
  const v = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
  const ability = at => {
    const kind = v.getUint32(at, true), f = n => v.getFloat32(at + n, true), i = n => v.getInt32(at + n, true);
    const payload = kind === 0 ? {} : kind === 1 ? {enhanced_attack: v.getUint32(at + 20, true)} :
      kind === 2 ? {range: f(20), modifier: f(24)} :
      kind === 3 ? {range: f(20), radius: f(24), modifier: f(28), duration: i(32)} :
      kind === 4 || kind === 7 ? {range: f(20), modifier: f(24), duration: i(28)} :
      kind === 5 ? {range: f(20), modifier: f(24)} :
      kind === 6 ? {range: f(20), radius: f(24), modifier: f(28), duration: i(32)} : null;
    check(payload !== null && Object.values(payload).every(Number.isFinite), 'Unsupported ability payload');
    return {kind, cooldown: i(4), max_charges: v.getUint32(at + 8, true), targets: v.getUint32(at + 12, true),
      attributes: v.getUint32(at + 16, true), payloadRawHex: Buffer.from(raw.subarray(at + 20, at + 36)).toString('hex'), payload};
  };
  return {type: v.getInt32(0, true), passive: v.getUint8(8), attack_type: v.getUint8(84),
    abilities: [ability(12), ability(48)], stats: fields(raw, layout, 88), rawHex: Buffer.from(raw).toString('hex')};
}
const rawVersion = call('Version');
const [major,minor,patch]=engineVersion.split('.').map(BigInt);
check(typeof rawVersion === 'bigint' && rawVersion === (major|(minor<<8n)|(patch<<16n)), 'Engine version export mismatch');
const engineIdentity = {advertisedVersion: engineVersion, versionExport: {name:'Version', arity:0, returnType:'i64', decimal:rawVersion.toString(), hex:'0x'+rawVersion.toString(16).padStart(16,'0')}, buildHashExported: false, buildHash: null};
let handle = 0n;
const result = {settings: null, items: [], camps: [], champions: [], passives: [], maps};
let failure = null;
try {
  result.settings = fields(query('Game_API_GetSettings', [], 120), layouts.game_settings);
  for (const [id, name] of oldNames.items.entries())
    result.items.push({id, name, ...fields(query('Game_API_GetItemInfo', [id], 40), layouts.item_info)});
  const input = allocate(fs.statSync(maps[0].file).size), out = allocate(8);
  try {
    new Uint8Array(exports.memory.buffer, input.pointer + 16, input.size).set(fs.readFileSync(maps[0].file));
    const status = call('Game_FromMapFileBytes', input.pointer + 16, input.size, out.pointer + 16, 0);
    handle = new DataView(exports.memory.buffer).getBigUint64(out.pointer + 16, true);
    check(status === 0 && handle !== 0n, 'Metadata-only constructor failed');
    bytes(input); bytes(out);
  } finally { free(input); free(out); }
  for (const [id, name] of [...oldNames.camps, 'golem'].entries())
    result.camps.push({id, name, ...design(query('Game_API_Camp_Design', [handle, id], 124), layouts.camp_stats)});
  for (const [id, name] of [...oldNames.champions, 'heart_of_ruin'].entries()) {
    result.champions.push({id, name, ...design(query('Game_API_Champion_Design', [handle, id], 128), layouts.champion_stats)});
    const raw = query('Game_API_GetChampionPassiveInfo', [id], 8), view = new DataView(raw.buffer);
    result.passives.push({id, name, type: view.getUint32(0, true), magnitude: view.getFloat32(4, true)});
  }
} catch (error) { failure = error.message; }
finally {
  if (handle !== 0n) check(call('Game_Destroy', handle) === 0, 'Metadata context destruction failed');
  for (const row of resources) free(row);
  save('execution.json', {calls, resources, failure, metadataContextsCreated: handle === 0n ? 0 : 1,
    metadataContextsDestroyed: handle === 0n ? 0 : 1, runnerInitializations: 0, draftsAdvanced: 0, matchesLaunched: 0});
}
check(JSON.stringify(before) === JSON.stringify(dependencies.map(pin)), 'Capture inputs changed');
const report = {schemaVersion: 'plan5-native-reconciliation-runtime/1', engineIdentity, sdkConstants: {prism_focus_per_gem: 1.5}, status: failure ? 'failed' : 'passed',
  failure, engine: pin(engine), stagedSdk: pin(path.join(project, 'sdk/cpp/cambc.h')), before,
  result, allowedExports: Object.fromEntries(allowed), allResourcesFreed: resources.every(x => x.freed),
  sourceBeforeAfterEqual: true, measurement: {preparationSeconds: (prepared - begun) / 1000,
    captureAndPostcheckSeconds: (performance.now() - prepared) / 1000},
  limitations: ['Editor engine metadata only; native CLI parity, actual gameplay and fuel remain unverified.']};
save('runtime.json', report);
console.log(JSON.stringify({status: report.status, report: path.join(output, 'runtime.json'), maps: maps.length, camps: result.camps.length,
  champions: result.champions.length, items: result.items.length, failure, nativeMatches: 0}));
if (failure) process.exitCode = 1;
