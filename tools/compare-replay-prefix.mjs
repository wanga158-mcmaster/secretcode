// Read-only, from-start comparison of official exposed world snapshots.
// Run native invocations under arena.core.Slots(1); this tool never calls Action,
// Runner, branch or seek APIs. Equality says nothing about guest private memory,
// guest fuel, logs or engine internals absent from these official snapshots.
// CLI: node tools/compare-replay-prefix.mjs --spec SPEC.json --sha256 SPEC_HASH
// SPEC has exactly schemaVersion:1, profile:'cli-3.0.4' or 'cli-3.0.5', toolSha256,
// profileModuleSha256, profileRegistrySha256, officialAssets, baseline,
// candidate, stop, maxSteps and maxRound. Baseline/candidate are repo-contained
// {path,sha256}; officialAssets maps the three names below to absolute pinned
// {path,sha256} descriptors. stop requires {round,actor,phase:10,stepPhase:14,
// kind:3,awaitingInput:true}, with optional exact input/substep. It is the first
// diagnostic flush's owner-start, supplied by the producer's pinned receipt.
// maxRound bounds traversal; it may exceed stop.round. Only equal confirms the
// entire requested prefix. Missing/censored flush identity remains unresolved.
// There is no internal slot acquisition: the caller holds core.Slots(1).
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import {fileURLToPath} from 'node:url';
import {WASI} from 'node:wasi';
import {PROFILE_PATH, PROFILE_MODULE_PATH, PROFILE_SHA256, PROFILE_MODULE_SHA256, profileById, verifyProfileInputs} from './replay-profiles.mjs';

const SELF = fileURLToPath(import.meta.url);
const MASK = Object.freeze([48, 56]);
const TIMELINE_BYTES = 40152;
const STATE_SIZES = Object.freeze([184760]);
// Exact cached official frontend sources establish both layouts, the common
// header, spectator {team:2,champion:0xffffffff}, and timeline ID [48,56).
const FRONTENDS = Object.freeze({
  'cli-3.0.4': Object.freeze({
    'PanelChrome-BXlTYAwV.js': '960affde6198366d2347bd21f43efec5bc06acc6cb0c53e5f7f96dbf87f027e6',
    'index-CexarSMK.js': '8179bff9a6ada21a429f21b88f7e239377da217c600efa3dac6229b863133508',
    'v1_0_2-BIJCVwNu.wasm': '742919b4454b427912eece99e5675752791fe5d83167b2fee1766c908e9cccee',
  }),
  // The 3.0.5 frontend retains the exact PanelChrome snapshot decoders and
  // spectator perspective; its native ABI exports confirm the same sizes.
  'cli-3.0.5': Object.freeze({
    'PanelChrome-BXlTYAwV.js': '960affde6198366d2347bd21f43efec5bc06acc6cb0c53e5f7f96dbf87f027e6',
    'index-C8ICv-Xx.js': 'b80358b2189444187c38880ee7218d84b4a013a1b379b93470659c4c6b2c4a24',
    'v1_0_3-DRqNt1t2.wasm': '029ecd290dc400e75418178ec386d72cbd7d9f2fc2a2e451bf594ca23566df48',
  }),
});
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const need = (condition, message) => { if (!condition) throw Error(message); };
const object = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const exact = (value, keys) => object(value) && Object.keys(value).sort().join('|') === [...keys].sort().join('|');
const integer = (value, low, high) => Number.isSafeInteger(value) && value >= low && value <= high;
const validHash = value => typeof value === 'string' && /^[0-9a-f]{64}$/.test(value);

export function validateLimits(spec) {
  need(object(spec), 'comparison spec must be an object');
  need(integer(spec.maxSteps, 1, 1000000), 'maxSteps must be 1..1000000');
  need(integer(spec.maxRound, 1, 2000), 'maxRound must be 1..2000');
  const stop = spec.stop;
  const required = ['round', 'actor', 'phase', 'stepPhase', 'kind', 'awaitingInput'];
  need(object(stop) && required.every(k => Object.hasOwn(stop, k)) &&
    Object.keys(stop).every(k => [...required, 'input', 'substep'].includes(k)), 'stop must be an explicit owner-start moment');
  need(integer(stop.round, 1, spec.maxRound) && integer(stop.actor, 0, 15) &&
    stop.phase === 10 && stop.stepPhase === 14 && stop.kind === 3 && stop.awaitingInput === true,
  'stop must identify an awaiting champion owner-start before its action');
  for (const key of ['input', 'substep']) if (Object.hasOwn(stop, key))
    need(integer(stop[key], 0, 0xffffffff), 'invalid stop ' + key);
  return stop;
}

export function moment(bytes) {
  need(Buffer.isBuffer(bytes) && bytes.length >= 120 && bytes.readUInt32LE(0) === 1 &&
    bytes.readUInt32LE(4) === bytes.length, 'invalid official snapshot header/size');
  const u = offset => bytes.readUInt32LE(offset);
  need(u(20) <= 1, 'invalid awaiting-input flag');
  return {round:u(8), actor:u(60), phase:u(24), stepPhase:u(64), kind:u(68),
    awaitingInput:u(20) === 1, input:u(56), substep:u(104)};
}

function normalized(bytes) {
  const result = Buffer.from(bytes);
  result.fill(0, ...MASK);
  return result;
}

function capture(reader) {
  const frame = reader.read();
  need(object(frame) && Buffer.isBuffer(frame.state) && Buffer.isBuffer(frame.timeline), 'reader did not return snapshot buffers');
  need(STATE_SIZES.includes(frame.state.length) && frame.timeline.length === TIMELINE_BYTES, 'unsupported official snapshot size');
  const where = moment(frame.timeline);
  need(JSON.stringify(moment(frame.state)) === JSON.stringify(where), 'state and timeline moments disagree');
  return {moment:where, state:normalized(frame.state), timeline:normalized(frame.timeline)};
}

function difference(left, right) {
  for (const layout of ['state', 'timeline']) {
    const a = left[layout], b = right[layout];
    if (a.equals(b)) continue;
    let count = 0;
    const offsets = [];
    for (let i = 0; i < Math.max(a.length, b.length); i++) if (a[i] !== b[i]) {
      count++;
      if (offsets.length < 16) offsets.push({offset:i, baseline:a[i] ?? null, candidate:b[i] ?? null});
    }
    return {layout, differingBytes:count, offsets, baselineBytes:a.length, candidateBytes:b.length};
  }
  return null;
}

export function compareReaders(baseline, candidate, limits) {
  const stop = validateLimits(limits);
  const hashes = Object.fromEntries(['baseline', 'candidate'].map(role => [role,
    Object.fromEntries(['state', 'timeline'].map(k => [k, crypto.createHash('sha256')]))]));
  let previous, frames = 0;
  const result = (status, extra) => ({schemaVersion:1, readOnly:true, status, pass:status === 'equal',
    comparedFrames:frames, mask:[...MASK], maskMeaning:'official timeline identity only',
    prefixSha256:Object.fromEntries(Object.entries(hashes).map(([role, values]) => [role,
      Object.fromEntries(Object.entries(values).map(([k, hash]) => [k, hash.digest('hex')]))])),
    scope:'Official full spectator ReadState and ReadTimelineState bytes from initial state. Only an equal result confirms the complete included boundary; no guest private memory/fuel or unrepresented internals.', ...extra});
  for (let step = 0; step < limits.maxSteps; step++) {
    const a = capture(baseline), b = capture(candidate);
    if (step === 0) need(a.moment.input === 0 && a.moment.substep === 0 &&
      b.moment.input === 0 && b.moment.substep === 0, 'replay did not start at initial input/substep');
    for (const [role, frame] of [['baseline', a], ['candidate', b]])
      for (const layout of ['state', 'timeline']) hashes[role][layout].update(frame[layout]);
    frames++;
    const delta = difference(a, b);
    if (delta) return result('different', {firstDifference:{frame:step, baselineMoment:a.moment, candidateMoment:b.moment, ...delta}});
    if (previous) need(!a.state.equals(previous.state) || !a.timeline.equals(previous.timeline), 'ReplayNext made no observable progress');
    if (Object.entries(stop).every(([key, value]) => a.moment[key] === value))
      return result('equal', {boundary:a.moment, boundaryIncluded:true, stoppedBeforeBoundaryAdvance:true,
        snapshotBytes:{state:a.state.length, timeline:a.timeline.length}});
    // Draft rounds count picks and reset before gameplay. They are not a reason
    // to skip frames or to treat an unreachable gameplay boundary as reached.
    need(a.moment.phase === 12 || a.moment.round <= limits.maxRound, 'round cap reached before boundary');
    if (a.moment.phase !== 12) need(a.moment.round <= stop.round, 'declared boundary was not reached');
    if (step + 1 === limits.maxSteps) throw Error('step cap reached before boundary');
    previous = a;
    const left = baseline.advance(), right = candidate.advance();
    need(typeof left === 'boolean' && typeof right === 'boolean', 'invalid ReplayNext result');
    need(left === right, 'replays ended asymmetrically before boundary');
    need(left, 'replay ended before boundary');
  }
  throw Error('boundary was not reached');
}

export function pinnedFile(record, {root, external = false} = {}) {
  need(exact(record, ['path', 'sha256']) && typeof record.path === 'string' && validHash(record.sha256), 'malformed pinned file descriptor');
  need(root && path.isAbsolute(root), 'repository root must be absolute');
  if (external) need(path.isAbsolute(record.path), 'official asset path must be absolute');
  const declared = path.resolve(root, record.path);
  const resolved = fs.realpathSync(declared);
  if (!external) {
    const relative = path.relative(fs.realpathSync(root), resolved);
    need(relative !== '..' && !relative.startsWith('..' + path.sep) && !path.isAbsolute(relative), 'pinned file escaped repository');
  }
  const bytes = fs.readFileSync(resolved);
  need(sha(bytes) === record.sha256, 'pinned file hash mismatch: ' + record.path);
  return {path:resolved, sha256:record.sha256, bytes};
}

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const value of bytes) {
    crc ^= value;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

export function replayPayload(bytes) {
  need(Buffer.isBuffer(bytes) && bytes.length >= 22 && bytes.readUInt32LE(0) === 0x04034b50, 'expected a CLI replay ZIP bundle');
  let end = -1;
  for (let at = bytes.length - 22; at >= Math.max(0, bytes.length - 65557); at--)
    if (bytes.readUInt32LE(at) === 0x06054b50) { end = at; break; }
  need(end >= 0 && !bytes.readUInt16LE(end + 4) && !bytes.readUInt16LE(end + 6), 'unsupported ZIP end/disk structure');
  const count = bytes.readUInt16LE(end + 10);
  let offset = bytes.readUInt32LE(end + 16), found;
  need(count !== 0xffff && offset !== 0xffffffff, 'ZIP64 is unsupported');
  for (let index = 0; index < count; index++) {
    need(offset + 46 <= end && bytes.readUInt32LE(offset) === 0x02014b50, 'malformed ZIP directory');
    const flags = bytes.readUInt16LE(offset + 8), method = bytes.readUInt16LE(offset + 10);
    const crc = bytes.readUInt32LE(offset + 16), compressed = bytes.readUInt32LE(offset + 20), size = bytes.readUInt32LE(offset + 24);
    const nameLen = bytes.readUInt16LE(offset + 28), extraLen = bytes.readUInt16LE(offset + 30), commentLen = bytes.readUInt16LE(offset + 32);
    need(offset + 46 + nameLen + extraLen + commentLen <= end, 'truncated ZIP directory');
    const name = bytes.subarray(offset + 46, offset + 46 + nameLen).toString('utf8');
    if (name === 'replay.repl26a') {
      need(!found && !(flags & 1) && size > 0 && size <= 256 * 1024 * 1024 && [0, 8].includes(method), 'unsupported replay ZIP entry');
      const local = bytes.readUInt32LE(offset + 42);
      need(local + 30 <= offset && bytes.readUInt32LE(local) === 0x04034b50, 'malformed ZIP local header');
      const data = local + 30 + bytes.readUInt16LE(local + 26) + bytes.readUInt16LE(local + 28);
      need(data + compressed <= offset, 'truncated compressed replay');
      const input = bytes.subarray(data, data + compressed);
      found = method === 8 ? zlib.inflateRawSync(input, {maxOutputLength:size}) : Buffer.from(input);
      need(found.length === size && crc32(found) === crc, 'replay entry length/CRC mismatch');
    }
    offset += 46 + nameLen + extraLen + commentLen;
  }
  need(found, 'replay payload is missing');
  return found;
}

async function nativeReader(wasm, payload, profile) {
  const wasi = new WASI({version:'preview1', args:[], env:{}, preopens:{}});
  let e, game, closed = false;
  const pointers = [];
  const view = () => new DataView(e.memory.buffer);
  const cstring = ptr => {
    const bytes = new Uint8Array(e.memory.buffer);
    let end = ptr;
    while (end < bytes.length && bytes[end]) end++;
    return new TextDecoder().decode(bytes.subarray(ptr, end));
  };
  const checked = code => { if (code !== 0) throw Error('official decoder status ' + code + ': ' + cstring(e.GetLastErrorMsg())); };
  const close = () => {
    if (closed) return;
    closed = true;
    try { if (game !== undefined) checked(e.Game_Destroy(game)); }
    finally { for (const pointer of pointers) e.Engine_Free(pointer); }
  };
  try {
    const instance = await WebAssembly.instantiate(await WebAssembly.compile(wasm), {
      wasi_snapshot_preview1:wasi.wasiImport,
      deep_wasm:{on_assert_fail_impl(a, b, c, d) { throw Error('official decoder assertion: ' + cstring(a)); }},
    });
    wasi.initialize(instance);
    e = instance.exports;
    for (const [name, arity] of Object.entries({Engine_Malloc:1, Engine_Free:1, Game_FromReplayFileBytes:3,
      Game_Visualiser_SnapshotSize:0, Game_Visualiser_TimelineSnapshotSize:0, Game_Visualiser_ReadState:3,
      Game_Visualiser_ReadTimelineState:2, Game_Visualiser_Timeline_ReplayNext:2, Game_Visualiser_Timeline_GetMain:2,
      Game_Visualiser_Timeline_Info:5, Game_Destroy:1}))
      need(typeof e[name] === 'function' && e[name].length === arity, 'unsupported official ABI: ' + name);
    const stateBytes = e.Game_Visualiser_SnapshotSize();
    need(STATE_SIZES.includes(stateBytes) && e.Game_Visualiser_TimelineSnapshotSize() === TIMELINE_BYTES &&
      profile.timelineSnapshotBytes === TIMELINE_BYTES, 'unsupported official snapshot layout');
    const alloc = size => {
      const pointer = e.Engine_Malloc(size);
      need(pointer, 'official allocation failed'); pointers.push(pointer); return pointer;
    };
    const out = alloc(64), state = alloc(stateBytes), timeline = alloc(TIMELINE_BYTES), perspective = alloc(8), input = alloc(payload.length);
    new Uint8Array(e.memory.buffer, input, payload.length).set(payload);
    checked(e.Game_FromReplayFileBytes(input, payload.length, out));
    game = view().getBigUint64(out, true);
    view().setUint32(perspective, 2, true); view().setUint32(perspective + 4, 0xffffffff, true);
    checked(e.Game_Visualiser_Timeline_GetMain(game, out));
    const handle = view().getBigUint64(out, true);
    checked(e.Game_Visualiser_Timeline_Info(game, handle, out, out + 8, out + 16));
    const inputs = view().getUint32(out, true);
    let last;
    return {
      read() {
        checked(e.Game_Visualiser_ReadTimelineState(game, timeline));
        checked(e.Game_Visualiser_ReadState(game, perspective, state));
        const result = {timeline:Buffer.from(new Uint8Array(e.memory.buffer, timeline, TIMELINE_BYTES)),
          state:Buffer.from(new Uint8Array(e.memory.buffer, state, stateBytes))};
        last = moment(result.timeline);
        return result;
      },
      advance() {
        const code = e.Game_Visualiser_Timeline_ReplayNext(game, 1);
        if (code && last?.input >= inputs) return false;
        checked(code); return true;
      }, close,
    };
  } catch (error) { close(); throw error; }
}

export async function runSpec(spec, {root = process.cwd(), toolPath = SELF} = {}) {
  need(exact(spec, ['schemaVersion', 'profile', 'toolSha256', 'profileModuleSha256', 'profileRegistrySha256',
    'officialAssets', 'baseline', 'candidate', 'stop', 'maxSteps', 'maxRound']), 'unexpected comparison spec fields');
  validateLimits(spec);
  need(spec.schemaVersion === 1 && Object.hasOwn(FRONTENDS, spec.profile), 'unsupported comparison schema/profile');
  need(validHash(spec.toolSha256) && sha(fs.readFileSync(toolPath)) === spec.toolSha256, 'comparison tool bytes changed');
  verifyProfileInputs();
  need(spec.profileModuleSha256 === PROFILE_MODULE_SHA256 && spec.profileRegistrySha256 === PROFILE_SHA256, 'replay profile inputs changed');
  const profile = profileById(spec.profile);
  const assets = FRONTENDS[spec.profile];
  const relative = value => {
    const result = path.relative(root, value);
    need(result !== '..' && !result.startsWith('..' + path.sep) && !path.isAbsolute(result), 'provenance path escaped repository');
    return result.replaceAll('\\', '/');
  };
  need(exact(spec.officialAssets, Object.keys(assets)), 'official asset declarations differ from supported frontend');
  const pinnedAssets = {};
  for (const [name, expected] of Object.entries(assets)) {
    const asset = pinnedFile(spec.officialAssets[name], {root, external:true});
    need(path.basename(asset.path) === name && asset.sha256 === expected, 'official asset identity differs: ' + name);
    pinnedAssets[name] = asset;
  }
  const engine = pinnedAssets[profile.engineFile];
  need(engine?.sha256 === profile.decoderSha256, 'decoder and profile identities disagree');
  const replays = {};
  for (const role of ['baseline', 'candidate']) {
    const file = pinnedFile(spec[role], {root});
    const payload = replayPayload(file.bytes);
    need(payload.length >= 14 && payload.readBigUInt64LE(0) === 125779999602273n, 'unknown replay header');
    const version = offset => [...payload.subarray(offset, offset + 3)].join('.');
    need(version(8) === profile.engineVersion && profile.replayFormatVersions.includes(version(11)), 'replay engine/format differs from pinned profile');
    replays[role] = {...file, payload, payloadSha256:sha(payload)};
  }
  let baseline, candidate;
  try {
    baseline = await nativeReader(engine.bytes, replays.baseline.payload, profile);
    candidate = await nativeReader(engine.bytes, replays.candidate.payload, profile);
    const result = compareReaders(baseline, candidate, spec);
    return {...result, requestedBoundary:spec.stop, maxSteps:spec.maxSteps, maxRound:spec.maxRound,
      provenance:{tool:{path:relative(toolPath), sha256:spec.toolSha256}, profile:spec.profile,
        profileModuleSha256:PROFILE_MODULE_SHA256, profileRegistrySha256:PROFILE_SHA256,
        profileModule:{path:relative(PROFILE_MODULE_PATH), sha256:PROFILE_MODULE_SHA256},
        profileRegistry:{path:relative(PROFILE_PATH), sha256:PROFILE_SHA256},
        officialAssets:spec.officialAssets, node:{version:process.version, executable:process.execPath},
        replays:Object.fromEntries(Object.entries(replays).map(([role, file]) => [role,
          {path:relative(file.path), sha256:file.sha256, payloadSha256:file.payloadSha256}])),
        positioning:'from-replay-initial-state; no seek/branch/action/runner calls', slots:'caller-owned arena.core.Slots(1)'}};
  } finally { try { candidate?.close(); } finally { baseline?.close(); } }
}

async function main(argv) {
  need(argv.length === 4 && argv[0] === '--spec' && argv[2] === '--sha256' && validHash(argv[3]), 'usage: compare-replay-prefix.mjs --spec FILE --sha256 HASH');
  const file = pinnedFile({path:argv[1], sha256:argv[3]}, {root:process.cwd()});
  const result = await runSpec(JSON.parse(file.bytes.toString('utf8')));
  result.spec = {path:path.relative(process.cwd(), file.path).replaceAll('\\', '/'), sha256:file.sha256};
  console.log(JSON.stringify(result, null, 2));
  process.exitCode = result.pass ? 0 : 1;
}
if (process.argv[1] && path.resolve(process.argv[1]) === SELF)
  main(process.argv.slice(2)).catch(error => { console.log(JSON.stringify({schemaVersion:1, readOnly:true,
    status:'incomplete', pass:false, reason:error.message})); process.exitCode = 1; });
