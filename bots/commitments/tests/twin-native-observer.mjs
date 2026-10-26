// Read-only extension of the pinned official replay observer. The Python
// caller holds core.Slots; this script never starts a game, fork, or action.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const TOOL = path.join(ROOT, 'tools/inspect-strategy-replay.mjs');
const PIN = '9ab4e069aec4c7f8682f5ff484c8206f44139188598dca04bb5bad2f4a4abd4c';
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const original = fs.readFileSync(TOOL, 'utf8');
if (sha(Buffer.from(original)) !== PIN) throw Error('Pinned replay observer changed.');
let generated = original;
const replace = (before, after) => {
  if (generated.split(before).length !== 2) throw Error('Observer extension seam changed: ' + before);
  generated = generated.replace(before, after);
};
replace("from './replay-profiles.mjs'", `from ${JSON.stringify(new URL('../../../tools/replay-profiles.mjs', import.meta.url).href)}`);
replace("const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');", `const ROOT = ${JSON.stringify(ROOT)};`);
replace('const samples = [], captures = [], transitions = [], lastVisible = new Map();', `
const samples = [], captures = [], transitions = [], lastVisible = new Map();
const twinEligibility = [];
const twinQueryCounts = {champion:0, gems:0, errors:0};
const twinOut = e.Engine_Malloc(64);
const twinVisionOracle = {queries:0,mismatches:0};
for(const [name,arity] of Object.entries({Managed_Bitboard_Create:3,Managed_Bitboard_GetRawBufferSize:2,
  Managed_Bitboard_ImportRawBuffer:3,Managed_Bitboard_Test:4,Managed_Bitboard_Destroy:1}))
  if(typeof e[name]!=='function'||e[name].length!==arity) throw Error('Unsupported native vision oracle: '+name);
checked(e.Managed_Bitboard_Create(width,height,twinOut));
const twinVisionBoard=view().getBigUint64(twinOut,true);
checked(e.Managed_Bitboard_GetRawBufferSize(twinVisionBoard,twinOut));
const twinVisionBytes=view().getUint32(twinOut,true);
if(!twinVisionBytes||twinVisionBytes>67108864) throw Error('Invalid native vision storage size.');
const twinQuery = (name, id, bytes, count) => {
  if(typeof e[name] !== 'function' || e[name].length !== 4) throw Error('Unverified query arity: '+name);
  new Uint8Array(e.memory.buffer,twinOut,64).fill(0xa5);
  twinQueryCounts[count]++;
  const code=e[name](game,perspective,id,twinOut);
  if(code) {twinQueryCounts.errors++;checked(code);}
  if(new Uint8Array(e.memory.buffer,twinOut+bytes,64-bytes).some(value=>value!==0xa5))
    throw Error('Native query exceeded verified output width: '+name);
  return new DataView(e.memory.buffer);
};`);
replace('    const observedGroups = selected.map(group => {', `
    if(round >= from) {
      const owned = twinQuery('Game_API_Champion_Get',actor,44,'champion');
      const selfState=owned.getUint32(twinOut+40,true);
      const altars=(map.terrain?.altar??[]).map(tile=>{
        if(!Number.isSafeInteger(tile)||tile<0||tile>=width*height) throw Error('Unverified altar tile encoding.');
        return {x:tile%width,y:Math.floor(tile/width)};
      });
      if(!altars.length) throw Error('Exact geometry has no altar tiles.');
      checked(e.Managed_Bitboard_ImportRawBuffer(twinVisionBoard,buffer,twinVisionBytes));
      const oracle=[];
      const verifiedSees=(point,label)=>{
        if(!Number.isSafeInteger(point.x)||!Number.isSafeInteger(point.y)||point.x<0||point.y<0||point.x>=width||point.y>=height)
          throw Error('Visibility query outside verified coordinates.');
        new Uint8Array(e.memory.buffer,twinOut,64).fill(0xa5);
        checked(e.Managed_Bitboard_Test(twinVisionBoard,point.x,point.y,twinOut));twinVisionOracle.queries++;
        if(new Uint8Array(e.memory.buffer,twinOut+4,60).some(value=>value!==0xa5)) throw Error('Native vision oracle output width changed.');
        const raw=view().getInt32(twinOut,true);
        if(raw!==0&&raw!==1) throw Error('Native vision oracle is not Boolean.');
        const native=!!raw,decoded=sees(point);
        if(native!==decoded){twinVisionOracle.mismatches++;throw Error('Native/decoded personal visibility mismatch.');}
        oracle.push({label,x:point.x,y:point.y,native,decoded});return native;
      };
      if(me.health>0&&!verifiedSees(me,'self')) throw Error('Native personal vision does not contain living self.');
      const sameOwnerPeers=[];
      for(let index=0;index<u(172);++index) {
        const p=196+index*252;
        if(u(p+8)!==team||u(p+4)===actor) continue;
        const point={x:i(p+16),y:i(p+20)};
        const visible=point.x>=0&&point.y>=0&&point.x<width&&point.y<height&&verifiedSees(point,'peer:'+u(p+4));
        const record={id:u(p+4),personallyVisible:visible};
        if(visible) {
          const live=twinQuery('Game_API_Champion_Get',record.id,44,'champion');
          Object.assign(record,{team:live.getInt32(twinOut,true),type:live.getInt32(twinOut+4,true),
            x:live.getInt32(twinOut+8,true),y:live.getInt32(twinOut+12,true),
            hp:live.getFloat32(twinOut+16,true),stateBits:live.getUint32(twinOut+40,true)});
          const gems=twinQuery('Game_API_Champion_GetGems',record.id,4,'gems');
          record.gems=gems.getUint32(twinOut,true);
        }
        sameOwnerPeers.push(record);
      }
      twinEligibility.push({moment:{round,input:inputPosition,substep},self:{...me,stateBits:selfState},
        staging:round%16<=3,target:round-round%16+12,
        peers:sameOwnerPeers,altars:altars.map(tile=>({...tile,personallyVisible:verifiedSees(tile,'altar')})),visibilityOracle:oracle,
        observationScope:'same-owner personal visibility and legal native peer queries; no private runtime route/campaign/fuel reconstruction'});
    }
    const observedGroups = selected.map(group => {`);
replace('const result = { schemaVersion:', `const result = { twinEligibility, twinQueryCounts, twinVisionOracle,
  twinObserverExtension:{source:'bots/commitments/tests/twin-native-observer.mjs',parentToolSha256:${JSON.stringify(PIN)},
    intervention:'No native actions or world/private-state edits; only additional read-only queries.'}, schemaVersion:`);
replace('checked(e.Game_Destroy(game));', 'checked(e.Managed_Bitboard_Destroy(twinVisionBoard));\ne.Engine_Free(twinOut);\nchecked(e.Game_Destroy(game));');
const scratch = path.join(ROOT, '.cache/arena/orchestration/twin-exploration');
fs.mkdirSync(scratch,{recursive:true});
const filename=path.join(scratch,`native-observer-${process.pid}.mjs`);
fs.writeFileSync(filename,generated,'utf8');
const result=spawnSync(process.execPath,['--disable-warning=ExperimentalWarning',filename,...process.argv.slice(2)],
  {cwd:ROOT,encoding:'utf8',timeout:120000,maxBuffer:16*1024*1024,windowsHide:true});
if(result.error) throw result.error;
if(result.status!==0) throw Error('Native observer failed: '+result.stderr+'\n'+result.stdout);
const data=JSON.parse(result.stdout);
if(data.toolSha256!==sha(Buffer.from(generated))||sha(fs.readFileSync(filename))!==sha(Buffer.from(generated)))
  throw Error('Executed observer body differs from the generated UTF-8 bytes.');
data.twinObserverExtension.sha256=sha(fs.readFileSync(fileURLToPath(import.meta.url)));
data.twinObserverExtension.generatedSha256=sha(Buffer.from(generated));
console.log(JSON.stringify(data,null,2));
