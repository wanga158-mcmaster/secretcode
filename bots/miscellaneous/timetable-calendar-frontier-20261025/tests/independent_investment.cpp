#include "capital_execution.h"
#include <cstdio>
#include <limits>
#include <type_traits>

namespace {
namespace capital=tt::capital_execution;
namespace investment=tt::production_investment;
using capital::Point;
int checks{},failures{};
void check(bool okay,const char* label) {
    ++checks;
    if(!okay){++failures;if(failures<=16)std::printf("FAIL %s\n",label);}
}
std::int64_t distance(Point a,Point b) {
    const auto dx=static_cast<std::int64_t>(a.x)-b.x;
    const auto dy=static_cast<std::int64_t>(a.y)-b.y;
    return std::max(dx<0?-dx:dx,dy<0?-dy:dy);
}
bool oracle(const std::array<investment::Member,investment::max_members>& members,
            std::size_t count,Point perch,int width,int height,float range,float radius) {
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const Point aim{x,y};
        if(static_cast<double>(distance(perch,aim))>range)continue;
        bool all=true;
        for(std::size_t i=0;i<count;++i)
            if(members[i].hp>0 && static_cast<double>(distance(members[i].pos,aim))>radius)all=false;
        if(all)return true;
    }
    return false;
}
capital::Key key() {
    capital::Key value;value.group=19;value.perch={8,22};value.gold=180;
    value.focus=100;value.strength=45;value.max_hp=value.hp=1200;
    value.member_count=2;value.member_hp[0]=120;value.member_hp[1]=220;
    value.inventory_count=4;value.inventory[0]=1;value.inventory[3]=2;
    return value;
}
void charged_return_checks() {
    using D=capital::ReturnDisposition;
    std::array<Point,capital::max_return_steps+1> path{};
    std::array<double,capital::max_return_steps+1> charge{};
    path[0]={0,0};path[1]={1,0};path[2]={2,0};path[3]={3,0};
    charge[0]=0;charge[1]=40;charge[2]=5;
    capital::ReturnWord word;
    check(word.build(path,charge,4,4,1) && word.kind==capital::ReturnClass::Charged,
          "charged return retains an explicit finite ordinary-step word");
    check(word.suffix[0]>=45 && word.suffix[0]<45.000001 &&
          word.suffix[1]==5 && word.suffix[2]==0 && word.suffix[3]==0,
          "suffix excludes origin but includes every destination End exactly once");
    check(word.global_reserve==40 && word.reserve[0]==40 && word.reserve[1]==40 &&
          word.reserve[2]==5 && word.reserve[3]==0,
          "global transition reserve retains the largest raw volley across both travel directions");
    check(word.reverse_prefix[0]==0 && word.reverse_prefix[1]==0 && word.reverse_prefix[2]==40 &&
          word.reverse_prefix[3]>=45 && word.reverse_prefix[3]<45.000001,
          "reverse prefix excludes current pose and charges actual reverse landings through quiet origin");
    capital::ReturnProgress progress;
    capital::ReturnObservation observation;
    observation.actual={0,0};observation.hp=1000;observation.incoming_multiplier=1;
    observation.raw_upper=0;observation.input=observation.fenced_input=11;observation.exact=true;
    auto result=capital::check_return(word,progress,observation);
    check(result.disposition==D::Ready && result.next.x==1 && result.next.y==0 &&
          result.required_hp>=85 && result.required_hp<85.000001,
          "fresh checked pose covers remaining destinations or a next-step reversal plus the global reserve");
    const auto required=result.required_hp;
    observation.hp=required;
    check(capital::check_return(word,progress,observation).disposition==D::Unsafe,
          "HP equal to the complete charge and reserve is not survival authority");
    observation.hp=std::nextafter(required,std::numeric_limits<double>::infinity());
    check(capital::check_return(word,progress,observation).disposition==D::Ready,
          "strictly positive headroom at the conservative arithmetic boundary is retained");
    observation.hp=1000;observation.incoming_multiplier=0;
    result=capital::check_return(word,progress,observation);
    check(result.disposition==D::Ready && result.required_hp==required,
          "even zero actual incoming multiplier receives no beneficial mitigation credit");
    for(const double invalid:{-1.0,1.01,std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()}) {
        observation.incoming_multiplier=invalid;
        check(capital::check_return(word,progress,observation).disposition==D::Invalid,
              "invalid actual incoming multiplier cannot certify a raw-damage envelope");
    }
    observation.incoming_multiplier=1;
    for(const double invalid:{0.0,-1.0,std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()}) {
        observation.hp=invalid;
        check(capital::check_return(word,progress,observation).disposition==D::Invalid,
              "dead, negative or nonfinite actual HP is unavailable");
    }
    observation.hp=1000;observation.exact=false;
    check(capital::check_return(word,progress,observation).disposition==D::Unknown,
          "unknown body or point provenance is not promoted by a stored route");
    observation.exact=true;observation.fenced_input=12;
    check(capital::check_return(word,progress,observation).disposition==D::Unknown,
          "cross-input observation composition has no fresh authority");
    observation.fenced_input=11;observation.actual={0,1};
    check(capital::check_return(word,progress,observation).disposition==D::Displaced,
          "displacement cannot reinterpret the saved suffix from another origin");
    observation.actual={0,0};observation.raw_upper=1;
    check(capital::check_return(word,progress,observation).disposition==D::Unknown,
          "larger actual point potential invalidates the retained static envelope");
    observation.raw_upper=0;
    for(const double invalid:{-1.0,std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()}) {
        observation.raw_upper=invalid;
        check(capital::check_return(word,progress,observation).disposition==D::Invalid,
              "fresh unknown numerical point potential cannot inherit a retained finite bound");
    }
    observation.raw_upper=0;
    check(capital::advance_return(word,progress,{0,0},false,11,11)==D::Blocked &&
          progress.index==0 && !progress.has_moved,
          "bool-false move preserves origin, complete remaining charge and input history");
    check(capital::advance_return(word,progress,{1,0},false,11,11)==D::Displaced && progress.index==0,
          "a false command accompanied by a different pose is displacement, not a step receipt");
    check(capital::advance_return(word,progress,{1,0},true,11,12)==D::Unknown && progress.index==0,
          "accepted-command flag cannot cross a stale result fence");
    check(capital::advance_return(word,progress,{2,0},true,11,11)==D::Displaced && progress.index==0,
          "successful command must read back exactly its declared adjacent destination");
    check(capital::advance_return(word,progress,{1,0},true,11,11)==D::Ready &&
          progress.index==1 && progress.has_moved && progress.last_move_input==11,
          "only an actual accepted and correctly read-back move advances the word");
    observation.actual={1,0};observation.raw_upper=40;
    check(capital::check_return(word,progress,observation).disposition==D::NeedNewInput,
          "same moved input cannot acquire another ordinary-step authority");
    observation.input=observation.fenced_input=12;
    result=capital::check_return(word,progress,observation);
    check(result.disposition==D::Ready && result.required_hp>=85 && result.required_hp<85.000001,
          "next actual input funds the prospective landing plus its reverse before admitting another step");
    check(capital::advance_return(word,progress,{2,0},true,12,12)==D::Ready,
          "a second input can record the next ordinary primitive");
    check(capital::advance_return(word,progress,{3,0},true,12,12)==D::NeedNewInput && progress.index==2,
          "even valid destination readback cannot record two moves in one input");
    check(capital::advance_return(word,progress,{3,0},true,13,13)==D::Ready && progress.index==3,
          "final accepted step records position without granting a same-input job continuation");
    observation.actual={3,0};observation.raw_upper=0;
    observation.input=observation.fenced_input=13;
    check(capital::check_return(word,progress,observation).disposition==D::NeedNewInput,
          "terminal arrival still waits for an actual new unmoved input");
    observation.input=observation.fenced_input=14;observation.raw_upper=1;
    check(capital::check_return(word,progress,observation).disposition==D::Unknown,
          "stored quiet terminal is insufficient when its fresh actual potential is positive");
    observation.raw_upper=0;observation.hp=1;
    const auto arrived=capital::check_return(word,progress,observation);
    check(arrived.disposition==D::Arrived && arrived.required_hp==0 && arrived.transition_reserve==0,
          "fresh quiet final pose needs positive life but no past-volley reserve or reverse-trip budget");
    ++progress.index;
    check(capital::check_return(word,progress,observation).disposition==D::Invalid,
          "oversized cursor cannot index beyond the retained route");

    capital::ReturnWord invalid;
    check(capital::check_return(invalid,{},observation).disposition==D::Unknown,
          "unbuilt return word cannot inherit body authority");
    check(!invalid.build(path,charge,0,4,1) && !invalid.valid &&
          !invalid.build(path,charge,capital::max_return_steps+2,4,1) &&
          !invalid.build(path,charge,4,0,1),
          "empty, oversized and invalid-map words reject before array access");
    charge[3]=1;
    check(!invalid.build(path,charge,4,4,1),"every declared return word must end at a quiet point");
    charge[3]=0;const auto saved=charge;
    for(const double bad:{-1.0,std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::quiet_NaN()}) {
        charge[1]=bad;
        check(!invalid.build(path,charge,4,4,1) && !invalid.valid,
              "negative or nonfinite destination charge never becomes a finite cap");
    }
    charge=saved;charge[0]=0;charge[1]=std::numeric_limits<double>::max();charge[2]=0;
    check(!invalid.build(path,charge,4,4,1),
          "unrepresentable sum with the global reserve is Unknown, never saturated");
    charge[0]=0;charge[1]=charge[2]=std::numeric_limits<double>::max();
    check(!invalid.build(path,charge,4,4,1),"overflow in the remaining destination sum invalidates the word");
    charge=saved;const auto saved_path=path;path[1]={0,0};
    check(!invalid.build(path,charge,4,4,1),"a repeated adjacent point is a wait, not a free ordinary step");
    path=saved_path;path[1]={3,0};
    check(!invalid.build(path,charge,4,4,1),"a nonadjacent jump is not an ordinary return primitive");
    path[1]={std::numeric_limits<int>::max()-1,0};
    check(!invalid.build(path,charge,4,std::numeric_limits<int>::max(),1),
          "huge coordinate differences are checked without signed-int overflow");
    path=saved_path;charge.fill(0);
    check(word.build(path,charge,4,4,1) && word.kind==capital::ReturnClass::Quiet && word.suffix[0]==0,
          "all-zero destination exposure is a supported quiet return class");
    check(word.build(path,charge,1,4,1),"a zero-edge word is supported when its sole point is quiet");
    observation.actual={0,0};observation.hp=1;observation.raw_upper=0;
    check(capital::check_return(word,{},observation).disposition==D::Arrived,
          "already-quiet origin needs no imaginary movement or damage payment");
    charge[0]=1;
    check(!invalid.build(path,charge,1,4,1),"a positive-potential zero-edge word has no quiet terminal");
    check(!invalid.build(path,charge,4,4,1),
          "positive origin cannot invent a quiet reverse-prefix recovery endpoint");
    path[0]={0,0};path[1]={1,0};path[2]={0,0};path[3]={1,0};path[4]={2,0};
    charge.fill(0);charge[1]=charge[3]=10;
    check(word.build(path,charge,5,3,1) && word.suffix[0]>=20 && word.suffix[0]<20.000001,
          "nonconsecutive revisits pay a fresh destination charge for each End");

    for(std::size_t i=0;i<path.size();++i){path[i]={static_cast<int>(i),0};charge[i]=(i&1u)?3:0;}
    check(word.build(path,charge,path.size(),static_cast<int>(path.size()),1) &&
          word.count==capital::max_return_steps+1 && word.suffix[0]>=768 && word.suffix[0]<768.000001,
          "the full 512-step word has finite bounded storage and retains every charged landing");

    // Independent interval schedules: forward completion, immediate reversal,
    // or one accepted next step followed by reversal. Enumerate one additional
    // End at every point of the represented word, including previously crossed
    // high-potential points on the reverse path. A later fresh guard may choose
    // retreat; this does not promise all future forward choices will remain safe.
    bool oracle_ok=true;std::uint64_t interval_cases{};
    for(int a=0;a<=7;++a)for(int b=0;b<=7;++b) {
        for(std::size_t i=0;i<4;++i)path[i]={static_cast<int>(i),0};
        charge.fill(0);charge[1]=a;charge[2]=b;
        oracle_ok&=word.build(path,charge,4,4,1);
        for(std::size_t index=0;index<4;++index) {
            long double future=0,back=0,next_back=0,worst=0;
            for(std::size_t destination=index+1;destination<4;++destination)future+=charge[destination];
            for(std::size_t reverse=index;reverse>0;--reverse)back+=charge[reverse-1];
            if(index+1<4) {
                next_back=charge[index+1];
                for(std::size_t reverse=index+1;reverse>0;--reverse)next_back+=charge[reverse-1];
                for(std::size_t refusal=0;refusal<4;++refusal)
                    worst=std::max({worst,future+charge[refusal],back+charge[refusal],next_back+charge[refusal]});
            }
            capital::ReturnProgress cursor;cursor.index=index;
            observation.actual=path[index];observation.hp=static_cast<double>(worst)+1;
            observation.raw_upper=charge[index];
            const auto bound=capital::check_return(word,cursor,observation);
            oracle_ok&=static_cast<long double>(bound.remaining_charge)>=future &&
                static_cast<long double>(bound.reverse_charge)>=back &&
                static_cast<long double>(bound.required_hp)>=worst &&
                bound.required_hp<static_cast<double>(worst)+0.000001 &&
                bound.disposition==(index==3?D::Arrived:D::Ready);
            ++interval_cases;
        }
    }
    check(oracle_ok && interval_cases==256,
          "charged envelope covers independently enumerated forward, immediate or next-step reverse schedules");

    path[0]={0,0};path[1]={1,0};path[2]={2,0};path[3]={3,0};
    charge.fill(0);charge[1]=charge[2]=100;
    check(word.build(path,charge,4,4,1),"late-prefix recovery example has a valid quiet-ended return word");
    capital::ReturnProgress late;late.index=2;late.has_moved=true;late.last_move_input=20;
    observation.actual=path[2];observation.raw_upper=100;observation.hp=150;
    observation.input=observation.fenced_input=21;
    const auto trap=capital::check_return(word,late,observation);
    check(trap.disposition==D::Unsafe && trap.remaining_charge==0 && trap.reverse_charge==100 &&
          trap.transition_reserve==100 && trap.required_hp>=300 && trap.required_hp<300.000001,
          "a cheap remaining suffix cannot hide a previously crossed expensive reverse prefix");
    capital::ReturnProgress early;early.index=1;
    observation.actual=path[1];observation.hp=201;
    const auto stop_before_trap=capital::check_return(word,early,observation);
    check(stop_before_trap.disposition==D::Unsafe && stop_before_trap.reverse_charge==0 &&
          observation.hp>stop_before_trap.reverse_charge+word.global_reserve,
          "prospective guard can refuse the next dangerous leg while current reverse remains payable");
    charge[2]=0;
    check(word.build(path,charge,4,4,1),"a later quiet intermediate point keeps the actual prefix");
    late.index=2;observation.actual=path[2];observation.raw_upper=0;observation.hp=150;
    const auto past_volley=capital::check_return(word,late,observation);
    check(past_volley.disposition==D::Unsafe && past_volley.reverse_charge==100 &&
          past_volley.transition_reserve==100,
          "global reserve retains a dangerous past source even after current and future points become quiet");
}
void paid_handoff_calendar_check() {
    // One full wolf is independently small enough to expose the two otherwise
    // easy-to-omit owner inputs: fourteen base casts versus thirteen after one
    // Focus shard saves eight rounds, so a seven-round trip wins by one while
    // the actual nine-round trip loses by one. Damage/capture/bank are identical.
    investment::CalendarInput input;
    auto& shop=input.shop;shop.gold=30;shop.body={1200,1200,45,100};shop.growth={100,5,10};
    shop.metadata_complete=shop.shop_at_declared_spawn=shop.stable_raw_stat_projection=true;
    for(std::size_t i=0;i<investment::family_count;++i) {
        auto& family=shop.families[i];family.has_result=true;
        family.shard={static_cast<std::uint32_t>(2*i),30,5,{0,0,1},investment::Passive::None,0,true};
        family.result={static_cast<std::uint32_t>(2*i+1),150,0,{0,0,5},investment::Passive::None,0,false};
    }
    input.now=100;input.deadline=1000;input.job_count=1;input.recall_to_first_shop_ready=5;
    input.quiet_decision=input.no_pending_capture_or_channel=input.stable_outgoing_supported=true;
    auto& job=input.jobs[0];job.group=14;job.generation=1;job.observed_round=100;
    job.available_from=100;job.valid_through=1000;job.member_count=1;
    job.members[0]={{3,0},1350,true};job.worker={0,0};job.aim={3,0};job.width=5;job.height=2;
    job.basic_range=1;job.cast_range=4;job.area_radius=1;job.cast_modifier=1;
    job.queried_current_damage=100;job.cooldown_period=8;job.ingress_shop=1;
    job.damage_to_capture=1;job.capture_payout=6;job.payout_to_bank=3;job.gems=11;
    job.full_group_known=job.availability_supported=job.geometry_supported=true;
    job.zero_incoming_through_continuation=job.team_displacement_accounted=job.residual_persists_while_away=true;
    const auto evaluate=[](const investment::CalendarInput& value) {
        investment::Valuation calendar(value);
        for(std::size_t slice=0;slice<1000 && calendar.status()==investment::Status::NeedCompute;++slice)
            calendar.advance(investment::max_advance_units);
        return calendar.result();
    };
    const auto omitted=evaluate(input);
    job.ingress_shop+=2; // actual route-planning input and final fresh unmoved input
    const auto paid=evaluate(input);
    check(omitted.status==investment::Status::Ready && paid.status==investment::Status::Ready &&
          omitted.keep.commands[0]==14 && omitted.invest.commands[0]==13 &&
          omitted.decision==investment::Decision::EarlierSameWork && omitted.same_work_time_saved==1 &&
          paid.decision==investment::Decision::Keep && paid.same_work_time_saved==-1 &&
          paid.invest.bank_round[0]==omitted.invest.bank_round[0]+2 &&
          paid.keep.bank_round[0]==omitted.keep.bank_round[0],
          "recall5 plus one buy and named route plus two real handoffs can turn apparent same-job profit into NoProfit");
}
}
int main() {
    static_assert(std::is_trivially_copyable_v<capital::Key>);
    static_assert(std::is_trivially_copyable_v<capital::Outcome>);
    std::array<investment::Member,investment::max_members> members{};
    Point aim{-77,-88};
    members[0]={{4,0},100,true};members[1]={{0,4},100,true};
    check(!capital::fixed_aim(members,2,{0,0},6,6,4,1,aim) && aim.x==-77 && aim.y==-88,
          "individually reachable members need one common center; failure leaves output unchanged");
    members[0]={{1,0},100,true};
    check(!capital::fixed_aim(members,1,{0,0},3,2,0.9f,0.9f,aim),
          "fractional cast and area distances are separately constrained on integer tiles");
    check(capital::fixed_aim(members,1,{0,0},3,2,1,0,aim) && aim.x==1 && aim.y==0,
          "exact cast boundary and zero area radius find the only center");
    members[0]={{2,2},100,true};members[1]={{4,2},50,true};
    check(capital::fixed_aim(members,2,{0,2},5,5,3,1,aim) && aim.x==3 && aim.y==2,
          "two touching area boxes produce a boundary center inside the cast box");
    members[2]={{-100,1000},0,true};
    check(capital::fixed_aim(members,3,{0,2},5,5,3,1,aim),
          "exact dead-member geometry cannot restrict living-member coverage");
    members[2].hp_exact=false;
    check(!capital::fixed_aim(members,3,{0,2},5,5,3,1,aim),
          "unknown death cannot be silently omitted from complete group support");
    members[0]={{0,0},0,true};
    check(!capital::fixed_aim(members,1,{0,0},1,1,0,0,aim),
          "all-dead array is not a damaging stationary job");
    members[0]={{0,0},1,true};
    check(!capital::fixed_aim(members,0,{0,0},1,1,0,0,aim) &&
          !capital::fixed_aim(members,members.size()+1,{0,0},1,1,0,0,aim) &&
          !capital::fixed_aim(members,1,{0,0},0,1,0,0,aim) &&
          !capital::fixed_aim(members,1,{-1,0},1,1,0,0,aim),
          "invalid counts, map shape or perch reject before indexing");
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    const auto inf=std::numeric_limits<float>::infinity();
    check(!capital::fixed_aim(members,1,{0,0},1,1,nan,0,aim) &&
          !capital::fixed_aim(members,1,{0,0},1,1,0,inf,aim) &&
          !capital::fixed_aim(members,1,{0,0},1,1,-1,0,aim),
          "nonfinite or negative geometry is unsupported");
    for(const float invalid:{nan,inf,-1.0f}) {
        members[0].hp=invalid;
        check(!capital::fixed_aim(members,1,{0,0},1,1,0,0,aim),
              "nonfinite or negative exact HP is unsupported");
    }
    members[0]={{std::numeric_limits<int>::max()-2,std::numeric_limits<int>::max()-2},1,true};
    check(capital::fixed_aim(members,1,{0,0},std::numeric_limits<int>::max(),
              std::numeric_limits<int>::max(),std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max(),aim) &&
          aim.x>=0 && aim.y>=0,
          "huge finite radii and map endpoints do not overflow float-to-int or signed coordinate sums");

    constexpr float radii[]{0,0.9f,1,1.9f,2.1f};
    bool exhaustive=true;std::uint64_t cases=0;
    for(int width=1;width<=3;++width)for(int height=1;height<=3;++height)
        for(int py=0;py<height;++py)for(int px=0;px<width;++px)
            for(int ay=0;ay<height;++ay)for(int ax=0;ax<width;++ax)
                for(int by=0;by<height;++by)for(int bx=0;bx<width;++bx)
                    for(const float range:radii)for(const float radius:radii) {
                        members[0]={{ax,ay},1,true};members[1]={{bx,by},2,true};
                        aim={-77,-88};
                        const bool got=capital::fixed_aim(members,2,{px,py},width,height,range,radius,aim);
                        const bool expected=oracle(members,2,{px,py},width,height,range,radius);
                        exhaustive&=got==expected;
                        if(got)exhaustive&=aim.x>=0 && aim.y>=0 && aim.x<width && aim.y<height &&
                            static_cast<double>(distance({px,py},aim))<=range &&
                            static_cast<double>(distance({ax,ay},aim))<=radius &&
                            static_cast<double>(distance({bx,by},aim))<=radius;
                        else exhaustive&=aim.x==-77 && aim.y==-88;
                        ++cases;
                    }
    check(exhaustive,"fixed aim agrees with independent brute-force aim enumeration on all small-map two-member cases");

    const auto original=key();capital::Negative negative;
    check(!negative.matches(original),"empty negative has no suppression authority");
    negative.remember(original,capital::Reason::NoProfit);
    check(negative.valid && negative.reason==capital::Reason::NoProfit && negative.matches(original),
          "completed NoProfit retains its exact physical and economic key");
    bool unchanged=true;
    for(std::uint32_t round=100;round<180;++round) {
        const auto cooldown=round%8;
        (void)cooldown;
        unchanged&=negative.matches(original);
    }
    check(unchanged,"a new external owner round or cooldown alone cannot erase a retained negative key");
    const auto changed=[&](capital::Key other,const char* label){check(!negative.matches(other),label);};
    auto other=original;other.group++;changed(other,"different group invalidates negative");
    other=original;other.perch.x++;changed(other,"different perch X invalidates negative");
    other=original;other.perch.y++;changed(other,"different perch Y invalidates negative");
    other=original;other.gold++;changed(other,"new spendable gold invalidates negative");
    other=original;other.focus++;changed(other,"raw Focus change invalidates negative");
    other=original;other.strength++;changed(other,"raw Strength change invalidates negative");
    other=original;other.max_hp++;changed(other,"maximum HP change invalidates negative");
    other=original;other.hp--;changed(other,"current HP change invalidates negative");
    other=original;other.member_count++;changed(other,"member shape metadata change invalidates negative");
    other=original;other.inventory_count++;changed(other,"inventory shape metadata change invalidates negative");
    for(std::size_t i=0;i<original.member_count;++i) {
        other=original;other.member_hp[i]--;
        changed(other,"every active member residual invalidates negative");
    }
    for(std::size_t i=0;i<original.inventory_count;++i) {
        other=original;other.inventory[i]++;
        changed(other,"every active inventory component invalidates negative");
    }
    other=original;other.member_hp.back()=nan;other.inventory.back()=99;
    check(negative.matches(other),"inactive array padding is not physical/economic key data");
    other=original;other.member_count=other.member_hp.size()+1;
    changed(other,"oversized member prefix cannot match or overrun storage");
    other=original;other.inventory_count=other.inventory.size()+1;
    changed(other,"oversized inventory prefix cannot match or overrun storage");
    other=original;other.focus=nan;changed(other,"NaN body cannot match an exact key");
    other=original;other.member_hp[0]=inf;changed(other,"nonfinite active residual cannot match");
    negative.remember(other,capital::Reason::Unsupported);
    check(!negative.valid && negative.reason==capital::Reason::None,
          "malformed key cannot acquire negative-cache authority");
    negative.remember(original,capital::Reason::NoProfit);negative.clear();
    check(!negative.valid && !negative.matches(original) && negative.reason==capital::Reason::None,
          "explicit clearing releases a completed negative");

    capital::Outcome outcome;
    check(outcome.conditional && !outcome.handled && !outcome.returned && !outcome.projection_matches &&
          outcome.group==-1 && outcome.reason==capital::Reason::None,
          "default outcome makes no native return or projection-readback claim");
    charged_return_checks();
    paid_handoff_calendar_check();
    std::printf("capital execution: %d checks, %d failures; %llu exhaustive geometry cases\n",
        checks,failures,static_cast<unsigned long long>(cases));
    return failures?1:0;
}
