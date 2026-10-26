import assert from 'node:assert/strict';
import fs from 'node:fs';
import crypto from 'node:crypto';
import {withNative} from './caller-native.mjs';
import {instantiateGuest} from './caller-host.mjs';
import {collectContinuation,executeContinuation} from './caller-continue.mjs';

const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const replay='results/population/fireflies-startup/witnesses/collision-b-repair.repl26a';
const replaySha='541d0bb12e35b2a83811915a4a48c432a644138e422c655ef05e014399253941';
const arm=process.argv[2]??'fireflies-on',mode=process.argv[3]??'checkpoint';
const build=process.argv[4]??'.cache/arena/orchestration/fireflies-caller/build';
assert.ok(['fireflies-on','fireflies-control','breakpoint'].includes(arm));
assert.ok(['checkpoint','partial0','partial1','recover','capture','channel','lowfuel','missing'].includes(mode));
assert.equal(sha(fs.readFileSync(replay)),replaySha);
const options=round=>({replay,actor:4,team:1,fromRound:round,toRound:round,mapId:'00collision'});
const bytes=n=>{n.readHeader();const b=Buffer.from(new Uint8Array(n.e.memory.buffer,n.snapshot,n.profile.timelineSnapshotBytes));b.fill(0,48,56);return b;};

function alignedFork(native,round,continueReference=false){
  const {e,game,timeline,out,view,checked}=native;
  let reference;
  for(let step=0;step<20000;++step){
    const h=native.readHeader();
    if(h.round===round&&h.actor===4&&h.phase===10&&h.stepPhase===14&&h.kind===3&&h.awaitingInput){reference=native.physicalState();break;}
    assert.ok(h.round<=round,'reference search stays in the single requested round');
    checked(e.Game_Visualiser_Timeline_ReplayNext(game,1));
  }
  assert.ok(reference,'actual ready owner start exists within bound');
  const canonical=bytes(native),attempts=[];
  const retained=continueReference?collectContinuation(native):null;
  for(let delta=1;delta<=12;++delta){
    const position=reference.header.input-delta;
    assert.ok(position>=0);
    checked(e.Game_Visualiser_Timeline_Seek(game,timeline,position,0));
    const parent=native.readHeader();
    if(parent.round!==round||parent.phase!==10||!parent.awaitingInput||parent.actor===4){attempts.push({position,parent,skip:'not retained preceding owner input'});continue;}
    const parentBytes=bytes(native);
    const status=e.Game_Visualiser_Timeline_Branch(game,timeline,position,out);
    if(status===18){assert.match(native.cstring(e.GetLastErrorMsg()),/environment debug step/);attempts.push({position,status});continue;}
    checked(status);
    const branch=view().getBigUint64(out,true);
    checked(e.Game_Visualiser_Timeline_Seek(game,branch,position,0));
    assert.deepEqual(bytes(native),parentBytes,'fork parent equals retained reference except timeline identity');
    const advanced=native.advanceToOwner({round,fromActor:parent.actor,targetActor:4,maxSteps:16});
    const current=bytes(native);
    if(!current.equals(canonical)){attempts.push({position,parent,skip:'prior input omits retained work',after:advanced.header});continue;}
    assert.equal(advanced.header.actor,4);assert.equal(advanced.header.phase,10);assert.equal(advanced.header.awaitingInput,true);
    return {reference,branchPosition:position,branch:branch.toString(),transitions:advanced.transitions,
      comparison:{maskedBytes:[48,56],snapshotBytes:canonical.length,referenceSha256:sha(canonical),forkSha256:sha(current),equal:true},attempts,retained};
  }
  throw Error('no exactly compatible prior-owner fork among twelve retained input boundaries');
}

const report={schema:1,case:'fireflies-collision-b-conditional-caller',arm,mode,admission:false,
  scope:'Actual reachable spawn-exit private checkpoint followed by an explicit native-world intervention; not the historical autonomous neighbor decision.',
  replay:{path:replay,sha256:replaySha},checks:{}};
try{
  let guest;
  await withNative(options(4),async native=>{
    report.certificateFork=alignedFork(native,4);delete report.certificateFork.retained;
    const before=native.physicalState();
    assert.equal(before.champion.type,10);assert.equal(before.champion.team,1);
    assert.equal(before.champion.health,before.champion.maxHealth,'full native HP at certificate start');
    const home=native.map.spawns.teamB;
    const atHome=p=>p.x>=home.minX&&p.x<home.maxX&&p.y>=home.minY&&p.y<home.maxY;
    assert.equal(atHome(before.champion),true);
    guest=await instantiateGuest(build+'/'+arm+'.wasm',native,{fuel:200000,stage:'boot'});
    for(let step=0;step<8;++step){
      const ready=guest.instance.exports.fixture_boot();
      if(ready===3)break;
      assert.ok(step<7,'actual bounded setup completes within eight declared calls');
      guest.beginStage('boot'+(step+1),200000);
    }
    assert.equal(guest.instance.exports.fixture_get_initialized(),1);assert.equal(guest.instance.exports.fixture_get_connected(),1);
    guest.beginStage('certificate',100000);
    const certificateStart=guest.trace.length;
    assert.equal(guest.instance.exports.fixture_turn(),3);
    const after=native.physicalState();
    assert.equal(atHome(after.champion),false,'actual production action exits own spawn');
    assert.equal(after.champion.team,1);assert.equal(after.champion.type,10);
    assert.equal(guest.instance.exports.fixture_get_recovering(),0,'real spawn reset remains false after actual legal exit');
    assert.ok(guest.actions.some(a=>a.stage==='certificate'&&a.operation==='move'&&a.success===true),'native engine executed certificate movement');
    report.certificate={before,after,privateState:guest.privateState(),traceStart:certificateStart,
      causalSource:'Actual turn resets recovery on spawn; a successful exit path cannot run farm no-selection or gold-recall afterward.',
      noFalseRecoverySetter:true};
    report.checks.reachable_offspawn_recovery_false=true;
  });
  const round=mode==='capture'?468:mode==='channel'?482:469;
  await withNative(options(round),async native=>{
    report.comparisonFork=alignedFork(native,round,mode==='checkpoint');
    const retained=report.comparisonFork.retained;delete report.comparisonFork.retained;
    if(retained)report.nativePacket={snapshotBytes:40152,normalizedTimelineIdentity:[48,56],hex:retained.firstPacketHex,sha256:sha(Buffer.from(retained.firstPacketHex,'hex'))};
    const memoryBefore=guest.memoryHash(),checkpointPrivate=guest.privateState();
    guest.setNative(native);
    const memoryAfter=guest.memoryHash();
    assert.equal(memoryAfter,memoryBefore,'world intervention retains the complete initialized guest memory');
    report.worldIntervention={checkpointPrivate,memoryBeforeSha256:memoryBefore,memoryAfterSha256:memoryAfter,equal:true,
      scope:'The native observable world changes; the existing WASM instance and all initialized private memory are retained. This is a declared conditional intervention, not an intervening historical trajectory.'};
    if(mode==='partial0'||mode==='partial1')assert.equal(guest.instance.exports.fixture_set_partial(Number(mode.slice(-1))),Number(mode.slice(-1)));
    if(mode==='recover')assert.equal(guest.instance.exports.fixture_force_recovering(),1);
    const privateBefore=guest.privateState();
    guest.beginStage('comparison',mode==='lowfuel'?29999:100000);
    if(mode==='missing')guest.setMissingQuery('champion');
    const before=native.physicalState(),traceStart=guest.trace.length,actionsStart=guest.actions.length;
    let failure=null;
    try{assert.equal(guest.instance.exports.fixture_turn(),3);}catch(error){failure=error.message;}
    const after=native.physicalState();
    report.comparison={round,before,after,privateBefore,privateAfter:guest.privateState(),traceStart,
      actions:guest.actions.slice(actionsStart),failure,bank:guest.bank.filter(b=>b.stage==='comparison')};
    report.trace=guest.trace;report.actions=guest.actions;report.telemetry=guest.logs;
    report.checks.complete_prefix=failure===null;
    report.checks.native_action_executed=report.comparison.actions.some(a=>a.success===true);
    report.fixtureStatus=failure?'incomplete':'executed';
    if(retained&&failure===null)report.continuation=executeContinuation(native,guest,retained);
  });
  assert.equal(sha(fs.readFileSync(replay)),replaySha);
}catch(error){report.fixtureStatus='incomplete';report.failure=error.message;report.cleanupFailures=error.cleanupFailures;process.exitCode=1;}
console.log(JSON.stringify(report,null,2));
