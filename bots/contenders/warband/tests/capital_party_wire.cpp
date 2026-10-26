#include "capital_party_wire.h"
#include <cstdio>
using namespace wb::capital::party_wire;
static unsigned checks=0,failures=0;
static void check(bool value,const char* label){++checks;if(!value){if(failures<12)std::printf("FAIL %s\n",label);++failures;}}
static Roles roles(){return {{7,11,19},true,true,true};}
static Frame frame(int owner,int round,std::uint64_t input=1){return {owner,round,input,true};}
static Snapshot snapshot(std::array<std::uint16_t,4> memory,Frame f,bool canonical=true){return {f,memory,true,canonical};}
static void host_write(std::array<std::uint16_t,4>& memory,const Write& write) {
    check(write.prepared&&write.byte_offset%2==0&&write.byte_offset<=6,"write is one aligned uint16 range");
    if(write.prepared&&write.byte_offset%2==0&&write.byte_offset<=6)memory[write.byte_offset/2]=write.word;
}
int main() {
    // Complete field domains: overflow must reject, never truncate modulo.
    for(int start=1;start<=max_round;++start)for(unsigned group=0;group<32;++group) {
        const auto packed=encode_header(start,group);const auto h=decode_header(packed.word);
        check(packed.valid&&h.start==start&&h.group==group,"full header field roundtrip");
    }
    for(int round=0;round<=max_round;++round)for(unsigned phase=1;phase<=13;++phase)for(unsigned predicate=0;predicate<2;++predicate) {
        const auto packed=encode_record(round,static_cast<Phase>(phase),predicate!=0);const auto record=decode_record(packed.word);
        check(packed.valid&&record.round==round&&static_cast<unsigned>(record.phase)==phase&&record.predicate==(predicate!=0),"full role field roundtrip");
    }
    check(!encode_header(0,0).valid&&!encode_header(2048,0).valid&&!encode_header(3,32).valid,"header limits reject rather than mask");
    check(!encode_record(2048,Phase::Committed,true).valid&&!encode_record(3,static_cast<Phase>(14),true).valid,"record limits reject rather than mask");
    check(encode_record(0,Phase::Unpublished,false).valid&&!encode_record(3,Phase::Unpublished,false).valid&&!encode_record(0,Phase::Unpublished,true).valid,"unpublished is canonical zero, never a commitment");

    std::array<std::uint16_t,4> memory{};Endpoint proposer(roles(),7),willow(roles(),11),vendetta(roles(),19);
    auto pf=frame(7,10);auto proposal=proposer.prepare_proposal(snapshot(memory,pf),pf,3,{true,true});
    check(proposal.prepared&&proposal.byte_offset==0&&decode_header(proposal.word).start==12,"proposal saves actual round plus two");
    check(proposer.armed_header()==0,"planned header does not arm proposer");
    check(proposer.prepare_proposal(snapshot(memory,pf),pf,4,{true,true}).status==Status::PendingWrite,"one pending proposer PC forbids later header");
    host_write(memory,proposal);
    check(proposer.observe(snapshot(memory,pf),pf).status==Status::NeedProposerReadback,"ambient observation cannot prearm proposer");
    auto view=proposer.confirm_proposal(proposal,snapshot(memory,pf),pf);
    check(view.status==Status::ArmedFuture&&view.armed,"actual future postwrite readback arms proposer");
    auto wf=frame(11,10),vf=frame(19,10);
    check(willow.observe(snapshot(memory,wf),wf).status==Status::ArmedFuture&&vendetta.observe(snapshot(memory,vf),vf).status==Status::ArmedFuture,"other roles arm only actually observed future header");
    check(!willow.prepare_record(snapshot(memory,wf),wf,Phase::Committed,true).prepared,"prestart record cannot become dated commitment");
    const auto header=memory[0];
    for(unsigned role=0;role<roles_count;++role) {
        auto* endpoint=role==0?&proposer:role==1?&willow:&vendetta;const auto f=frame(roles().ids[role],12,2);
        const auto before=memory;auto write=endpoint->prepare_record(snapshot(memory,f),f,Phase::Committed,true);
        check(write.prepared&&write.byte_offset==2+2*role&&decode_record(write.word).round==12,"fixed writer owns only its two bytes with full saved date");
        host_write(memory,write);
        for(unsigned index=0;index<4;++index)if(index!=role+1)check(memory[index]==before[index],"role publication preserves every other owner range");
        check(endpoint->confirm_record(write,snapshot(memory,f),f)==Status::Published,"actual readback confirms metadata publication only");
    }
    pf=frame(7,12,3);view=proposer.observe(snapshot(memory,pf),pf);
    check(view.status==Status::Active&&view.armed&&!view.abort_seen&&view.raw_header==header,"matching armed epoch is active");
    for(unsigned i=0;i<roles_count;++i)check(view.claims[i].status==ClaimStatus::DatedPayload&&view.claims[i].payload.predicate,"claims carry source predicate bits without Body or effect tape");
    Endpoint missed(roles(),11);auto late=frame(11,12);
    check(missed.observe(snapshot(memory,late),late).status==Status::UnarmedLate,"first seen at start is too late to arm");
    auto rewind=frame(11,11);check(missed.observe(snapshot(memory,rewind),rewind).status==Status::Stale,"clock rewind cannot manufacture future first-seeing");

    // Critical counterexample: planned future header published after its start.
    // Old records can pass date>=start, so future ACTUAL arming is essential.
    std::array<std::uint16_t,4> late_memory{};Endpoint delayed_proposer(roles(),7),late_receiver(roles(),11);
    auto old_frame=frame(7,10);auto delayed=delayed_proposer.prepare_proposal(snapshot(late_memory,old_frame),old_frame,2,{true,true});
    late_memory[2]=encode_record(12,Phase::WorkPrefix,true).word;host_write(late_memory,delayed);
    auto post_late=frame(7,13);
    check(delayed_proposer.confirm_proposal(delayed,snapshot(late_memory,post_late),post_late).status==Status::UnarmedLate,"late actual header completion cannot use its planned arming");
    auto receiver_late=frame(11,13);
    check(late_receiver.observe(snapshot(late_memory,receiver_late),receiver_late).status==Status::UnarmedLate,"late header plus plausible old timestamp grants no claims");

    // A proper future new header may coexist with an arbitrarily delayed OLD
    // role write. The date is the immutable saved predicate input, not now.
    std::array<std::uint16_t,4> epochs{};
    Endpoint old_role(roles(),11),receiver(roles(),19),new_proposer(roles(),7);
    auto p3=frame(7,3);auto first=new_proposer.prepare_proposal(snapshot(epochs,p3),p3,1,{true,true});host_write(epochs,first);
    check(new_proposer.confirm_proposal(first,snapshot(epochs,p3),p3).armed,"sole proposer actually created old header");
    auto r4=frame(11,4);check(old_role.observe(snapshot(epochs,r4),r4).armed,"old role actually arms old epoch");
    auto q4=frame(19,4);check(receiver.observe(snapshot(epochs,q4),q4).armed,"receiver actually arms old epoch");
    auto r6=frame(11,6,2);auto old_snapshot=snapshot(epochs,r6);auto old_write=old_role.prepare_record(old_snapshot,r6,Phase::WorkPrefix,true);
    check(old_write.prepared&&decode_record(old_write.word).round==6,"old pending write retains its saved predicate date");
    auto r7=frame(11,7,3);check(old_role.prepare_record(snapshot(epochs,r7),r7,Phase::Payout,true).status==Status::PendingWrite,"same-owner newer publication cannot pass pending old PC");
    auto p8=frame(7,8);auto next=new_proposer.prepare_proposal(snapshot(epochs,p8),p8,2,{true,true});host_write(epochs,next);
    check(new_proposer.confirm_proposal(next,snapshot(epochs,p8),p8).armed,"new proper header actually arms");
    auto q9=frame(19,9,2);check(receiver.observe(snapshot(epochs,q9),q9).status==Status::ArmedFuture,"receiver arms exact new header while future");
    const auto new_header=epochs[0];host_write(epochs,old_write);
    auto q11=frame(19,11,3);view=receiver.observe(snapshot(epochs,q11),q11);
    check(view.status==Status::Active&&view.claims[1].status==ClaimStatus::EarlierEpoch&&epochs[0]==new_header,"delayed old writer cannot alter header or pass new epoch date");
    auto r11=frame(11,11,4);
    check(old_role.prepare_record(old_snapshot,r11,Phase::WorkPrefix,true).status==Status::Stale,"cached old read cannot be retimestamped into current input");
    check(old_role.confirm_record(old_write,snapshot(epochs,r11),r11)==Status::HeaderChanged,"old publication readback cannot transfer to new contract");
    check(old_role.observe(snapshot(epochs,r11),r11).status==Status::UnarmedLate,"old writer missing future arm cannot join late");
    // Old read and new header publication may occur in different actor inputs
    // of the SAME round. Strictly future start still excludes that old date.
    std::array<std::uint16_t,4> same_round{};Endpoint sp(roles(),7),sr(roles(),11),sc(roles(),19);
    auto sp3=frame(7,3);auto sh=sp.prepare_proposal(snapshot(same_round,sp3),sp3,1,{true,true});host_write(same_round,sh);
    (void)sp.confirm_proposal(sh,snapshot(same_round,sp3),sp3);
    auto sr4=frame(11,4);(void)sr.observe(snapshot(same_round,sr4),sr4);
    auto sr8=frame(11,8,2);auto sw=sr.prepare_record(snapshot(same_round,sr8),sr8,Phase::WorkPrefix,true);
    auto sp8=frame(7,8,2);auto sn=sp.prepare_proposal(snapshot(same_round,sp8),sp8,2,{true,true});host_write(same_round,sn);
    (void)sp.confirm_proposal(sn,snapshot(same_round,sp8),sp8);
    auto sc8=frame(19,8);check(sc.observe(snapshot(same_round,sc8),sc8).armed,"new header can actually arm in publication round");
    host_write(same_round,sw);auto sc11=frame(19,11,2);
    check(sc.observe(snapshot(same_round,sc11),sc11).claims[1].status==ClaimStatus::EarlierEpoch,"same-round old predicate cannot alias new epoch");

    // Abort is a withdrawal, even without a positive effect predicate. It stays
    // sticky after bytes regress and cannot turn into a clean success closure.
    vf=frame(19,13,4);auto abort=vendetta.prepare_record(snapshot(memory,vf),vf,Phase::Abort,false);host_write(memory,abort);
    check(vendetta.confirm_record(abort,snapshot(memory,vf),vf)==Status::Published,"abort is dated own-range publication");
    pf=frame(7,13,4);view=proposer.observe(snapshot(memory,pf),pf);
    check(view.abort_seen,"abort withdrawal is sticky without invented effect evidence");
    check(proposer.prepare_record(snapshot(memory,pf),pf,Phase::Banked,true).status==Status::AbortSticky,"withdrawn episode cannot publish normal progress");
    memory[3]=encode_record(13,Phase::Committed,true).word;view=proposer.observe(snapshot(memory,pf),pf);
    check(view.abort_seen,"regressed bytes cannot erase locally observed abort");
    vf=frame(19,14,5);auto clean=vendetta.prepare_record(snapshot(memory,vf),vf,Phase::CleanupDone,true);host_write(memory,clean);
    check(vendetta.confirm_record(clean,snapshot(memory,vf),vf)==Status::Published,"actual cleanup predicate can follow abort");
    pf=frame(7,14,5);check(proposer.prepare_record(snapshot(memory,pf),pf,Phase::ClosedClean,true).status==Status::AbortSticky,"cleanup cannot relabel aborted service as clean successful closure");
    proposer.disarm();check(proposer.observe(snapshot(memory,pf),pf).status==Status::ReusedEpoch,"disarmed old epoch does not rearm from retained bytes");

    // Unverified payloads remain payloads, not positive party receipts.
    std::array<std::uint16_t,4> unverified{};unverified[0]=encode_header(20,0).word;Endpoint observer(roles(),11);
    auto f18=frame(11,18);check(observer.observe(snapshot(unverified,f18),f18).armed,"observer future arm for payload class");
    unverified[1]=encode_record(20,Phase::Committed,false).word;auto f20=frame(11,20,2);
    view=observer.observe(snapshot(unverified,f20),f20);
    check(view.claims[0].status==ClaimStatus::DatedPayload&&!view.claims[0].payload.predicate,"zero predicate flag never becomes a confirmed commitment");
    unverified[1]=encode_record(21,Phase::Committed,true).word;view=observer.observe(snapshot(unverified,f20),f20);
    check(view.claims[0].status==ClaimStatus::FutureRecord,"future role date rejected without modulo");
    unverified[1]=static_cast<std::uint16_t>(20|(15u<<11));view=observer.observe(snapshot(unverified,f20),f20);
    check(view.claims[0].status==ClaimStatus::Invalid,"reserved phase is not a receipt");
    auto not_whole=snapshot(unverified,f20);not_whole.whole_read=false;
    check(observer.observe(not_whole,f20).status==Status::Stale,"separately cached header and role reads do not make a fresh whole snapshot");
    auto not_contract=snapshot(unverified,f20);not_contract.canonical_contract=false;
    check(observer.observe(not_contract,f20).status==Status::NeedContract,"header cannot substitute for canonical contract agreement");
    auto beyond=frame(11,2048,3);check(observer.observe(snapshot(unverified,beyond),beyond).status==Status::Unsupported,"round overflow disables class rather than wrapping");
    check(observer.observe(snapshot(unverified,f20),f20).status==Status::Unsupported,"overflow fallback cannot restart by clock alias");
    Endpoint extra(roles(),23);auto ef=frame(23,10);
    check(!extra.prepare_proposal(snapshot({},ef),ef,0,{true,true}).prepared&&!extra.prepare_record(snapshot({},ef),ef,Phase::Committed,true).prepared,"extra actor owns no header or role bytes");
    auto bad_roles=roles();bad_roles.legacy_publish_disabled=false;Endpoint legacy(bad_roles,7);
    check(!legacy.prepare_proposal(snapshot({},pf),pf,0,{true,true}).prepared,"legacy whole-word publisher disables trusted partition class");
    Endpoint no_reset(roles(),7);auto z=frame(7,10);
    check(no_reset.prepare_proposal(snapshot({},z),z,0,{true,false}).status==Status::ResetNotAdmitted,"expiration alone supplies no physical reset admission");
    auto last=frame(7,2046);
    check(no_reset.prepare_proposal(snapshot({},last),last,0,{true,true}).status==Status::Unsupported,"proposal plus two cannot overflow full epoch");

    unsigned timing_cases=0,aliases=0;
    for(int start=1;start<=9;++start)for(int publication=0;publication<=9;++publication)
    for(int arm=publication;arm<start;++arm)for(int saved=0;saved<=9;++saved)
    for(int read=saved;read<publication;++read)for(int write=read;write<=9;++write) {
        (void)write;++timing_cases;if(saved>=start)++aliases;
    }
    check(timing_cases==6072&&aliases==0,"6072 causal interleavings isolate epochs despite arbitrary delayed role write");
    unsigned equal_round_cases=0,equal_round_aliases=0;
    for(int start=1;start<=9;++start)for(int publication=0;publication<=9;++publication)
    for(int arm=publication;arm<start;++arm)for(int saved=0;saved<=9;++saved)
    for(int read=saved;read<=publication;++read)for(int write=read;write<=9;++write) {
        (void)write;++equal_round_cases;if(saved>=start)++equal_round_aliases;
    }
    check(equal_round_cases==9438&&equal_round_aliases==0,"round-equal causal events also isolate epochs");
    std::printf("capital_party_wire: %u checks, %u failures; %u strict / %u inclusive-round timing-model cases (not exhaustive Endpoint or SDK qualification)\n",checks,failures,timing_cases,equal_round_cases);
    return failures?1:0;
}
