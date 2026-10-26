// Pure chronological projection of pinned, read-only native replay observations.
// A step names the action; simultaneous deltas are observations, not causal proof.
const KIND_NAMES = ['phase', 'respawn', 'capture', 'tick', 'move', 'attack', 'cast', 'input',
  'end-turn', 'set-priority', 'unknown-10', 'recall', 'tribute', 'shop', 'ward', 'consumable',
  'cancel-channel', 'sandbox', 'gems-submitted', 'draft', 'altar-lock', 'teleport-channel',
  'teleport-arrival', 'movement-blocked', 'resign', 'sell-item', 'turn-rollover', 'bot-error'];
const PHASES = new Map([[0, 'round-start'], [1, 'round-start'], [4, 'round-start'],
  [7, 'round-start'], [9, 'round-start'], [10, 'action'], [13, 'owner-end'],
  [14, 'owner-start'], [11, 'finished'], [12, 'draft']]);
const CHANNEL_KINDS = new Set([11, 12, 16, 18, 21, 22]);
const own = (value, key) => Object.hasOwn(value ?? {}, key);
const same = (a, b) => a === b || JSON.stringify(a) === JSON.stringify(b);
const point = value => value && Number.isFinite(value.x) && Number.isFinite(value.y) &&
  value.x >= 0 && value.y >= 0 ? [value.x, value.y] : null;
const distance = (a, b) => a && b ? Math.max(Math.abs(a[0] - b[0]), Math.abs(a[1] - b[1])) : null;
const positionEqual = (a, b) => a?.[0] === b?.[0] && a?.[1] === b?.[1];
const ref = (kind, id) => `${kind}:${id}`;
const compactMoment = moment => {
  if (!moment) return null;
  const step = moment.step ?? moment;
  return {round: step.round ?? moment.round, input: step.input ?? moment.input,
    substep: step.substep ?? moment.substep, phase: step.phase ?? moment.phase,
    kind: step.kind ?? moment.kind, snapshotRound: moment.round};
};

function changes(kind, before = [], after = []) {
  const old = new Map(before.map((value, index) => [value.id ?? index, value]));
  const next = new Map(after.map((value, index) => [value.id ?? index, value]));
  const result = [];
  for (const id of [...new Set([...old.keys(), ...next.keys()])].sort((a, b) => a - b)) {
    const a = old.get(id), b = next.get(id);
    if (!a || !b) { result.push({entity: ref(kind, id), before: a ?? null, after: b ?? null}); continue; }
    const beforeFields = {}, afterFields = {};
    for (const key of new Set([...Object.keys(a), ...Object.keys(b)])) {
      if (key === 'id' || key === 'personalVision' || same(a[key], b[key])) continue;
      beforeFields[key] = own(a, key) ? a[key] : null;
      afterFields[key] = own(b, key) ? b[key] : null;
    }
    if (Object.keys(beforeFields).length) result.push({entity: ref(kind, id), before: beforeFields, after: afterFields});
  }
  return result;
}

function tagsFor(step, deltas) {
  const tags = new Set([KIND_NAMES[step.kind] ?? `unknown-${step.kind}`]);
  if ([4, 22, 23].includes(step.kind)) tags.add('movement');
  if (step.kind === 6 && step.abilityType === 1) tags.add('attack-reset');
  if (CHANNEL_KINDS.has(step.kind)) tags.add('channel');
  if ([8, 26, 27].includes(step.kind) || [13, 14].includes(step.phase)) tags.add('turn');
  if ([13, 15, 18, 25].includes(step.kind)) tags.add('economy');
  for (const delta of deltas) {
    const a = delta.before, b = delta.after;
    if (!a || !b) continue;
    if (own(b, 'x') || own(b, 'y')) tags.add('movement');
    if (own(b, 'health') && Number.isFinite(a.health) && Number.isFinite(b.health))
      tags.add(b.health < a.health ? 'damage' : 'healing');
    if (own(b, 'shield') || own(b, 'buffs') || own(b, 'stunned')) tags.add('combat-state');
    if (own(b, 'alive')) tags.add(b.alive ? 'respawn' : 'death');
    if (own(b, 'gold') || own(b, 'gems') || own(b, 'items') || own(b, 'consumables') || delta.entity.startsWith('score:')) tags.add('economy');
    if (own(b, 'channel')) tags.add('channel');
    if (delta.entity.startsWith('group:')) tags.add('capture');
    if (own(b, 'personalVisibleEnemies') || own(b, 'personalVisibleChampions')) tags.add('visibility');
  }
  return [...tags];
}

function stateFlags(champion) {
  if (!champion) return {availability: null, alive: null, stunned: null, channel: null};
  const bits = champion.stateBits ?? champion.state;
  return {availability: champion.availability ?? (Number.isInteger(bits) ? {
    move: !!(bits & 8), attack: !!(bits & 16), cast: !!(bits & 2048), completedTurn: !!(bits & 32)} : null),
  alive: champion.alive ?? (Number.isInteger(bits) ? !!(bits & 1) : null),
  stunned: champion.stunned ?? (Array.isArray(champion.buffs) ? champion.buffs.some(b => b.type === 1) : null),
  stunImmunity: champion.stunImmunity ?? null, channel: champion.channel ?? null,
  restrictions: champion.restrictions ?? null, respawnTimer: champion.respawnTimer ?? null};
}

function endedTurn(step, before, after) {
  const explicit = step.kind === 8 && step.actor?.kind === 1 && step.substep === 0;
  if (!explicit && ![26, 27].includes(step.kind)) return null;
  const flags = stateFlags(before);
  const available = flags.availability;
  return {reason: explicit ? 'explicit-end' : step.kind === 26 ? 'turn-rollover' : 'bot-error',
    observedBeforeEnd: flags,
    unusedAllowances: available ? ['move', 'attack', 'cast'].filter(key => available[key] === true) : null,
    afterCompletedTurn: stateFlags(after).availability?.completedTurn ?? null};
}

function candidateTargets(step, before, after) {
  if (![5, 6, 23].includes(step.kind)) return [];
  const to = step.to;
  if (!Array.isArray(to) || to.some(value => value < 0)) return [];
  const result = [];
  const caster = before?.champions.find(c => step.actor?.kind === 1 && c.id === step.actor.id) ??
    after.champions.find(c => step.actor?.kind === 1 && c.id === step.actor.id);
  const ability = caster?.abilities?.find(value => value.slot === step.slot);
  // Ground/self casts have endpoint coordinates too. An occupant of that tile
  // is not thereby the ability's selected unit target.
  if (step.kind === 6 && ability && !(ability.targets & 7)) return [];
  for (const [kind, a, b] of [['champion', before?.champions, after.champions], ['camp', before?.camps, after.camps]]) {
    const seen = new Set();
    for (const [when, entries] of [['before', a ?? []], ['after', b ?? []]]) {
      for (const value of entries) if (value.alive !== false && !(Number.isFinite(value.health) && value.health <= 0) &&
          positionEqual(point(value), to) && !seen.has(value.id)) {
        seen.add(value.id);
        result.push({entity: ref(kind, value.id), basis: `recorded-to-matches-${when}-position`, attribution: 'inferred'});
      }
    }
  }
  return result;
}

function movementDeltas(before, after, step) {
  if (!before) return [];
  const old = new Map(before.champions.map(c => [c.id, c]));
  const result = [];
  for (const champion of after.champions) {
    const previous = old.get(champion.id);
    if (!previous || (previous.x === champion.x && previous.y === champion.y)) continue;
    const a = point(previous), b = point(champion);
    const isActor = step.actor?.kind === 1 && step.actor.id === champion.id;
    let mode = 'recorded-displacement';
    if (!previous.alive && champion.alive) mode = 'respawn';
    else if (previous.alive && !champion.alive) mode = 'death-removal';
    // The global action can coincide with another champion's displacement.
    // Its kind/endpoints describe only the matching native champion actor.
    else if (isActor && previous.channel?.kind === 'recall' && champion.channel?.kind === 'none') mode = 'recall-arrival';
    else if (isActor && step.kind === 22) mode = 'teleport-arrival';
    else if (isActor && step.kind === 6 && step.abilityType === 6) mode = 'dash';
    else if (isActor && step.kind === 4) mode = 'ordinary-step';
    else if (isActor && step.kind === 6) mode = 'cast-displacement';
    result.push({champion: champion.id, mode, before: a, after: b,
      recorded: {from: isActor ? step.from ?? null : null, to: isActor ? step.to ?? null : null},
      intermediatePath: null,
      pathAvailability: 'endpoints-only'});
  }
  return result;
}

function seesTile(champion, at, stride) {
  if (!Array.isArray(champion?.personalVision) || !at || !Number.isInteger(stride)) return null;
  const [x, y] = at;
  if (x < 0 || y < 0) return false;
  const chunk = (y >> 3) * stride + (x >> 3), bit = ((y & 7) << 3) + (x & 7);
  const word = champion.personalVision.find(([index]) => index === chunk)?.[1] ?? '0';
  return (BigInt(`0x${word}`) & (1n << BigInt(bit))) !== 0n;
}

function tileVisionChanges(a, b, width, height, stride) {
  if (!Array.isArray(a) || !Array.isArray(b) || ![width, height, stride].every(Number.isInteger))
    return {newlyVisibleTiles: null, noLongerVisibleTiles: null};
  const old = new Map(a.map(([index, value]) => [index, BigInt(`0x${value}`)]));
  const next = new Map(b.map(([index, value]) => [index, BigInt(`0x${value}`)]));
  const newlyVisibleTiles = [], noLongerVisibleTiles = [];
  for (const chunk of new Set([...old.keys(), ...next.keys()])) {
    const before = old.get(chunk) ?? 0n, after = next.get(chunk) ?? 0n;
    const changed = before ^ after;
    if (!changed) continue;
    for (let bit = 0; bit < 64; ++bit) {
      const mask = 1n << BigInt(bit);
      if (!(changed & mask)) continue;
      const x = (chunk % stride) * 8 + (bit % 8), y = Math.floor(chunk / stride) * 8 + Math.floor(bit / 8);
      if (x >= width || y >= height) continue;
      (after & mask ? newlyVisibleTiles : noLongerVisibleTiles).push(y * width + x);
    }
  }
  return {newlyVisibleTiles: newlyVisibleTiles.sort((a, b) => a - b), noLongerVisibleTiles: noLongerVisibleTiles.sort((a, b) => a - b)};
}

function revealDeltas(before, after, registerVision, options) {
  if (!before) return [];
  const old = new Map(before.champions.map(c => [c.id, c]));
  return after.champions.flatMap(champion => {
    const previous = old.get(champion.id);
    const a = previous?.personalVisibleEnemies, b = champion.personalVisibleEnemies;
    const ca = previous?.personalVisibleChampions, cb = champion.personalVisibleChampions;
    const va = previous?.personalVision, vb = champion.personalVision;
    if (same(a, b) && same(ca, cb) && same(va, vb)) return [];
    return [{observer: champion.id,
      beforeVision: registerVision(va), afterVision: registerVision(vb),
      newlyVisibleEnemies: Array.isArray(a) && Array.isArray(b) ? b.filter(id => !a.includes(id)) : null,
      noLongerVisibleEnemies: Array.isArray(a) && Array.isArray(b) ? a.filter(id => !b.includes(id)) : null,
      newlyVisibleChampions: Array.isArray(ca) && Array.isArray(cb) ? cb.filter(id => !ca.includes(id)) : null,
      noLongerVisibleChampions: Array.isArray(ca) && Array.isArray(cb) ? ca.filter(id => !cb.includes(id)) : null,
      ...tileVisionChanges(va, vb, options.width, options.height, options.stride), scope: 'personal-vision'}];
  });
}

function nearbyContext(before, after, actor, step, radius, geometry, width) {
  const a = before?.champions.find(c => c.id === actor), b = after.champions.find(c => c.id === actor);
  const centers = [point(a), point(b), step.kind === 23 ? step.to : null].filter(Boolean);
  if (!centers.length) return null;
  const near = value => centers.some(center => distance(point(value), center) !== null && distance(point(value), center) <= radius);
  const project = (kind, values, observer) => (values ?? []).filter(near).map(value => ({entity: ref(kind, value.id),
    at: point(value), ...(kind === 'champion' ? {team: value.team, alive: value.alive,
      personallyVisibleToActor: value.id === actor ? true : Array.isArray(observer?.personalVisibleChampions) ?
        observer.personalVisibleChampions.includes(value.id) : value.team === observer?.team ? null :
          observer?.personalVisibleEnemies?.includes(value.id) ?? null} : {health: value.health})}));
  const terrain = after.terrain ?? before?.terrain ?? null;
  let localTerrain = terrain?.tiles ? terrain.tiles.filter(tile => near(tile)) : terrain;
  if (geometry?.terrain && Number.isInteger(width)) {
    localTerrain = Object.entries(geometry.terrain).flatMap(([kind, tiles]) => Array.isArray(tiles) ?
      tiles.flatMap(index => {
        const value = {x: index % width, y: Math.floor(index / width)};
        return near(value) ? [{kind, at: point(value)}] : [];
      }) : []);
    for (const [team, key] of [[0, 'teamA'], [1, 'teamB']]) {
      const bounds = geometry.spawns?.[key];
      if (!bounds) continue;
      for (let y = bounds.minY; y < bounds.maxY; ++y)
        for (let x = bounds.minX; x < bounds.maxX; ++x)
          if (near({x, y})) localTerrain.push({kind: 'spawn', team, at: [x, y]});
    }
  }
  return {scope: 'omniscient-positioning-with-separate-personal-visibility', radius,
    before: {champions: project('champion', before?.champions, a), camps: project('camp', before?.camps, a)},
    after: {champions: project('champion', after.champions, b), camps: project('camp', after.camps, b)},
    terrain: localTerrain,
    terrainAvailability: geometry?.terrain ? 'selected-matching-engine-geometry-export' : terrain ? 'recorded-static-map' : 'unavailable'};
}

/**
 * Input observations must be consecutive native ReplayNext(game,1) states.
 * Query failures remain null. Full champion states are interned once and events
 * carry [before,after] indexes so kind/target filters do not need hidden deltas.
 */
export function createInteractionTrace(options = {}) {
  const from = options.from ?? 0, to = options.to ?? 2000;
  const maxEvents = options.maxEvents ?? 20000, maxBytes = options.maxBytes ?? 32 * 1024 * 1024;
  const radius = options.nearbyRadius ?? 4;
  const actors = options.actors ? new Set(options.actors) : null;
  const teams = options.teams ? new Set(options.teams) : null;
  const targets = options.targets ?? options.target;
  const targetFilter = targets ? new Set(targets) : null;
  const kinds = options.kinds ? new Set(options.kinds) : null;
  const events = [], states = [], stateIds = new Map(), visions = [], visionIds = new Map(), roster = new Map();
  const abilityDefinitions = [], abilityIds = new Map();
  const terrain = [], terrainIds = new Map();
  const metrics = new Map(), separations = new Map(), exchanges = new Map();
  let previous, initial = null, stopped = null, previousKey, bytes = 0;
  let pushed = 0, candidateCount = 0, omittedByFilter = 0, duplicateStates = 0;
  let firstObserved = null, lastObserved = null, lastRetained = null;

  const selectedChampion = champion => champion && (!actors || actors.has(champion.id)) && (!teams || teams.has(champion.team));
  const registerVision = vision => {
    if (!Array.isArray(vision)) return null;
    const key = JSON.stringify(vision);
    if (visionIds.has(key)) return visionIds.get(key);
    const id = visions.length;
    visions.push(vision); visionIds.set(key, id); bytes += Buffer.byteLength(key);
    return id;
  };
  const registerTerrain = tile => {
    const key = JSON.stringify(tile);
    if (terrainIds.has(key)) return terrainIds.get(key);
    const id = terrain.length;
    terrain.push(tile); terrainIds.set(key, id); bytes += Buffer.byteLength(key);
    return id;
  };
  const registerState = champion => {
    if (!champion) return null;
    const {abilities, ...fields} = champion;
    let abilityDefinition = null;
    if (Array.isArray(abilities)) {
      const key = JSON.stringify({type: champion.type, abilities});
      if (!abilityIds.has(key)) {
        abilityIds.set(key, abilityDefinitions.length);
        abilityDefinitions.push({type: champion.type, abilities});
        bytes += Buffer.byteLength(key);
      }
      abilityDefinition = abilityIds.get(key);
    }
    const normalized = {...fields, abilityDefinition,
      ...(own(champion, 'personalVision') ? {personalVision: registerVision(champion.personalVision)} : {})};
    const key = JSON.stringify(normalized);
    if (stateIds.has(key)) return stateIds.get(key);
    const id = states.length;
    states.push(normalized); stateIds.set(key, id); bytes += Buffer.byteLength(key);
    return id;
  };
  const stats = entity => {
    if (!metrics.has(entity)) metrics.set(entity, {entity, observedHealthLost: 0, observedHealing: 0,
      observedShieldLost: 0, observedShieldGained: 0, goldChange: 0, gemsChange: 0,
      attacks: 0, casts: 0, moves: 0, endpointDisplacement: 0, blockedAttempts: 0, deaths: 0,
      events: []});
    return metrics.get(entity);
  };
  const addMetricEvent = (metric, id) => { if (metric.events.at(-1) !== id) metric.events.push(id); };

  function push(state) {
    if (stopped) return false;
    if (!state?.moment || !Array.isArray(state.champions) || !Array.isArray(state.camps))
      throw Error('Malformed interaction observation');
    pushed++;
    const moment = state.moment, step = moment.step ?? moment;
    const native = compactMoment(moment);
    const round = native.round;
    const key = `${native.input}/${native.substep}/${native.phase}/${native.kind}/${step.actor?.kind ?? 0}/${step.actor?.id ?? 0}/${moment.phase}/${moment.awaitingInput ? 1 : 0}/${moment.round}`;
    if (key === previousKey) { duplicateStates++; return true; }
    previousKey = key;
    for (const c of state.champions) roster.set(c.id, {id: c.id, team: c.team, type: c.type});
    if (moment.phase === 12 || step.phase === 12 || round < from || round > to) { previous = state; return true; }
    firstObserved ??= native; lastObserved = native;
    const before = previous;
    const beforeChampions = before?.champions ?? [];
    const old = new Map(beforeChampions.map(c => [c.id, c]));
    const next = new Map(state.champions.map(c => [c.id, c]));
    const phase = PHASES.get(step.phase) ?? `unknown-${step.phase}`;
    let actor = step.actor?.kind === 1 ? step.actor.id : null;
    if (actor === null && step.actor?.kind === 4) {
      const spell = [...(state.spells ?? []), ...(before?.spells ?? [])].find(value => value.id === step.actor.id);
      if (spell?.ownerKind === 1) actor = spell.ownerId;
    }
    if (actor === null && ['owner-start', 'owner-end'].includes(phase)) actor = moment.currentChampion ?? null;
    const actingChampion = next.get(actor) ?? old.get(actor);
    const team = actingChampion?.team ?? null;
    const deltas = before ? [
      ...changes('champion', beforeChampions, state.champions),
      ...changes('camp', before.camps, state.camps),
      ...changes('group', before.groups, state.groups),
      ...changes('score', (before.scores ?? []).map((value, id) => ({id, value})), (state.scores ?? []).map((value, id) => ({id, value})))
    ] : [];
    const candidates = candidateTargets(step, before, state);
    const tags = tagsFor(step, deltas);
    const visionAffected = before ? state.champions.filter(champion =>
      !same(old.get(champion.id)?.personalVision, champion.personalVision)).map(champion => champion.id) : [];
    if (visionAffected.length && !tags.includes('visibility')) tags.push('visibility');
    const moves = movementDeltas(before, state, step);
    if (moves.length) tags.includes('movement') || tags.push('movement');
    const affected = deltas.filter(d => d.entity.startsWith('champion:')).map(d => Number(d.entity.split(':')[1]));
    const involved = new Set([actor, ...affected, ...visionAffected, ...candidates.filter(c => c.entity.startsWith('champion:')).map(c => Number(c.entity.split(':')[1]))]);
    let relevant = (!actors && !teams) || [...involved].some(id => selectedChampion(next.get(id) ?? old.get(id)));
    if (!relevant) {
      const selectedPositions = state.champions.filter(selectedChampion).map(point).filter(Boolean);
      relevant = deltas.some(delta => {
        const id = Number(delta.entity.split(':')[1]);
        if (delta.entity.startsWith('camp:')) {
          const at = point(state.camps.find(c => c.id === id)) ?? point(before?.camps.find(c => c.id === id));
          return selectedPositions.some(center => distance(at, center) !== null && distance(at, center) <= radius);
        }
        if (delta.entity.startsWith('group:')) {
          const group = options.geometry?.groups?.find(value => value.nativeId === id || value.id === id);
          return group?.members?.some(tile => selectedPositions.some(center =>
            distance(point(tile), center) !== null && distance(point(tile), center) <= radius)) ?? false;
        }
        return false;
      });
    }
    const targetKeys = new Set([...candidates.map(c => c.entity), ...deltas.map(d => d.entity)]);
    if (Array.isArray(step.to)) targetKeys.add(`tile:${step.to.join(':')}`);
    const targetMatches = !targetFilter || [...targetFilter].some(target => targetKeys.has(target) || targetKeys.has(`champion:${target}`));
    const kindMatches = !kinds || tags.some(tag => kinds.has(tag)) || kinds.has(String(step.kind));
    candidateCount++;
    if (!relevant || !targetMatches || !kindMatches) { omittedByFilter++; previous = state; return true; }
    if (events.length >= maxEvents) { stopped = 'event_cap'; return false; }
    if (!initial) {
      const baseline = before ?? state;
      initial = {moment: compactMoment(baseline.moment),
        champions: baseline.champions.map(c => ({id: c.id, state: registerState(c)})),
        camps: baseline.camps, groups: baseline.groups ?? [], scores: baseline.scores ?? null,
        immediatelyBeforeFirstEvent: !!before};
      bytes += Buffer.byteLength(JSON.stringify(initial));
    }
    const replayPrefix = (options.replaySha256 ?? 'unhashed').slice(0, 16);
    const id = `${replayPrefix}:${round}:${native.input}:${native.substep}:${native.phase}:${native.kind}:${step.actor?.kind ?? 0}:${step.actor?.id ?? 0}:${moment.phase}:${moment.awaitingInput ? 1 : 0}:${moment.round}`;
    const actorState = [registerState(old.get(actor)), registerState(next.get(actor))];
    const championStates = [...involved].filter(id => id !== null && (old.has(id) || next.has(id)))
      .sort((a, b) => a - b).map(id => ({id, before: registerState(old.get(id)), after: registerState(next.get(id))}));
    const event = {id, round, actor, team, phase, kind: KIND_NAMES[step.kind] ?? `unknown-${step.kind}`, tags,
      native: {...native, actor: step.actor ?? null, currentChampion: moment.currentChampion ?? null,
        processPhaseAfter: moment.phase ?? null, awaitingInputAfter: moment.awaitingInput ?? null},
      action: {recordedFrom: step.from ?? null, recordedTo: step.to ?? null,
        abilitySlot: step.slot === 0xffffffff ? null : step.slot ?? null,
        abilityType: step.kind === 6 ? step.abilityType ?? null : null,
        recordedTargetId: null, targetCandidates: candidates,
        targetIdentity: 'coordinate-candidates'},
      actorState, championStates, observed: deltas,
      attribution: 'unassigned'};
    if (moves.length) event.movement = moves;
    if (step.kind === 23) {
      const a = stateFlags(old.get(actor)).availability, b = stateFlags(next.get(actor)).availability;
      const consumed = Object.fromEntries(['move', 'attack', 'cast'].map(k => [k,
        typeof a?.[k] === 'boolean' && typeof b?.[k] === 'boolean' ? a[k] && !b[k] : null]));
      event.blockedAttempt = {recordedFrom: step.from ?? null, recordedTo: step.to ?? null,
        result: 'native-movement-blocked', actionAllowanceConsumed: Object.values(consumed).includes(true) ? true :
          Object.values(consumed).includes(null) ? null : false, allowancesConsumed: consumed,
        allowancesBefore: a, allowancesAfter: b,
        cooldownsBefore: old.get(actor)?.cooldowns ?? null, cooldownsAfter: next.get(actor)?.cooldowns ?? null};
      const destinationBefore = seesTile(old.get(actor), step.to, options.stride);
      const destinationAfter = seesTile(next.get(actor), step.to, options.stride);
      const occupants = [
        ...beforeChampions.filter(c => c.id !== actor && c.alive && positionEqual(point(c), step.to)).map(c => ref('champion', c.id)),
        ...(before?.camps ?? []).filter(c => c.health > 0 && positionEqual(point(c), step.to)).map(c => ref('camp', c.id))];
      event.blockedAttempt.destinationPersonalVisibility = {before: destinationBefore, after: destinationAfter};
      event.blockedAttempt.preOccupants = occupants;
      event.blockedAttempt.hiddenOccupantInference = destinationBefore === false && destinationAfter === true && occupants.length ?
        {candidates: occupants, basis: 'Recorded blocked endpoint was personally unseen before, visible after, and omnisciently occupied before.', provenCause: false} : null;
    }
    const reveals = revealDeltas(before, state, registerVision, options);
    if (reveals.length) event.visibility = reveals;
    if (moves.length || [4, 5, 6, 23].includes(step.kind)) event.nearby = nearbyContext(before, state, actor, step, radius, options.geometry, options.width);
    if (Array.isArray(event.nearby?.terrain)) event.nearby.terrain = event.nearby.terrain.map(registerTerrain);
    const end = endedTurn(step, old.get(actor), next.get(actor));
    if (end) event.turnEnd = end;
    const eventBytes = Buffer.byteLength(JSON.stringify(event));
    // Reserve bounded space for summary and coverage metadata. State interning is
    // bounded by maxEvents even if the last candidate crosses the byte limit.
    if (bytes + eventBytes > maxBytes * 0.8) { stopped = 'byte_cap'; return false; }
    events.push(event); bytes += eventBytes; lastRetained = native;

    if (actor !== null && [4, 5, 6, 23].includes(step.kind)) {
      const metric = stats(ref('champion', actor));
      if (step.kind === 5) metric.attacks++;
      if (step.kind === 6) metric.casts++;
      if (step.kind === 23) metric.blockedAttempts++;
      addMetricEvent(metric, id);
    }
    for (const movement of moves) {
      const metric = stats(ref('champion', movement.champion));
      if (!['respawn', 'death-removal'].includes(movement.mode)) {
        metric.moves++; metric.endpointDisplacement += distance(movement.before, movement.after) ?? 0;
      }
      addMetricEvent(metric, id);
    }
    for (const delta of deltas) {
      if (!delta.before || !delta.after) continue;
      const a = delta.before, b = delta.after, metric = stats(delta.entity);
      let changed = false;
      for (const [field, loss, gain] of [['health', 'observedHealthLost', 'observedHealing'], ['shield', 'observedShieldLost', 'observedShieldGained']]) {
        if (Number.isFinite(a[field]) && Number.isFinite(b[field])) {
          metric[loss] += Math.max(0, a[field] - b[field]); metric[gain] += Math.max(0, b[field] - a[field]); changed = true;
        }
      }
      for (const field of ['gold', 'gems']) if (Number.isFinite(a[field]) && Number.isFinite(b[field])) {
        metric[`${field}Change`] += b[field] - a[field]; changed = true;
      }
      if (a.alive === true && b.alive === false) { metric.deaths++; changed = true; }
      if (changed) addMetricEvent(metric, id);
    }
    if (actor !== null && [5, 6].includes(step.kind)) {
      for (const candidate of candidates) {
        const delta = deltas.find(d => d.entity === candidate.entity);
        const hp = delta && Number.isFinite(delta.before?.health) && Number.isFinite(delta.after?.health) ?
          Math.max(0, delta.before.health - delta.after.health) : null;
        const shield = delta && Number.isFinite(delta.before?.shield) && Number.isFinite(delta.after?.shield) ?
          Math.max(0, delta.before.shield - delta.after.shield) : null;
        if (!(hp > 0) && !(shield > 0)) continue;
        const key = `${actor}/${candidate.entity}`;
        if (!exchanges.has(key)) exchanges.set(key, {actor, targetCandidate: candidate.entity,
          observedHealthDecrease: 0, observedShieldDecrease: 0, attacks: 0, casts: 0, events: [],
          attribution: 'Coordinate-derived target candidate and coincident state decreases; causal damage attribution is unproven.'});
        const exchange = exchanges.get(key);
        exchange.observedHealthDecrease += hp ?? 0; exchange.observedShieldDecrease += shield ?? 0;
        exchange.attacks += step.kind === 5 ? 1 : 0; exchange.casts += step.kind === 6 ? 1 : 0;
        exchange.events.push(id);
      }
    }
    for (const movement of moves) {
      if (!movement.before || !movement.after) continue;
      for (const other of state.champions) {
        if (other.id === movement.champion || other.team === next.get(movement.champion)?.team ||
            (!selectedChampion(other) && !selectedChampion(next.get(movement.champion)))) continue;
        const beforeOther = point(old.get(other.id)), afterOther = point(other);
        const a = distance(movement.before, beforeOther), b = distance(movement.after, afterOther);
        if (a === null || b === null || a === b || Math.min(a, b) > radius + 2) continue;
        const key = [movement.champion, other.id].sort((x, y) => x - y).join(':');
        if (!separations.has(key)) separations.set(key, {champions: key.split(':').map(Number), changes: []});
        const series = separations.get(key).changes;
        if (series.at(-1)?.event !== id) series.push({event: id, before: a, after: b});
      }
    }
    previous = state;
    return true;
  }

  function finish(scanInfo = {}) {
    const scanStopped = scanInfo.stopped ?? null;
    const historyExhausted = scanStopped === 'timeline_end';
    const gameFinished = historyExhausted && scanInfo.lastMoment?.phase === 11;
    const requestedEndReached = scanStopped === 'round_range_end' ||
      (gameFinished && lastObserved !== null && lastObserved.round >= to);
    const complete = !stopped && requestedEndReached && firstObserved !== null && firstObserved.round <= from;
    const result = {schemaVersion: 'interaction-trace-v1',
      perspective: 'omniscient-replay-observations-with-explicit-per-champion-personal-visibility',
      stateReferences: 'states[] holds complete queried champion snapshots; actorState and championStates reference its zero-based indexes. Null means unavailable, including failed queries.',
      causalAttribution: 'Action identities, observed effects, and coordinate-derived target candidates remain separate.',
      observationSemantics: {attribution: 'Unassigned means effects occurred across this native step; health loss alone does not prove causal attack/cast damage.',
        targetIdentity: 'Native history records target coordinates, not unit target IDs. Candidate IDs match living before/after occupants and are inferred.',
        movement: 'Endpoint displacement is measured. Intermediate dash tiles are unavailable and never reconstructed.',
        turnEnd: 'Unused allowances are native observations before ending, not classifications of mistakes.'},
      filters: {actors: actors ? [...actors] : null, teams: teams ? [...teams] : null,
        targets: targetFilter ? [...targetFilter] : null, kinds: kinds ? [...kinds] : null, nearbyRadius: radius},
      entities: [...roster.values()].sort((a, b) => a.id - b.id), initial, states, visions, terrain, abilityDefinitions, events,
      abilityEncoding: 'states[].abilityDefinition indexes immutable queried type/abilities records; null means unavailable.',
      terrainEncoding: 'nearby.terrain entries index terrain[] static tile records; null means unavailable.',
      visionEncoding: {stateField: 'personalVision indexes visions[]; null means unavailable.',
        chunks: 'visions[] contains sorted [nativeChunkIndex, unsigned-u64-hex] pairs; omitted chunks are zero.',
        tileChanges: 'row-major tile index = y * width + x', width: options.width ?? null,
        height: options.height ?? null, chunkStride: options.stride ?? null},
      native: scanInfo.native ?? null,
      summaries: {scope: 'derived-from-retained-events-only',
        damageMeaning: 'Observed health/shield decreases, not causal attack damage; shield expiry and death removal may contribute.',
        entities: [...metrics.values()].filter(m => m.events.length).sort((a, b) => a.entity.localeCompare(b.entity)),
        separation: [...separations.values()], exchanges: [...exchanges.values()]},
      coverage: {requested: {fromRound: from, toRound: to}, firstObserved, lastObserved, lastRetained,
        nativeObservations: pushed, duplicateStates, candidateEvents: candidateCount, omittedByFilter,
        retainedEvents: events.length, maxEvents, maxBytes, estimatedPayloadBytes: bytes,
        complete, truncated: !!stopped || (!complete && scanStopped !== null), historyExhausted, gameFinished,
        requestedEndReached, emptyWindow: firstObserved === null,
        lastNativeMoment: scanInfo.lastMoment ?? null,
        stopped: stopped ?? scanStopped, firstBeforeStateAvailable: initial?.immediatelyBeforeFirstEvent ?? false,
        queryFailures: scanInfo.queryFailures ?? scanInfo.native?.failures ?? scanInfo.native?.queryFailures ?? null,
        queryObservationComplete: Number.isInteger(scanInfo.native?.errors) ? scanInfo.native.errors === 0 : null,
        note: complete ? 'All walked native boundaries in the selected window were considered; explicit filters affect retention.' :
          'Window incomplete or decoder stopped early; no claim of complete interaction coverage.'}};
    // A final serialized cap also covers state tables and summaries. If the
    // metadata itself exceeds a very small requested cap, return only the
    // explicit coverage result rather than silently crossing the bound.
    if (Buffer.byteLength(JSON.stringify(result)) > maxBytes) {
      result.coverage.complete = false; result.coverage.truncated = true; result.coverage.stopped = 'byte_cap';
      result.summaries = {scope: 'unavailable-after-final-byte-cap', entities: [], separation: [], exchanges: []};
      while (result.events.length && Buffer.byteLength(JSON.stringify(result)) > maxBytes) {
        result.events.splice(Math.max(0, result.events.length - Math.max(1, Math.ceil(result.events.length / 4))));
      }
      const needed = new Set(result.initial?.champions.map(c => c.state) ?? []);
      for (const event of result.events) {
        for (const value of event.actorState) if (value !== null) needed.add(value);
        for (const value of event.championStates) {
          if (value.before !== null) needed.add(value.before);
          if (value.after !== null) needed.add(value.after);
        }
      }
      const remap = new Map([...needed].sort((a, b) => a - b).map((id, index) => [id, index]));
      result.states = [...remap.keys()].map(id => states[id]);
      if (result.initial) for (const champion of result.initial.champions) champion.state = remap.get(champion.state);
      for (const event of result.events) {
        event.actorState = event.actorState.map(id => id === null ? null : remap.get(id));
        for (const value of event.championStates) {
          value.before = value.before === null ? null : remap.get(value.before);
          value.after = value.after === null ? null : remap.get(value.after);
        }
      }
      if (Buffer.byteLength(JSON.stringify(result)) > maxBytes) {
        result.initial = null; result.states = []; result.visions = []; result.terrain = []; result.events = [];
      }
      result.coverage.retainedEvents = result.events.length;
      result.coverage.lastRetained = result.events.at(-1)?.native ?? null;
      result.coverage.note = 'Output truncated by byte cap; summaries unavailable. No complete-window claim.';
      if (Buffer.byteLength(JSON.stringify(result)) > maxBytes)
        return {schemaVersion: result.schemaVersion, events: [], states: [], visions: [], coverage: {complete: false, truncated: true, stopped: 'byte_cap'}};
    }
    return result;
  }
  return {push, finish, get truncated() { return !!stopped; }};
}
