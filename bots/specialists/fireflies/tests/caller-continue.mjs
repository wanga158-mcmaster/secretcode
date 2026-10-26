// Bounded fixed-input continuation. Every native frame must equal the retained
// reference except the documented timeline identity bytes [48,56).
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');

function frame(n){
  const header=n.readHeader();
  const raw=Buffer.from(new Uint8Array(n.e.memory.buffer,n.snapshot,n.profile.timelineSnapshotBytes));
  const bytes=Buffer.from(raw);bytes.fill(0,48,56);
  const d=new DataView(raw.buffer,raw.byteOffset,raw.byteLength),u=o=>d.getUint32(o,true),i=o=>d.getInt32(o,true);
  assert.equal(raw.length,40152);assert.ok(u(172)<=16&&u(176)<=500);
  const entities=[];
  for(const [count,start,stride] of [[u(172),196,252],[u(176),4228,28]])
    for(let j=0;j<count;++j){const p=start+j*stride;entities.push({kind:u(p),index:u(p+4),x:i(p+16),y:i(p+20),health:d.getFloat32(p+24,true)});}
  return {header,bytes,sha256:sha(bytes),physical:n.physicalState(),entities,
    step:{from:[i(80),i(84)],to:[i(88),i(92)],slot:u(108),type:u(112),priority:i(116)}};
}
const ready=h=>h.phase===10&&h.awaitingInput;
const ownerReady=h=>ready(h)&&h.actor===4&&h.kind===3&&h.stepPhase===14;
function distance(n,s){return Math.min(...n.map.terrain.altar.map(tile=>Math.max(Math.abs(tile%n.width-s.champion.x),Math.abs(Math.floor(tile/n.width)-s.champion.y))));}

export function collectContinuation(n){
  const first=frame(n);assert.ok(ownerReady(first.header));assert.equal(first.header.round,469);
  const frames=[first];let stop;
  for(let count=0;count<2000;++count){
    n.checked(n.e.Game_Visualiser_Timeline_ReplayNext(n.game,1));const current=frame(n);frames.push(current);
    assert.ok(current.header.round<=479,'fixed-input continuation remains within ten owner turns');
    if(ownerReady(current.header)&&distance(n,current.physical)<=4){stop='shared-nearby-altar-range';break;}
  }
  assert.equal(stop,'shared-nearby-altar-range','finite retained reference reaches the declared boundary');
  n.checked(n.e.Game_Visualiser_Timeline_Seek(n.game,n.timeline,first.header.input-1,0));
  let restored=false;
  for(let step=0;step<32;++step){const current=frame(n);if(current.bytes.equals(first.bytes)){restored=true;break;}n.checked(n.e.Game_Visualiser_Timeline_ReplayNext(n.game,1));}
  assert.ok(restored,'restore exact original starting snapshot after bounded reference collection');
  return {frames,stop,limit:{ownerRounds:10,nativeFrames:2000,nearestAltarStop:4},
    firstPacketHex:first.bytes.toString('hex')};
}

export function executeContinuation(n,guest,reference){
  const {frames}=reference,receipts=[],calls=[];
  let cursor=frames.findIndex((f,index)=>index>0&&f.header.kind===4&&f.header.actor===4&&f.header.round===469);
  assert.ok(cursor>0,'retained first owner movement exists');
  const first=frame(n);
  assert.ok(first.bytes.equals(frames[cursor].bytes),'actual first move equals full retained snapshot beyond timeline identity');
  receipts.push({operation:'initial-owner-move',referenceFrame:cursor,sha256:first.sha256,equal:true});
  for(++cursor;cursor<frames.length;++cursor){
    const expected=frames[cursor],before=frame(n);let operation,status=0;
    if(ready(before.header)){
      if([4,5].includes(expected.header.kind)){
        assert.equal(before.header.actor,expected.header.actor);assert.equal(before.header.round,expected.header.round);
        assert.equal(expected.header.stepActor,before.header.actor);
        if(before.header.actor===4){
          assert.ok(ownerReady(before.header),'actual guest is called exactly at selected owner start');
          const actionStart=guest.actions.length,bankStart=guest.bank.length,privateBefore=guest.privateState();
          guest.beginStage('continuation-'+before.header.round,100000);
          assert.equal(guest.instance.exports.fixture_turn(),3);
          const actual=frame(n),actions=guest.actions.slice(actionStart);
          calls.push({round:before.header.round,before:before.physical,after:actual.physical,privateBefore,privateAfter:guest.privateState(),actions,bank:guest.bank.slice(bankStart)});
          if(!actual.bytes.equals(expected.bytes)){
            const changedPhysicalFields=['x','y','health','maxHealth','gold','gems','stateBits'].filter(key=>actual.physical.champion[key]!==expected.physical.champion[key]);
            if(JSON.stringify(actual.physical.submitted)!==JSON.stringify(expected.physical.submitted))changedPhysicalFields.push('submitted');
            return {status:changedPhysicalFields.length?'physical-split':'native-metadata-difference',changedPhysicalFields,referenceFrame:cursor,before:before.physical,expected:expected.physical,actual:actual.physical,actions,receipts,calls,limit:reference.limit};
          }
          operation='actual-owner-caller';
        }else if(expected.header.kind===4){
          const actor=before.entities.find(e=>e.kind===1&&e.index===before.header.actor);
          assert.deepEqual([actor.x,actor.y],expected.step.from,'fixed peer move source');
          assert.equal(n.e.Game_Action_Move.length,4);status=n.e.Game_Action_Move(n.game,...expected.step.to,n.out);n.checked(status);assert.equal(n.view().getInt32(n.out,true),1);operation='fixed-peer-move';
        }else{
          const candidates=before.entities.filter(e=>e.health>0&&e.x===expected.step.to[0]&&e.y===expected.step.to[1]);
          assert.equal(candidates.length,1,'unique retained live peer attack target');const target=candidates[0];
          const pointer=n.alloc(8);try{n.view().setUint32(pointer,target.kind,true);n.view().setUint32(pointer+4,target.index,true);assert.equal(n.e.Game_Action_Attack.length,2);status=n.e.Game_Action_Attack(n.game,pointer);n.checked(status);}finally{n.free(pointer);}operation='fixed-peer-attack';
        }
      }else{
        // The next retained frame is the exact native Yielded result. A camp's
        // kind8 in automatic phases is handled by Step below, never a fake turn.
        if(expected.header.kind!==8)return {status:'protocol-obstruction',reason:'The next retained input has no independently resolved native action/target mapping; no passive input was substituted.',referenceFrame:cursor,before:before.physical,expectedHeader:expected.header,expectedStep:expected.step,receipts,calls,limit:reference.limit,unrecordedPassivePeerTurns:0};
        status=n.e.Game_Runner_EndTurn(n.game,0);n.checked(status);operation='fixed-retained-end-turn';
      }
    }else{status=n.e.Game_Runner_Step(n.game);n.checked(status);operation='native-automatic-step';}
    const after=frame(n);assert.ok(after.bytes.equals(expected.bytes),'full native retained frame '+cursor+' after '+operation);
    receipts.push({operation,status,referenceFrame:cursor,round:after.header.round,input:after.header.input,substep:after.header.substep,sha256:after.sha256,equal:true});
  }
  const final=frame(n);assert.ok(ownerReady(final.header));assert.ok(distance(n,final.physical)<=4);
  return {status:'same-physical-path-through-shared-nearby-range',stop:reference.stop,limit:reference.limit,
    beforeInitialMove:frames[0].physical,final:final.physical,nearestAltarDistance:distance(n,final.physical),
    referenceFrames:frames.length,receipts,calls,unrecordedPassivePeerTurns:0,peerPoliciesReexecuted:false};
}
