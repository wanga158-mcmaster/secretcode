// Read saved replay state through the pinned official visualiser WASM.
// No bot, Game_Runner_* or Game_Action_* function is invoked. This loads and
// seeks existing history only; it neither starts a match nor changes its file.
//
//   node tools/inspect-strategy-replay.mjs --replay <saved.repl26a> --actor 3 --team 1
//     [--from-round 70] [--to-round 125] [--group-keys g_1365,g_1206] [--map-id 00collision]
//   node tools/inspect-strategy-replay.mjs --replay <saved.repl26a> --actor all [--team 0|1]
// JSON goes to stdout. Team is absolute 0=A / 1=B; actor is the global native actor ID (the
// statistics roster id). Samples are the selected actor's ready owner-turn starts, before its
// actions. `--actor all` records every champion in one pass as compact tracks
// [[round, x, y, health, gold, gems], ...] (dead champions sample at x = y = -1, health 0).
// The decoder profile comes from the replay header unless --profile is given; camp geometry
// defaults to maps/geometry.json when it was exported by the same engine (--geometry overrides).
// Capture records use the official omniscient timeline's reward transitions;
// per-actor visibility and live group queries are reported separately.
// --channels 1 adds native channel/score samples and per-substep before/after
// receipts for one actor in a window of at most 64 rounds. It queries the
// selected actor through its own perspective, plus both public team scores.
// Channel kind comes from SDK state flags; no channel has a null timer.
// --locks 1 adds bounded personal altar-lock queries to current-profile traces.
// --trace 1 --actors 4,5,9 --from-round 45 --to-round 60 adds chronological
// multi-champion native interactions, exact state references and linked summaries.
// --format jsonl emits metadata followed by events. See docs/replay-analysis.md.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import zlib from 'node:zlib';
import {fileURLToPath} from 'node:url';
import { WASI } from 'node:wasi';
import {spawnSync} from 'node:child_process';
import {PROFILE_MODULE_SHA256, PROFILE_SHA256, REPLAY_PROFILES, profileById, verifyProfileInputs} from './replay-profiles.mjs';
import {createInteractionSampler} from './replay-interaction-native.mjs';
import {createInteractionTrace} from './replay-interaction-events.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const DEFAULT_GEOMETRY = path.join(ROOT, 'maps/geometry.json');
const ASSETS = path.join(process.env.LOCALAPPDATA ?? path.join(os.homedir(), 'AppData/Local'), 'cambc/assets');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const args = new Map();
for (let i = 2; i < process.argv.length; i += 2) {
  const key = process.argv[i], value = process.argv[i + 1];
  if (!key?.startsWith('--') || value === undefined || args.has(key)) throw Error('Expected unique --key value arguments.');
  args.set(key, value);
}
const allowed = new Set(['--replay', '--actor', '--actors', '--team', '--teams', '--targets', '--kinds', '--trace', '--format', '--max-events', '--max-bytes', '--nearby-radius', '--from-round', '--to-round', '--group-keys', '--map-id', '--geometry', '--max-steps', '--state-changes', '--captures', '--seek', '--profile', '--channels', '--locks']);
for (const key of args.keys()) if (!allowed.has(key)) throw Error(`Unknown option ${key}`);
const trace = args.get('--trace') === '1';
if (args.has('--trace') && !['0', '1'].includes(args.get('--trace'))) throw Error('--trace requires 0 or 1.');
const format = args.get('--format') ?? 'json';
if (!['json', 'jsonl'].includes(format) || (format === 'jsonl' && !trace)) throw Error('--format jsonl requires --trace 1.');
if (trace && !args.has('--actor')) args.set('--actor', 'all');
const allActors = args.get('--actor') === 'all';
if (!args.has('--replay') || !args.has('--actor') || (!allActors && !args.has('--team')))
  throw Error('--replay and --actor are required (--team too unless --actor all).');
const integer = (key, fallback, max) => {
  const value = Number(args.get(key) ?? fallback);
  if (!Number.isSafeInteger(value) || value < 0 || value > max) throw Error(`Invalid ${key}`);
  return value;
};
const actor = allActors ? -1 : integer('--actor', 0, 15);
const team = allActors && !args.has('--team') ? -1 : integer('--team', 0, 1);
const from = integer('--from-round', 0, 2000), to = integer('--to-round', 2000, 2000);
const maxSteps = integer('--max-steps', 1000000, 10000000);
const maxEvents = integer('--max-events', 20000, 100000), maxBytes = integer('--max-bytes', 16777216, 134217728);
const nearbyRadius = integer('--nearby-radius', 4, 16);
const list = (key, max) => {
  if (!args.has(key)) return null;
  const parts = args.get(key).split(',');
  if (parts.some(value => !/^\d+$/.test(value))) throw Error(`Invalid ${key}`);
  const values = parts.map(Number);
  if (!values.length || values.some(value => !Number.isSafeInteger(value) || value < 0 || value > max) || new Set(values).size !== values.length)
    throw Error(`Invalid ${key}`);
  return new Set(values);
};
const traceActors = list('--actors', 15) ?? (allActors ? null : new Set([actor]));
const traceTeams = list('--teams', 1) ?? (team < 0 ? null : new Set([team]));
const traceTargets = args.has('--targets') ? new Set(args.get('--targets').split(',')) : null;
const traceKinds = args.has('--kinds') ? new Set(args.get('--kinds').split(',')) : null;
if (traceTargets && [...traceTargets].some(value => !/^(?:(?:champion|camp|group|score):)?\d+$|^tile:\d+:\d+$/.test(value)))
  throw Error('Invalid --targets (use champion:ID, camp:ID, group:ID, score:TEAM or tile:X:Y).');
const eventKinds = new Set(['phase', 'respawn', 'capture', 'tick', 'move', 'attack', 'cast', 'input', 'end-turn', 'set-priority',
  'unknown-10', 'recall', 'tribute', 'shop', 'ward', 'consumable', 'cancel-channel', 'sandbox', 'gems-submitted', 'draft',
  'altar-lock', 'teleport-channel', 'teleport-arrival', 'movement-blocked', 'resign', 'sell-item', 'turn-rollover', 'bot-error',
  'movement', 'channel', 'turn', 'economy', 'damage', 'healing', 'combat-state', 'death', 'visibility']);
if (traceKinds && [...traceKinds].some(value => !eventKinds.has(value) && !/^(?:[0-9]|1[0-9]|2[0-7])$/.test(value)))
  throw Error('Invalid --kinds (use native kind numbers 0-27 or documented event/tag names).');
if (!trace && ['--actors', '--teams', '--targets', '--kinds', '--max-events', '--max-bytes', '--nearby-radius'].some(key => args.has(key)))
  throw Error('Interaction filters and bounds require --trace 1.');
if (trace && (to - from + 1 > 64 || maxEvents < 1 || maxBytes < 1024)) throw Error('Trace requires at most 64 rounds and positive output bounds (at least 1024 bytes).');
if (to < from || !maxSteps) throw Error('Invalid round range or step cap.');
const stateChanges = args.has('--state-changes');
if (args.has('--channels') && !['0', '1'].includes(args.get('--channels')))
  throw Error('--channels requires 0 or 1.');
const channels = args.get('--channels') === '1';
if (args.has('--locks') && !['0', '1'].includes(args.get('--locks'))) throw Error('--locks requires 0 or 1.');
const personalLocks = args.get('--locks') === '1';
if (personalLocks && !trace) throw Error('Personal altar locks require --trace 1.');
if (channels && (allActors || to - from + 1 > 64))
  throw Error('Channel receipts require one actor and at most 64 rounds.');
if (args.has('--captures') && !['0', '1'].includes(args.get('--captures')))
  throw Error('--captures requires 0 or 1.');
if (args.has('--seek') && !['0', '1'].includes(args.get('--seek')))
  throw Error('--seek requires 0 or 1.');
const captureTransitions = stateChanges || args.get('--captures') === '1' || (!trace && args.get('--captures') !== '0');
if (stateChanges && (args.get('--state-changes') !== '1' || to - from + 1 > 64))
  throw Error('State-change receipts require --state-changes 1 and at most 64 rounds.');
if (allActors && (stateChanges || args.get('--group-keys')))
  throw Error('--actor all records positions only; group observations and state changes need one actor.');
const groupKeys = (args.get('--group-keys') ?? '').split(',').filter(Boolean);
// The Node inspector always enters the same machine-wide pool as arena. The
// Python bridge holds the slot until this process exits, including decoder errors.
if (process.env.ARENA_REPLAY_SLOT !== '1') {
  const python = process.env.ARENA_PY ?? path.join(os.homedir(), '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe');
  const bridge = spawnSync(python, ['-B', path.join(ROOT, 'tools/replay-native-slot.py'), process.execPath,
    ...process.execArgv, fileURLToPath(import.meta.url), ...process.argv.slice(2)], {stdio: 'inherit', windowsHide: true});
  if (bridge.error) throw Error(`Cannot enter arena replay slot pool: ${bridge.error.message}`);
  process.exit(bridge.status ?? 1);
}
const replayPath = path.resolve(args.get('--replay'));
const traceSourceNames = ['inspect-strategy-replay.mjs', 'replay-interaction-native.mjs', 'replay-interaction-events.mjs', 'replay-native-slot.py'];
const traceSourceHashes = trace ? Object.fromEntries(traceSourceNames.map(name => [name, sha(fs.readFileSync(path.join(ROOT, 'tools', name)))])) : null;
const bundle = fs.readFileSync(replayPath);

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const value of bytes) {
    crc ^= value;
    for (let bit = 0; bit < 8; ++bit) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function replayPayload(bytes) {
  if (bytes.readUInt32LE(0) !== 0x04034b50) throw Error('Expected the CLI replay ZIP bundle.');
  let end = -1;
  for (let at = bytes.length - 22; at >= Math.max(0, bytes.length - 65557); --at)
    if (bytes.readUInt32LE(at) === 0x06054b50) { end = at; break; }
  if (end < 0 || bytes.readUInt16LE(end + 4) || bytes.readUInt16LE(end + 6)) throw Error('Unsupported ZIP end/disk structure.');
  const count = bytes.readUInt16LE(end + 10);
  let offset = bytes.readUInt32LE(end + 16), found;
  if (count === 0xffff || offset === 0xffffffff) throw Error('ZIP64 is unsupported.');
  for (let index = 0; index < count; ++index) {
    if (bytes.readUInt32LE(offset) !== 0x02014b50) throw Error('Malformed ZIP directory.');
    const flags = bytes.readUInt16LE(offset + 8), method = bytes.readUInt16LE(offset + 10);
    const crc = bytes.readUInt32LE(offset + 16), compressed = bytes.readUInt32LE(offset + 20), size = bytes.readUInt32LE(offset + 24);
    const nameLen = bytes.readUInt16LE(offset + 28), extraLen = bytes.readUInt16LE(offset + 30), commentLen = bytes.readUInt16LE(offset + 32);
    const name = bytes.subarray(offset + 46, offset + 46 + nameLen).toString('utf8');
    if (name === 'replay.repl26a') {
      if (found || (flags & 1) || size > 256 * 1024 * 1024 || ![0, 8].includes(method)) throw Error('Unsupported replay entry.');
      const local = bytes.readUInt32LE(offset + 42);
      if (bytes.readUInt32LE(local) !== 0x04034b50) throw Error('Malformed ZIP local header.');
      const data = local + 30 + bytes.readUInt16LE(local + 26) + bytes.readUInt16LE(local + 28);
      const input = bytes.subarray(data, data + compressed);
      if (input.length !== compressed) throw Error('Truncated compressed replay.');
      found = method === 8 ? zlib.inflateRawSync(input, { maxOutputLength: size }) : Buffer.from(input);
      if (found.length !== size || crc32(found) !== crc) throw Error('Replay entry length/CRC mismatch.');
    }
    offset += 46 + nameLen + extraLen + commentLen;
  }
  if (!found) throw Error('Replay payload is missing.');
  return found;
}
const payload = replayPayload(bundle);
if (payload.length < 14 || payload.readBigUInt64LE(0) !== 125779999602273n)
  throw Error('Unknown or truncated replay header.');
const headerVersion = offset => [...payload.subarray(offset, offset + 3)].join('.');
const replayEngine = headerVersion(8), replayFormat = headerVersion(11);
let profile;
if (args.has('--profile')) profile = profileById(args.get('--profile'));
else {
  const matching = REPLAY_PROFILES.filter(item => item.engineVersion === replayEngine && item.replayFormatVersions.includes(replayFormat));
  if (matching.length !== 1) throw Error(`No unique decoder profile for replay engine ${replayEngine} format ${replayFormat}; pass --profile.`);
  profile = matching[0];
}
if (replayEngine !== profile.engineVersion || !profile.replayFormatVersions.includes(replayFormat))
  throw Error('Replay engine/format version differs from selected decoder profile.');
// Geometry: explicit --geometry must match the decoder; the default export is used only when
// it was produced by the same engine (otherwise queries stay actor-only).
let geometryPath = args.has('--geometry') ? path.resolve(args.get('--geometry')) : null;
const implicitGeometry = !geometryPath;
let geometryBytes = null, geometry = null;
if (geometryPath || fs.existsSync(DEFAULT_GEOMETRY)) {
  geometryPath ??= DEFAULT_GEOMETRY;
  geometryBytes = fs.readFileSync(geometryPath);
  geometry = JSON.parse(geometryBytes.toString('utf8').replace(/^﻿/, ''));
  if (geometry.engineSha256 !== profile.decoderSha256) {
    if (!implicitGeometry) throw Error('Geometry decoder profile differs from the replay decoder.');
    geometryPath = geometryBytes = geometry = null;
  }
}
if (groupKeys.length && !geometry) throw Error('Group queries need a geometry export from the replay engine (--geometry).');
const pin = profile.decoderSha256;
const engineCandidates = [path.join(ASSETS, profile.cacheRelease, 'visualiser/assets', profile.engineFile),
  path.join(ASSETS, profile.cacheRelease, 'mapeditor/assets', profile.engineFile), geometry?.enginePath].filter(Boolean);
const enginePath = engineCandidates.find(candidate => fs.existsSync(candidate));
if (!enginePath) throw Error(`Official decoder ${profile.engineFile} is not cached; run any cambc ${profile.cliVersion} command that fetches assets.`);
const wasm = fs.readFileSync(enginePath);
if (sha(wasm) !== pin) throw Error('Pinned official decoder identity differs.');
const wasi = new WASI({ version: 'preview1', args: [], env: {}, preopens: {} });
let instance;
const cstring = ptr => {
  const bytes = new Uint8Array(instance.exports.memory.buffer);
  let end = ptr;
  while (end < bytes.length && bytes[end]) ++end;
  return new TextDecoder().decode(bytes.subarray(ptr, end));
};
instance = await WebAssembly.instantiate(await WebAssembly.compile(wasm), {
  wasi_snapshot_preview1: wasi.wasiImport,
  deep_wasm: { on_assert_fail_impl(a, b, c, d) { throw Error(`Official decoder assertion ${cstring(b)}:${c} ${cstring(a)} ${cstring(d)}`); } }
});
wasi.initialize(instance);
const e = instance.exports, view = () => new DataView(e.memory.buffer);
const checked = code => { if (code !== 0) throw Error(`Official decoder ${code}: ${cstring(e.GetLastErrorMsg())}`); };
if (e.Game_Visualiser_TimelineSnapshotSize() !== profile.timelineSnapshotBytes) throw Error('Unsupported official timeline snapshot layout.');
const out = e.Engine_Malloc(64), snapshot = e.Engine_Malloc(profile.timelineSnapshotBytes), perspective = e.Engine_Malloc(8), input = e.Engine_Malloc(payload.length);
new Uint8Array(e.memory.buffer, input, payload.length).set(payload);
checked(e.Game_FromReplayFileBytes(input, payload.length, out));
const game = view().getBigUint64(out, true);
// These exact read-only export signatures and output widths were verified from
// the pinned WASM type section, SDK declarations and native canary-buffer reads.
// Champion_Get is the 44-byte SDK champion_info record, not a timeline offset.
const channelABI = {Game_API_Champion_Get: {parameters: 4, outputBytes: 44},
  Game_API_Champion_GetChannelTime: {parameters: 4, outputBytes: 4},
  Game_API_GetSubmittedGemCount: {parameters: 3, outputBytes: 4}};
const channelQueries = {champion: 0, timer: 0, submittedGems: 0, errors: 0};
let channelOut = 0;
if (channels) {
  for (const [name, shape] of Object.entries(channelABI))
    if (typeof e[name] !== 'function' || e[name].length !== shape.parameters)
      throw Error(`Unsupported read-only channel ABI: ${name}`);
  channelOut = e.Engine_Malloc(64);
}
const channelQuery = (name, params, counter) => {
  new Uint8Array(e.memory.buffer, channelOut, 64).fill(0xa5);
  channelQueries[counter]++;
  const code = e[name](...params, channelOut);
  if (code !== 0) {
    channelQueries.errors++;
    throw Error(`Native channel query ${name} failed (${code}, query errors ${channelQueries.errors}): ${cstring(e.GetLastErrorMsg())}`);
  }
  if (new Uint8Array(e.memory.buffer, channelOut + channelABI[name].outputBytes,
    64 - channelABI[name].outputBytes).some(byte => byte !== 0xa5)) {
    channelQueries.errors++;
    throw Error(`Native channel query output layout differs: ${name} (query errors ${channelQueries.errors})`);
  }
  return view();
};
const nativeChannelState = (champion, moment) => {
  const info = channelQuery('Game_API_Champion_Get', [game, perspective, champion.id], 'champion');
  if (info.getUint32(channelOut, true) !== champion.team || info.getInt32(channelOut + 4, true) !== champion.type ||
      info.getInt32(channelOut + 8, true) !== champion.x || info.getInt32(channelOut + 12, true) !== champion.y ||
      info.getFloat32(channelOut + 16, true) !== champion.health)
    throw Error('Native channel champion query differs from the pinned timeline snapshot.');
  const stateBits = info.getUint32(channelOut + 40, true);
  if (stateBits & ~0x1bff) throw Error('Unsupported native champion state flags.');
  const active = [[2, 'recall'], [4, 'tribute'], [4096, 'teleport']].filter(([bit]) => stateBits & bit);
  if (active.length > 1) throw Error('Conflicting native champion channel flags.');
  const channelKind = active[0]?.[1] ?? 'none';
  let channelRemainingRounds = null;
  if (active.length) {
    const timer = channelQuery('Game_API_Champion_GetChannelTime', [game, perspective, champion.id], 'timer');
    channelRemainingRounds = timer.getInt32(channelOut, true);
    if (channelRemainingRounds < 0) throw Error('Invalid native channel timer.');
  }
  const teamSubmittedGems = [0, 1].map(side => {
    const score = channelQuery('Game_API_GetSubmittedGemCount', [game, side], 'submittedGems');
    return score.getUint32(channelOut, true);
  });
  // Sequence is a host scan counter and differs after seeking. Receipts use
  // native input/substep identities so walking and seeking are comparable.
  const nativeMoment = {...moment};
  delete nativeMoment.sequence;
  return {moment: nativeMoment, actor: {...champion, stateBits, channelKind, channelRemainingRounds}, teamSubmittedGems};
};
checked(e.Game_GetBitboardMetadata(game, out));
// +8/+12 are chunk counts. Raw boards use the padded chunk stride at +16.
const width = view().getInt32(out, true), height = view().getInt32(out + 4, true), stride = view().getUint32(out + 16, true);
// Map ids in the export are pool names (00reverse_spiral); file stems (00reverse-spiral) also match.
const wantedMap = args.get('--map-id');
const sameMap = id => !wantedMap || id === wantedMap || id === wantedMap.replace(/-/g, '_');
let maps = geometry ? geometry.maps.filter(item => item.width === width && item.height === height && sameMap(item.id)
  && groupKeys.every(key => item.groups.some(group => group.key === key))) : [];
if (maps.length !== 1) {
  if (geometry && (!implicitGeometry || groupKeys.length))
    throw Error('Geometry selection is missing or ambiguous; specify --map-id and valid group keys.');
  geometryPath = geometryBytes = null;
  maps = [{id: wantedMap ?? null, width, height, groups: []}];
}
const map = maps[0];
const selected = groupKeys.map(key => {
  const group = map.groups.find(item => item.key === key);
  if (!group || !Number.isInteger(group.nativeId)) throw Error(`Missing physical/native group ${key}`);
  return group;
});
view().setUint32(perspective, Math.max(team, 0), true); view().setUint32(perspective + 4, Math.max(actor, 0), true);
checked(e.Game_Visualiser_Timeline_GetMain(game, out));
const timeline = view().getBigUint64(out, true);
checked(e.Game_Visualiser_Timeline_Info(game, timeline, out, out + 8, out + 16));
const timelineInputs = view().getUint32(out, true);
checked(e.Game_Visualiser_Timeline_Seek(game, timeline, 0, 0));
let seekOperations = 0, positionedInput = 0;
// Input indices are not rounds. Compare the actual decoded round at each seek
// and begin before the window so its first owner-start observation is retained.
// Personal visibility/capture histories must still be walked from their start.
if (!captureTransitions && !selected.length && from > 0 && maxSteps >= 64 && args.get('--seek') !== '0') {
  const roundAtInput = inputPosition => {
    checked(e.Game_Visualiser_Timeline_Seek(game, timeline, inputPosition, 0));
    checked(e.Game_Visualiser_ReadTimelineState(game, snapshot));
    seekOperations++;
    const d = view();
    if (d.getUint32(snapshot, true) !== 1 || d.getUint32(snapshot + 4, true) !== profile.timelineSnapshotBytes)
      throw Error('Official timeline ABI mismatch during seek.');
    // Draft rounds count picks and reset before gameplay. Only gameplay round
    // numbers are ordered; every draft position precedes the requested window.
    return d.getUint32(snapshot + 24, true) === 12 ? -1 : d.getUint32(snapshot + 8, true);
  };
  let low = 0, high = timelineInputs;
  while (low + 1 < high) {
    const middle = low + Math.floor((high - low) / 2);
    if (roundAtInput(middle) < from) low = middle;
    else high = middle;
  }
  positionedInput = low;
  if (roundAtInput(low) >= from) throw Error('Timeline seek did not position before requested round.');
}
const samples = [], captures = [], transitions = [], lastVisible = new Map();
const tracks = new Map(), lastTrackRound = new Map();
const interactionSampler = trace ? createInteractionSampler({e, game, view, profile, width, height, stride, checked, cstring,
  personalLocks, lockActors: traceActors, lockTeams: traceTeams}) : null;
const interactionTrace = trace ? createInteractionTrace({actors: traceActors, teams: traceTeams,
  targets: traceTargets, kinds: traceKinds, from, to, maxEvents, maxBytes, nearbyRadius,
  replaySha256: sha(bundle), geometry: geometryBytes ? map : null, width, height, stride}) : null;
let lastTraceMoment = null;
const channelSamples = [], channelReceipts = [];
let previousChannel;
let previous, lastSampleRound = -1, steps = 0, stopped = 'step_cap', lastSignature;
for (; steps + seekOperations < maxSteps; ++steps) {
  checked(e.Game_Visualiser_ReadTimelineState(game, snapshot));
  const d = view(), u = offset => d.getUint32(snapshot + offset, true), i = offset => d.getInt32(snapshot + offset, true);
  if (u(0) !== 1 || u(4) !== 40152) throw Error('Official timeline ABI mismatch.');
  const round = u(8), inputPosition = u(56), kind = u(68), substep = u(104);
  const moment = {round, input: inputPosition, substep, sequence: steps,
    kind, phase: u(64), actor: u(60), awaitingInput: u(20) === 1};
  if (trace && u(24) !== 12) lastTraceMoment = {round, input: inputPosition, substep,
    phase: u(24), awaitingInput: u(20) === 1, step: {round: u(96), input: u(100), substep, phase: u(64), kind}};
  if (u(24) !== 12 && (trace ? u(96) : round) > to) { stopped = 'round_range_end'; break; }
  if (u(172) > 16 || u(180) > 500) throw Error('Invalid official timeline counts.');
  // Phase 14 is champion-start processing; kind 3 is its tick. Awaiting-input
  // verifies that the owner is ready, rather than midway through channel ticks.
  const ownerStart = u(20) === 1 && kind === 3 && u(64) === 14;
  const champion = p => ({ id: u(p + 4), team: u(p + 8), type: u(p + 12), x: i(p + 16), y: i(p + 20), health: d.getFloat32(snapshot + p + 24, true), gold: u(p + 36), gems: u(p + 40) });
  if (allActors && ownerStart && round >= from && round <= to && lastTrackRound.get(u(60)) !== round) {
    for (let index = 0; index < u(172); ++index) {
      const p = 196 + index * 252;
      if (u(p + 4) !== u(60)) continue;
      const c = champion(p);
      if (team < 0 || c.team === team) {
        if (!tracks.has(c.id)) tracks.set(c.id, {id: c.id, team: c.team, type: c.type, samples: []});
        tracks.get(c.id).samples.push([round, c.x, c.y, Math.round(c.health * 10) / 10, c.gold, c.gems]);
      }
      break;
    }
    lastTrackRound.set(u(60), round);
  }
  // Snapshot callers omit capture receipts. Read detailed state only at the
  // selected owner's sampling point; preserve pre-window personal visibility.
  const readySample = !allActors && u(60) === actor && ownerStart && lastSampleRound !== round;
  const needChampions = captureTransitions || (readySample && (round >= from || selected.length > 0)) ||
    (channels && u(24) !== 12 && round >= Math.max(0, from - 1));
  const champions = [];
  for (let index = 0; needChampions && index < u(172); ++index) {
    const p = 196 + index * 252;
    if (!captureTransitions && (u(p + 4) !== actor || u(p + 8) !== team)) continue;
    champions.push(champion(p));
  }
  const groups = [];
  for (let index = 0; captureTransitions && index < u(180); ++index) groups.push({ rewardAvailable: !!u(18228 + index * 8), captureTeam: u(18232 + index * 8) });
  if (captureTransitions && round >= from && previous && kind === 2) {
    for (let index = 0; index < groups.length; ++index) {
      if (!previous.groups[index]?.rewardAvailable || groups[index].rewardAvailable) continue;
      const group = map.groups.find(item => item.nativeId === index);
      const recipientDeltas = champions.flatMap(champion => {
        const old = previous.champions.find(item => item.id === champion.id);
        if (!old) return [];
        const gold = champion.gold - old.gold, gems = champion.gems - old.gems;
        return gold > 0 || gems > 0 ? [{ actor: champion.id, team: champion.team, gold, gems }] : [];
      });
      const capture = { round, inputPosition, substep, nativeGroupId: index, group: group?.key ?? null,
        capturingTeam: previous.groups[index].captureTeam, recipientDeltas };
      if (stateChanges) {
        const eligible = group ? previous.champions.filter(champion => champion.health > 0 &&
          champion.team === capture.capturingTeam && group.members.some(tile => tile.x === champion.x && tile.y === champion.y)) : null;
        capture.before = {moment: previous.moment, champions: previous.champions};
        capture.after = {moment, champions};
        capture.eligibleActors = eligible?.map(champion => champion.id) ?? null;
        capture.rewardBefore = previous.groups[index].rewardAvailable;
        capture.rewardAfter = groups[index].rewardAvailable;
      }
      captures.push(capture);
    }
  }
  const me = allActors ? undefined : champions.find(item => item.id === actor && item.team === team);
  if (stateChanges && round >= from && previous && me) {
    const before = previous.champions.find(item => item.id === actor && item.team === team);
    if (before && ['x', 'y', 'health', 'gold', 'gems'].some(key => before[key] !== me[key])) {
      if (transitions.length >= 256) { stopped = 'transition_cap'; break; }
      transitions.push({before: {moment: previous.moment, state: before}, after: {moment, state: me}});
    }
  }
  if (me && readySample) {
    checked(e.Game_Visualiser_GetVisionBuffer(game, perspective, out));
    const buffer = view().getUint32(out, true), bits = view();
    const sees = position => {
      const { x, y } = position;
      const chunk = (y >> 3) * stride + (x >> 3), bit = ((y & 7) << 3) + (x & 7);
      return (bits.getBigUint64(buffer + chunk * 8, true) & (1n << BigInt(bit))) !== 0n;
    };
    const observedGroups = selected.map(group => {
      const visible = group.members.some(sees);
      if (visible) lastVisible.set(group.key, round);
      const last = lastVisible.get(group.key);
      let state = null, rawTimer = null;
      if (visible) {
        checked(e.Game_API_CampGroup_GetState(game, perspective, group.nativeId, out));
        state = view().getUint32(out, true);
        if (state === 3) {
          checked(e.Game_API_CampGroup_GetCooldown(game, perspective, group.nativeId, out)); rawTimer = view().getInt32(out, true);
        }
      }
      const anchor = group.members[0];
      return { group: group.key, nativeGroupId: group.nativeId, personallyVisible: visible,
        lastPersonallyVisibleRound: last ?? null, observationAge: last === undefined ? null : round - last,
        arrivalEstimate: round + Math.max(Math.abs(me.x - anchor.x), Math.abs(me.y - anchor.y)),
        state, rawTimer, nominalDueIfObservedRespawning: state === 3 ? round + Math.max(0, rawTimer) : null };
    });
    if (round >= from && round <= to) samples.push({ round, inputPosition, substep, actor: me, groups: observedGroups });
    lastSampleRound = round;
  }
  previous = captureTransitions ? { champions, groups, moment } : undefined;
  const signature = trace ? [inputPosition, substep, round, u(24), kind, u(76), u(64), u(20), u(96), u(100)].join('/') :
    `${inputPosition}/${substep}/${round}/${u(24)}/${kind}/${u(76)}`;
  if (signature === lastSignature) { stopped = 'timeline_end'; break; }
  lastSignature = signature;
  if (channels && u(24) !== 12 && round >= Math.max(0, from - 1) && me) {
    const current = nativeChannelState(me, moment);
    if (readySample && round >= from) channelSamples.push(current);
    if (round >= from && previousChannel &&
        (['x', 'y', 'health', 'gold', 'gems', 'stateBits', 'channelKind', 'channelRemainingRounds']
          .some(key => previousChannel.actor[key] !== current.actor[key]) ||
          current.teamSubmittedGems.some((value, side) => value !== previousChannel.teamSubmittedGems[side]))) {
      if (channelReceipts.length >= 512) { stopped = 'channel_receipt_cap'; break; }
      channelReceipts.push({before: previousChannel, after: current});
    }
    previousChannel = current;
  }
  if (trace && u(24) !== 12 && round >= Math.max(0, from - 1)) {
    const frame = interactionSampler.sample(snapshot);
    lastTraceMoment = frame.moment;
    if (interactionTrace.push(frame) === false) { stopped = 'trace_output_cap'; break; }
  }
  const code = e.Game_Visualiser_Timeline_ReplayNext(game, 1);
  if (code && inputPosition >= timelineInputs) { stopped = 'timeline_end'; break; }
  checked(code);
}
const result = { schemaVersion: trace ? '2.0' : '1.1', readOnly: true, decoder: 'official-visualiser-timeline-snapshot-40152',
  decoderSha256: pin, replayPath, replaySha256: sha(bundle), payloadSha256: sha(payload), geometryPath,
  decoderProfile: profile.id, profileRegistrySha256: PROFILE_SHA256,
  profileModuleSha256: PROFILE_MODULE_SHA256,
  geometrySha256: geometryBytes ? sha(geometryBytes) : null, mapId: map.id,
  mapIdentityNote: geometryBytes ? 'Map label/native group mapping comes from the selected geometry export (matched by dimensions and --map-id).' :
    'Actor-only snapshot; map label is caller supplied and no camp geometry or source-map hash is inferred.',
  width, height, actor: allActors ? 'all' : actor, team: team < 0 ? null : team, fromRound: from, toRound: to,
  timelineInputs, inspectedSteps: steps + seekOperations, stopped, samples, captures,
  positioning: {mode: seekOperations ? 'observed-round-binary-seek' : 'from-start',
    seekOperations, positionedInput, scannedSteps: steps},
  ...(allActors ? {trackFields: ['round', 'x', 'y', 'health', 'gold', 'gems'],
    tracks: [...tracks.values()].sort((a, b) => a.id - b.id)} : {}),
  ...(stateChanges ? {receiptMode: 'state-changes-v1', mapSha256: map.mapSha256, outcomeTransitions: transitions,
    outcomeOnly: true, fitSupported: false, fullGameSealed: false} : {}),
  ...(channels ? {channelReceiptMode: 'native-channel-score-v1',
    toolSha256: sha(fs.readFileSync(fileURLToPath(import.meta.url))), channelABI,
    channelQueries, channelSamples, channelReceipts,
    channelObservationComplete: channelQueries.champion > 0 && ['round_range_end', 'timeline_end'].includes(stopped),
    channelObservationNote: 'Native selected-actor state and both absolute team scores; no-channel timer is null, not a failed query converted to zero. Receipts may include a before-state from the preceding round.'} : {}),
  ...(trace ? {toolSha256: traceSourceHashes['inspect-strategy-replay.mjs'],
    helperSha256: Object.fromEntries(Object.entries(traceSourceHashes).filter(([name]) => name !== 'inspect-strategy-replay.mjs')),
    engineVersion: replayEngine, replayFormatVersion: replayFormat,
    interactions: interactionTrace.finish({stopped, lastMoment: lastTraceMoment, requestedFromRound: from,
      requestedToRound: to, native: interactionSampler.metadata()})} : {}) };
// Tracks are large; keep them compact. Single-actor output stays indented.
verifyProfileInputs();
if (trace && traceSourceNames.some(name => sha(fs.readFileSync(path.join(ROOT, 'tools', name))) !== traceSourceHashes[name]))
  throw Error('Replay inspector inputs changed during decoding; restart inspection.');
if (format === 'jsonl') {
  const {interactions, ...metadata} = result;
  const {events, ...traceMetadata} = interactions;
  console.log(JSON.stringify({record: 'metadata', ...metadata, interactions: traceMetadata}));
  for (const event of events) console.log(JSON.stringify({record: 'event', ...event}));
} else console.log(allActors || trace ? JSON.stringify(result) : JSON.stringify(result, null, 2));
interactionSampler?.destroy();
checked(e.Game_Destroy(game));
for (const ptr of [out, snapshot, perspective, input]) e.Engine_Free(ptr);
if (channelOut) e.Engine_Free(channelOut);
