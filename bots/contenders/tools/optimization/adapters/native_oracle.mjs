// Native gameplay oracle. Each case reconstructs and branches a fresh Game; no raw
// memory clone is used. The domain is intentionally narrower than opt::sim.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import zlib from 'node:zlib';
import {WASI} from 'node:wasi';
import {fileURLToPath, pathToFileURL} from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const args = new Map();
for (let i = 2; i < process.argv.length; i += 2) {
  if (!process.argv[i].startsWith('--') || i + 1 >= process.argv.length) throw Error('Expected --name value');
  args.set(process.argv[i].slice(2), process.argv[i + 1]);
}
const ROOT = path.resolve(args.get('repo') || path.join(HERE, '../../../../..'));
const responsePath = args.get('response');
if (!args.has('request') || !responsePath) throw Error('--request and --response are required');
const requestBytes = fs.readFileSync(args.get('request'));
const req = JSON.parse(requestBytes.toString('utf8').replace(/^\uFEFF/, ''));
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const stable = value => JSON.stringify(value);
const save = value => {
  value.request_sha256 = sha(requestBytes);
  fs.mkdirSync(path.dirname(path.resolve(responsePath)), {recursive: true});
  const temporary = responsePath + '.tmp-' + process.pid;
  fs.writeFileSync(temporary, JSON.stringify(value, null, 2) + '\n');
  fs.renameSync(temporary, responsePath);
};
const unsupported = reason => ({
  schema: 'contenders-native-oracle/v1', native: false, supported: false, passed: false,
  scope: 'unsupported', reason, scenario_sha256: req.scenario_sha256 || null,
  trace_sha256: req.trace_sha256 || null,
  capabilities: {domain: 'frozen-ryth-mirror-v1', generic_scenario_restore: false}
});
if (req.kind === 'scenario_validation') {
  save(unsupported('Arbitrary camp/roster scenarios cannot be restored by this oracle. Use native_transition_batch in its declared Ryth mirror domain.'));
  process.exit(0);
}
if (!['identity', 'native_transition_batch'].includes(req.kind)) {
  save(unsupported('Unknown oracle request kind'));
  process.exit(0);
}

function firstExisting(candidates) {
  for (const file of candidates.filter(Boolean)) if (fs.existsSync(file)) return path.resolve(file);
  throw Error('Required native oracle asset missing: ' + candidates.filter(Boolean).join(', '));
}
function payloadOf(bytes) {
  if (bytes.length > 14 && bytes.readBigUInt64LE(0) === 125779999602273n) return bytes;
  let end = -1;
  for (let p = bytes.length - 22; p >= Math.max(0, bytes.length - 65557); --p)
    if (bytes.readUInt32LE(p) === 0x06054b50) { end = p; break; }
  if (end < 0) throw Error('Replay ZIP end missing');
  const count = bytes.readUInt16LE(end + 10);
  let offset = bytes.readUInt32LE(end + 16);
  for (let n = 0; n < count; ++n) {
    if (bytes.readUInt32LE(offset) !== 0x02014b50) throw Error('Replay ZIP directory invalid');
    const method = bytes.readUInt16LE(offset + 10), compressed = bytes.readUInt32LE(offset + 20);
    const size = bytes.readUInt32LE(offset + 24), nl = bytes.readUInt16LE(offset + 28);
    const el = bytes.readUInt16LE(offset + 30), cl = bytes.readUInt16LE(offset + 32);
    const name = bytes.subarray(offset + 46, offset + 46 + nl).toString();
    if (name === 'replay.repl26a') {
      if (![0, 8].includes(method) || size > 256 * 1024 * 1024) throw Error('Unsupported replay payload');
      const local = bytes.readUInt32LE(offset + 42);
      const start = local + 30 + bytes.readUInt16LE(local + 26) + bytes.readUInt16LE(local + 28);
      const data = bytes.subarray(start, start + compressed);
      const result = method === 8 ? zlib.inflateRawSync(data, {maxOutputLength: size}) : Buffer.from(data);
      if (result.length !== size) throw Error('Replay payload length mismatch');
      return result;
    }
    offset += 46 + nl + el + cl;
  }
  throw Error('Replay payload not found');
}
const profilePath = firstExisting([args.get('profiles'), path.join(ROOT, 'tools/replay-profiles.json')]);
const profile = JSON.parse(fs.readFileSync(profilePath)).profiles.find(p => p.id === 'cli-3.0.5');
if (!profile || profile.engineVersion !== '1.0.3') throw Error('Required pinned gameplay profile missing');
const assetBase = process.env.LOCALAPPDATA || path.join(os.homedir(), 'AppData/Local');
const enginePath = firstExisting([
  args.get('engine'), process.env.OPT_GAME_ENGINE, path.join(HERE, '../assets/oracle/gameplay.wasm'),
  path.join(assetBase, 'cambc/assets', profile.cacheRelease, 'visualiser/assets', profile.engineFile)
]);
const replayPath = firstExisting([
  args.get('seed-replay'), path.join(HERE, '../assets/oracle/seed.repl26a'),
  path.join(ROOT, 'results/contenders/foundation/model-lab-import/seed.repl26a')
]);
const samplerPath = firstExisting([args.get('sampler'), path.join(ROOT, 'tools/replay-interaction-native.mjs')]);
const bundle = fs.readFileSync(replayPath), payload = payloadOf(bundle), wasm = fs.readFileSync(enginePath);
if (sha(wasm) !== profile.decoderSha256 || [...payload.subarray(8, 11)].join('.') !== '1.0.3')
  throw Error('Native engine/replay identity mismatch');
const identity = {
  engine_version: profile.engineVersion, engine_sha256: sha(wasm), engine_build: profile.engineBuildHash,
  replay_sha256: sha(bundle), payload_sha256: sha(payload), profile_sha256: sha(fs.readFileSync(profilePath)),
  sampler_sha256: sha(fs.readFileSync(samplerPath)), oracle_sha256: sha(fs.readFileSync(fileURLToPath(import.meta.url))),
  input: 2666, owner: 7, opponent: 2, domain: 'frozen-ryth-mirror-v1'
};
if (req.kind === 'identity') {
  save({schema: 'contenders-native-oracle/v1', native: true, supported: true, identity,
    capabilities: {domain: identity.domain, generic_scenario_restore: false,
      setup: ['positions', 'reduced HP', 'constructed shield'], actions: ['basic', 'cast', 'reset', 'move', 'next_owner']}});
  process.exit(0);
}
const {createInteractionSampler} = await import(pathToFileURL(samplerPath).href);
const wasi = new WASI({version: 'preview1', args: [], env: {}, preopens: {}});
let instance;
const cstr = p => {
  const bytes = new Uint8Array(instance.exports.memory.buffer);
  let q = p; while (q < bytes.length && bytes[q]) ++q;
  return new TextDecoder().decode(bytes.subarray(p, q));
};
instance = await WebAssembly.instantiate(await WebAssembly.compile(wasm), {
  wasi_snapshot_preview1: wasi.wasiImport,
  deep_wasm: {on_assert_fail_impl(a, b, c, d) { throw Error('Native assert: ' + cstr(b) + ':' + c + ' ' + cstr(a) + ' ' + cstr(d)); }}
});
wasi.initialize(instance);
const e = instance.exports, dv = () => new DataView(e.memory.buffer), pointers = [];
const alloc = count => { const p = e.Engine_Malloc(count); if (!p) throw Error('Native allocation failed'); pointers.push(p); return p; };
const output = alloc(512), input = alloc(payload.length), snap = alloc(profile.timelineSnapshotBytes), argumentsPtr = alloc(128);
new Uint8Array(e.memory.buffer, input, payload.length).set(payload);
const entity = (id, offset = 0) => {
  dv().setUint32(argumentsPtr + offset, 1, true); dv().setUint32(argumentsPtr + offset + 4, id, true);
  return argumentsPtr + offset;
};
const position = (x, y) => {
  dv().setInt32(argumentsPtr + 32, x, true); dv().setInt32(argumentsPtr + 36, y, true);
  return argumentsPtr + 32;
};
const checked = (name, params) => {
  const code = e[name](...params);
  if (code !== 0) throw Error(name + ' failed (' + code + '): ' + cstr(e.GetLastErrorMsg()));
};
const cases = req.cases || [];
if (!Array.isArray(cases) || cases.length > 64) throw Error('One oracle batch supports at most 64 cases');
const results = [];

function runCase(specification, index) {
  let game, sampler;
  const actions = specification.actions || [{type: 'basic', target: 'target'}];
  if (!Array.isArray(actions) || actions.length > 32) throw Error('Case action prefix exceeds 32 requests');
  const result = {case_id: specification.id || String(index), specification, native: true,
    supported: true, setup: [], transitions: [], identity, status: 'ok'};
  checked('Game_FromReplayFileBytes', [input, payload.length, output]);
  game = dv().getBigUint64(output, true);
  try {
    const setupKeys = new Set(['id', 'family', 'actor_hp', 'target_hp', 'target_shield',
      'actor_position', 'target_position', 'actions']);
    for (const key of Object.keys(specification))
      if (!setupKeys.has(key)) throw Error('Unsupported native setup field: ' + key);
    const actionKeys = new Set(['type', 'kind', 'owner', 'target', 'slot', 'x', 'y']);
    for (const action of actions) {
      if (!action || typeof action !== 'object' || Array.isArray(action)) throw Error('Actions must be objects');
      for (const key of Object.keys(action))
        if (!actionKeys.has(key)) throw Error('Unsupported native action field: ' + key);
      for (const field of ['owner', 'target'])
        if (action[field] !== undefined && !['actor', 'target', 7, 2].includes(action[field]))
          throw Error('Only the retained pair can own or receive actions');
    }
    checked('Game_GetBitboardMetadata', [game, output]);
    const width = dv().getInt32(output, true), height = dv().getInt32(output + 4, true), stride = dv().getUint32(output + 16, true);
    sampler = createInteractionSampler({e, game, view: dv, profile, width, height, stride, cstring: cstr});
    const header = () => {
      checked('Game_Visualiser_ReadTimelineState', [game, snap]);
      const d = dv(), u = o => d.getUint32(snap + o, true);
      return {round: u(8), phase: u(24), awaiting: u(20), input: u(56), owner: u(60), substep: u(104)};
    };
    const states = () => { header(); return sampler.sample(snap).champions; };
    const project = () => {
      const all = states(), h = header();
      const actors = [7, 2].map(id => {
        const a = all.find(c => c.id === id);
        if (!a || a.type !== 0) throw Error('Seed is not the supported Ryth pair');
        const stun = a.buffs?.find(b => b.type === 1);
        return {id, team: a.team, type: a.type, x: a.x, y: a.y, hp: a.health, max_hp: a.maxHealth,
          strength: a.strength, focus: a.focus, range: a.range, alive: a.alive,
          state_bits: a.stateBits, move: a.availability.move, attack: a.availability.attack, cast: a.availability.cast,
          completed: a.availability.completedTurn, cooldowns: a.cooldowns, shield: a.shield,
          stunned: a.stunned, stun_timer: stun?.timer ?? null, restrictions: a.restrictions,
          abilities: a.abilities, gems: a.gems, gold: a.gold};
      });
      return {...h, actors};
    };
    checked('Game_Visualiser_Timeline_GetMain', [game, output]);
    const main = dv().getBigUint64(output, true);
    checked('Game_Visualiser_Timeline_Seek', [game, main, 2666, 0]);
    result.source = project();
    result.source_sha256 = sha(stable(result.source));
    checked('Game_Visualiser_Timeline_Branch', [game, main, 2666, output]);
    const branch = project();
    if (stable(branch) !== stable(result.source)) throw Error('Fresh branch differs from common prefix');
    if (branch.phase !== 10 || branch.awaiting !== 1 || branch.owner !== 7) throw Error('Wrong owner input boundary');
    const setupCall = (name, params, description) => {
      checked(name, params); result.setup.push({name, description, after: project()});
    };
    const ap = specification.actor_position || [35, 18], tp = specification.target_position || [34, 18];
    for (const p of [ap, tp]) if (!Array.isArray(p) || p.length !== 2 || !p.every(Number.isInteger))
      throw Error('Positions must be integer pairs');
    // Move the target first; starting default target position is distant.
    setupCall('Game_Sandbox_Teleport', [game, entity(2), position(tp[0], tp[1])], 'Construct target position');
    if (ap[0] !== 35 || ap[1] !== 18)
      setupCall('Game_Sandbox_Teleport', [game, entity(7), position(ap[0], ap[1])], 'Construct actor position');
    for (const [field, id, source] of [['actor_hp', 7, 2], ['target_hp', 2, 7]]) {
      const hp = specification[field];
      if (hp === undefined) continue;
      const current = project().actors.find(a => a.id === id).hp;
      if (!Number.isFinite(hp) || hp <= 0 || hp > current) throw Error('HP setup can only reduce living seed HP');
      if (hp < current)
        setupCall('Game_Sandbox_Damage', [game, entity(source, 0), entity(id, 16), current - hp], 'Construct reduced HP');
    }
    const shield = specification.target_shield || 0;
    if (!Number.isFinite(shield) || shield < 0 || shield > 10000) throw Error('Invalid constructed shield');
    if (shield > 0)
      setupCall('Game_Sandbox_Buff', [game, entity(2), 0, 2, shield], 'Construct shield (not a champion cast)');
    result.before = project();
    const resolve = v => v === 'actor' ? 7 : v === 'target' || v === undefined ? 2 : v;
    for (const action of actions) {
      const before = project(), type = action.type || action.kind;
      let code = 0, applied = true;
      if (type === 'next_owner') {
        const desired = resolve(action.owner || 'target');
        checked('Game_Runner_EndTurn', [game, 0]);
        let found = false;
        for (let n = 0; n < 2000; ++n) {
          checked('Game_Runner_Step', [game]);
          const h = header();
          if (h.phase === 10 && h.awaiting) {
            if (h.owner === desired) { found = true; break; }
            checked('Game_Runner_EndTurn', [game, 0]);
          }
        }
        if (!found) throw Error('Requested owner input not reached within bounded prefix');
      } else {
        const expectedOwner = resolve(action.owner || 'actor');
        if (before.owner !== expectedOwner) throw Error('Action owner differs from native current input');
        if (type === 'basic') code = e.Game_Action_Attack(game, entity(resolve(action.target)));
        else if (type === 'cast' || type === 'reset') {
          const slot = type === 'reset' ? 1 : Number(action.slot || 0);
          if (![0, 1].includes(slot)) throw Error('Only actual Ryth slots 0/1 are supported');
          const target = resolve(action.target);
          const targetState = before.actors.find(a => a.id === target);
          const x = action.x ?? targetState?.x ?? 0, y = action.y ?? targetState?.y ?? 0;
          new Uint8Array(e.memory.buffer, output, 8).fill(0xa5);
          let targetPtr = entity(target);
          if (slot === 1) {
            dv().setUint32(targetPtr, 0, true); dv().setUint32(targetPtr + 4, 0, true);
          }
          code = e.Game_Action_Cast(game, slot, targetPtr, position(slot === 1 ? -1 : x, slot === 1 ? -1 : y), output);
          applied = code === 0 && dv().getInt32(output, true) !== 0;
          if (new Uint8Array(e.memory.buffer, output + 4, 4).some(v => v !== 0xa5))
            throw Error('Cast output canary altered');
        } else if (type === 'move') {
          if (!Number.isInteger(action.x) || !Number.isInteger(action.y)) throw Error('Move requires integer coordinates');
          new Uint8Array(e.memory.buffer, output, 8).fill(0xa5);
          code = e.Game_Action_Move(game, action.x, action.y, output);
          applied = code === 0 && dv().getInt32(output, true) !== 0;
          if (new Uint8Array(e.memory.buffer, output + 4, 4).some(v => v !== 0xa5))
            throw Error('Move output canary altered');
        } else throw Error('Unsupported native action: ' + type);
      }
      if (code !== 0) applied = false;
      result.transitions.push({action, code, applied, error: code ? cstr(e.GetLastErrorMsg()) : null,
        before, after: project(), comparison_scope: type === 'next_owner' ? 'observed owner boundary, not predicted hidden world' : 'immediate two-actor projection'});
    }
    result.after = project();
    result.query_status = sampler.metadata();
    if (result.query_status.errors !== 0) throw Error('Native sampler had query failures');
  } catch (error) {
    result.status = 'unsupported_or_setup_error'; result.supported = false;
    result.failure = String(error);
  } finally {
    if (sampler) sampler.destroy();
    checked('Game_Destroy', [game]);
  }
  return result;
}
try {
  for (let i = 0; i < cases.length; ++i) results.push(runCase(cases[i], i));
  const prefixes = new Set(results.filter(r => r.source_sha256).map(r => r.source_sha256));
  if (prefixes.size > 1) throw Error('Independent fresh cases disagree on their unmutated source prefix');
  save({schema: 'contenders-native-oracle/v1', native: true, supported: true, identity, results,
    reconstructed_games: results.length, independent_source_prefixes: prefixes.size,
    original_seed_unchanged: sha(fs.readFileSync(replayPath)) === identity.replay_sha256,
    capabilities: {domain: identity.domain, generic_scenario_restore: false, omniscient_oracle: true,
      limitation: 'Controlled suffixes of one existing Ryth mirror input. Other actors/world continue during next_owner. No policy information permission or full-game fidelity claim.'}});
} finally {
  for (const p of pointers) e.Engine_Free(p);
}
