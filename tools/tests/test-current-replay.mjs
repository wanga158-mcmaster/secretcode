// Current vendor bundle ABI and native replay canary. Uses cached official assets and a
// retained arena fixture; never starts a game, browser, or network request.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {WASI} from 'node:wasi';
import {profileById, profileForEngine} from '../replay-profiles.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const profile = profileById('cli-3.0.5');
const assets = path.join(process.env.LOCALAPPDATA ?? path.join(os.homedir(), 'AppData/Local'), 'cambc/assets');
const engine = path.join(assets, profile.cacheRelease, 'visualiser/assets', profile.engineFile);
const replay = path.join(root, 'tools/tests/fixtures/00collision-cli-3.0.5.repl26a');
const statsFile = path.join(root, 'tools/tests/fixtures/00collision-cli-3.0.5.json');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

test('current official engine retains the verified version and timeline ABI', {skip: !fs.existsSync(engine)}, async () => {
  const bytes = fs.readFileSync(engine);
  assert.equal(sha(bytes), profile.decoderSha256);
  const module = await WebAssembly.compile(bytes);
  assert.ok(WebAssembly.Module.imports(module).every(item =>
    item.kind === 'function' && item.module === 'wasi_snapshot_preview1'));
  const wasi = new WASI({version: 'preview1', args: [], env: {}, preopens: {}});
  const instance = await WebAssembly.instantiate(module, {wasi_snapshot_preview1: wasi.wasiImport});
  wasi.initialize(instance);
  const e = instance.exports;
  const packed = Number(e.Version());
  assert.equal([packed & 255, (packed >>> 8) & 255, (packed >>> 16) & 255].join('.'), profile.engineVersion);
  assert.equal(e.Game_Visualiser_TimelineSnapshotSize(), profile.timelineSnapshotBytes);
  assert.equal(e.Game_FromReplayFileBytes.length, 3);
  assert.equal(e.Game_Destroy.length, 1);
});

test('current native fixture resolves its engine and decodes bounded channel samples',
  {skip: !fs.existsSync(engine) || !fs.existsSync(replay) || !fs.existsSync(statsFile)}, () => {
    const stats = JSON.parse(fs.readFileSync(statsFile, 'utf8'));
    assert.equal(profileForEngine(stats.match.engineVersion, stats.match.engineBuildHash).id, profile.id);
    assert.deepEqual(stats.diagnostics.botFaults, {teamA: 0, teamB: 0});
    for (const event of stats.events.filter(item => ['botFault', 'fuelExhausted'].includes(item.kind)))
      assert.equal(event.entries.length, 0);
    const result = spawnSync(process.execPath, ['--disable-warning=ExperimentalWarning',
      path.join(root, 'tools/inspect-strategy-replay.mjs'), '--replay', replay,
      '--actor', '0', '--team', '0', '--from-round', '1', '--to-round', '3',
      '--channels', '1', '--captures', '0', '--max-steps', '2000'],
    {cwd: root, encoding: 'utf8', windowsHide: true, timeout: 60000, maxBuffer: 4 * 1024 * 1024});
    assert.equal(result.error, undefined);
    assert.equal(result.status, 0, result.stderr);
    const decoded = JSON.parse(result.stdout);
    assert.equal(decoded.decoderProfile, profile.id);
    assert.equal(decoded.decoderSha256, profile.decoderSha256);
    assert.equal(decoded.geometrySha256, sha(fs.readFileSync(path.join(root, 'maps/geometry.json'))));
    assert.equal(decoded.mapId, '00collision');
    assert.ok(decoded.samples.length > 0);
    assert.equal(decoded.channelQueries.errors, 0);
    assert.equal(decoded.channelObservationComplete, true);
    assert.ok(decoded.channelSamples.length > 0);
    assert.notEqual(decoded.stopped, 'step_cap');
  });
