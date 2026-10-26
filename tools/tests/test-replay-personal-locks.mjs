// Personal altar-lock ABI checks. Pure memory-backed queries; no replay/game or actions.
import assert from 'node:assert/strict';
import test from 'node:test';
import {createPersonalAltarLockObserver} from '../replay-interaction-native.mjs';
import {profileById} from '../replay-profiles.mjs';

const profile = profileById('cli-3.0.5');
const lock = (extra = {}) => ({kind: 6, handle: 42, team: 1, x: 7, y: 5, hitsRemaining: 6, ...extra});

function fixture(options = {}) {
  const memory = new WebAssembly.Memory({initial: 2});
  const allocations = [], frees = [], calls = [];
  let next = 4096;
  const view = () => new DataView(memory.buffer);
  const e = {
    memory,
    Engine_Malloc(bytes) {
      const pointer = next;
      next += Math.ceil(bytes / 8) * 8 + 16;
      assert.ok(next < memory.buffer.byteLength);
      allocations.push({pointer, bytes});
      return pointer;
    },
    Engine_Free(pointer) { frees.push(pointer); },
    Game_API_GetSettings(out) {
      const data = view();
      new Uint8Array(memory.buffer, out, 120).fill(0);
      data.setUint32(out + 104, options.hits ?? 7, true);
      if (options.settingsOverrun) data.setUint8(out + 120, 0);
      return options.settingsError ?? 0;
    },
    Game_API_GetAltarLocks(game, perspective, out, capacity, count) {
      assert.equal(game, 123);
      const data = view();
      const person = {team: data.getUint32(perspective, true), id: data.getUint32(perspective + 4, true)};
      calls.push({...person, out, capacity});
      const code = typeof options.queryError === 'function' ? options.queryError(person, capacity) : options.queryError ?? 0;
      if (code) return code;
      const records = options.records ? options.records(person) : [lock()];
      if (!capacity) {
        assert.equal(out, 0, 'count-only query must not offer an output buffer');
        data.setUint32(count, options.count ?? records.length, true);
        if (options.countOverrun) data.setUint8(count + 4, 0);
        return 0;
      }
      if (options.growMemory) memory.grow(1);
      const fresh = view();
      records.forEach((record, index) => {
        const at = out + index * 24;
        fresh.setUint32(at, record.kind, true);
        fresh.setUint32(at + 4, record.handle, true);
        fresh.setUint32(at + 8, record.team, true);
        fresh.setInt32(at + 12, record.x, true);
        fresh.setInt32(at + 16, record.y, true);
        fresh.setUint32(at + 20, record.hitsRemaining, true);
      });
      fresh.setUint32(count, options.returnedCount ?? records.length, true);
      if (options.payloadOverrun) fresh.setUint8(out + capacity * 24, 0);
      if (options.payloadCountOverrun) fresh.setUint8(count + 4, 0);
      return 0;
    },
  };
  return {e, view, calls, allocations, frees,
    create(extra = {}) { return createPersonalAltarLockObserver({e, game: 123, view, profile, width: 32, height: 24, ...extra}); },
    assertFreed() {
      assert.deepEqual([...frees].sort((a, b) => a - b), allocations.map(value => value.pointer).sort((a, b) => a - b));
      assert.equal(new Set(frees).size, frees.length, 'native allocation freed more than once');
    }};
}

function rejectsObservation(options, pattern = /altar|lock|native|ABI|count|record|output/i) {
  const f = fixture(options);
  let observer;
  try {
    observer = f.create();
    assert.throws(() => observer.observe({id: 3, team: 0}), pattern);
  } finally {
    observer?.destroy();
    f.assertFreed();
  }
}

test('locks use each selected champion perspective and queried nondefault hit setting', () => {
  const first = lock({handle: 0xf1234567});
  const f = fixture({records: ({id}) => id === 3 ? [first] : []});
  const observer = f.create({actors: new Set([3, 4]), teams: new Set([0])});
  try {
    assert.equal(observer.observe({id: 2, team: 0}), null);
    assert.equal(observer.observe({id: 3, team: 1}), null);
    assert.equal(f.calls.length, 0, 'unselected actors must not acquire lock observations');
    const observed = observer.observe({id: 3, team: 0});
    assert.deepEqual(observed, [first]);
    assert.deepEqual(observer.observe({id: 4, team: 0}), []);
    assert.deepEqual(observed, [first], 'later query must not mutate the earlier observation');
    assert.deepEqual(f.calls.map(({team, id}) => [team, id]), [[0, 3], [0, 3], [0, 4]]);
    assert.deepEqual(observer.metadata().settings, {altarLockHits: 7});
    assert.equal(observer.metadata().maxRecords, 128);
    assert.equal(observer.metadata().recordBytes, 24);
    assert.equal(observer.metadata().errors, 0);
  } finally {
    observer.destroy();
    observer.destroy();
    f.assertFreed();
  }
  assert.throws(() => observer.observe({id: 3, team: 0}), /destroy|closed/i);
});

test('query failure remains unknown and cannot reuse an earlier successful list', () => {
  let failing = false;
  const f = fixture({queryError: () => failing ? 97 : 0});
  const observer = f.create();
  try {
    assert.deepEqual(observer.observe({id: 3, team: 0}), [lock()]);
    failing = true;
    assert.equal(observer.observe({id: 3, team: 0}), null);
    assert.ok(observer.metadata().errors >= 1);
    assert.ok(Object.keys(observer.metadata().failures).some(key => key.includes('97')));
  } finally { observer.destroy(); f.assertFreed(); }
});

test('payload-query failure is unknown rather than a successful empty observation', () => {
  const f = fixture({queryError: (_person, capacity) => capacity ? 97 : 0});
  const observer = f.create();
  try {
    assert.equal(observer.observe({id: 3, team: 0}), null);
    assert.equal(f.calls.length, 2);
    assert.ok(observer.metadata().errors >= 1);
  } finally { observer.destroy(); f.assertFreed(); }
});

test('lock payload views refresh after native memory growth', () => {
  const f = fixture({growMemory: true});
  const observer = f.create();
  try { assert.deepEqual(observer.observe({id: 3, team: 0}), [lock()]); }
  finally { observer.destroy(); f.assertFreed(); }
});

test('oversized lock counts are rejected before a payload query', () => {
  const f = fixture({count: 129});
  const observer = f.create();
  try {
    assert.throws(() => observer.observe({id: 3, team: 0}), /count|bound|capacity|lock/i);
    assert.equal(f.calls.length, 1);
  } finally { observer.destroy(); f.assertFreed(); }
});

test('count and payload canaries detect native output overruns', () => {
  for (const options of [{countOverrun: true}, {payloadOverrun: true}, {payloadCountOverrun: true}])
    rejectsObservation(options);
});

test('two-stage lock count must remain consistent', () => {
  rejectsObservation({returnedCount: 0});
  rejectsObservation({returnedCount: 2});
});

test('lock identity, team, coordinates and remaining hits are validated', () => {
  for (const record of [lock({kind: 5}), lock({team: 2}), lock({x: -1}), lock({x: 32}),
    lock({y: -1}), lock({y: 24}), lock({hitsRemaining: 0}), lock({hitsRemaining: 8})])
    rejectsObservation({records: () => [record]});
  rejectsObservation({records: () => [lock(), lock({x: 8})]});
});

test('settings canary and unknown hit settings never produce guessed lock data', () => {
  for (const options of [{settingsOverrun: true}, {hits: 0}, {settingsError: 97}]) {
    const f = fixture(options);
    let observer;
    try {
      let failed = false;
      try { observer = f.create(); } catch { failed = true; }
      if (!failed) assert.equal(observer.observe({id: 3, team: 0}), null);
      assert.equal(f.calls.length, 0, 'unknown initial-hit setting cannot authorize lock payloads');
    } finally { observer?.destroy(); f.assertFreed(); }
  }
});

test('unsupported pinned profiles or query arity fail before lock reads', () => {
  for (const configure of [
    f => ({profile: {...profile, id: 'unreviewed-profile'}}),
    f => { f.e.Game_API_GetAltarLocks = function () {}; return {}; },
    f => { f.e.Game_API_GetSettings = function () {}; return {}; },
  ]) {
    const f = fixture();
    assert.throws(() => f.create(configure(f)), /profile|arity|GetAltarLocks|GetSettings|ABI/i);
    assert.equal(f.calls.length, 0);
    f.assertFreed();
  }
});
