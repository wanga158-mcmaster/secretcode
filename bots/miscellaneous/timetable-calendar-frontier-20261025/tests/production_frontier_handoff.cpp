#include "../src/production_frontier_handoff.h"
#include <array>
#include <cstdio>

namespace {
namespace h=tt::production_frontier_handoff;
int checks{},failures{};
void check(bool value,const char* text) {
    ++checks;if(!value){++failures;std::printf("FAIL %s\n",text);}
}
h::Key key() {return {100,17,23,31,{4,8},1,3};}
h::Frame frame(int epoch=101) {
    return {epoch,17,23,31,{4,8},1,3,true,true,true,true,true,h::Readiness::Valued};
}
// Pure caller ordering witness only: the adapter's action-tail reservation,
// actual SDK facts, exact valuation and full validation are supplied premises.
struct Caller {
    h::Receipt receipt;
    std::array<char,8> order{};unsigned used{},commands{},validations{},selections{};
    void ordinary_command() {order[used++]='A';++commands;}
    void input(h::Frame value,bool full_self_and_life) {
        order[used++]='G';
        if(receipt.opportunity(value)==h::Opportunity::Validate) {
            order[used++]='V';++validations;
            if(full_self_and_life) {++selections;order[used++]='S';receipt.consume();return;}
            receipt.close();
        }
        ordinary_command();
    }
};
void ready_pre_action() {
    Caller caller;check(caller.receipt.arm(key()),"funded actual publication arms a receipt");
    check(caller.receipt.opportunity(frame(100))==h::Opportunity::Deferred,
        "funded epoch itself does not invent a next-input opportunity");
    caller.input(frame(),true);
    check(caller.validations==1&&caller.selections==1&&caller.commands==0&&caller.used==3&&
        caller.order[0]=='G'&&caller.order[1]=='V'&&caller.order[2]=='S',
        "fresh next-input ready policy is checked before incumbent command");
    check(!caller.receipt.pending()&&!caller.receipt.checking()&&!caller.receipt.key(),
        "accepted selection retires publication provenance");
    check(caller.receipt.opportunity(frame(102))==h::Opportunity::Closed,
        "retired receipt cannot nominate again");
    Caller failed;failed.receipt.arm(key());failed.input(frame(),false);
    check(failed.validations==1&&failed.selections==0&&failed.commands==1&&
        failed.order[1]=='V'&&failed.order[2]=='A',
        "failed expensive self/Life validation immediately leaves funded incumbent action");
    check(failed.receipt.opportunity(frame(102))==h::Opportunity::Closed,
        "failed collector cannot retry and starve physical fallback");
}
void readiness_and_preparation() {
    for(const auto ready:{h::Readiness::None,h::Readiness::ProgrammeOnly}) {
        Caller caller;caller.receipt.arm(key());auto value=frame();value.readiness=ready;
        caller.input(value,true);
        check(caller.commands==1&&caller.validations==0&&caller.selections==0,
            "missing value or ProducerReady alone falls through the ordinary incumbent");
        check(caller.receipt.pending()&&caller.receipt.key()==key(),
            "a no-value check preserves original date and pending receipt");
        value.readiness=h::Readiness::Valued;
        check(caller.receipt.opportunity(value)==h::Opportunity::Deferred,
            "same-epoch eager redispatch cannot turn fallback into another check");
        value.origin.x++;
        check(caller.receipt.opportunity(value)==h::Opportunity::Closed,
            "actual move closes receipt even after a no-policy check at that epoch");
    }
    h::Receipt receipt;receipt.arm(key());auto preparing=frame();preparing.route_ready=false;
    check(receipt.opportunity(preparing)==h::Opportunity::Deferred&&receipt.pending(),
        "mandatory pure route preparation remains incumbent-owned");
    preparing.epoch=105;
    check(receipt.opportunity(preparing)==h::Opportunity::Deferred&&receipt.key()==key(),
        "elapsed route-preparation epochs preserve original physical provenance");
    preparing.route_ready=true;
    check(receipt.opportunity(preparing)==h::Opportunity::Validate,
        "new route-ready pre-action boundary can inspect an already-valued policy");
    check(receipt.opportunity(frame(106))==h::Opportunity::Closed,
        "one expensive attempt is consumed even if adapter has not yet closed it");
}
void premise_and_freshness_guards() {
    for(unsigned field=0;field<6;++field) {
        h::Receipt receipt;receipt.arm(key());auto changed=frame();
        switch(field) {
        case 0:++changed.query;break;case 1:++changed.body;break;case 2:++changed.field;break;
        case 3:++changed.origin.y;break;case 4:++changed.phase;break;default:++changed.group;break;
        }
        changed.readiness=h::Readiness::None;changed.route_ready=false;
        check(receipt.opportunity(changed)==h::Opportunity::Closed&&!receipt.pending(),
            "observed query/body/field/origin/incumbent mismatch closes before readiness");
    }
    h::Receipt receipt;receipt.arm(key());auto stale=frame(1000);stale.fresh_input=false;
    check(receipt.opportunity(stale)==h::Opportunity::Deferred&&receipt.pending()&&
        receipt.key()->funded_epoch==100&&receipt.last_checked_epoch()==-1,
        "round advancement alone cannot assert fresh self or silently redate a receipt");
    for(unsigned guard=0;guard<3;++guard) {
        auto unsafe=frame();if(guard==0)unsafe.quiet=false;
        else if(guard==1)unsafe.no_enemy=false;else unsafe.before_command=false;
        check(receipt.opportunity(unsafe)==h::Opportunity::Deferred&&receipt.pending(),
            "quiet/no-enemy/pre-command boundary remains an explicit caller premise");
    }
    check(receipt.opportunity(frame())==h::Opportunity::Validate&&receipt.checking(),
        "only the full fresh boundary requests independent self/Life validation");
    receipt.close();check(!receipt.key(),"explicit retirement releases provenance");
    auto invalid=key();invalid.group=-1;
    check(!receipt.arm(invalid)&&!receipt.pending(),"unnamed incumbent cannot arm handoff");
}
}
int main() {
    ready_pre_action();readiness_and_preparation();premise_and_freshness_guards();
    std::printf("production frontier handoff (pure, no SDK actions): %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
