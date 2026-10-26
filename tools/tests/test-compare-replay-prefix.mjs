// Pure comparator tests. Native same-replay/different-replay controls are run
// separately under arena.core.Slots(1); importing this file starts no native API.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath, pathToFileURL} from 'node:url';
const moduleURL = process.env.REPLAY_PREFIX_TEST_MODULE ? pathToFileURL(path.resolve(process.env.REPLAY_PREFIX_TEST_MODULE)) :
  new URL('../compare-replay-prefix.mjs', import.meta.url);
const {compareReaders, moment, validateLimits, pinnedFile, replayPayload, runSpec} = await import(moduleURL);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const stop = {round:2, actor:2, phase:10, stepPhase:14, kind:3, awaitingInput:true};
const limits = () => ({stop:{...stop}, maxSteps:20, maxRound:3});

function frame(round, input, extra = {}) {
  const timeline = Buffer.alloc(40152), state = Buffer.alloc(184760);
  const values = {0:1, 4:40152, 8:round, 20:1, 24:10, 48:1, 56:input, 60:2, 64:14, 68:3, 104:0};
  for (const [offset, value] of Object.entries(values)) timeline.writeUInt32LE(value, Number(offset));
  timeline.copy(state, 0, 0, 120); state.writeUInt32LE(state.length, 4);
  for (const [offset, value] of Object.entries(extra)) state[Number(offset)] = value;
  return {state, timeline};
}
const frames = () => [frame(0, 0), frame(1, 1), frame(2, 2)];
function reader(values) {
  let at = 0;
  return {advances:0, read() { return values[at]; }, advance() { this.advances++; return ++at < values.length; }};
}
const compare = (left = frames(), right = frames(), options = limits()) => compareReaders(reader(left), reader(right), options);

test('includes the exact boundary and never advances it', () => {
  const a = reader(frames()), b = reader(frames());
  const result = compareReaders(a, b, limits());
  assert.equal(result.pass, true); assert.equal(result.status, 'equal');
  assert.equal(result.comparedFrames, 3); assert.equal(a.advances, 2); assert.equal(b.advances, 2);
  assert.deepEqual(result.boundary, {...stop, input:2, substep:0});
  assert.equal(result.stoppedBeforeBoundaryAdvance, true);
  assert.deepEqual(result.prefixSha256.baseline, result.prefixSha256.candidate);
});

test('masks only the established timeline ID in both complete layouts', () => {
  const a = frames(), b = frames();
  for (const item of b) for (const layout of ['state', 'timeline']) item[layout].fill(0xff, 48, 56);
  assert.equal(compare(a, b).pass, true);
  b[0].state[47] = 1;
  const result = compare(a, b);
  assert.equal(result.status, 'different'); assert.equal(result.firstDifference.offsets[0].offset, 47);
});

test('a difference on the exact boundary frame rejects before any boundary advance', () => {
  const changed = frames(); changed[2].state[7464] = 1;
  const a = reader(frames()), b = reader(changed);
  const result = compareReaders(a, b, limits());
  assert.equal(result.status, 'different'); assert.equal(result.firstDifference.frame, 2);
  assert.equal(a.advances, 2); assert.equal(b.advances, 2);
  assert.equal(result.boundaryIncluded, undefined);
});

for (const [label, offset] of [['camp health', 7464], ['champion cooldown', 232], ['champion channel timer', 576]])
  test(label + ' difference cannot hide behind identical compact actor tracks', () => {
    const b = frames(); b[1].state[offset] = 1;
    const result = compare(frames(), b);
    assert.equal(result.pass, false); assert.equal(result.firstDifference.frame, 1);
    assert.equal(result.firstDifference.layout, 'state'); assert.equal(result.firstDifference.offsets[0].offset, offset);
  });

test('timeline-only difference and asymmetric moments are reported', () => {
  const b = frames(); b[1].timeline[150] = 1;
  assert.equal(compare(frames(), b).firstDifference.layout, 'timeline');
  const c = frames();
  for (const k of ['state', 'timeline']) c[1][k].writeUInt32LE(7, 56);
  const result = compare(frames(), c);
  assert.equal(result.status, 'different'); assert.equal(result.firstDifference.candidateMoment.input, 7);
});

test('byte diagnostics are bounded but count every differing byte', () => {
  const b = frames(); b[1].state.fill(2, 1000, 1100);
  const difference = compare(frames(), b).firstDifference;
  assert.equal(difference.differingBytes, 100); assert.equal(difference.offsets.length, 16);
});

test('missing boundary, asymmetric end and step caps fail closed', () => {
  assert.throws(() => compare([frame(0, 0)], [frame(0, 0)]), /ended before boundary/);
  assert.throws(() => compare([frame(0, 0)], frames()), /asymmetrically/);
  assert.throws(() => compare(frames(), frames(), {...limits(), maxSteps:2}), /step cap/);
});

test('round caps and skipped exact boundary fail closed', () => {
  const a = [frame(0, 0), frame(4, 1)];
  assert.throws(() => compare(a, a), /round cap/);
  const b = [frame(0, 0), frame(3, 1)];
  assert.throws(() => compare(b, b), /boundary was not reached/);
  const c = limits(); c.stop.input = 50;
  assert.throws(() => compare(frames(), frames(), c), /ended before boundary/);
});

test('no progress cannot be manufactured by changing only timeline IDs', () => {
  const a = [frame(0, 0), frame(0, 0)];
  for (const k of ['state', 'timeline']) a[1][k].fill(2, 48, 56);
  assert.throws(() => compare(a, a), /no observable progress/);
});

test('late starts, malformed buffers, legacy sizes and mixed-layout moments reject', () => {
  assert.throws(() => compare([frame(1, 5)], [frame(1, 5)]), /initial input/);
  const bad = frames(); bad[0].state = Buffer.alloc(184696);
  assert.throws(() => compare(bad, bad), /unsupported official snapshot size/);
  const mixed = frames(); mixed[0].state.writeUInt32LE(50, 56);
  assert.throws(() => compare(mixed, mixed), /moments disagree/);
  assert.throws(() => compareReaders({read:() => ({state:[], timeline:[]})}, reader(frames()), limits()), /snapshot buffers/);
  assert.throws(() => moment(Buffer.alloc(120)), /header/);
});

test('ReplayNext errors and malformed return values never pass', () => {
  const bad = reader(frames()); bad.advance = () => { throw Error('native status 11'); };
  assert.throws(() => compareReaders(bad, reader(frames()), limits()), /native status 11/);
  const malformed = reader(frames()); malformed.advance = () => 0;
  assert.throws(() => compareReaders(malformed, reader(frames()), limits()), /invalid ReplayNext result/);
});

test('owner-start and finite cap schemas reject weak boundaries', () => {
  for (const patch of [{maxSteps:0}, {maxSteps:1000001}, {maxRound:0}, {maxRound:2001}, {stop:{}},
    {stop:{...stop, phase:14}}, {stop:{...stop, awaitingInput:false}}, {stop:{...stop, actor:16}},
    {stop:{...stop, round:4}}, {stop:{...stop, unknown:1}}, {stop:{...stop, input:-1}}])
    assert.throws(() => validateLimits({...limits(), ...patch}));
  assert.doesNotThrow(() => validateLimits({...limits(), stop:{...stop, input:2, substep:0}}));
});

test('pinned files require exact descriptor/hash and repository containment', () => {
  const cache = path.resolve('.cache');
  const base = path.resolve(process.env.REPLAY_PREFIX_TEST_SCRATCH || '.cache/arena/tests/replay-prefix');
  const relativeBase = path.relative(cache, base);
  assert.ok(relativeBase !== '..' && !relativeBase.startsWith('..' + path.sep) && !path.isAbsolute(relativeBase),
    'regenerable test output must remain under repository .cache');
  fs.mkdirSync(base, {recursive:true});
  const resolvedBase = path.relative(fs.realpathSync(cache), fs.realpathSync(base));
  assert.ok(resolvedBase !== '..' && !resolvedBase.startsWith('..' + path.sep) && !path.isAbsolute(resolvedBase),
    'resolved test output must remain under repository cache');
  const temp = fs.mkdtempSync(path.join(base, 'prefix-unit-'));
  try {
    const root = path.join(temp, 'repo'); fs.mkdirSync(root);
    const file = path.join(root, 'input'); fs.writeFileSync(file, 'evidence');
    const record = {path:'input', sha256:sha(Buffer.from('evidence'))};
    assert.equal(pinnedFile(record, {root}).bytes.toString(), 'evidence');
    for (const descriptor of [null, [], {...record, extra:1}, {...record, sha256:'x'}, {...record, sha256:'a'.repeat(64)}])
      assert.throws(() => pinnedFile(descriptor, {root}));
    const outside = path.join(temp, 'outside'); fs.writeFileSync(outside, 'evidence');
    assert.throws(() => pinnedFile({...record, path:outside}, {root}), /escaped repository/);
    assert.throws(() => pinnedFile(record, {root, external:true}), /must be absolute/);
    assert.equal(pinnedFile({...record, path:outside}, {root, external:true}).bytes.toString(), 'evidence');
  } finally {
    const relativeTemp = path.relative(fs.realpathSync(base), fs.realpathSync(temp));
    assert.ok(relativeTemp && relativeTemp !== '..' && !relativeTemp.startsWith('..' + path.sep) && !path.isAbsolute(relativeTemp),
      'verify the resolved recursive-delete target is inside this test cache');
    fs.rmSync(temp, {recursive:true, force:true});
  }
});

test('malformed ZIP and malformed spec reject before native replay execution', async () => {
  for (const input of [Buffer.alloc(0), Buffer.alloc(22), Buffer.from('not a replay')])
    assert.throws(() => replayPayload(input), /ZIP/);
  await assert.rejects(runSpec({}), /spec fields/);
});

test('stale tool/profile identities and malformed asset declarations reject before native execution', async () => {
  const tool = fileURLToPath(moduleURL);
  const spec = {...limits(), schemaVersion:1, profile:'cli-3.0.4', toolSha256:sha(fs.readFileSync(tool)),
    profileModuleSha256:sha(fs.readFileSync(path.resolve('tools/replay-profiles.mjs'))),
    profileRegistrySha256:sha(fs.readFileSync(path.resolve('tools/replay-profiles.json'))),
    officialAssets:{}, baseline:{}, candidate:{}};
  await assert.rejects(runSpec({...spec, profile:'cli-3.0.2'}), /schema\/profile/);
  await assert.rejects(runSpec({...spec, toolSha256:'0'.repeat(64)}), /tool bytes changed/);
  await assert.rejects(runSpec({...spec, profileRegistrySha256:'0'.repeat(64)}), /profile inputs changed/);
  await assert.rejects(runSpec(spec), /asset declarations/);
  const officialAssets = {'PanelChrome-BXlTYAwV.js':[], 'index-CexarSMK.js':{}, 'v1_0_2-BIJCVwNu.wasm':{}};
  await assert.rejects(runSpec({...spec, officialAssets}), /malformed pinned file/);
});

test('current profile requires its own verified frontend and decoder declarations', async () => {
  const tool = fileURLToPath(moduleURL);
  const spec = {...limits(), schemaVersion:1, profile:'cli-3.0.5', toolSha256:sha(fs.readFileSync(tool)),
    profileModuleSha256:sha(fs.readFileSync(path.resolve('tools/replay-profiles.mjs'))),
    profileRegistrySha256:sha(fs.readFileSync(path.resolve('tools/replay-profiles.json'))),
    officialAssets:{}, baseline:{}, candidate:{}};
  await assert.rejects(runSpec(spec), /asset declarations/);
  const historical = {'PanelChrome-BXlTYAwV.js':{}, 'index-CexarSMK.js':{}, 'v1_0_2-BIJCVwNu.wasm':{}};
  await assert.rejects(runSpec({...spec, officialAssets:historical}), /asset declarations/);
  const current = {'PanelChrome-BXlTYAwV.js':[], 'index-C8ICv-Xx.js':{}, 'v1_0_3-DRqNt1t2.wasm':{}};
  await assert.rejects(runSpec({...spec, officialAssets:current}), /malformed pinned file/);
  await assert.rejects(runSpec({...spec, profileRegistrySha256:'0'.repeat(64)}), /profile inputs changed/);
});
