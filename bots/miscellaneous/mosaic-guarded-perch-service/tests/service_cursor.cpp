#include "service_cursor.h"
#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
using namespace mosaic::service_cursor;
void require(bool value, int line) {
    if (!value) { std::fprintf(stderr,"service cursor assertion at %d\n",line); std::exit(1); }
}
#define CHECK(condition) require(bool(condition), __LINE__)
Key key() { return {0,{3,0},{9,1,1,1},1,4,2,7,0,0}; }
const std::array<Raw,3> r18{{Raw::move({4,1}),Raw::move({4,0}),Raw::move({3,1})}};
bool occupied(Point p) { return p == Point(4,1) || p == Point(4,0); }
View fresh(Cursor& cursor,const Key& binding,Catalogue catalogue) {
    const auto view=cursor.bind(binding,catalogue);
    // Test event: parent has now completed the new packet and matched its key.
    return view.status==Status::deferred?cursor.acknowledge_fresh_packet(binding):view;
}

void r18_semantic_progress() {
    Cursor cursor;
    const auto binding = key();
    std::optional<Selection> previous;
    // Ryth at 4,1; Marcella at 4,0; 3,1 is free. These are truth-fixture
    // classifications, never occupancy facts retained by the cursor itself.
    for (int epoch=100;epoch<=102;++epoch) {
        const int revision = epoch*10;
        const int hp = 1000-(epoch-100)*11;
        const bool ready = epoch != 101;
        CHECK(revision>0&&hp>0&&(ready||epoch==101));
        const auto view=fresh(cursor,binding,{r18,true});
        CHECK(view.status==Status::ready&&view.selection);
        CHECK(view.selection->index==unsigned(epoch-100));
        CHECK(view.selection->raw==r18[unsigned(epoch-100)]);
        if(previous) {
            const auto stale=cursor.report(*previous,Outcome::complete_rejected);
            CHECK(!stale.accepted&&stale.view.problem==Problem::stale_selection);
        }
        const auto selection=*view.selection;
        previous=selection;
        const auto result=cursor.report(selection,occupied(selection.raw.destination)
            ?Outcome::complete_rejected:Outcome::applied);
        CHECK(result.accepted&&result.advanced);
        if(epoch<102) {
            CHECK(result.view.status==Status::deferred&&result.view.selection);
            CHECK(result.view.deferred_by==DeferredBy::next_packet);
            CHECK(result.view.selection->raw!=selection.raw);
        } else CHECK(result.view.status==Status::closed&&result.view.closed_by==ClosedBy::applied);
    }
    CHECK(cursor.bind(binding,{r18,true}).status==Status::closed);
}

void retain_partial_and_advance_classified() {
    Cursor cursor;
    const auto binding=key();
    auto view=cursor.bind(binding,{r18,true});
    const auto first=*view.selection;
    auto result=cursor.report(first,Outcome::partial_capture);
    CHECK(result.accepted&&!result.advanced&&result.view.status==Status::deferred);
    CHECK(result.view.deferred_by==DeferredBy::partial_capture&&result.view.selection->raw==first.raw);
    view=cursor.bind(binding,{r18,true});
    CHECK(view.selection->index==0&&view.selection->generation==first.generation);
    CHECK(view.status==Status::deferred);
    CHECK(!cursor.report(*view.selection,Outcome::complete_rejected).accepted);
    auto wrong_key=binding;++wrong_key.origin.x;
    CHECK(cursor.acknowledge_fresh_packet(wrong_key).problem==Problem::key_mismatch);
    CHECK(cursor.peek().status==Status::deferred&&cursor.peek().selection->index==0);
    result=cursor.report(*view.selection,Outcome::admission_deferred);
    CHECK(!result.advanced&&result.view.selection->raw==first.raw);
    view=fresh(cursor,binding,{r18,true});
    result=cursor.report(*view.selection,Outcome::complete_unsupported);
    CHECK(result.advanced&&result.view.selection->index==1);
    view=fresh(cursor,binding,{r18,true});
    result=cursor.report(*view.selection,Outcome::assessed_incomplete);
    CHECK(result.advanced&&result.view.selection->index==2);
    view=fresh(cursor,binding,{r18,true});
    result=cursor.report(*view.selection,Outcome::sdk_refused);
    CHECK(result.advanced&&result.view.status==Status::exhausted);

    Cursor refused;
    view=fresh(refused,binding,{r18,true});
    result=refused.report(*view.selection,Outcome::sdk_refused);
    CHECK(result.view.status==Status::deferred&&result.view.deferred_by==DeferredBy::fresh_reobservation);
    view=fresh(refused,binding,{r18,true});
    CHECK(view.status==Status::ready&&view.selection->index==1&&view.selection->raw!=first.raw);
}

void finite_exhaustion_and_explicit_release() {
    Cursor cursor;
    const auto binding=key();
    Selection old;
    for(unsigned i=0;i<r18.size();++i) {
        const auto view=fresh(cursor,binding,{r18,true});
        CHECK(view.selection&&view.selection->index==i);
        old=*view.selection;
        CHECK(cursor.report(old,Outcome::complete_rejected).accepted);
    }
    CHECK(cursor.peek().status==Status::exhausted&&!cursor.peek().selection);
    for(int new_nonce=0;new_nonce<100;++new_nonce)
        CHECK(cursor.bind(binding,{r18,true}).status==Status::exhausted);
    const auto reopened=cursor.rebase(binding,{r18,true},RebaseReason::observed_release);
    CHECK(reopened.status==Status::ready&&reopened.selection->index==0);
    CHECK(reopened.selection->raw==r18[0]&&reopened.selection->generation!=old.generation);
    CHECK(!cursor.report(old,Outcome::applied).accepted);
    // Explicit release only permits reconsidering the raw first nominee. It
    // says nothing about whether a new physical assessor will accept it.
    std::array<Raw,Cursor::capacity> full;
    for(unsigned i=0;i<full.size();++i)full[i]=Raw::move({int(i%9),int(i/9)});
    Cursor maximum;
    for(unsigned i=0;i<full.size();++i) {
        const auto view=fresh(maximum,binding,{full,true});
        CHECK(view.selection&&view.selection->index==i&&view.selection->raw==full[i]);
        CHECK(maximum.report(*view.selection,Outcome::complete_unsupported).accepted);
    }
    CHECK(maximum.peek().status==Status::exhausted);
}

void identity_and_catalogue_integrity() {
    Cursor cursor;
    CHECK(cursor.peek().status==Status::unknown&&cursor.peek().problem==Problem::unbound);
    auto binding=key();
    auto view=cursor.bind(binding,{r18,true});
    cursor.report(*view.selection,Outcome::complete_rejected);
    auto reversed=r18;std::reverse(reversed.begin(),reversed.end());
    view=cursor.bind(binding,{reversed,true});
    CHECK(view.status==Status::unknown&&view.problem==Problem::catalogue_mismatch&&!view.selection);
    view=cursor.bind(binding,{r18,true});
    CHECK(view.selection->index==1); // Restoring the known catalogue does not restart.
    for(int field=0;field<13;++field) {
        auto changed=key();
        if(field==0)++changed.actor;
        if(field==1)++changed.origin.x;
        if(field==2)++changed.origin.y;
        if(field==3)++changed.geometry.type;
        if(field==4)++changed.geometry.move_speed;
        if(field==5)++changed.geometry.basic_range;
        if(field==6)++changed.geometry.splash_radius;
        if(field==7)++changed.mode;
        if(field==8)++changed.group_id;
        if(field==9)++changed.group_cycle;
        if(field==10)++changed.goal_identity;
        if(field==11)++changed.body_generation;
        if(field==12)++changed.stat_generation;
        view=cursor.bind(changed,{r18,true});
        CHECK(view.status==Status::ready&&view.selection->index==0);
        cursor.report(*view.selection,Outcome::complete_rejected);
        for(int nonce=0;nonce<3;++nonce) {
            view=cursor.bind(changed,{r18,true});
            CHECK(view.selection->index==1);
        }
    }
    const std::array<Raw,2> duplicate{{Raw::move({1,1}),Raw::move({1,1})}};
    Cursor invalid;
    CHECK(invalid.bind(binding,{duplicate,true}).problem==Problem::duplicate);
    std::array<Raw,82> overflow{};
    CHECK(invalid.bind(binding,{overflow,true}).problem==Problem::capacity);
    const std::array<Raw,1> bad{{Raw::basic(-1)}};
    CHECK(invalid.bind(binding,{bad,true}).problem==Problem::invalid_raw);
}

void unknown_is_not_exhausted_and_actions_close() {
    Cursor cursor;
    const auto binding=key();
    CHECK(cursor.bind(binding,{{},false}).status==Status::unknown);
    CHECK(cursor.rebase(binding,{{},true},RebaseReason::explicit_family_rebase).status==Status::exhausted);
    const std::array<Raw,2> mixed{{Raw::move({4,1}),Raw::basic(3)}};
    auto view=cursor.rebase(binding,{mixed,false},RebaseReason::explicit_family_rebase);
    cursor.report(*view.selection,Outcome::complete_rejected);
    view=fresh(cursor,binding,{mixed,false});
    CHECK(view.selection->raw.kind==Kind::basic_camp&&view.selection->raw.camp_index==3);
    const auto result=cursor.report(*view.selection,Outcome::assessed_incomplete);
    CHECK(result.view.status==Status::unknown&&result.view.problem==Problem::incomplete_catalogue);
    for(const auto outcome:{Outcome::applied,Outcome::uncertain_effect,Outcome::error}) {
        view=cursor.rebase(binding,{r18,true},RebaseReason::explicit_family_rebase);
        CHECK(cursor.report(*view.selection,outcome).view.status==Status::closed);
        CHECK(!cursor.peek().selection&&cursor.bind(binding,{r18,true}).status==Status::closed);
        CHECK(cursor.acknowledge_fresh_packet(binding).status==Status::closed);
        view=cursor.rebase(binding,{r18,true},RebaseReason::physical_phase_committed);
        CHECK(view.status==Status::ready&&view.selection->index==0);
    }
}

void current_temporal_permission_retains() {
    struct CompleteCurrentBody { bool can_move{},can_attack{},stunned{}; };
    const std::array<CompleteCurrentBody,3> blocked{{{false,true,false},{true,false,false},{true,true,true}}};
    const auto binding=key();
    for(unsigned scenario=0;scenario<blocked.size();++scenario) {
        const auto body=blocked[scenario];
        CHECK(!body.can_move||!body.can_attack||body.stunned);
        const std::array<Raw,2> catalogue{{Raw::basic(9),
            scenario==1?Raw::basic(3):Raw::move({3,1})}};
        Cursor cursor;
        auto view=cursor.bind(binding,{catalogue,true});
        cursor.report(*view.selection,Outcome::complete_unsupported);
        view=fresh(cursor,binding,{catalogue,true});
        const auto second=*view.selection;
        CHECK(second.index==1);
        // The parent's complete current self packet classifies a temporal
        // permission/stun block, not candidate geometry or budget admission.
        const auto retained=cursor.report(second,Outcome::temporal_deferred);
        CHECK(retained.accepted&&!retained.advanced&&retained.view.status==Status::deferred);
        CHECK(retained.view.deferred_by==DeferredBy::temporal_state);
        CHECK(retained.view.selection->index==1&&retained.view.selection->raw==second.raw);
        CHECK(retained.view.selection->generation==second.generation);
        CHECK(cursor.bind(binding,{catalogue,true}).status==Status::deferred);
        CHECK(!cursor.report(second,Outcome::complete_rejected).accepted);
        // A later fresh ready body packet resumes this same second nominee.
        const CompleteCurrentBody ready{true,true,false};
        CHECK(ready.can_move&&ready.can_attack&&!ready.stunned);
        view=cursor.acknowledge_fresh_packet(binding);
        CHECK(view.status==Status::ready&&view.selection->index==1&&view.selection->raw==second.raw);
        CHECK(cursor.report(*view.selection,Outcome::applied).view.status==Status::closed);
        CHECK(!cursor.report(second,Outcome::temporal_deferred).accepted);
        CHECK(cursor.bind(binding,{catalogue,true}).status==Status::closed);
    }
    Cursor exhausted;
    const std::array<Raw,1> single{{Raw::move({3,1})}};
    const auto selection=*exhausted.bind(binding,{single,true}).selection;
    CHECK(exhausted.report(selection,Outcome::complete_rejected).view.status==Status::exhausted);
    CHECK(!exhausted.report(selection,Outcome::temporal_deferred).accepted);
    CHECK(exhausted.acknowledge_fresh_packet(binding).status==Status::exhausted);
}
} // namespace

int main() {
    r18_semantic_progress();
    retain_partial_and_advance_classified();
    finite_exhaustion_and_explicit_release();
    identity_and_catalogue_integrity();
    unknown_is_not_exhausted_and_actions_close();
    current_temporal_permission_retains();
    std::puts("service cursor: semantic progression, temporal retention/barriers, exhaustion/rebase and closure pass");
}
