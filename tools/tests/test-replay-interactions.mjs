// Retained replay/native interaction regressions. No bots, actions or servers.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath} from 'node:url';
import {profileById} from '../replay-profiles.mjs';
import {createInteractionTrace} from '../replay-interaction-events.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const decoder = path.join(root, 'tools/inspect-strategy-replay.mjs');
const fixture = path.join(root, 'tools/tests/fixtures/skirmish-cli-3.0.5.repl26a');
const collision = path.join(root, 'tools/tests/fixtures/00collision-cli-3.0.5.repl26a');
const hidden = path.join(root, 'tools/tests/fixtures/hidden-occupant-cli-3.0.5.repl26a');
const teleport = path.join(root, 'tools/tests/fixtures/teleport-cli-3.0.4.repl26a');
const fixtureSha = '0df351adfcacf0ea7c1d34fdd4463b4a6da886b0f92168fbaec4b30046bd3b8a';
const profile = profileById('cli-3.0.5');
const assets = path.join(process.env.LOCALAPPDATA ?? path.join(os.homedir(), 'AppData/Local'), 'cambc/assets');
const available = fs.existsSync(fixture) && fs.existsSync(path.join(assets, profile.cacheRelease, 'visualiser/assets', profile.engineFile));
const older = profileById('cli-3.0.4');
const teleportAvailable = fs.existsSync(teleport) && fs.existsSync(path.join(assets, older.cacheRelease, 'visualiser/assets', older.engineFile));
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

function inspect(rounds = [45, 60], extra = [], replay = fixture, jsonl = false) {
  const map = replay === fixture ? '00eight' : replay === hidden ? '00contest' : replay === collision ? '00collision' : null;
  const result = spawnSync(process.execPath, ['--disable-warning=ExperimentalWarning', decoder,
    '--replay', replay, '--trace', '1', '--from-round', String(rounds[0]), '--to-round', String(rounds[1]),
    ...(map ? ['--map-id', map] : []), ...extra],
    {cwd: root, encoding: 'utf8', windowsHide: true, timeout: 60000, maxBuffer: 64 * 1024 * 1024});
  assert.equal(result.error, undefined);
  assert.equal(result.status, 0, result.stderr.slice(0, 1600));
  return jsonl ? result.stdout.trim().split(/\r?\n/).map(line => JSON.parse(line)) : JSON.parse(result.stdout);
}
let cached;
const skirmish = () => cached ??= inspect();
const eventAt = (trace, input, substep = 0) => {
  const event = trace.events.find(event => event.native.input === input && event.native.substep === substep);
  assert.ok(event, `missing native event ${input}/${substep}`);
  return event;
};
const actorState = (trace, event, which) => trace.states[event.actorState[which]];
const stateFor = (trace, event, id, which) => {
  const entry = event.championStates.find(champion => champion.id === id);
  assert.ok(entry, `missing champion ${id} at ${event.id}`);
  return trace.states[which ? entry.after : entry.before];
};
const observed = (event, entity) => {
  const change = event.observed.find(change => change.entity === entity);
  assert.ok(change, `missing ${entity} delta at ${event.id}`);
  return change;
};

test('trace options reject unsafe bounds and invalid filters before replay reads', () => {
  const cases = [
    [['--trace', '2'], /--trace requires 0 or 1/],
    [['--actor', 'all', '--locks', '2'], /--locks requires 0 or 1/],
    [['--actor', 'all', '--locks', '1'], /Personal altar locks require --trace 1/],
    [['--trace', '1', '--from-round', '1', '--to-round', '65'], /at most 64 rounds/],
    [['--trace', '1', '--from-round', '1', '--to-round', '2', '--max-events', '0'], /positive output bounds/],
    [['--trace', '1', '--from-round', '1', '--to-round', '2', '--actors', '16'], /Invalid --actors/],
    [['--trace', '1', '--from-round', '1', '--to-round', '2', '--teams', '2'], /Invalid --teams/],
    [['--actor', 'all', '--actors', '0,1'], /require --trace 1/],
    [['--actor', 'all', '--format', 'jsonl'], /jsonl requires --trace 1/],
    [['--trace', '1', '--from-round', '1', '--to-round', '2', '--actors', ''], /Invalid --actors/],
    [['--trace', '1', '--from-round', '1', '--to-round', '2', '--kinds', 'typo'], /Invalid --kinds/],
    [['--trace', '1', '--from-round', '1', '--to-round', '2', '--targets', 'enemy'], /Invalid --targets/],
  ];
  for (const [extra, message] of cases) {
    const result = spawnSync(process.execPath, [decoder, '--replay', 'missing', ...extra],
      {cwd: root, encoding: 'utf8', windowsHide: true});
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, message);
  }
});

test('native trace pins replay and decoder, bounds rounds and retains both teams', {skip: !available}, () => {
  const output = skirmish(), trace = output.interactions;
  assert.equal(sha(fs.readFileSync(fixture)), fixtureSha);
  assert.equal(output.replaySha256, fixtureSha);
  assert.equal(output.decoderSha256, profile.decoderSha256);
  assert.equal(output.decoderProfile, profile.id);
  assert.equal(output.readOnly, true);
  assert.equal(trace.coverage.complete, true);
  assert.equal(trace.coverage.truncated, false);
  assert.ok(trace.events.length > 100);
  assert.equal(new Set(trace.events.map(event => event.id)).size, trace.events.length);
  assert.deepEqual([...new Set(trace.events.filter(event => event.actor !== null).map(event => event.team))].sort(), [0, 1]);
  assert.ok(trace.events.every(event => event.round >= 45 && event.round <= 60));
  assert.ok(trace.events.every(event => event.native.input !== undefined && event.native.substep !== undefined && event.native.phase !== undefined));
  for (const phase of ['round-start', 'owner-start', 'action', 'owner-end'])
    assert.ok(trace.events.some(event => event.phase === phase), phase);
  assert.ok(trace.states.every(state => state.respawnTimer === null));
  assert.ok(trace.native.queries > 0);
  assert.equal(trace.native.errors, 0);
  assert.ok(trace.native.unavailable.some(value => value.includes('respawnTimer')));
});

test('native sequence preserves move, attack, reset and second attack within one owner turn', {skip: !available}, () => {
  const trace = skirmish().interactions;
  const sequence = [1190, 1191, 1192, 1193].map(input => eventAt(trace, input));
  assert.deepEqual(sequence.map(event => [event.round, event.actor, event.kind]),
    [[45, 5, 'move'], [45, 5, 'attack'], [45, 5, 'cast'], [45, 5, 'attack']]);
  assert.deepEqual(sequence.map(event => trace.events.indexOf(event)),
    [...sequence.map(event => trace.events.indexOf(event))].sort((a, b) => a - b));
  assert.deepEqual(sequence[0].movement[0].before, [20, 10]);
  assert.deepEqual(sequence[0].movement[0].after, [21, 10]);
  assert.equal(actorState(trace, sequence[1], 1).availability.attack, false);
  assert.equal(sequence[2].action.abilityType, 1);
  assert.equal(actorState(trace, sequence[2], 0).availability.attack, false);
  assert.equal(actorState(trace, sequence[2], 1).availability.attack, true);
  assert.equal(actorState(trace, sequence[2], 1).availability.cast, false);
  assert.equal(actorState(trace, sequence[3], 1).availability.attack, false);
  assert.equal(sequence[2].movement, undefined, 'self-cast origin sentinel must not invent a move');
});

test('native dash and ranged action restrictions use observed flags and recorded endpoints', {skip: !available}, () => {
  const trace = skirmish().interactions;
  const dash = eventAt(trace, 1286);
  assert.deepEqual([dash.actor, dash.action.abilitySlot, dash.action.abilityType], [1, 1, 6]);
  assert.equal(dash.movement[0].mode, 'dash');
  assert.deepEqual([dash.movement[0].before, dash.movement[0].after], [[20, 12], [22, 10]]);
  assert.equal(dash.movement[0].intermediatePath, null);
  const ranged = eventAt(trace, 1298);
  assert.equal(actorState(trace, ranged, 0).isMelee, false);
  assert.equal(ranged.action.abilityType, 6);
  assert.equal(eventAt(trace, 1299).kind, 'attack');
  const ordinary = trace.events.find(event => event.kind === 'move' && actorState(trace, event, 0)?.isMelee === false);
  assert.ok(ordinary);
  assert.equal(actorState(trace, ordinary, 1).availability.move, false);
  assert.equal(actorState(trace, ordinary, 1).availability.attack, false);
});

test('native shields absorb damage and timer zero remains present until actual expiry', {skip: !available}, () => {
  const trace = skirmish().interactions;
  const shield = observed(eventAt(trace, 1302), 'champion:2');
  assert.deepEqual([shield.before.shield, shield.after.shield], [0, 150]);
  const hit = eventAt(trace, 1306);
  const before = stateFor(trace, hit, 2, 0), after = stateFor(trace, hit, 2, 1);
  assert.deepEqual([before.shield, after.shield], [150, 30]);
  assert.equal(before.health, after.health);
  assert.equal(after.stunned, true);
  const pending = eventAt(trace, 1327, 4);
  assert.equal(stateFor(trace, pending, 9, 1).buffs.find(buff => buff.type === 0).timer, 0);
  const absorbedAtZero = eventAt(trace, 1335);
  assert.equal(stateFor(trace, absorbedAtZero, 9, 0).buffs.find(buff => buff.type === 0).timer, 0);
  assert.equal(stateFor(trace, absorbedAtZero, 9, 0).shield, 22.55974578857422);
  assert.equal(stateFor(trace, absorbedAtZero, 9, 1).shield, 0);
  assert.equal(stateFor(trace, absorbedAtZero, 9, 1).health, 687.5597534179688);
});

test('native stun timing and immunity distinguish buff presence from timer expiry', {skip: !available}, () => {
  const trace = skirmish().interactions;
  const end = eventAt(trace, 1279, 4);
  const pending = stateFor(trace, end, 8, 1);
  assert.equal(end.phase, 'owner-end');
  assert.deepEqual(pending.buffs.map(buff => [buff.type, buff.timer]), [[1, 0]]);
  assert.equal(pending.stunned, true);
  assert.equal(pending.availability.cast, false);
  const expiry = eventAt(trace, 1312, 7);
  assert.equal(expiry.phase, 'owner-start');
  assert.equal(stateFor(trace, expiry, 8, 0).stunned, true);
  assert.equal(stateFor(trace, expiry, 8, 1).stunned, false);
  assert.equal(stateFor(trace, expiry, 8, 1).availability.move, true);
  const aoe = eventAt(trace, 1534);
  assert.deepEqual([aoe.actor, aoe.action.abilityType], [5, 3]);
  const immune = stateFor(trace, aoe, 7, 1);
  assert.deepEqual([immune.type, immune.stunImmunity, immune.stunned, immune.health], [7, true, false, 860]);
  assert.equal(stateFor(trace, aoe, 6, 1).stunned, true);
  assert.equal(stateFor(trace, aoe, 10, 1).stunned, true);
});

test('native death and economy are linked to exact events without asserting causal damage totals', {skip: !available}, () => {
  const trace = skirmish().interactions;
  const attack = eventAt(trace, 1452), reset = eventAt(trace, 1453), last = eventAt(trace, 1454);
  assert.deepEqual([attack.actor, reset.actor, last.actor], [4, 4, 4]);
  assert.equal(reset.kind, 'cast');
  assert.equal(stateFor(trace, last, 9, 0).alive, true);
  assert.equal(stateFor(trace, last, 9, 1).alive, false);
  assert.equal(actorState(trace, last, 1).gold - actorState(trace, last, 0).gold, 100);
  assert.equal(last.action.recordedTargetId, null);
  assert.ok(last.action.targetCandidates.some(candidate => candidate.entity === 'champion:9' && candidate.attribution === 'inferred'));
  const victim = trace.summaries.entities.find(entity => entity.entity === 'champion:9');
  assert.ok(victim.observedHealthLost > 0 && victim.deaths > 0);
  assert.ok(victim.events.includes(last.id));
  assert.equal(victim.damageDealt, undefined);
  assert.ok(trace.events.some(event => event.observed.some(delta => delta.entity.startsWith('camp:'))));
  assert.ok(trace.events.some(event => event.tags.includes('capture')));
});

test('native turn endings report unused actions separately from stun and death', {skip: !available}, () => {
  const trace = skirmish().interactions;
  const freeEnd = trace.events.find(event => event.turnEnd?.unusedAllowances?.length &&
    event.turnEnd.observedBeforeEnd.alive && !event.turnEnd.observedBeforeEnd.stunned);
  assert.ok(freeEnd);
  const stunnedEnd = trace.events.find(event => event.turnEnd?.observedBeforeEnd.stunned);
  const deadEnd = trace.events.find(event => event.turnEnd?.observedBeforeEnd.alive === false);
  assert.ok(stunnedEnd);
  assert.ok(deadEnd);
  assert.deepEqual(stunnedEnd.turnEnd.unusedAllowances, []);
  assert.deepEqual(deadEnd.turnEnd.unusedAllowances, []);
  assert.equal(freeEnd.turnEnd.mistake, undefined);
});

test('native hidden occupant failure reveals the enemy and preserves all action allowances', {skip: !available || !fs.existsSync(hidden)}, () => {
  assert.equal(sha(fs.readFileSync(hidden)), '84bcdd2abf6fb40d3aa6460fe12c2c93af0fc0df8a6494026af309367a3fdf34');
  const trace = inspect([113, 114], ['--actors', '5'], hidden).interactions;
  const failure = eventAt(trace, 3957);
  assert.deepEqual([failure.round, failure.actor, failure.team, failure.kind], [113, 5, 1, 'movement-blocked']);
  assert.deepEqual([failure.action.recordedFrom, failure.action.recordedTo], [[28, 22], [27, 21]]);
  assert.equal(failure.movement, undefined);
  assert.deepEqual(failure.blockedAttempt.destinationPersonalVisibility, {before: false, after: true});
  assert.equal(failure.blockedAttempt.actionAllowanceConsumed, false);
  assert.deepEqual(failure.blockedAttempt.allowancesConsumed, {move: false, attack: false, cast: false});
  assert.deepEqual(failure.blockedAttempt.allowancesBefore, failure.blockedAttempt.allowancesAfter);
  assert.deepEqual(failure.blockedAttempt.cooldownsBefore, failure.blockedAttempt.cooldownsAfter);
  assert.equal(actorState(trace, failure, 0).stateBits, 2073);
  assert.equal(actorState(trace, failure, 1).stateBits, 2073);
  assert.deepEqual(failure.visibility.find(value => value.observer === 5).newlyVisibleEnemies, [3]);
  assert.ok(failure.blockedAttempt.preOccupants.includes('champion:3'));
  assert.equal(failure.blockedAttempt.hiddenOccupantInference.provenCause, false);
  assert.equal(failure.nearby.before.champions.find(value => value.entity === 'champion:3').personallyVisibleToActor, false);
  assert.equal(failure.nearby.after.champions.find(value => value.entity === 'champion:3').personallyVisibleToActor, true);
  assert.equal(trace.native.errors, 0);
});

test('seeked window agrees with walking for exact events, states, baselines and derived summaries', {skip: !available}, () => {
  const seeked = inspect([45, 49], ['--seek', '1']);
  const walked = inspect([45, 49], ['--seek', '0']);
  assert.equal(seeked.positioning.mode, 'observed-round-binary-seek');
  assert.ok(seeked.inspectedSteps < walked.inspectedSteps);
  for (const key of ['initial', 'states', 'events', 'summaries'])
    assert.deepEqual(seeked.interactions[key], walked.interactions[key], key);
});

test('native actor/team/target/kind filters retain self-contained interaction state references', {skip: !available}, () => {
  const output = inspect([53, 53], ['--actors', '4,9', '--teams', '0,1', '--targets', 'champion:9', '--kinds', 'attack']);
  const trace = output.interactions;
  assert.ok(trace.events.length > 0);
  assert.ok(trace.events.every(event => event.kind === 'attack'));
  assert.ok(trace.events.every(event => event.action.targetCandidates.some(candidate => candidate.entity === 'champion:9') ||
    event.observed.some(delta => delta.entity === 'champion:9')));
  assert.ok(trace.events.some(event => event.native.input === 1454));
  for (const event of trace.events) {
    for (const index of event.actorState.filter(index => index !== null)) assert.ok(trace.states[index]);
    for (const state of event.championStates) {
      if (state.before !== null) assert.equal(trace.states[state.before].id, state.id);
      if (state.after !== null) assert.equal(trace.states[state.after].id, state.id);
    }
  }
  const ground = inspect([53, 53], ['--actors', '4', '--targets', 'tile:25:11', '--kinds', 'attack']).interactions;
  assert.ok(ground.events.some(event => event.native.input === 1454));
  assert.ok(ground.events.every(event => event.action.recordedTo.join(':') === '25:11'));
});

test('native JSONL records equal JSON events and truncation remains explicit', {skip: !available}, () => {
  const output = inspect([45, 45], ['--max-events', '3']);
  assert.equal(output.interactions.events.length, 3);
  assert.equal(output.interactions.coverage.truncated, true);
  assert.equal(output.interactions.coverage.complete, false);
  const lines = inspect([45, 45], ['--max-events', '3', '--format', 'jsonl'], fixture, true);
  assert.equal(lines[0].record, 'metadata');
  assert.equal(lines.length, 4);
  assert.deepEqual(lines.slice(1).map(({record, ...event}) => event), output.interactions.events);
  assert.equal(lines[0].interactions.coverage.truncated, true);
});

test('native channels retain owner-start completion and submitted scores', {skip: !available || !fs.existsSync(collision)}, () => {
  const trace = inspect([40, 46], ['--actors', '0'], collision).interactions;
  assert.ok(trace.states.some(state => state.id === 0 && state.channel?.kind === 'tribute' && state.channel.timer === 0));
  const completion = trace.events.find(event => event.phase === 'owner-start' && event.observed.some(delta =>
    delta.entity === 'score:0' && delta.after?.value > delta.before?.value));
  assert.ok(completion);
  const bag = observed(completion, 'champion:0');
  assert.equal(bag.after.gems, 0);
  assert.ok(bag.before.gems > 0);
});

test('native teleport channel counts down and arrives at owner start without an invented path', {skip: !teleportAvailable}, () => {
  assert.equal(sha(fs.readFileSync(teleport)), '7a08cf9b5b128cd1346bcd317d985983be45afe192d557f5ae7dcd4a03410882');
  const output = inspect([257, 260], ['--actors', '6'], teleport), trace = output.interactions;
  assert.equal(output.decoderProfile, 'cli-3.0.4');
  const start = eventAt(trace, 3682);
  assert.deepEqual([start.round, start.actor, start.team, start.kind], [257, 6, 1, 'teleport-channel']);
  assert.deepEqual(start.action.recordedFrom, [0, 0], 'channel input records an origin sentinel');
  assert.deepEqual([actorState(trace, start, 0).x, actorState(trace, start, 0).y], [23, 18]);
  assert.deepEqual(start.action.recordedTo, [26, 15]);
  assert.equal(actorState(trace, start, 1).stateBits, 4097);
  assert.equal(actorState(trace, start, 1).channel.kind, 'teleport');
  assert.ok(trace.states.some(state => state.id === 6 && state.channel?.kind === 'teleport' && state.channel.timer === 0));
  const arrival = eventAt(trace, 3717, 7);
  assert.deepEqual([arrival.round, arrival.actor, arrival.kind, arrival.phase], [260, 6, 'teleport-arrival', 'owner-start']);
  assert.deepEqual(arrival.movement.find(value => value.champion === 6).after, [26, 15]);
  assert.equal(arrival.movement.find(value => value.champion === 6).intermediatePath, null);
  assert.equal(actorState(trace, arrival, 1).stateBits, 2073);
  assert.equal(actorState(trace, arrival, 1).channel.kind, 'none');
  assert.equal(trace.native.errors, 0);
});

const syntheticChampion = (id, team, extra = {}) => ({id, team, type: 0, x: id, y: 0,
  health: 100, maxHealth: 100, alive: true, gold: 0, gems: 0, items: [], stateBits: 2073,
  availability: {move: true, attack: true, cast: true, completedTurn: false}, cooldowns: [0, 0],
  buffs: [], shield: 0, stunned: false, channel: {kind: 'none', timer: null}, personalVisibleEnemies: [], ...extra});
function syntheticFrame(round, input, kind, champions, extra = {}) {
  return {moment: {round, input, substep: 0, phase: 10, currentChampion: 0, awaitingInput: true,
    step: {round, input, substep: 0, phase: 10, kind, actor: {kind: 1, id: 0}, from: [0, 0], to: [1, 0], slot: null}},
  champions, camps: [], groups: [], scores: [0, 0], ...extra};
}

test('pure movement labels follow only the native champion actor', () => {
  for (const [kind, abilityType, expectedMode] of [[4, null, 'ordinary-step'], [6, 6, 'dash'],
    [22, null, 'teleport-arrival'], [6, 4, 'cast-displacement']]) {
    const collector = createInteractionTrace({from: 1, to: 1});
    collector.push(syntheticFrame(0, 0, 3, [syntheticChampion(0, 0), syntheticChampion(1, 1, {x: 5, y: 5})]));
    const actorX = kind === 4 ? 1 : 2;
    const next = syntheticFrame(1, 1, kind, [syntheticChampion(0, 0, {x: actorX}), syntheticChampion(1, 1, {x: 4, y: 3})]);
    Object.assign(next.moment.step, {from: [0, 0], to: [actorX, 0], abilityType});
    collector.push(next);
    const event = collector.finish({stopped: 'round_range_end'}).events[0];
    const actorMove = event.movement.find(value => value.champion === 0);
    const otherMove = event.movement.find(value => value.champion === 1);
    assert.equal(actorMove.mode, expectedMode);
    assert.deepEqual(actorMove.recorded, {from: [0, 0], to: [actorX, 0]});
    assert.equal(otherMove.mode, 'recorded-displacement');
    assert.deepEqual([otherMove.before, otherMove.after], [[5, 5], [4, 3]]);
    assert.deepEqual(otherMove.recorded, {from: null, to: null});
    assert.equal(actorMove.intermediatePath, null);
    assert.equal(otherMove.intermediatePath, null);
  }
});

test('pure recall movement inference requires the same native actor and channel transition', () => {
  const collector = createInteractionTrace({from: 1, to: 1});
  collector.push(syntheticFrame(0, 0, 3, [syntheticChampion(0, 0, {channel: {kind: 'recall', timer: 0}}),
    syntheticChampion(1, 1, {channel: {kind: 'recall', timer: 0}})]));
  const next = syntheticFrame(1, 1, 3, [syntheticChampion(0, 0, {x: 2}), syntheticChampion(1, 1, {x: 4, y: 3})]);
  next.moment.step.phase = 14;
  collector.push(next);
  const event = collector.finish({stopped: 'round_range_end'}).events[0];
  assert.equal(event.movement.find(value => value.champion === 0).mode, 'recall-arrival');
  const other = event.movement.find(value => value.champion === 1);
  assert.equal(other.mode, 'recorded-displacement');
  assert.deepEqual([other.before, other.after], [[1, 0], [4, 3]]);
  assert.deepEqual(other.recorded, {from: null, to: null});
});

test('pure movement does not confuse another native entity kind with the same champion id', () => {
  const collector = createInteractionTrace({from: 1, to: 1});
  const spells = [{id: 0, ownerKind: 1, ownerId: 0}];
  collector.push(syntheticFrame(0, 0, 3, [syntheticChampion(0, 0)], {spells}));
  const next = syntheticFrame(1, 1, 6, [syntheticChampion(0, 0, {x: 2})], {spells});
  Object.assign(next.moment.step, {actor: {kind: 4, id: 0}, abilityType: 6, from: [0, 0], to: [2, 0]});
  collector.push(next);
  const event = collector.finish({stopped: 'round_range_end'}).events[0];
  assert.equal(event.actor, 0, 'recorded spell owner is context, not the raw champion action');
  const move = event.movement[0];
  assert.equal(move.mode, 'recorded-displacement');
  assert.deepEqual([move.before, move.after], [[0, 0], [2, 0]]);
  assert.deepEqual(move.recorded, {from: null, to: null});
  assert.equal(move.intermediatePath, null);
});

test('pure collector preserves failed-query nulls and never attributes health loss as attack damage', () => {
  const collector = createInteractionTrace({from: 1, to: 1, replaySha256: 'a'.repeat(64)});
  collector.push(syntheticFrame(0, 0, 3, [syntheticChampion(0, 0), syntheticChampion(1, 1)]));
  collector.push(syntheticFrame(1, 1, 5, [syntheticChampion(0, 0, {strength: null, shield: null,
    cooldowns: [null, 0], availability: null, stateBits: null}), syntheticChampion(1, 1, {health: 70})]));
  const trace = collector.finish({stopped: 'round_range_end'});
  const event = trace.events[0];
  const state = actorState(trace, event, 1);
  assert.deepEqual([state.strength, state.shield, state.cooldowns[0], state.availability], [null, null, null, null]);
  assert.deepEqual(observed(event, 'champion:1').after.health, 70);
  assert.equal(event.action.recordedTargetId, null);
  assert.equal(event.damage, undefined);
  assert.equal(trace.summaries.entities.find(entity => entity.entity === 'champion:1').observedHealthLost, 30);
  assert.match(trace.causalAttribution, /separate/);
});

test('exhausted retained history never masquerades as a complete requested window', () => {
  for (const phase of [10, 11]) {
    const collector = createInteractionTrace({from: 1, to: 3});
    collector.push(syntheticFrame(0, 0, 3, [syntheticChampion(0, 0)]));
    collector.push(syntheticFrame(1, 1, 8, [syntheticChampion(0, 0)]));
    const trace = collector.finish({stopped: 'timeline_end', lastMoment: {round: 1, phase}});
    assert.equal(trace.coverage.complete, false);
    assert.equal(trace.coverage.truncated, true);
    assert.equal(trace.coverage.historyExhausted, true);
    assert.equal(trace.coverage.gameFinished, phase === 11);
    assert.equal(trace.coverage.requestedEndReached, false);
  }
});

test('native recall arrival preserves arrival HP and healing occurs at owner end', {skip: !available || !fs.existsSync(collision)}, () => {
  const trace = inspect([53, 58], ['--actors', '2'], collision).interactions;
  const arrival = eventAt(trace, 928, 7);
  assert.equal(arrival.phase, 'owner-start');
  const movement = arrival.movement.find(value => value.champion === 2);
  assert.equal(movement.mode, 'recall-arrival');
  assert.deepEqual([movement.before, movement.after], [[23, 12], [3, 1]]);
  assert.equal(actorState(trace, arrival, 0).health, 295);
  assert.equal(actorState(trace, arrival, 1).health, 295);
  assert.ok(trace.events.some(event => event.phase === 'owner-end' && event.observed.some(delta =>
    delta.entity === 'champion:2' && delta.after?.health > delta.before?.health)));
});

test('native byte caps and windows beyond a finished replay report incomplete coverage', {skip: !available}, () => {
  const capped = inspect([45, 45], ['--max-bytes', '4096']).interactions;
  assert.equal(capped.coverage.truncated, true);
  assert.equal(capped.coverage.complete, false);
  assert.ok(Buffer.byteLength(JSON.stringify(capped)) <= 4096);
  const absent = inspect([1950, 2000], [], collision).interactions;
  assert.equal(absent.events.length, 0);
  assert.equal(absent.coverage.complete, false);
  assert.equal(absent.coverage.emptyWindow, true);
  assert.equal(absent.coverage.historyExhausted, true);
});

test('spell removal keeps its recorded champion owner without assigning causal damage', () => {
  const collector = createInteractionTrace({from: 1, to: 1});
  collector.push(syntheticFrame(0, 0, 3, [syntheticChampion(0, 0), syntheticChampion(1, 1)],
    {spells: [{id: 77, ownerKind: 1, ownerId: 0}]}));
  const next = syntheticFrame(1, 1, 3, [syntheticChampion(0, 0), syntheticChampion(1, 1, {health: 50})], {spells: []});
  next.moment.step.actor = {kind: 4, id: 77};
  next.moment.step.phase = 4;
  collector.push(next);
  const event = collector.finish({stopped: 'round_range_end'}).events[0];
  assert.equal(event.actor, 0);
  assert.equal(event.team, 0);
  assert.equal(event.phase, 'round-start');
  assert.equal(event.attribution, 'unassigned');
});

test('visibility filters retain tile reveals even when no champion becomes visible', () => {
  const collector = createInteractionTrace({from: 1, to: 1, kinds: new Set(['visibility']), actors: new Set([0]),
    width: 8, height: 8, stride: 1});
  collector.push(syntheticFrame(0, 0, 3, [syntheticChampion(0, 0, {personalVision: [[0, '0000000000000001']]})]));
  collector.push(syntheticFrame(1, 1, 3, [syntheticChampion(0, 0, {personalVision: [[0, '0000000000000003']]})]));
  const trace = collector.finish({stopped: 'round_range_end'});
  assert.equal(trace.events.length, 1);
  assert.deepEqual(trace.events[0].visibility[0].newlyVisibleTiles, [1]);
  assert.deepEqual(trace.events[0].visibility[0].newlyVisibleEnemies, []);
});
