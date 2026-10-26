// Test-only native replay setup. The caller owns the single arena slot and any
// explicitly authorized fork actions; this module never starts an ordinary game.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import zlib from 'node:zlib';
import {fileURLToPath} from 'node:url';
import {WASI} from 'node:wasi';
import {PROFILE_MODULE_SHA256, PROFILE_SHA256, REPLAY_PROFILES, profileById,
  verifyProfileInputs} from '../../../../tools/replay-profiles.mjs';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
export const TOOL_SHA256 = '9ab4e069aec4c7f8682f5ff484c8206f44139188598dca04bb5bad2f4a4abd4c';
const TOOL = path.join(ROOT, 'tools/inspect-strategy-replay.mjs');
const SPLIT = 'const samples = [], captures = [], transitions = [], lastVisible = new Map();';
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const AsyncFunction = Object.getPrototypeOf(async function () {}).constructor;

function replaceOnce(source, marker, replacement) {
  if (source.split(marker).length !== 2) throw Error('Native setup rewrite marker changed: ' + marker.slice(0, 72));
  return source.replace(marker, replacement);
}

function argumentList(options) {
  if (!options || typeof options.replay !== 'string' || !Number.isInteger(options.actor) ||
      options.actor < 0 || options.actor > 15 || ![0, 1].includes(options.team))
    throw Error('withNative needs replay, actor (0..15), and absolute team (0 or 1).');
  const argv = ['node', TOOL, '--replay', path.resolve(ROOT, options.replay), '--actor', String(options.actor),
    '--team', String(options.team), '--from-round', String(options.fromRound ?? 0),
    '--to-round', String(options.toRound ?? options.fromRound ?? 2000), '--captures', '0', '--seek', '1'];
  for (const [name, value] of [['--map-id', options.mapId], ['--geometry', options.geometry],
    ['--profile', options.profile], ['--max-steps', options.maxSteps]])
    if (value !== undefined) argv.push(name, String(value));
  return argv;
}

function generatedSetup() {
  verifyProfileInputs();
  const full = fs.readFileSync(TOOL, 'utf8');
  if (digest(Buffer.from(full)) !== TOOL_SHA256) throw Error('Pinned native decoder setup changed.');
  if (full.split(SPLIT).length !== 2) throw Error('Native setup split boundary changed.');
  const original = full.split(SPLIT)[0];
  let source = original;
  const imports = ["import fs from 'node:fs';", "import path from 'node:path';", "import os from 'node:os';",
    "import crypto from 'node:crypto';", "import zlib from 'node:zlib';", "import {fileURLToPath} from 'node:url';",
    "import { WASI } from 'node:wasi';",
    "import {PROFILE_MODULE_SHA256, PROFILE_SHA256, REPLAY_PROFILES, profileById} from './replay-profiles.mjs';"];
  for (const line of imports) source = replaceOnce(source, line, '');
  source = replaceOnce(source, "const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');", 'const ROOT = __root;');
  const argvReferences = source.split('process.argv').length - 1;
  if (argvReferences !== 3) throw Error('Native setup argv parser changed.');
  source = source.replaceAll('process.argv', '__argv');
  source = replaceOnce(source, 'const e = instance.exports, view = () => new DataView(e.memory.buffer);',
    `const e = instance.exports, view = () => new DataView(e.memory.buffer);
__engine = e;
const __allocate = size => {
  if (!Number.isSafeInteger(size) || size <= 0 || size > 268435456) throw Error('Invalid native allocation size.');
  const pointer = e.Engine_Malloc(size);
  if (!pointer) throw Error('Native allocation failed.');
  __pointers.push(pointer);
  return pointer;
};`);
  source = replaceOnce(source, "const checked = code => { if (code !== 0) throw Error(`Official decoder ${code}: ${cstring(e.GetLastErrorMsg())}`); };",
    "const checked = code => { if (code !== 0) throw Error(`Official decoder ${code}: ${cstring(e.GetLastErrorMsg())}`); };\n__checked = checked;");
  const allocations = source.split('e.Engine_Malloc(').length - 1;
  // Five original sites, plus the allocator body just inserted above.
  if (allocations !== 6) throw Error('Native setup allocation sites changed.');
  source = source.replaceAll('e.Engine_Malloc(', '__allocate(');
  source = replaceOnce(source, 'const pointer = __allocate(size);', 'const pointer = e.Engine_Malloc(size);');
  source = replaceOnce(source, 'const game = view().getBigUint64(out, true);',
    'const game = view().getBigUint64(out, true);\n__game = game;');
  if (source.includes('import.meta') || /^import /m.test(source)) throw Error('Unrewritten native setup module syntax.');
  return {source, provenance: {tool: path.relative(ROOT, TOOL).replaceAll('\\', '/'), toolSha256: TOOL_SHA256,
    prefixSha256: digest(Buffer.from(original)), generatedPrefixSha256: digest(Buffer.from(source)),
    splitMarker: SPLIT, importedLines: imports.length, argvReferences, originalAllocationSites: allocations - 1,
    profileRegistrySha256: PROFILE_SHA256, profileModuleSha256: PROFILE_MODULE_SHA256}};
}

const CONTEXT = `
// Native Game_GetBitboardMetadata and the SDK host's bc_metadata are different
// records. The frontend reads five native uint32 fields through offset16.
const metadata = __allocate(64);
new Uint8Array(e.memory.buffer,metadata,64).fill(0xa5);
checked(e.Game_GetBitboardMetadata(game, metadata));
const metadataCanary = Buffer.from(new Uint8Array(e.memory.buffer, metadata, 64));
if(metadataCanary.subarray(20).some(value=>value!==0xa5))
  throw Error('Native metadata exceeds the frontend five-field layout: '+metadataCanary.subarray(0,32).toString('hex'));
const nativeMetadataBytes=metadataCanary.subarray(0,20);
for(const name of ['Bitboard_GetRawBufferSize','DistanceField_GetRawBufferSize'])
  if(typeof e[name]!=='function'||e[name].length!==2) throw Error('Native buffer-size ABI changed: '+name);
const sizeBuffer=__allocate(64);
const rawSize=name=>{
  new Uint8Array(e.memory.buffer,sizeBuffer,64).fill(0xa5);
  checked(e[name](metadata,sizeBuffer));
  if(new Uint8Array(e.memory.buffer,sizeBuffer+4,60).some(value=>value!==0xa5))
    throw Error('Native buffer-size output exceeded four bytes: '+name);
  return view().getUint32(sizeBuffer,true);
};
const boardBytes=rawSize('Bitboard_GetRawBufferSize'),fieldBytes=rawSize('DistanceField_GetRawBufferSize');
if (nativeMetadataBytes.readInt32LE(0) !== width || nativeMetadataBytes.readInt32LE(4) !== height ||
    nativeMetadataBytes.readUInt32LE(16) !== stride || !Number.isSafeInteger(boardBytes)||!Number.isSafeInteger(fieldBytes)||
    boardBytes<=0||fieldBytes<=0||boardBytes>67108864||fieldBytes>268435456)
  throw Error('Native bitboard metadata/size functions changed.');
const sdkMetadataBytes=Buffer.alloc(28);nativeMetadataBytes.copy(sdkMetadataBytes);
sdkMetadataBytes.writeUInt32LE(boardBytes,20);sdkMetadataBytes.writeUInt32LE(fieldBytes,24);
const sdkMetadataPtr=__allocate(28);
new Uint8Array(e.memory.buffer,sdkMetadataPtr,28).set(sdkMetadataBytes);
const metadataBytes=sdkMetadataBytes;
const metadataLayout={nativeBytes:20,sdkBytes:28,nativeOffsets:{width:0,height:4,chunkedWidth:8,chunkedHeight:12,chunkStride:16},
  sdkComputedOffsets:{boardBytes:20,fieldBytes:24},nativeCanaryHex32:metadataCanary.subarray(0,32).toString('hex'),
  boardSizeFunction:'Bitboard_GetRawBufferSize',fieldSizeFunction:'DistanceField_GetRawBufferSize'};
const unitBuffer = __allocate(64), scalarBuffer = __allocate(16);
const readHeader = () => {
  checked(e.Game_Visualiser_ReadTimelineState(game, snapshot));
  const d = view(), u = offset => d.getUint32(snapshot + offset, true);
  if (u(0) !== 1 || u(4) !== profile.timelineSnapshotBytes) throw Error('Official timeline ABI mismatch.');
  // The frontend's header parser distinguishes the current game phase (+24)
  // from the last recorded step phase (+64); actions use the former.
  return {round:u(8), input:u(56), substep:u(104), kind:u(68), phase:u(24), stepPhase:u(64),
    actor:u(60), stepActor:u(76), awaitingInput:u(20)===1};
};
const readChampion = (id = actor) => {
  new Uint8Array(e.memory.buffer, unitBuffer, 64).fill(0xa5);
  checked(e.Game_API_Champion_Get(game, perspective, id, unitBuffer));
  if (new Uint8Array(e.memory.buffer, unitBuffer + 44, 20).some(value => value !== 0xa5))
    throw Error('Native champion output exceeded its verified 44-byte record.');
  const bytes = Buffer.from(new Uint8Array(e.memory.buffer, unitBuffer, 44)), d = view();
  const result = {id, team:d.getInt32(unitBuffer,true), type:d.getInt32(unitBuffer+4,true),
    x:d.getInt32(unitBuffer+8,true), y:d.getInt32(unitBuffer+12,true), health:d.getFloat32(unitBuffer+16,true),
    maxHealth:d.getFloat32(unitBuffer+20,true), strength:d.getFloat32(unitBuffer+24,true), focus:d.getFloat32(unitBuffer+28,true),
    moveSpeed:d.getInt32(unitBuffer+32,true), attackRange:d.getInt32(unitBuffer+36,true), stateBits:d.getUint32(unitBuffer+40,true),
    infoHex:bytes.toString('hex')};
  checked(e.Game_API_Champion_GetGold(game,perspective,id,scalarBuffer)); result.gold=view().getUint32(scalarBuffer,true);
  checked(e.Game_API_Champion_GetGems(game,perspective,id,scalarBuffer)); result.gems=view().getUint32(scalarBuffer,true);
  return result;
};
const physicalState = () => {
  const header=readHeader(), champion=readChampion(), submitted=[];
  for(const side of [0,1]) { checked(e.Game_API_GetSubmittedGemCount(game,side,scalarBuffer)); submitted.push(view().getUint32(scalarBuffer,true)); }
  return {header,champion,submitted};
};
const assertHeader = (actual, expected) => {
  for(const key of ['round','input','substep','phase','stepPhase','actor','stepActor','awaitingInput'])
    if(expected[key] !== undefined && actual[key] !== expected[key])
      throw Error('Native header mismatch for '+key+': expected '+expected[key]+', got '+actual[key]);
};
const seekExact = (point, handle=timeline) => {
  if(!point || !Number.isInteger(point.input) || !Number.isInteger(point.substep) || point.input<0 || point.substep<0)
    throw Error('seekExact needs nonnegative integer input and substep.');
  checked(e.Game_Visualiser_Timeline_Seek(game,handle,point.input,point.substep));
  const result=physicalState(); assertHeader(result.header,point);
  if(result.champion.team!==team || result.champion.id!==actor) throw Error('Native perspective changed.');
  return result;
};
const seekReady = (point,handle=timeline) => seekExact({...point,actor,phase:point.phase??10,awaitingInput:true},handle);
const samePhysical = (a,b) => a.champion.infoHex===b.champion.infoHex && a.champion.gold===b.champion.gold &&
  a.champion.gems===b.champion.gems && JSON.stringify(a.submitted)===JSON.stringify(b.submitted) &&
  a.header.round===b.header.round && a.header.actor===b.header.actor && a.header.phase===b.header.phase;
const forkBeforeAction = ({startInput, maxCandidates=8, expected, requireReady=true}={}) => {
  if(!Number.isInteger(startInput) || startInput<0 || !Number.isInteger(maxCandidates) || maxCandidates<1 || maxCandidates>16)
    throw Error('forkBeforeAction needs startInput and a 1..16 candidate bound.');
  const before=physicalState(), attempts=[];
  if(expected) {
    if(expected.header) assertHeader(before.header,expected.header);
    if(expected.champion) for(const [key,value] of Object.entries(expected.champion))
      if(before.champion[key]!==value) throw Error('Pre-fork native champion mismatch for '+key);
  }
  if(before.header.actor!==actor || before.champion.team!==team) throw Error('Fork is not the selected owner.');
  for(let index=0;index<maxCandidates;++index) {
    const current=physicalState();
    if(!samePhysical(before,current)) throw Error('Native state changed while locating a pre-action fork.');
    const candidate=startInput+index;
    const status=e.Game_Visualiser_Timeline_Branch(game,timeline,candidate,out);
    if(status===0) {
      const handle=view().getBigUint64(out,true), after=physicalState();
      if(!samePhysical(before,after)) throw Error('Fork already changed native physical state.');
      if(requireReady && (!after.header.awaitingInput || after.header.phase!==10 || after.header.actor!==actor))
        throw Error('Fork is not an awaiting-input champion action boundary.');
      attempts.push({input:candidate,status,header:after.header});
      return {timeline:handle,input:candidate,before,after,attempts};
    }
    const diagnostic=cstring(e.GetLastErrorMsg());
    attempts.push({input:candidate,status,diagnostic,header:current.header});
    if(status!==18 || !/Cannot branch at an environment debug step/.test(diagnostic)) checked(status);
    // Branch considers the requested input; do not ReplayNext here, because
    // doing so could apply the very action whose pre-state is under test.
    if(!samePhysical(before,physicalState())) throw Error('Rejected branch changed native physical state.');
  }
  throw Error('No unchanged editable action boundary within the explicit candidate bound.');
};
const branchAt = (position,handle=timeline) => {
  if(!Number.isInteger(position)||position<0) throw Error('branchAt needs a nonnegative input position.');
  const before=physicalState();
  checked(e.Game_Visualiser_Timeline_Branch(game,handle,position,out));
  return {timeline:view().getBigUint64(out,true),position,before,after:physicalState()};
};
const advanceToOwner = ({round,targetActor=actor,fromActor,maxSteps=16}={}) => {
  if(!Number.isInteger(round)||!Number.isInteger(maxSteps)||maxSteps<1||maxSteps>32)
    throw Error('advanceToOwner needs a round and a 1..32 transition bound.');
  const transitions=[];
  let header=readHeader();
  if(header.round!==round) throw Error('Owner transition starts in another round.');
  if(header.phase===10 && header.awaitingInput && header.actor!==targetActor) {
    if(!Number.isInteger(fromActor)||header.actor!==fromActor)
      throw Error('The prior owner must be explicitly identified before ending its turn.');
    const before=header;
    // Frontend ir.Yielded=0; priorities are preserved, never invented here.
    checked(e.Game_Runner_EndTurn(game,0)); header=readHeader();
    transitions.push({operation:'EndTurn',result:0,before,after:header});
  }
  for(let index=0;index<maxSteps;++index) {
    if(header.round!==round) throw Error('Owner transition crossed the requested round.');
    if(header.phase===10 && header.awaitingInput && header.actor===targetActor)
      return {header,physical:physicalState(),transitions};
    if(![13,14].includes(header.phase)) throw Error('Unexpected phase while advancing to the selected owner: '+header.phase);
    const before=header;
    checked(e.Game_Runner_Step(game)); header=readHeader();
    transitions.push({operation:'Step',before,after:header});
  }
  throw Error('Owner transition exceeded its explicit step bound.');
};
const free = pointer => {
  const index=__pointers.indexOf(pointer);
  if(index<0) throw Error('Native pointer is not an owned live allocation.');
  e.Engine_Free(pointer); __pointers.splice(index,1);
};
// Callback allocations through either spelling participate in the same finally
// cleanup. Native functions and memory otherwise remain the exact exports.
const callbackExports=Object.freeze({...e,Engine_Malloc:__allocate,Engine_Free:free});
const context={e:callbackExports,game,view,checked,cstring,out,snapshot,perspective,input,timeline,timelineInputs,
  metadata,metadataPtr:metadata,nativeMetadataBytes,sdkMetadataPtr,sdkMetadataBytes,metadataBytes,metadataLayout,
  boardBytes,fieldBytes,width,height,stride,actor,team,
  map,geometry,geometryPath,enginePath,profile,replayPath,replaySha256:sha(bundle),payloadSha256:sha(payload),
  payloadBytes:Buffer.from(payload),setupProvenance:__provenance,allocate:__allocate,alloc:__allocate,free,
  readHeader,readChampion,physicalState,seekExact,seekReady,forkBeforeAction,branchAt,advanceToOwner};
const value=await __callback(context);
if(sha(fs.readFileSync(replayPath))!==sha(bundle)) throw Error('Native replay bytes changed during the callback.');
if(sha(fs.readFileSync(__tool))!==__provenance.toolSha256) throw Error('Native decoder changed during the callback.');
__verifyProfileInputs();
return value;
`;

/** One independent official engine instance; caller-provided callback may be async. */
export async function withNative(options, callback) {
  if (typeof callback !== 'function') throw Error('withNative needs an async callback.');
  const setup = generatedSetup();
  const body = `let __engine, __checked, __failure; let __game=0n; const __pointers=[];
try {
${setup.source}
${CONTEXT}
} catch(error) { __failure=error; throw error; }
finally {
  const failures=[];
  if(__engine && __game) try { __checked(__engine.Game_Destroy(__game)); } catch(error) { failures.push(String(error.message??error)); }
  if(__engine) for(const pointer of __pointers.reverse()) try { __engine.Engine_Free(pointer); } catch(error) { failures.push(String(error.message??error)); }
  if(failures.length) {
    if(__failure && typeof __failure==='object') __failure.cleanupFailures=failures;
    else throw Error('Native cleanup failed: '+failures.join('; '));
  }
}
//# sourceURL=fireflies-native-prefix.generated.mjs`;
  const run = new AsyncFunction('fs','path','os','crypto','zlib','WASI','PROFILE_MODULE_SHA256','PROFILE_SHA256',
    'REPLAY_PROFILES','profileById','__root','__argv','__callback','__provenance','__tool','__verifyProfileInputs',body);
  return run(fs,path,os,crypto,zlib,WASI,PROFILE_MODULE_SHA256,PROFILE_SHA256,REPLAY_PROFILES,profileById,
    ROOT,argumentList(options),callback,setup.provenance,TOOL,verifyProfileInputs);
}

export default withNative;
