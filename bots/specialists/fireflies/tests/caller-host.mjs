// Scoped actual-caller experiment host. Native game facts come only from the
// pinned official engine. Unsupported ABI calls fail closed. No games are run.
import fs from 'node:fs';
import crypto from 'node:crypto';
import {WASI} from 'node:wasi';

const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const GUARD = 32;
const serial = value => typeof value === 'bigint' ? value.toString() : value;
const requireInteger = (value, label, maximum = 0xffffffff) => {
  if (!Number.isSafeInteger(value) || value < 0 || value > maximum) throw Error(label + ' is outside its ABI range');
  return value;
};

/** Instantiate a copied production reactor; caller explicitly drives every turn.
 * fuel is a declared conditional host input, never a native guest-fuel claim.
 * A numeric fuel value is constant for that stage; an array must be monotone and
 * supplies one value per query, retaining its final value after exhaustion.
 */
export async function instantiateGuest(wasmPath, initialNative, options = {}) {
  let native = initialNative, instance, stage, fuelValues, fuelIndex, missing = null;
  const trace = [], actions = [], logs = [], bank = [], immutable = new Map(), immutableQueries = new Map();
  let logBytes = 0;
  const maxLogBytes = options.maxLogBytes ?? 1048576;
  const memory = () => instance.exports.memory;
  const guest = (pointer, bytes) => {
    requireInteger(pointer, 'guest pointer'); requireInteger(bytes, 'guest byte count', 268435456);
    if (pointer + bytes > memory().buffer.byteLength) throw Error('Guest buffer is outside linear memory');
    return new Uint8Array(memory().buffer, pointer, bytes);
  };
  const write = (pointer, bytes) => guest(pointer, bytes.length).set(bytes);
  const u32 = (pointer, value) => { guest(pointer, 4); new DataView(memory().buffer).setUint32(pointer, value, true); };
  const add = (operation, details = {}) => { const record = {stage, operation, ...details}; trace.push(record); return record; };
  const errorText = () => native.cstring(native.e.GetLastErrorMsg());
  let lastNativeOperation = 'none';
  const fn = (name, arity) => {
    lastNativeOperation = name;
    const value = native.e[name];
    if (typeof value !== 'function' || value.length !== arity) throw Error('Unverified native ABI: ' + name + ' arity ' + arity);
    return value;
  };
  const allocation = (bytes, fill = 0xa5) => {
    requireInteger(bytes, 'native byte count', 268435456);
    const pointer = native.alloc(Math.max(1, bytes) + GUARD);
    new Uint8Array(native.e.memory.buffer, pointer, Math.max(1, bytes) + GUARD).fill(fill);
    return {pointer, bytes, fill};
  };
  const finish = block => {
    const actual = new Uint8Array(native.e.memory.buffer, block.pointer + Math.max(1, block.bytes), GUARD);
    if (actual.some(value => value !== block.fill)) throw Error('Native output overran guarded buffer: '+lastNativeOperation+' expected '+block.bytes+' bytes');
    return Buffer.from(new Uint8Array(native.e.memory.buffer, block.pointer, block.bytes));
  };
  const callOutput = (name, args, bytes, {record = true, fill = 0xa5, allowError = false} = {}) => {
    const block = allocation(bytes, fill);
    try {
      const status = fn(name, args.length + 1)(...args, block.pointer);
      const result = finish(block);
      if (record) add(name, {args: args.map(serial), status, bytes, sha256: sha(result), hex: bytes <= 160 ? result.toString('hex') : undefined});
      if (status !== 0 && !allowError) native.checked(status);
      result.status = status;
      return result;
    } finally { native.free(block.pointer); }
  };
  const query = (name, args, pointer, bytes) => {
    const result = callOutput(name, args, bytes, {allowError:true});
    if (result.status === 0) write(pointer, result); return result.status;
  };
  const scalar = (name, args) => callOutput(name, args, 4).readUInt32LE();
  const boolQuery = (name, args, pointer, width=1) => {
    const bytes = callOutput(name, args, width);
    const value=width===4?bytes.readUInt32LE():bytes[0];
    if (value > 1) throw Error('Native boolean is neither zero nor one: ' + name);
    u32(pointer, value); return 0;
  };
  const checkImmutable = (key, bytes) => {
    const hash = sha(bytes), prior = immutable.get(key);
    if (prior !== undefined && prior !== hash) throw Error('Conditional world changed cached immutable query: ' + key);
    immutable.set(key, hash); return bytes;
  };
  const immutableQuery = (key, name, args, pointer, bytes) => {
    immutableQueries.set(key,{name,args:args.map(value=>typeof value==='bigint'?{game:true}:value),bytes});
    write(pointer, checkImmutable(key, callOutput(name, args, bytes))); return 0;
  };
  const identity = context => {
    if (!context || !Buffer.isBuffer(context.sdkMetadataBytes) || context.sdkMetadataBytes.length !== 28 ||
        !context.metadataLayout || context.metadataLayout.nativeBytes !== 20 || context.metadataLayout.sdkBytes !== 28)
      throw Error('Native context lacks verified native20/SDK28 metadata translation');
    return JSON.stringify({metadata: context.sdkMetadataBytes.toString('hex'), actor: context.actor, team: context.team,
      engine: context.profile.decoderSha256, mapSha256: context.map.mapSha256});
  };
  const initialIdentity = identity(initialNative);
  const beginStage = (name, offeredFuel) => {
    if (typeof name !== 'string' || !name) throw Error('Stage needs a nonempty name');
    const values = Array.isArray(offeredFuel) ? [...offeredFuel] : [offeredFuel];
    if (!values.length || values.some(value => !Number.isSafeInteger(value) || value < 0 || value > 1000000000) ||
        values.some((value, index) => index && value > values[index - 1])) throw Error('Host fuel must be explicit and monotone');
    stage = name; fuelValues = values; fuelIndex = 0;
    add('host_stage', {fuel: values, conditionalInput: true, nativeFuelMeasurement: false});
  };
  beginStage(options.stage ?? 'initialize', options.fuel);

  // Official geometry exporter establishes the native list ABI; it preserves
  // engine group/member order. No geometry sorting or live-state substitution.
  const nativeList = (name, args, capacity) => {
    requireInteger(capacity, 'native list capacity', 1048576);
    const array = allocation(capacity * 4), count = allocation(4);
    try {
      const status = fn(name, args.length + 3)(...args, capacity ? array.pointer : 0, capacity, count.pointer);
      const result = finish(array), countBytes = finish(count);
      if (status !== 0) native.checked(status);
      const length = countBytes.readUInt32LE();
      if (length > capacity) throw Error('Native list exceeded declared capacity: ' + name);
      const values = Array.from({length}, (_, index) => result.readUInt32LE(index * 4));
      add(name, {args: args.map(serial), capacity, count: length, values}); return values;
    } finally { native.free(array.pointer); native.free(count.pointer); }
  };
  const layout = () => {
    const n = scalar('Game_API_Map_GetNumCampGroups', [native.game]);
    const groups = nativeList('Game_API_Map_GetCampGroups', [native.game], n), words = [];
    if (groups.length !== n) throw Error('Static camp group count mismatch');
    for (const group of groups) {
      const reward = callOutput('Game_API_Map_CampGroup_Get', [native.game, group], 8);
      const respawn = scalar('Game_API_Map_CampGroup_GetRespawnTime', [native.game, group]);
      const count = scalar('Game_API_Map_CampGroup_GetNumCamps', [native.game, group]);
      const camps = nativeList('Game_API_Map_CampGroup_GetCampInfo', [native.game, group], count);
      if (camps.length !== count) throw Error('Static camp member count mismatch');
      words.push(group, reward.readUInt32LE(4), reward.readUInt32LE(0), respawn, count);
      for (const id of camps) {
        const info = callOutput('Game_API_Map_CampInfo_Get', [native.game, id], 12);
        words.push(id, info.readUInt32LE(8), info.readUInt32LE(0), info.readUInt32LE(4));
      }
    }
    const bytes = Buffer.alloc(words.length * 4); words.forEach((value, index) => bytes.writeUInt32LE(value, index * 4));
    return checkImmutable('map_camp_layout', bytes);
  };
  const pointerBoard = (name, args) => {
    const result = callOutput(name, args, 4), pointer = result.readUInt32LE();
    if (!pointer || pointer + native.boardBytes > native.e.memory.buffer.byteLength) throw Error('Invalid game-owned board pointer: ' + name);
    return Buffer.from(new Uint8Array(native.e.memory.buffer, pointer, native.boardBytes));
  };
  const boardQuery = (name, pointer) => {
    // Two poisons establish complete direct-board writes instead of confusing a
    // returned game-owned pointer with an output board of the same C arity.
    const a = callOutput(name, [native.game, native.perspective], native.boardBytes, {fill: 0xa5});
    const b = callOutput(name, [native.game, native.perspective], native.boardBytes, {fill: 0x5a});
    if (!a.equals(b)) throw Error('Native board ABI did not fill its declared output: ' + name);
    if (name === 'Game_API_GetVision') {
      const official = pointerBoard('Game_Visualiser_GetVisionBuffer', [native.game, native.perspective]);
      if (!a.equals(official)) throw Error('Personal vision API differs from official perspective vision buffer');
    }
    write(pointer, a); return 0;
  };
  // One native buffer per guest address, including identical input/output
  // pointers. Partial overlap is rejected; exact in-place aliases are retained.
  const pure = (name, specs, argumentBuilder) => {
    const blocks = new Map();
    try {
      for (const spec of specs) {
        guest(spec.pointer, spec.bytes);
        const prior = blocks.get(spec.pointer);
        if (prior) { if (prior.bytes !== spec.bytes) throw Error('Aliased native buffer size mismatch'); prior.output ||= spec.output; continue; }
        for (const [pointer, block] of blocks) if (pointer < spec.pointer + spec.bytes && spec.pointer < pointer + block.bytes)
          throw Error('Partial guest buffer overlap lacks a verified native ABI');
        const block = {...allocation(spec.bytes), output: !!spec.output, normalize: spec.normalize};
        new Uint8Array(native.e.memory.buffer, block.pointer, spec.bytes).set(guest(spec.pointer, spec.bytes));
        blocks.set(spec.pointer, block);
      }
      const at = pointer => { const block = blocks.get(pointer); if (!block) throw Error('Unmarshalled pure argument'); return block.pointer; };
      const args = argumentBuilder(at), status = fn(name, args.length + 1)(native.metadataPtr, ...args);
      for (const [pointer, block] of blocks) {
        let bytes = finish(block);
        if(block.normalize){
          const width=rawBoolWidths[block.normalize];
          if(![1,4].includes(width))throw Error('Raw boolean output width lacks its runtime certificate');
          const value=width===1?bytes[0]:bytes.readInt32LE();
          if(value!==0&&value!==1)throw Error('Raw boolean output is neither zero nor one');
          bytes=Buffer.alloc(4);bytes.writeInt32LE(value);
        }
        if (block.output && status === 0) write(pointer, bytes);
      }
      if (status !== 0) native.checked(status);
      // Pure calls are counted, not logged individually (setup has thousands).
      pureCounts[name] = (pureCounts[name] ?? 0) + 1; return 0;
    } finally { for (const block of blocks.values()) native.free(block.pointer); }
  };
  const pureCounts = {};
  const rawBoolWidths = {};
  const validatePureABI = () => {
    // Disposable metadata and storage protect the game-owned metadata while
    // checking the raw APIs' extra metadata argument and result semantics.
    const meta=allocation(20), bits=allocation(native.boardBytes), out=allocation(4);
    try {
      const metadata=Buffer.from(native.nativeMetadataBytes);
      if(metadata.length!==20)throw Error('Native metadata proof is absent');
      new Uint8Array(native.e.memory.buffer,meta.pointer,20).set(metadata);
      native.checked(fn('Bitboard_Zero',2)(meta.pointer,bits.pointer));
      if(finish(bits).some(value=>value!==0))throw Error('Raw Bitboard_Zero metadata-first ABI failed');
      const certifyTrue = (name,args) => {
        new Uint8Array(native.e.memory.buffer,out.pointer,4+GUARD).fill(0xa5);
        native.checked(fn(name,args.length+1)(...args,out.pointer));
        const bytes=finish(out);
        const width=bytes.equals(Buffer.from([1,0,0,0]))?4:bytes.equals(Buffer.from([1,0xa5,0xa5,0xa5]))?1:0;
        if(!width)throw Error('Raw true boolean output width is not certified: '+name);
        rawBoolWidths[name]=width;
      };
      certifyTrue('Bitboard_IsZero',[meta.pointer,bits.pointer]);
      native.checked(fn('Bitboard_Set',5)(meta.pointer,bits.pointer,0,0,1));
      finish(bits);
      certifyTrue('Bitboard_Test',[meta.pointer,bits.pointer,0,0]);
      new Uint8Array(native.e.memory.buffer,out.pointer,4+GUARD).fill(0xa5);
      native.checked(fn('Bitboard_NumSetBits',3)(meta.pointer,bits.pointer,out.pointer));
      if(finish(out).readUInt32LE()!==1)throw Error('Raw Bitboard count ABI failed');
      if(!finish(meta).equals(metadata))throw Error('Pure raw function mutated immutable metadata');
      add('pure_abi_certificate',{metadataArgument:'first',metadataBytes:20,boardBytes:native.boardBytes,
        booleanOutputWidths:{...rawBoolWidths},
        assertions:['zero clears whole board','set(0,0) then test is true','set bit count is one','metadata and output guards unchanged']});
    } finally {native.free(meta.pointer);native.free(bits.pointer);native.free(out.pointer);}
  };
  const board = (pointer, output = false) => ({pointer, bytes: native.boardBytes, output});
  const field = (pointer, output = false) => ({pointer, bytes: native.fieldBytes, output});
  const small = (pointer, bytes, output = true) => ({pointer, bytes, output});
  const env = {};
  env.game_settings = out => immutableQuery('settings', 'Game_API_GetSettings', [], out, instance.exports.fixture_size_settings());
  env.shared_memory_size = out => immutableQuery('shared_memory_size', 'Game_API_GetSharedMemorySize', [native.game], out, 4);
  env.draft_memory_size = out => immutableQuery('draft_memory_size', 'Game_API_GetDraftMemorySize', [native.game], out, 4);
  env.read_draft_memory = (offset, out, bytes) => {
    guest(out, bytes); const block = allocation(bytes);
    try { const status = fn('Game_API_ReadDraftMemory', 5)(native.game, native.team, offset, block.pointer, bytes);
      const result = finish(block); if (status !== 0) native.checked(status);
      const key='draft_memory:' + offset + ':' + bytes;
      immutableQueries.set(key,{draft:true,offset,bytes});
      write(out, checkImmutable(key, result)); add('Game_API_ReadDraftMemory', {offset, bytes, sha256: sha(result), team: native.team}); return 0;
    } finally { native.free(block.pointer); }
  };
  env.bc_metadata = out => { write(out, native.sdkMetadataBytes); add('bc_metadata', {bytes: 28, hex: native.sdkMetadataBytes.toString('hex'), layout: native.metadataLayout}); return 0; };
  env.my_champion = () => native.actor;
  env.my_team = () => native.team;
  env.bc_map_symmetry = out => immutableQuery('symmetry', 'Game_API_Map_GetSymmetry', [native.game], out, 4);
  env.spawn_region = (team, out) => immutableQuery('spawn:' + team, 'Game_API_GetSpawnRegion', [native.game, team], out, 16);
  env.bc_map_mask = (kind, out) => {
    const name = ['Game_GetWallBuffer','Game_GetBushBuffer','Game_GetHoleBuffer','Game_GetAltarBuffer'][kind];
    if (!name) throw Error('Unknown SDK terrain mask tag');
    immutableQueries.set('terrain:'+kind,{terrain:true,name});
    write(out, checkImmutable('terrain:' + kind, pointerBoard(name, [native.game]))); return 0;
  };
  env.map_camp_layout = (out, capacity, count) => {
    const bytes = layout(), length = bytes.length / 4; u32(count, length);
    if (capacity === 0 && out === 0) return 0;
    write(out, bytes.subarray(0,capacity*4)); return 0;
  };
  env.camp_design = (type, out) => immutableQuery('camp_design:' + type, 'Game_API_Camp_Design', [native.game, type], out, instance.exports.fixture_size_camp_design());
  env.champion_design = (type, out) => immutableQuery('champion_design:' + type, 'Game_API_Champion_Design', [native.game, type], out, instance.exports.fixture_size_champion_design());
  env.consumable_info = (type, out) => immutableQuery('consumable:' + type, 'Game_API_GetConsumableInfo', [type], out, instance.exports.fixture_size_consumable_info());
  env.item_info = (type, out) => immutableQuery('item:' + type, 'Game_API_GetItemInfo', [type], out, instance.exports.fixture_size_item_info());
  env.champion = (id, out) => query('Game_API_Champion_Get', [native.game, native.perspective, id], out, 44);
  env.champion_identity = (id, out) => {
    const bytes=callOutput('Game_API_Champion_GetIdentity',[native.game,native.perspective,id],8);
    write(out,checkImmutable('champion_identity:'+id,bytes));
    immutableQueries.set('champion_identity:'+id,{identity:true,id});return 0;
  };
  env.champion_is_alive = (id, out) => boolQuery('Game_API_Champion_IsAlive', [native.game, id], out,4);
  env.champion_ability_props = (id, slot, out) => query('Game_API_Champion_GetAbilityProps', [native.game, native.perspective, id, slot], out, 36);
  env.champion_cooldown = (id, slot, out) => query('Game_API_Champion_GetCooldown', [native.game, native.perspective, id, slot], out, 4);
  env.champion_gems = (id, out) => query('Game_API_Champion_GetGems', [native.game, native.perspective, id], out, 4);
  env.champion_gold = (id, out) => query('Game_API_Champion_GetGold', [native.game, native.perspective, id], out, 4);
  env.bc_champion_channel_time = (id, out) => query('Game_API_Champion_GetChannelTime', [native.game, native.perspective, id], out, 4);
  env.champion_can_shop = (id, consumable, out) => boolQuery('Game_API_Champion_CanShop', [native.game, native.perspective, id, consumable], out,4);
  const inventoryQuery = (name,id,out,count) => {
    // The scoped native ABI probe verifies buffer/capacity order, the complete
    // owning timeline array, and status111 for capacities42-1 / 4-1 / zero.
    // This bridge deliberately exposes only the owner covered by that proof.
    if(id!==native.actor)throw Error('Inventory ABI certificate covers only the native owner');
    const block=allocation(count*4);
    try{
      const status=fn(name,5)(native.game,native.perspective,id,block.pointer,count);
      const bytes=finish(block);if(status!==0)native.checked(status);
      write(out,bytes);add(name,{id,capacityElements:count,bytes:bytes.length,sha256:sha(bytes),values:Array.from({length:count},(_,i)=>bytes.readUInt32LE(i*4))});return 0;
    }finally{native.free(block.pointer);}
  };
  env.champion_items = (id,out) => inventoryQuery('Game_API_Champion_GetItems',id,out,42);
  env.champion_consumables = (id,out) => inventoryQuery('Game_API_Champion_GetConsumables',id,out,4);
  env.bc_round_number = out => query('Game_API_GetRoundNumber', [native.game], out, 4);
  env.bc_submitted_gems = (team, out) => query('Game_API_GetSubmittedGemCount', [native.game, team], out, 4);
  env.map_camp_info = (id, out) => immutableQuery('camp_info:' + id, 'Game_API_Map_CampInfo_Get', [native.game, id], out, 12);
  env.map_camp_group_of = (id, out) => immutableQuery('camp_group_of:' + id, 'Game_API_Map_CampInfo_GetGroup', [native.game, id], out, 4);
  env.camp_at = (x, y, out) => query('Game_API_GetCamp', [native.game, native.perspective, x, y], out, 4);
  env.champion_at = (x, y, out) => query('Game_API_GetChampion', [native.game, native.perspective, x, y], out, 4);
  env.camp = (id, out) => query('Game_API_Camp_Get', [native.game, native.perspective, id], out, 32);
  env.camp_state = (id, out) => query('Game_API_Camp_GetState', [native.game, native.perspective, id], out, 4);
  env.camp_group_state = (id, out) => query('Game_API_CampGroup_GetState', [native.game, native.perspective, id], out, 4);
  env.camp_group_cooldown = (id, out) => query('Game_API_CampGroup_GetCooldown', [native.game, native.perspective, id], out, 4);
  for (const [guestName, nativeName] of Object.entries({board_camp_occupancy:'GetCampOccupancy', board_ally_occupancy:'GetAllyOccupancy', board_enemy_occupancy:'GetEnemyOccupancy', board_teleport_occupancy:'GetTeleportOccupancy', board_vision:'GetVision'}))
    env[guestName] = out => boardQuery('Game_API_' + nativeName, out);
  env.fuel_remaining = () => {
    const value = fuelValues[Math.min(fuelIndex++, fuelValues.length - 1)];
    add('host_fuel', {value, query: fuelIndex, conditionalInput: true, nativeFuelMeasurement: false}); return BigInt(value);
  };
  env.bc_get_last_error = (out, capacity) => {
    const bytes = Buffer.from(errorText());
    if (out && capacity) write(out, bytes.subarray(0, Math.min(capacity, bytes.length)));
    return bytes.length;
  };
  env.bc_Bitboard_Zero = out => pure('Bitboard_Zero', [board(out,true)], at => [at(out)]);
  env.bc_Bitboard_Set = (out,x,y,value) => pure('Bitboard_Set',[board(out,true)],at=>[at(out),x,y,value]);
  env.bc_Bitboard_Test = (input,x,y,out) => pure('Bitboard_Test',[board(input),{...small(out,4),normalize:'Bitboard_Test'}],at=>[at(input),x,y,at(out)]);
  env.bc_Bitboard_IsZero = (input,out) => pure('Bitboard_IsZero',[board(input),{...small(out,4),normalize:'Bitboard_IsZero'}],at=>[at(input),at(out)]);
  env.bc_Bitboard_NumSetBits = (input,out) => pure('Bitboard_NumSetBits',[board(input),small(out,4)],at=>[at(input),at(out)]);
  env.bc_Bitboard_GetSetBits = (input,out,capacity,count) => pure('Bitboard_GetSetBits',[board(input),small(out,capacity*8),small(count,4)],at=>[at(input),at(out),capacity,at(count)]);
  env.bc_Bitboard_Not = (input,out) => pure('Bitboard_Not',[board(input),board(out,true)],at=>[at(input),at(out)]);
  for(const name of ['Union','Intersect']) env['bc_Bitboard_'+name]=(a,b,out)=>pure('Bitboard_'+name,[board(a),board(b),board(out,true)],at=>[at(a),at(b),at(out)]);
  env.bc_Bitboard_ImportRawBuffer = (out,bytes,size) => pure('Bitboard_ImportRawBuffer',[board(out,true),small(bytes,size,false)],at=>[at(out),at(bytes),size]);
  env.bc_Bitboard_DrawChebyshev = (out,x,y,radius) => pure('Bitboard_DrawChebyshev',[board(out,true)],at=>[at(out),x,y,radius]);
  env.bc_Bitboard_ReachableMask = (walls,x,y,out,distance) => pure('Bitboard_ReachableMask',[board(walls),board(out,true)],at=>[at(walls),x,y,at(out),distance]);
  env.bc_DistanceField_Get = (input,x,y,out) => pure('DistanceField_Get',[field(input),small(out,2)],at=>[at(input),x,y,at(out)]);
  env.bc_DistanceField_ComputeWithExpansionCount = (walls,roots,visited,out,distance,count) => pure('DistanceField_ComputeWithExpansionCount',[board(walls),board(roots),board(visited,true),field(out,true),small(count,2)],at=>[at(walls),at(roots),at(visited),at(out),distance,at(count)]);
  env.bc_DistanceField_ComputeToWithExpansionCount = (walls,roots,destinations,visited,out,distance,count) => pure('DistanceField_ComputeToWithExpansionCount',[board(walls),board(roots),board(destinations),board(visited,true),field(out,true),small(count,2)],at=>[at(walls),at(roots),at(destinations),at(visited),at(out),distance,at(count)]);

  // Only Move has an independently verified native result ABI in this host.
  // The native context must be an editable pre-action fork at the actual owner.
  env.move_to_with_result = (x,y,out) => {
    const before = native.physicalState();
    if (!before.header.awaitingInput || before.header.phase !== 10 || before.header.actor !== native.actor || before.champion.team !== native.team)
      throw Error('Move is outside the verified native owner action boundary');
    const result = callOutput('Game_Action_Move', [native.game,x,y], 4), value = result.readInt32LE();
    if (value !== 0 && value !== 1) throw Error('Move result is neither zero nor one');
    const after = native.physicalState();
    if (after.champion.id !== native.actor || after.champion.team !== native.team || after.champion.type !== before.champion.type || after.header.round !== before.header.round || after.header.actor!==native.actor)
      throw Error('Native movement changed owner identity or crossed a round');
    u32(out,value); const record={stage,operation:'move',x,y,success:value!==0,before,after}; actions.push(record); trace.push(record); return 0;
  };
  env.recall = () => {
    const before=native.physicalState();
    if(!before.header.awaitingInput||before.header.phase!==10||before.header.actor!==native.actor)
      throw Error('Recall is outside the verified native owner action boundary');
    // Same status-only one-game-argument ABI is exercised by the retained
    // events-branch reconstruction; it does not return a result pointer.
    const status=fn('Game_Action_Recall',1)(native.game);native.checked(status);
    const after=native.physicalState();
    const record={stage,operation:'recall',status,success:true,before,after};actions.push(record);trace.push(record);return status;
  };
  const unknown = (name,args) => { add('unsupported_import',{name,args:args.map(serial)}); throw Error('Unknown or unverified guest import: '+name); };
  // Never silently stand in for an action, inventory query, or query failure.
  const wrap = (name, implementation) => (...args) => {
    if (missing === name) { add('missing_query',{name}); throw Error('Deliberately unavailable required query: '+name); }
    try { return implementation(...args); } catch(error) { add('import_failure',{name,message:error.message}); throw error; }
  };
  const module = await WebAssembly.compile(fs.readFileSync(wasmPath));
  const imports = {env:{},fixture:{bank_entry:()=>{const receipt={stage,kind:'entry',privateState:privateState(),native:native.physicalState()};bank.push(receipt);add('bank_entry',receipt);},bank_result:value=>{const receipt={stage,kind:'result',value,privateState:privateState(),native:native.physicalState()};bank.push(receipt);add('bank_result',receipt);}}};
  for (const item of WebAssembly.Module.imports(module)) {
    if (item.module === 'env') imports.env[item.name] = wrap(item.name,env[item.name]??((...args)=>unknown(item.name,args)));
    else if (!['fixture','wasi_snapshot_preview1'].includes(item.module)) throw Error('Unsupported guest import module '+item.module);
  }
  const wasi = new WASI({version:'preview1',args:[],env:{},preopens:{},returnOnExit:true});
  imports.wasi_snapshot_preview1 = {...wasi.wasiImport,fd_write:(fd,iovecs,count,written)=>{
    if (![1,2].includes(fd)) throw Error('Guest attempted unsupported fd_write descriptor');
    const parts=[];let total=0;
    for(let index=0;index<count;++index){guest(iovecs+index*8,8);const d=new DataView(memory().buffer),pointer=d.getUint32(iovecs+index*8,true),length=d.getUint32(iovecs+index*8+4,true);parts.push(Buffer.from(guest(pointer,length)));total+=length;}
    logBytes+=total;if(logBytes>maxLogBytes)throw Error('Guest telemetry exceeded explicit host bound');
    const bytes=Buffer.concat(parts);logs.push({stage,fd,bytes:bytes.length,text:bytes.toString('utf8'),sha256:sha(bytes)});u32(written,total);return 0;
  }};
  instance = await WebAssembly.instantiate(module,imports);
  const privateState = () => {
    const e=instance.exports, count=e.fixture_get_group_count();
    if(!Number.isInteger(count)||count<0||count>10000)throw Error('Invalid fixture group count');
    return {initialized:e.fixture_get_initialized(),connected:e.fixture_get_connected(),recovering:e.fixture_get_recovering(),partial:e.fixture_get_partial(),target:e.fixture_get_target(),state:e.fixture_get_state(),groupCount:count,captureGroup:e.fixture_get_capture_group(),suppression:Array.from({length:count},(_,i)=>e.fixture_get_suppression(i))};
  };
  // Pure size exports are intentionally safe before WASI initializes globals.
  for(const name of ['settings','camp_design','champion_design','item_info','consumable_info']){
    const value=instance.exports['fixture_size_'+name]();requireInteger(value,'SDK '+name+' size',4096);if(!value)throw Error('Zero SDK ABI record size');
    add('sdk_record_size',{name,bytes:value});
  }
  validatePureABI();
  wasi.initialize(instance);
  return {instance,trace,actions,logs,bank,pureCounts,beginStage,privateState,
    memoryHash:()=>sha(Buffer.from(new Uint8Array(memory().buffer))),
    boot:()=>instance.exports.fixture_boot(),turn:()=>instance.exports.fixture_turn(),
    setMissingQuery:name=>{if(name!==null&&typeof name!=='string')throw Error('Missing query must be an import name or null');missing=name;},
    setNative:context=>{
      if(identity(context)!==initialIdentity)throw Error('World intervention changed map, engine, owner, or native buffer ABI');
      native=context;
      // Repeat all already-cached immutable inputs before observing new live data.
      for(const [key,descriptor] of immutableQueries){
        if(descriptor.terrain)checkImmutable(key,pointerBoard(descriptor.name,[native.game]));
        else if(descriptor.identity)checkImmutable(key,callOutput('Game_API_Champion_GetIdentity',[native.game,native.perspective,descriptor.id],8));
        else if(descriptor.draft){
          const block=allocation(descriptor.bytes);
          try{const status=fn('Game_API_ReadDraftMemory',5)(native.game,native.team,descriptor.offset,block.pointer,descriptor.bytes);
            const bytes=finish(block);if(status!==0)native.checked(status);checkImmutable(key,bytes);
          }finally{native.free(block.pointer);}
        }else checkImmutable(key,callOutput(descriptor.name,descriptor.args.map(value=>value?.game?native.game:value),descriptor.bytes));
      }
      if(immutable.has('map_camp_layout'))layout();
      add('native_world_intervention',{replaySha256:native.replaySha256,header:native.readHeader(),privateState:privateState(),cachedImmutableEntries:immutable.size});
    }};
}
