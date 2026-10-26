// Read-only replay decoder regressions on a retained 3.0.4 fixture. Never starts a game or browser.
// node --test tools/tests/test-inspect-strategy-replay.mjs   (decoder tests skip when the official
// visualiser engine is not in the CLI's asset cache)
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath} from 'node:url';
import {PROFILE_SHA256, REPLAY_PROFILES, profileById, profileForDecoder, profileForEngine} from '../replay-profiles.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../..');
const decoder = path.join(root, 'tools/inspect-strategy-replay.mjs');
const replay = path.join(here, 'fixtures/00collision-fundamentals-vs-starter.repl26a');
const paddedReplay = path.join(here, 'fixtures/padded-vision-cli-3.0.4.repl26a');
const geometry = path.join(here, 'fixtures/00collision-cli-3.0.4-geometry.json');
const assets = path.join(process.env.LOCALAPPDATA ?? path.join(os.homedir(), 'AppData/Local'), 'cambc/assets');
const current = profileById('cli-3.0.4');
const available = fs.existsSync(path.join(assets, current.cacheRelease, 'visualiser/assets', current.engineFile));
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

test('retained replay fixtures are present independently of the official engine cache', () => {
  for (const file of [replay, paddedReplay, geometry])
    assert.ok(fs.existsSync(file), `Missing mandatory replay fixture: ${file}`);
  assert.equal(sha(fs.readFileSync(paddedReplay)), '824fd22a0e086a39a0ea25a31596f149b8dfc2be794c9187bb8287083d5612ce');
});

function run(extra, {ok = true} = {}) {
  const geometryArgs = extra.includes('--geometry') ? [] : ['--geometry', geometry];
  const result = spawnSync(process.execPath, ['--disable-warning=ExperimentalWarning', decoder,
    '--replay', replay, ...geometryArgs, ...extra],
    {cwd: root, encoding: 'utf8', windowsHide: true, timeout: 60000, maxBuffer: 16 * 1024 * 1024});
  assert.equal(result.error, undefined);
  if (!ok) { assert.notEqual(result.status, 0); return result.stderr; }
  assert.equal(result.status, 0, result.stderr.slice(0, 1000));
  return JSON.parse(result.stdout);
}
const actor = (id, team, rounds, extra = []) => run(['--actor', String(id), '--team', String(team),
  '--from-round', String(rounds[0]), '--to-round', String(rounds[1]), '--captures', '0', ...extra]);

test('current engine resolves uniquely while retained replay profiles remain distinct', () => {
  assert.equal(profileForEngine(current.engineVersion, current.engineBuildHash).id, current.id);
  assert.equal(profileForDecoder(current.decoderSha256).id, current.id);
  assert.notEqual(current.decoderSha256, profileById('cli-3.0.2').decoderSha256);
  assert.throws(() => profileForEngine(current.engineVersion, profileById('cli-3.0.2').engineBuildHash), /identity differs/);
  const newest = profileById('cli-3.0.5');
  assert.equal(profileForEngine(newest.engineVersion, newest.engineBuildHash).id, newest.id);
  assert.equal(profileForDecoder(newest.decoderSha256).id, newest.id);
  assert.notEqual(newest.decoderSha256, current.decoderSha256);
  assert.deepEqual(REPLAY_PROFILES.map(p => p.id).sort(), ['cli-3.0.2', 'cli-3.0.4', 'cli-3.0.5']);
  assert.throws(() => profileById('cli-2.2.5'), /Unsupported/);
});

test('option checks reject invalid values before reading replay inputs', () => {
  for (const [extra, message] of [[['--actor', '0', '--team', '0', '--captures', '2'], /--captures requires 0 or 1/],
    [['--actor', '0', '--team', '0', '--channels', '2'], /--channels requires 0 or 1/],
    [['--actor', 'all', '--channels', '1', '--from-round', '1', '--to-round', '3'], /one actor and at most 64 rounds/],
    [['--actor', '0', '--team', '0', '--channels', '1', '--from-round', '1', '--to-round', '65'], /one actor and at most 64 rounds/],
    [['--actor', '0'], /--team too unless --actor all/],
    [['--actor', 'all', '--group-keys', 'g_1'], /positions only/],
    [['--actor', '0', '--team', '0', '--nonsense', '1'], /Unknown option/]]) {
    const result = spawnSync(process.execPath, [decoder, '--replay', 'missing', ...extra],
      {cwd: root, encoding: 'utf8', windowsHide: true});
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, message);
  }
});

test('profile comes from the replay header and camp geometry from the retained engine fixture', {skip: !available}, () => {
  const decoded = actor(0, 0, [1, 3]);
  assert.equal(decoded.decoderProfile, 'cli-3.0.4');
  assert.equal(decoded.decoderSha256, current.decoderSha256);
  assert.equal(decoded.profileRegistrySha256, PROFILE_SHA256);
  assert.equal(decoded.mapId, '00collision');  // the only 41x41 pool map
  assert.equal(decoded.geometrySha256, sha(fs.readFileSync(geometry)));
  assert.deepEqual(decoded.samples.map(s => [s.round, s.actor.id, s.actor.team, s.actor.x, s.actor.y]),
    [[1, 0, 0, 3, 1], [2, 0, 0, 4, 2], [3, 0, 0, 5, 3]]);
  assert.match(run(['--actor', '0', '--team', '0', '--profile', 'cli-3.0.2'], {ok: false}), /differs from selected decoder profile/);
  assert.match(run(['--actor', '0', '--team', '0', '--from-round', '1', '--to-round', '3',
    '--channels', '1', '--profile', 'cli-3.0.2'], {ok: false}), /differs from selected decoder profile/);
  const wrong = path.join(os.tmpdir(), `geometry-mismatch-${process.pid}.json`);
  fs.writeFileSync(wrong, JSON.stringify({engineSha256: '0'.repeat(64), maps: []}));
  try { assert.match(run(['--actor', '0', '--team', '0', '--geometry', wrong], {ok: false}), /Geometry decoder profile differs/); }
  finally { fs.rmSync(wrong, {force: true}); }
});

test('fast snapshot path preserves samples and pre-window personal visibility', {skip: !available}, () => {
  const groups = ['--group-keys', 'g_552,g_762', '--map-id', '00collision'];
  const full = run(['--actor', '1', '--team', '0', '--from-round', '650', '--to-round', '660', '--captures', '1', ...groups]);
  const fast = run(['--actor', '1', '--team', '0', '--from-round', '650', '--to-round', '660', '--captures', '0', ...groups]);
  assert.equal(full.stopped, 'round_range_end');
  assert.equal(fast.inspectedSteps, full.inspectedSteps);
  assert.equal(fast.samples.length, 11);
  assert.deepEqual(fast.samples, full.samples);
  assert.deepEqual(fast.captures, []);
  assert.ok(fast.samples.every(s => s.groups.length === 2 && s.groups.every(g => g.lastPersonallyVisibleRound !== null)));
  assert.equal(fast.positioning.mode, 'from-start');
});

test('personal vision uses padded stride and agrees with native bitboard membership', {skip: !available}, () => {
  // The official managed-bitboard implementation is an independent oracle:
  // copy the native private-vision buffer, then query self and a remote tile.
  // This does not repeat the decoder's JavaScript chunk-index calculation.
  let source = fs.readFileSync(decoder, 'utf8');
  const replace = (old, replacement) => {
    assert.equal(source.split(old).length - 1, 1, `decoder seam: ${old}`);
    source = source.replace(old, replacement);
  };
  replace("from './replay-profiles.mjs'", `from ${JSON.stringify(new URL('../replay-profiles.mjs', import.meta.url).href)}`);
  replace("from './replay-interaction-native.mjs'", `from ${JSON.stringify(new URL('../replay-interaction-native.mjs', import.meta.url).href)}`);
  replace("from './replay-interaction-events.mjs'", `from ${JSON.stringify(new URL('../replay-interaction-events.mjs', import.meta.url).href)}`);
  replace("const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');", `const ROOT = ${JSON.stringify(root)};`);
  replace('const tracks = new Map(), lastTrackRound = new Map();', `
const tracks = new Map(), lastTrackRound = new Map();
const oracleOut = e.Engine_Malloc(64);
checked(e.Game_GetBitboardMetadata(game, oracleOut));
const visionOracle = {metadata: {
  chunkedWidth: view().getUint32(oracleOut + 8, true),
  chunkStride: view().getUint32(oracleOut + 16, true)}, points: []};
checked(e.Managed_Bitboard_Create(width, height, oracleOut));
const oracleBoard = view().getBigUint64(oracleOut, true);
checked(e.Managed_Bitboard_GetRawBufferSize(oracleBoard, oracleOut));
const oracleBytes = view().getUint32(oracleOut, true);`);
  replace('    const observedGroups = selected.map(group => {', `
    if (round === 500) {
      checked(e.Managed_Bitboard_ImportRawBuffer(oracleBoard, buffer, oracleBytes));
      for (const point of [{label: 'self', x: me.x, y: me.y}, {label: 'remote', x: 5, y: 17}]) {
        checked(e.Managed_Bitboard_Test(oracleBoard, point.x, point.y, oracleOut));
        visionOracle.points.push({...point, native: !!view().getInt32(oracleOut, true), decoded: sees(point)});
      }
    }
    const observedGroups = selected.map(group => {`);
  replace('const result = { schemaVersion:', 'const result = { visionOracle, schemaVersion:');
  replace('checked(e.Game_Destroy(game));', 'checked(e.Managed_Bitboard_Destroy(oracleBoard));\ne.Engine_Free(oracleOut);\nchecked(e.Game_Destroy(game));');
  const scratch = path.join(root, '.cache/arena-tests/replay-vision');
  fs.mkdirSync(scratch, {recursive: true});
  const generated = path.join(scratch, `native-membership-${process.pid}.mjs`);
  fs.writeFileSync(generated, source);
  try {
    assert.equal(sha(fs.readFileSync(paddedReplay)), '824fd22a0e086a39a0ea25a31596f149b8dfc2be794c9187bb8287083d5612ce');
    const python = process.env.ARENA_PY ?? path.join(os.homedir(), '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe');
    const result = spawnSync(python, ['-B', path.join(root, 'tools/tests/replay-native-oracle.py'), process.execPath,
      '--disable-warning=ExperimentalWarning', generated, '--replay', paddedReplay,
      '--actor', '6', '--team', '1', '--from-round', '500', '--to-round', '500', '--captures', '0'],
      {cwd: root, encoding: 'utf8', windowsHide: true, timeout: 60000, maxBuffer: 1024 * 1024});
    assert.equal(result.error, undefined);
    assert.equal(result.status, 0, result.stderr.slice(0, 1000));
    const actual = JSON.parse(result.stdout).visionOracle;
    assert.deepEqual(actual.metadata, {chunkedWidth: 5, chunkStride: 6});
    assert.deepEqual(actual.points, [
      {label: 'self', x: 30, y: 17, native: true, decoded: true},
      {label: 'remote', x: 5, y: 17, native: false, decoded: false}]);
  } finally { fs.rmSync(generated, {force: true}); }
});

test('observed-round seek preserves all window samples without treating indices as rounds', {skip: !available}, () => {
  const walked = actor(3, 1, [650, 660], ['--seek', '0']);
  const positioned = actor(3, 1, [650, 660], ['--seek', '1']);
  assert.deepEqual(positioned.samples, walked.samples);
  assert.equal(positioned.samples.length, 11);
  assert.equal(positioned.stopped, walked.stopped);
  assert.equal(positioned.positioning.mode, 'observed-round-binary-seek');
  assert.ok(positioned.inspectedSteps < walked.inspectedSteps / 10);
  assert.ok(positioned.positioning.seekOperations <= 33);
});

test('seek preserves first-round and terminal censors and respects tiny work budgets', {skip: !available}, () => {
  const firstRound = actor(6, 1, [0, 8], ['--seek', '0']).samples.filter(row => row.round === 1);
  assert.equal(firstRound.length, 1);
  for (const rounds of [[1, 1], [1128, 1129], [1129, 1130]]) {
    const walked = actor(6, 1, rounds, ['--seek', '0']);
    const positioned = actor(6, 1, rounds, ['--seek', '1']);
    assert.deepEqual(positioned.samples, walked.samples);
    assert.equal(positioned.stopped, walked.stopped);
    if (rounds[0] === 1) assert.deepEqual(positioned.samples, firstRound);
  }
  assert.equal(actor(0, 0, [1129, 1130]).samples.length, 1);  // the winner's last turn start
  assert.equal(actor(3, 1, [1129, 1130]).samples.length, 0);  // the game ended first
  const bounded = actor(0, 0, [650, 660], ['--seek', '1', '--max-steps', '1']);
  assert.equal(bounded.stopped, 'step_cap');
  assert.equal(bounded.inspectedSteps, 1);
  assert.equal(bounded.positioning.mode, 'from-start');
});

test('state-change receipts keep required capture work even when snapshots omit it', {skip: !available}, () => {
  const args = ['--actor', '0', '--team', '0', '--from-round', '40', '--to-round', '60', '--state-changes', '1'];
  const full = run([...args, '--captures', '1']);
  const requestedFast = run([...args, '--captures', '0']);
  const withChannels = run([...args, '--captures', '0', '--channels', '1']);
  assert.equal(full.receiptMode, 'state-changes-v1');
  assert.ok(full.captures.length > 0);
  assert.deepEqual(requestedFast.samples, full.samples);
  assert.deepEqual(requestedFast.captures, full.captures);
  assert.deepEqual(requestedFast.outcomeTransitions, full.outcomeTransitions);
  assert.deepEqual(withChannels.samples, full.samples);
  assert.deepEqual(withChannels.captures, full.captures);
  assert.deepEqual(withChannels.outcomeTransitions, full.outcomeTransitions);
  assert.equal(withChannels.channelQueries.errors, 0);
  assert.equal(withChannels.channelObservationComplete, true);
});

test('native channel receipts distinguish tribute start, completion and another actor banking', {skip: !available}, () => {
  const decoded = actor(0, 0, [40, 46], ['--channels', '1']);
  assert.equal(decoded.channelReceiptMode, 'native-channel-score-v1');
  assert.equal(decoded.channelObservationComplete, true);
  assert.equal(decoded.toolSha256, sha(fs.readFileSync(decoder)));
  assert.equal(decoded.channelQueries.errors, 0);
  assert.ok(decoded.channelQueries.champion > decoded.channelSamples.length);
  assert.ok(decoded.channelQueries.timer > 0);
  assert.equal(decoded.channelQueries.submittedGems, decoded.channelQueries.champion * 2);
  const at = round => decoded.channelSamples.find(s => s.moment.round === round);
  assert.deepEqual([at(42).actor.channelKind, at(42).actor.channelRemainingRounds, at(42).actor.gems,
    at(42).teamSubmittedGems[0]], ['none', null, 18, 0]);
  assert.deepEqual([at(43).actor.channelKind, at(43).actor.channelRemainingRounds, at(43).actor.gems],
    ['tribute', 1, 18]);
  assert.deepEqual([at(44).actor.channelKind, at(44).actor.channelRemainingRounds, at(44).actor.gems,
    at(44).teamSubmittedGems[0]], ['none', null, 0, 18]);
  assert.notDeepEqual([at(45).actor.x, at(45).actor.y], [at(44).actor.x, at(44).actor.y]);

  const start = decoded.channelReceipts.find(r => r.before.actor.channelKind === 'none' &&
    r.after.actor.channelKind === 'tribute');
  assert.deepEqual([start.after.moment.round, start.after.moment.input, start.after.moment.substep,
    start.after.actor.channelRemainingRounds, start.after.actor.gems], [42, 723, 0, 2, 18]);
  const complete = decoded.channelReceipts.find(r => r.before.actor.channelKind === 'tribute' &&
    r.after.actor.channelKind === 'none' && r.after.teamSubmittedGems[0] > r.before.teamSubmittedGems[0]);
  assert.deepEqual([complete.before.moment.round, complete.before.moment.input, complete.before.moment.substep,
    complete.after.moment.substep, complete.before.actor.channelRemainingRounds,
    complete.before.actor.gems, complete.after.actor.gems,
    complete.before.teamSubmittedGems[0], complete.after.teamSubmittedGems[0]], [44, 748, 6, 7, 0, 18, 0, 0, 18]);
  const otherBank = decoded.channelReceipts.find(r => r.before.teamSubmittedGems[0] === 18 && r.after.teamSubmittedGems[0] === 20);
  assert.equal(otherBank.after.moment.round, 46);
  assert.equal(otherBank.after.moment.actor, 2);
  assert.equal(otherBank.before.actor.gems, 0);
  assert.equal(otherBank.after.actor.gems, 0);
  assert.equal(otherBank.after.actor.channelKind, 'none');
});

test('optional channel queries preserve legacy output and identical receipts after seeking', {skip: !available}, () => {
  const baseline = actor(0, 0, [40, 46]);
  const disabled = actor(0, 0, [40, 46], ['--channels', '0']);
  assert.deepEqual(disabled, baseline);
  const positioned = actor(0, 0, [40, 46], ['--channels', '1', '--seek', '1']);
  const walked = actor(0, 0, [40, 46], ['--channels', '1', '--seek', '0']);
  assert.deepEqual(positioned.channelSamples, walked.channelSamples);
  assert.deepEqual(positioned.channelReceipts, walked.channelReceipts);
  assert.ok(positioned.inspectedSteps < walked.inspectedSteps);
  const legacy = {...positioned};
  for (const key of ['channelReceiptMode', 'toolSha256', 'channelABI', 'channelQueries', 'channelSamples',
    'channelReceipts', 'channelObservationComplete', 'channelObservationNote']) delete legacy[key];
  assert.deepEqual(legacy, baseline);
});

test('one all-actor pass equals the per-actor samples of every champion', {skip: !available}, () => {
  const all = run(['--actor', 'all', '--captures', '0']);
  assert.equal(all.actor, 'all');
  assert.equal(all.team, null);
  assert.deepEqual(all.trackFields, ['round', 'x', 'y', 'health', 'gold', 'gems']);
  assert.deepEqual(all.tracks.map(t => [t.id, t.team, t.type]),
    [[0, 0, 11], [1, 0, 10], [2, 0, 2], [3, 1, 3], [4, 1, 4], [5, 1, 11], [6, 1, 6]]);
  for (const track of all.tracks) {
    const single = actor(track.id, track.team, [0, 2000]).samples
      .map(s => [s.round, s.actor.x, s.actor.y, Math.round(s.actor.health * 10) / 10, s.actor.gold, s.actor.gems]);
    assert.deepEqual(track.samples, single, `actor ${track.id}`);
  }
  const cassi = all.tracks.find(t => t.id === 3).samples;
  assert.deepEqual(cassi.find(s => s[0] === 42).slice(1, 4), [-1, -1, 0]);  // dead after round 41
  const window = run(['--actor', 'all', '--team', '1', '--from-round', '500', '--to-round', '510', '--captures', '0']);
  assert.equal(window.positioning.mode, 'observed-round-binary-seek');
  assert.deepEqual(window.tracks.map(t => t.id), [3, 4, 5, 6]);
  for (const track of window.tracks)
    assert.deepEqual(track.samples, all.tracks.find(t => t.id === track.id).samples.filter(s => s[0] >= 500 && s[0] <= 510));
});
