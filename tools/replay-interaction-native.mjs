// Read-only queries for one exposed official timeline boundary. The caller owns
// replay traversal. No runner/action export is referenced by this module.
// Layouts: pinned official visualiser PanelChrome Tr/yr; SDK imports.h records
// champion_info(44), champion_design(128), ability_props(36), buff_data(12).
export const INTERACTION_KINDS = Object.freeze([
  'Phase', 'Respawn', 'Capture', 'Tick', 'Move', 'Attack', 'Cast ability',
  'Input', 'End turn', 'Set priority', null, 'Recall', 'Submit gems', 'Shop',
  'Place ward', 'Consumable', 'Cancel channel', 'Sandbox', 'Gems submitted',
  'Draft champion', 'Place altar lock', 'Teleport channel', 'Teleport complete',
  'Movement blocked', 'Resign', 'Sell item', 'Turn rollover', 'Bot error',
]);

export const INTERACTION_PHASES = Object.freeze({0: 'round-start', 1: 'camp-respawns',
  4: 'spell-entities', 7: 'camp-captures', 9: 'camp-turns', 10: 'actions',
  11: 'finished', 12: 'draft', 13: 'owner-end', 14: 'owner-start'});

export const INTERACTION_ABI = Object.freeze({
  Game_API_Champion_Get: [4, 44],
  Game_API_Champion_Design: [3, 128],
  Game_API_Champion_GetCooldown: [5, 4],
  Game_API_Champion_GetNumBuffs: [4, 4],
  Game_API_Champion_GetBuffData: [6, 36],
  Game_API_Champion_GetRestrictions: [4, 1],
  Game_API_Champion_GetChannelTime: [4, 4],
  Game_API_GetSubmittedGemCount: [3, 4],
  Game_Visualiser_GetVisionBuffer: [3, 4],
});

const BUFFER_BYTES = 256, CANARY = 0xa5, BUFF_NAMES = ['shield', 'stun', 'enhanced-attack'];
const fail = message => { throw Error(`Interaction decoder ABI: ${message}`); };
const finite = (value, label) => {
  if (!Number.isFinite(value)) fail(`nonfinite ${label}`);
  return value;
};

// Optional current-bundle observer. Its variable-length list owns separate
// bounded buffers, leaving the fixed champion-query scratch space untouched.
export function createPersonalAltarLockObserver({e, game, view, profile, width, height, actors = null, teams = null}) {
  if (profile.id !== 'cli-3.0.5') fail('personal altar locks require verified cli-3.0.5 profile');
  if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1 || width > 512 || height > 512)
    fail('invalid personal altar lock map dimensions');
  if (typeof e.Game_API_GetSettings !== 'function' || e.Game_API_GetSettings.length !== 1 ||
      typeof e.Game_API_GetAltarLocks !== 'function' || e.Game_API_GetAltarLocks.length !== 5)
    fail('personal altar lock query arity');
  const maxRecords = 128, recordBytes = 24, capacity = maxRecords * recordBytes + 16;
  const pointers = [e.Engine_Malloc(capacity), e.Engine_Malloc(8), e.Engine_Malloc(128), e.Engine_Malloc(8)];
  const [recordsOut, countOut, settingsOut, perspective] = pointers;
  if (pointers.some(ptr => !ptr)) {
    pointers.filter(Boolean).forEach(ptr => e.Engine_Free(ptr));
    fail('personal altar lock allocation failed');
  }
  let closed = false, queries = 0, errors = 0;
  const failures = {}, counts = {};
  const bytes = (ptr, size) => {
    if (!Number.isInteger(ptr) || ptr < 0 || ptr + size > e.memory.buffer.byteLength)
      fail('personal altar lock buffer outside native memory');
    return new Uint8Array(e.memory.buffer, ptr, size);
  };
  const fill = (ptr, size) => bytes(ptr, size).fill(CANARY);
  const guard = (ptr, used, size) => {
    if (bytes(ptr + used, size - used).some(byte => byte !== CANARY))
      fail('personal altar lock query wrote past declared output width');
  };
  const call = (name, args) => {
    if (closed) fail('personal altar lock observer already destroyed');
    queries++; counts[name] = (counts[name] ?? 0) + 1;
    const code = e[name](...args);
    if (code !== 0) {
      errors++; const key = `${name}/${code}`; failures[key] = (failures[key] ?? 0) + 1;
    }
    return code;
  };
  let settings;
  try {
    fill(settingsOut, 128);
    const settingsCode = call('Game_API_GetSettings', [settingsOut]);
    guard(settingsOut, 120, 128);
    if (settingsCode !== 0) fail('runtime altar lock settings query failed');
    settings = {altarLockHits: view().getUint32(settingsOut + 104, true)};
    if (settings.altarLockHits < 1 || settings.altarLockHits > 10000) fail('invalid runtime altar lock hit setting');
  } catch (error) {
    pointers.forEach(ptr => e.Engine_Free(ptr)); closed = true; throw error;
  }
  return {
    observe(champion) {
      if (closed) fail('personal altar lock observer already destroyed');
      if ((actors && !actors.has(champion.id)) || (teams && !teams.has(champion.team))) return null;
      const pv = view(); pv.setUint32(perspective, champion.team, true); pv.setUint32(perspective + 4, champion.id, true);
      fill(countOut, 8);
      const countCode = call('Game_API_GetAltarLocks', [game, perspective, 0, 0, countOut]);
      guard(countOut, 4, 8);
      if (countCode !== 0) return null;
      const expected = view().getUint32(countOut, true);
      if (expected > maxRecords) fail('unsupported personal altar lock count');
      if (!expected) return [];
      fill(recordsOut, capacity); fill(countOut, 8);
      const listCode = call('Game_API_GetAltarLocks', [game, perspective, recordsOut, maxRecords, countOut]);
      guard(countOut, 4, 8); guard(recordsOut, maxRecords * recordBytes, capacity);
      if (listCode !== 0) return null;
      const d = view(), count = d.getUint32(countOut, true);
      if (count !== expected || count > maxRecords) fail('personal altar lock count changed inside read-only frame');
      guard(recordsOut, count * recordBytes, capacity);
      const seen = new Set();
      return Array.from({length: count}, (_, index) => {
        const at = recordsOut + index * recordBytes;
        const record = {kind: d.getUint32(at, true), handle: d.getUint32(at + 4, true), team: d.getUint32(at + 8, true),
          x: d.getInt32(at + 12, true), y: d.getInt32(at + 16, true), hitsRemaining: d.getUint32(at + 20, true)};
        const key = `${record.kind}:${record.handle}`;
        if (record.kind !== 6 || record.team > 1 || record.x < 0 || record.y < 0 || record.x >= width || record.y >= height ||
            record.hitsRemaining < 1 || (settings && record.hitsRemaining > settings.altarLockHits) || seen.has(key))
          fail('invalid personal altar lock record');
        seen.add(key); return record;
      });
    },
    metadata: () => ({settings, queries, errors, counts: {...counts}, failures: {...failures}, maxRecords, recordBytes,
      abi: {Game_API_GetSettings: [1, 120], Game_API_GetAltarLocks: [5, 24]},
      actors: actors ? [...actors] : null, teams: teams ? [...teams] : null,
      scope: 'Fresh native GetAltarLocks queries under each selected champion personal perspective; null is unselected or failed, [] is a successful empty view.'}),
    destroy() { if (!closed) { pointers.forEach(ptr => e.Engine_Free(ptr)); closed = true; } },
  };
}

export function createInteractionSampler({e, game, view, profile, width, height, stride, cstring,
    personalLocks = false, lockActors = null, lockTeams = null}) {
  if (profile.timelineSnapshotBytes !== 40152 || !['cli-3.0.2', 'cli-3.0.4', 'cli-3.0.5'].includes(profile.id))
    fail('unsupported pinned snapshot profile');
  for (const [name, [arity]] of Object.entries(INTERACTION_ABI))
    if (typeof e[name] !== 'function' || e[name].length !== arity) fail(`${name}/${arity}`);
  if (!Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0 ||
      width > 512 || height > 512 || !Number.isInteger(stride) || stride < Math.ceil(width / 8) || stride > 128)
    fail('invalid bitboard dimensions');
  const pointers = [e.Engine_Malloc(BUFFER_BYTES), e.Engine_Malloc(8), e.Engine_Malloc(8)];
  const [queryOut, countOut, perspective] = pointers;
  if (pointers.some(ptr => !ptr)) fail('allocation failed');
  const lockObserver = personalLocks ? createPersonalAltarLockObserver({e, game, view, profile, width, height,
    actors: lockActors, teams: lockTeams}) : null;
  const counts = {}, failures = {}, examples = [], designs = new Map();
  let frames = 0, queries = 0, errors = 0, closed = false;
  const range = (ptr, length) => {
    if (!Number.isInteger(ptr) || ptr < 0 || !Number.isInteger(length) || length < 0 ||
        ptr + length > e.memory.buffer.byteLength) fail('output pointer outside native memory');
  };
  const fill = (ptr, length) => { range(ptr, length); new Uint8Array(e.memory.buffer, ptr, length).fill(CANARY); };
  const guard = (ptr, start, capacity) => {
    range(ptr, capacity);
    if (new Uint8Array(e.memory.buffer, ptr + start, capacity - start).some(byte => byte !== CANARY))
      fail('native query wrote past declared output width');
  };
  const noteFailure = (name, code, params) => {
    errors++;
    const key = `${name}/${code}`;
    failures[key] = (failures[key] ?? 0) + 1;
    if (examples.length < 16) examples.push({query: name, code, actor: Number.isInteger(params[2]) ? params[2] : null,
      message: cstring ? cstring(e.GetLastErrorMsg()).slice(0, 240) : null});
  };
  const query = (name, params, bytes = INTERACTION_ABI[name][1]) => {
    if (closed) fail('sampler already destroyed');
    fill(queryOut, BUFFER_BYTES);
    queries++;
    counts[name] = (counts[name] ?? 0) + 1;
    const code = e[name](...params, queryOut);
    guard(queryOut, bytes, BUFFER_BYTES);
    if (code !== 0) { noteFailure(name, code, params); return null; }
    return view();
  };
  const setPerspective = champion => {
    const d = view();
    d.setUint32(perspective, champion.team, true);
    d.setUint32(perspective + 4, champion.id, true);
  };
  const decodeAbility = (d, offset, slot) => {
    const kind = d.getUint32(offset, true), f = at => finite(d.getFloat32(offset + at, true), 'ability property'),
      i = at => d.getInt32(offset + at, true);
    const payload = kind === 0 ? {} : kind === 1 ? {enhancedAttack: d.getUint32(offset + 20, true)} :
      kind === 2 || kind === 5 ? {range: f(20), modifier: f(24)} :
      kind === 3 || kind === 6 ? {range: f(20), radius: f(24), modifier: f(28), duration: i(32)} :
      kind === 4 || kind === 7 ? {range: f(20), modifier: f(24), duration: i(28)} : null;
    if (payload === null) fail('unknown ability payload tag');
    return {slot, kind, maxCooldown: i(4), maxCharges: d.getUint32(offset + 8, true),
      targets: d.getUint32(offset + 12, true), attributes: d.getUint32(offset + 16, true), payload};
  };
  const designFor = type => {
    if (!designs.has(type)) {
      const d = query('Game_API_Champion_Design', [game, type]);
      if (!d) { designs.set(type, null); return null; }
      if (d.getInt32(queryOut, true) !== type) fail('champion design type differs from history');
      const passiveType = d.getUint8(queryOut + 8), attackType = d.getUint8(queryOut + 84);
      if (![0, 1].includes(attackType)) fail('unknown champion attack type');
      designs.set(type, {passiveType, isMelee: attackType === 0, stunImmunity: passiveType === 8,
        abilities: [0, 1].map(slot => decodeAbility(d, queryOut + 12 + slot * 36, slot))});
    }
    return designs.get(type);
  };
  const buffsFor = champion => {
    const countView = query('Game_API_Champion_GetNumBuffs', [game, perspective, champion.id]);
    if (!countView) return null;
    const expected = countView.getUint32(queryOut, true);
    if (expected > 3) fail('unsupported buff count');
    fill(queryOut, BUFFER_BYTES); fill(countOut, 8);
    const name = 'Game_API_Champion_GetBuffData';
    queries++; counts[name] = (counts[name] ?? 0) + 1;
    const params = [game, perspective, champion.id, queryOut, 3, countOut];
    const code = e[name](...params);
    guard(countOut, 4, 8); guard(queryOut, 36, BUFFER_BYTES);
    if (code !== 0) { noteFailure(name, code, params); return null; }
    const d = view(), count = d.getUint32(countOut, true);
    if (count !== expected || count > 3) fail('buff count changed inside read-only frame');
    guard(queryOut, count * 12, BUFFER_BYTES);
    const seen = new Set();
    return Array.from({length: count}, (_, index) => {
      const offset = queryOut + index * 12, type = d.getUint32(offset, true), timer = d.getInt32(offset + 4, true);
      if (type > 2 || seen.has(type) || timer < 0) fail('invalid buff record');
      seen.add(type);
      return {type, name: BUFF_NAMES[type], timer,
        ...(type === 0 ? {value: finite(d.getFloat32(offset + 8, true), 'shield value')} : {})};
    });
  };
  function fullChampion(champion, champions) {
    const design = designFor(champion.type);
    Object.assign(champion, design ?? {passiveType: null, isMelee: null, stunImmunity: null, abilities: null});
    setPerspective(champion);
    if (lockObserver) champion.personalAltarLocks = lockObserver.observe(champion);
    const d = query('Game_API_Champion_Get', [game, perspective, champion.id]);
    if (d) {
      if (d.getInt32(queryOut, true) !== champion.team || d.getInt32(queryOut + 4, true) !== champion.type ||
          d.getInt32(queryOut + 8, true) !== champion.x || d.getInt32(queryOut + 12, true) !== champion.y ||
          d.getFloat32(queryOut + 16, true) !== champion.health || d.getFloat32(queryOut + 20, true) !== champion.maxHealth)
        fail('self-perspective champion query differs from timeline');
      champion.strength = finite(d.getFloat32(queryOut + 24, true), 'Strength');
      champion.focus = finite(d.getFloat32(queryOut + 28, true), 'Focus');
      champion.moveSpeed = d.getInt32(queryOut + 32, true);
      champion.range = d.getInt32(queryOut + 36, true);
      champion.stateBits = d.getUint32(queryOut + 40, true);
      if (champion.stateBits & ~0x1bff) fail('unsupported state flags');
      const state = champion.stateBits;
      if (!!(state & 1) !== champion.alive) fail('state life bit differs from timeline');
      champion.availability = {move: !!(state & 8), attack: !!(state & 16), cast: !!(state & 2048), completedTurn: !!(state & 32)};
      const channels = [[2, 'recall'], [4, 'tribute'], [4096, 'teleport']].filter(([flag]) => state & flag);
      if (channels.length > 1) fail('conflicting channel flags');
      champion.channel = {kind: channels[0]?.[1] ?? 'none', timer: null};
      if (channels.length) {
        const timer = query('Game_API_Champion_GetChannelTime', [game, perspective, champion.id]);
        if (timer) {
          champion.channel.timer = timer.getInt32(queryOut, true);
          if (champion.channel.timer < 0) fail('negative active channel timer');
        }
      }
    } else Object.assign(champion, {strength: null, focus: null, moveSpeed: null, range: null, stateBits: null,
      availability: null, channel: null});
    champion.cooldowns = [0, 1].map(slot => {
      const result = query('Game_API_Champion_GetCooldown', [game, perspective, champion.id, slot]);
      return result ? result.getInt32(queryOut, true) : null;
    });
    champion.buffs = buffsFor(champion);
    champion.shield = champion.buffs ? champion.buffs.filter(buff => buff.type === 0).reduce((sum, buff) => sum + buff.value, 0) : null;
    // Presence remains true for a zero-timer stun awaiting owner-start expiry.
    champion.stunned = champion.buffs ? champion.buffs.some(buff => buff.type === 1) : null;
    const restrictions = query('Game_API_Champion_GetRestrictions', [game, perspective, champion.id]);
    champion.restrictions = restrictions ? restrictions.getUint8(queryOut) : null;
    if (champion.restrictions !== null && (champion.restrictions & ~0x3f)) fail('unknown buff restrictions');
    // No respawn timer is exposed by the SDK's authenticated champion contract.
    champion.respawnTimer = null;
    const vision = query('Game_Visualiser_GetVisionBuffer', [game, perspective]);
    champion.personalVisibleEnemies = null;
    champion.personalVisibleChampions = null;
    champion.personalVision = null;
    if (vision) {
      const pointer = vision.getUint32(queryOut, true), boardBytes = stride * Math.ceil(height / 8) * 8;
      range(pointer, boardBytes);
      const bits = view();
      champion.personalVision = [];
      for (let chunk = 0; chunk < boardBytes / 8; ++chunk) {
        const word = bits.getBigUint64(pointer + chunk * 8, true);
        if (word !== 0n) champion.personalVision.push([chunk, word.toString(16).padStart(16, '0')]);
      }
      const sees = other => {
        if (!other.alive || other.x < 0 || other.y < 0 || other.x >= width || other.y >= height) return false;
        const chunk = (other.y >> 3) * stride + (other.x >> 3), bit = ((other.y & 7) << 3) + (other.x & 7);
        return !!(bits.getBigUint64(pointer + chunk * 8, true) & (1n << BigInt(bit)));
      };
      champion.personalVisibleEnemies = champions.filter(other => other.team !== champion.team && sees(other)).map(other => other.id);
      champion.personalVisibleChampions = champions.filter(sees).map(other => other.id);
    }
  }
  function sample(snapshot) {
    if (closed) fail('sampler already destroyed');
    range(snapshot, 40152);
    frames++;
    const d = view(), u = offset => d.getUint32(snapshot + offset, true),
      i = offset => d.getInt32(snapshot + offset, true), f = offset => finite(d.getFloat32(snapshot + offset, true), `snapshot@${offset}`);
    if (u(0) !== 1 || u(4) !== 40152) fail('snapshot version/size mismatch');
    const championCount = u(172), campCount = u(176), groupCount = u(180), spellCount = u(192);
    if (championCount > 16 || campCount > 500 || groupCount > 500 || spellCount > 1024) fail('snapshot count bounds');
    const champions = Array.from({length: championCount}, (_, index) => {
      const p = 196 + index * 252;
      const champion = {id: u(p + 4), team: u(p + 8), type: u(p + 12), x: i(p + 16), y: i(p + 20),
        health: f(p + 24), maxHealth: f(p + 28), alive: !!u(p + 32), gold: u(p + 36), gems: u(p + 40), kills: u(p + 44),
        items: Array.from({length: 42}, (_, item) => [item, u(p + 48 + item * 4)]).filter(([, count]) => count !== 0),
        consumables: Array.from({length: 4}, (_, item) => [item, u(p + 216 + item * 4)]).filter(([, count]) => count !== 0),
        abilityTargets: [u(p + 232), u(p + 236)],
        stunSource: u(p + 248) ? {kind: u(p + 240), id: u(p + 244)} : null};
      if (champion.id > 15 || champion.team > 1 || champion.type > 255 ||
          (champion.alive && (champion.x < 0 || champion.y < 0 || champion.x >= width || champion.y >= height)))
        fail('invalid champion identity/position');
      return champion;
    });
    const camps = Array.from({length: campCount}, (_, index) => {
      const p = 4228 + index * 28;
      return {id: u(p + 4), type: u(p + 12), x: i(p + 16), y: i(p + 20), health: f(p + 24)};
    });
    const groups = Array.from({length: groupCount}, (_, id) => ({id,
      rewardAvailable: !!u(18228 + id * 8), captureTeam: u(18232 + id * 8)}));
    const spells = Array.from({length: spellCount}, (_, index) => {
      const p = 23764 + index * 16;
      return {kind: u(p), id: u(p + 4), ownerKind: u(p + 8), ownerId: u(p + 12)};
    });
    const kind = u(68), stepPhase = u(64), rawPhase = u(24);
    const moment = {round: u(8), turn: u(12), team: u(16), currentChampion: u(60), awaitingInput: u(20) === 1,
      phase: rawPhase, phaseName: INTERACTION_PHASES[rawPhase] ?? `unknown-${rawPhase}`,
      input: u(56), substep: u(104), step: {phase: stepPhase,
        phaseName: INTERACTION_PHASES[stepPhase] ?? `unknown-${stepPhase}`, kind,
        name: INTERACTION_KINDS[kind] ?? `unknown-${kind}`, actor: {kind: u(72), id: u(76)},
        from: [i(80), i(84)], to: [i(88), i(92)], round: u(96), input: u(100), substep: u(104),
        slot: u(108) === 0xffffffff ? null : u(108), abilityType: u(112), priority: i(116)}};
    // Construct all snapshot records before native queries, which may grow memory.
    champions.forEach(champion => fullChampion(champion, champions));
    const actingChampion = moment.step.actor.kind === 1 ? champions.find(champion => champion.id === moment.step.actor.id) : null;
    moment.actingTeam = actingChampion?.team ?? null;
    const scores = [0, 1].map(team => {
      const score = query('Game_API_GetSubmittedGemCount', [game, team]);
      return score ? score.getUint32(queryOut, true) : null;
    });
    return {moment, champions, camps, groups, scores, spells};
  }
  return {sample,
    metadata: () => ({frames, queries, errors, counts: {...counts}, failures: {...failures}, examples: [...examples],
      abi: INTERACTION_ABI, maxChampions: 16, maxCamps: 500, maxGroups: 500, maxBuffsPerChampion: 3,
      visionLayout: {encoding: 'sparse-u64-hex', width, height, chunkStride: stride,
        chunkIndex: '(y>>3)*chunkStride+(x>>3)', bitIndex: '((y&7)<<3)+(x&7)',
        note: 'Sorted [chunkIndex,16-digit numeric u64 hex] pairs; omitted chunks are zero. Private champion vision includes its own ward and movement reveal tiles.'},
      scope: 'Omniscient timeline entities; stats queried from each champion own perspective. personalVisibleEnemies is that champion vision, including its ward and movement reveals.',
      unavailable: ['respawnTimer (no matching SDK perspective contract)', 'intermediate movement path unless separately recorded as native substeps'],
      buffTiming: 'A present buff with timer 0 still awaits its owner-start expiry; presence is preserved.',
      ...(lockObserver ? {personalAltarLocks: lockObserver.metadata()} : {})}),
    destroy() {
      if (!closed) { lockObserver?.destroy(); pointers.forEach(pointer => e.Engine_Free(pointer)); closed = true; }
    }};
}
