// Focused read-only extension of the pinned official replay inspector.
// Python caller holds arena.core.Slots(1); no actions, forks or guest edits.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const ROOT=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const TOOL=path.join(ROOT,'tools/inspect-strategy-replay.mjs');
const PIN='9ab4e069aec4c7f8682f5ff484c8206f44139188598dca04bb5bad2f4a4abd4c';
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
let generated=fs.readFileSync(TOOL,'utf8');
if(sha(Buffer.from(generated,'utf8'))!==PIN)throw Error('Pinned inspector changed.');
if(process.argv.includes('all'))throw Error('Personal observer requires one owner.');
const replace=(before,after)=>{
 if(generated.split(before).length!==2)throw Error('Inspector extension seam changed: '+before);
 generated=generated.replace(before,after);
};
replace("from './replay-profiles.mjs'",`from ${JSON.stringify(new URL('../../../tools/replay-profiles.mjs',import.meta.url).href)}`);
replace("const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');",`const ROOT=${JSON.stringify(ROOT)};`);
replace('const samples = [], captures = [], transitions = [], lastVisible = new Map();',`
if(to>160)throw Error('Trailhead observation cutoff exceeded.');
const samples = [], captures = [], transitions = [], lastVisible = new Map();
const trailheadViews=[],trailheadQueries={champion:0,gems:0,errors:0};
const trailheadOracle={queries:0,mismatches:0};
const extra=e.Engine_Malloc(64);
for(const [name,arity] of Object.entries({Managed_Bitboard_Create:3,Managed_Bitboard_GetRawBufferSize:2,
 Managed_Bitboard_ImportRawBuffer:3,Managed_Bitboard_Test:4,Managed_Bitboard_Destroy:1}))
 if(typeof e[name]!=='function'||e[name].length!==arity)throw Error('Unsupported visibility ABI: '+name);
checked(e.Managed_Bitboard_Create(width,height,extra));const board=view().getBigUint64(extra,true);
checked(e.Managed_Bitboard_GetRawBufferSize(board,extra));const boardBytes=view().getUint32(extra,true);
if(!boardBytes||boardBytes>67108864)throw Error('Invalid native vision width.');
const personalQuery=(name,id,bytes,count)=>{
 if(typeof e[name]!=='function'||e[name].length!==4)throw Error('Unsupported personal query ABI: '+name);
 new Uint8Array(e.memory.buffer,extra,64).fill(0xa5);trailheadQueries[count]++;
 const code=e[name](game,perspective,id,extra);
 if(code){trailheadQueries.errors++;checked(code);}
 if(new Uint8Array(e.memory.buffer,extra+bytes,64-bytes).some(x=>x!==0xa5))throw Error('Personal native canary changed: '+name);
 return view();
};`);
replace('    const observedGroups = selected.map(group => {',`
    if(round>=from){
      checked(e.Managed_Bitboard_ImportRawBuffer(board,buffer,boardBytes));
      const oracle=[];
      const verifiedSees=(point,label)=>{
        if(!Number.isSafeInteger(point.x)||!Number.isSafeInteger(point.y)||point.x<0||point.y<0||point.x>=width||point.y>=height)
          throw Error('Point outside native vision bounds.');
        new Uint8Array(e.memory.buffer,extra,64).fill(0xa5);
        checked(e.Managed_Bitboard_Test(board,point.x,point.y,extra));trailheadOracle.queries++;
        if(new Uint8Array(e.memory.buffer,extra+4,60).some(x=>x!==0xa5))throw Error('Vision canary changed.');
        const raw=view().getInt32(extra,true);if(raw!==0&&raw!==1)throw Error('Vision membership is not Boolean.');
        const native=!!raw,decoded=sees(point);
        if(native!==decoded){trailheadOracle.mismatches++;throw Error('Native/decoded personal visibility mismatch.');}
        oracle.push({label,...point,native,decoded});return native;
      };
      if(me.health>0&&!verifiedSees(me,'self'))throw Error('Living self absent from personal vision.');
      const peers=[];
      for(let index=0;index<u(172);index++){
        const p=196+index*252,id=u(p+4);if(u(p+8)!==team)continue;
        const point={x:i(p+16),y:i(p+20)};
        const visible=point.x>=0&&point.y>=0&&point.x<width&&point.y<height&&verifiedSees(point,'ally:'+id);
        const peer={id,personallyVisible:visible};
        if(visible){
          const live=personalQuery('Game_API_Champion_Get',id,44,'champion');
          Object.assign(peer,{team:live.getInt32(extra,true),type:live.getInt32(extra+4,true),x:live.getInt32(extra+8,true),
            y:live.getInt32(extra+12,true),hp:live.getFloat32(extra+16,true),stateBits:live.getUint32(extra+40,true)});
          if(peer.team!==u(p+8)||peer.type!==u(p+12)||peer.x!==point.x||peer.y!==point.y||
            peer.hp!==d.getFloat32(snapshot+p+24,true))throw Error('Personal native header differs from timeline.');
          const gems=personalQuery('Game_API_Champion_GetGems',id,4,'gems');peer.gems=gems.getUint32(extra,true);
          if(peer.gems!==u(p+40))throw Error('Personal native bag differs from timeline.');
        }
        peers.push(peer);
      }
      const groups=selected.map(group=>({key:group.key,nativeId:group.nativeId,
        members:group.members.map(point=>({...point,personallyVisible:verifiedSees(point,'source:'+group.key)}))}));
      // This episode's named pad is public geometry, not a private target/history.
      const namedPad={x:20,y:27};
      trailheadViews.push({moment:{round,input:inputPosition,substep},self:me,peers,groups,
        group8Approach:{...namedPad,personallyVisible:verifiedSees(namedPad,'group8-approach')},oracle,
        scope:'Current owner personal view and legal visible ally queries; no private policy/campaign/route/admission reconstruction.'});
    }
    const observedGroups = selected.map(group => {`);
replace('const result = { schemaVersion:',`const result = {trailheadViews,trailheadQueries,trailheadOracle,
 trailheadObserver:{source:'bots/distributed/tests/trailhead-native-observer.mjs',parentToolSha256:${JSON.stringify(PIN)},
   nativeABI:{Game_API_Champion_Get:{parameters:4,outputBytes:44},Game_API_Champion_GetGems:{parameters:4,outputBytes:4}},
   actions:false,guestPrivateState:false,observationCutoff:160},schemaVersion:`);
replace('checked(e.Game_Destroy(game));','checked(e.Managed_Bitboard_Destroy(board));\ne.Engine_Free(extra);\nchecked(e.Game_Destroy(game));');
const scratch=path.join(ROOT,'.cache/arena/orchestration/trailhead-pair');
fs.mkdirSync(scratch,{recursive:true});const filename=path.join(scratch,`observer-${process.pid}.mjs`);
fs.writeFileSync(filename,generated,'utf8');
const run=spawnSync(process.execPath,['--disable-warning=ExperimentalWarning',filename,...process.argv.slice(2)],
 {cwd:ROOT,encoding:'utf8',timeout:180000,maxBuffer:24*1024*1024,windowsHide:true});
if(run.error)throw run.error;
if(run.status!==0)throw Error('Native observer failed: '+run.stderr+'\n'+run.stdout);
const data=JSON.parse(run.stdout);
if(sha(fs.readFileSync(filename))!==sha(Buffer.from(generated,'utf8')))throw Error('Generated observer UTF8 hash changed.');
if(data.toolSha256&&data.toolSha256!==sha(Buffer.from(generated,'utf8')))throw Error('Executed observer body changed.');
data.trailheadObserver.sha256=sha(fs.readFileSync(fileURLToPath(import.meta.url)));
data.trailheadObserver.generatedSha256=sha(Buffer.from(generated,'utf8'));
console.log(JSON.stringify(data,null,2));
