// Composed Capture-owner fixture; no native/fuel/strength qualification follows.
// Runtime and fixture call the SAME capture_service::dispatch seam; actor
// backends compose actual production payment, ordinary-point and capture guards.
//
// Capability being specified:
//   quiet Unknown -> admitted ALL-source word -> actual positive-support edge
//   -> next owner input still Unknown -> fresh paid leg 2 with the SAME owner
//   and actual reverse Trail -> observation at a quiet frontier -> fresh Reward
//   Step/Hold, or actual paid egress before Alive work revaluation.
//
// Current generic potential-entry pays ONE edge and its reverse. Its later
// return is correct for that limited contract. The first component below shows
// a full Ledger alternative; the ACTUAL shared runtime seam instead prices one
// next edge plus complete future reverse/refusal/two-input observation from
// current HP, retaining a finite descending geometry cursor and actual Trail.
// Initial P0 admission and charged continuation are distinct. No elapsed input
// manufactures observation, payment, receipt or progress.
//
// Unsupported premises / refinement boundary:
// * The fake actor supplies exact flags, HP, one interval per completed input,
//   point occupancy and a fresh personal-lifecycle oracle. No SDK/native fuel,
//   float32 rounding, actor-order micro, hidden champions or visibility engine
//   is qualified. Fixed raw NPC loss is intentionally a simple exact witness.
// * A bush screen near x6 can hide selected(8,2) from the earlier path despite
//   normal vision8; standing in its connected bush at x6 can reveal the source.
//   Raycasting and the separate personal-reveal/ward branch are premises here,
//   NOT facts derived from a fake reduced vision radius or an elapsed round.
// * Unknown intermediate reads occur before the move in the SAME input. No
//   additional observation-only charged yield is silently free. The Ledger
//   explicitly prices two waits at its quiet frontier and one maximum-origin
//   failure interval; refusal test spends one such charged interval.
// * Current capture authority requires zero EXTERNAL source exposure. Reward at
//   charged x2 therefore cannot capture yet; paid ingress/return owns that pose.
// * A Ready capture Hold is permission for one input, not actual timer progress,
//   receipt, release or renewed production. No such engine effects are invented.

#define TT_CAPTURE_FIXTURE
#include "../src/independent_capture.cpp"
#define TT_POTENTIAL_FIXTURE
#include "../src/independent_potential.cpp"
#define TT_ORDINARY_STEP_FIXTURE
#include "ordinary_step.h"
#include "production_walk.h"
#include "production_transit.h"
#include "capture_handoff.h"
#include "capture_service.h"
#define TT_REQUEST_PURE_ONLY
#include "../src/independent_request.cpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <vector>

namespace {
namespace cap=tt::capture_detail;
namespace pd=tt::potential_detail;
namespace pot=mb::production::potential;
namespace walk=tt::production_walk;
namespace transit=tt::production_transit;
using Point=pot::Point;
walk::Point wp(Point p) { return {p.x,p.y}; }
Point pp(walk::Point p) { return {p.x,p.y}; }
constexpr Point start{1,2},charged{2,2},external{3,3},frontier{6,2},selected{8,2};
struct Member { Point pos;float strength{};int range{}; };
struct Group { std::vector<Member> members; };
enum class Owner { LegacyWork,ObservationIngress,ReturnBeforeRevalue,Capture,Revalue,Acquire,Unknown };
struct Body
{
    Point pos{start};float hp{200};int epoch{10};
    bool alive{true},channel{},move{true},reveal{},selected_reward{true},selected_respawning{};
    bool block_forward{},block_return{},hidden_refusal{},capture_refusal{},reveal_enemy_on_refusal{},enemy_visible{};
    int attempts{},accepted{},capture_moves{},ordinary_moves{};
    std::uint32_t gems{};
    std::vector<Point> actual_edges;
};
struct OrdinaryActor
{
    Body& b;
    Point position() const { return b.pos; }
    float health() const { return b.hp; }
    pd::Flags flags() const { return {b.alive,b.channel,b.move}; }
    bool move_absolute(Point to)
    {
        ++b.attempts;
        if(b.hidden_refusal && to==Point{3,2})
        {if(b.reveal_enemy_on_refusal)b.enemy_visible=true;return false;}
        b.actual_edges.push_back(b.pos);b.actual_edges.push_back(to);
        b.pos=to;b.move=false;++b.accepted;++b.ordinary_moves;return true;
    }
};
struct CaptureActor
{
    Body& b;
    Point position() const { return b.pos; }
    float health() const { return b.hp; }
    cap::Bag bag() const { return {0,b.gems}; }
    cap::Flags flags() const { return {b.alive,b.channel,b.move}; }
    bool move_absolute(Point to)
    {
        ++b.attempts;
        if(b.capture_refusal){if(b.reveal_enemy_on_refusal)b.enemy_visible=true;return false;}
        b.actual_edges.push_back(b.pos);b.actual_edges.push_back(to);
        b.pos=to;b.move=false;++b.accepted;++b.capture_moves;return true;
    }
};
int checks{},failures{};
void check(bool yes,const char* text)
{++checks;if(!yes){++failures;std::printf("FAIL %s\n",text);}}

struct Scene
{
    // The selected source is NOT the reason charged(2,2) has positive support.
    // Its distance6 there is outside its range1; the external source supplies10.
    const std::vector<Group> groups{{{{selected,40,1}}},{{{external,10,1}}}};
    pot::Field field{12,6,{{selected,40,1},{external,10,1}}};
    Body body;
    walk::Trail history{24};
    std::optional<transit::Ledger> ledger;
    Owner owner{Owner::ObservationIngress};
    std::uint64_t graph{7},field_generation{3};

    Scene()
    {
        for(unsigned phase=0;phase<1000 && field.status()==pot::Status::NeedCompute;++phase)field.advance(64);
        check(field.status()==pot::Status::Ready,"bounded fixture preparation completes immutable field");
        history.anchor_quiet(wp(start));
    }
    bool quiet(Point p) const { const auto v=field.at(p);return v.status==pot::Status::Ready && v.upper==0; }
    transit::Key key() const { return {0,graph,field_generation,history.revision()}; }
    bool legal(Point p) const
    {
        return tt::ordinary_step_detail::may_attempt(p,12,6,
            [](Point){return false;},
            [&](Point q){return (body.block_forward && q==Point{3,2}) || (body.block_return && q==start);},
            [&](Point q){return q==external || (q==selected && !body.selected_reward && !body.selected_respawning);},
            [](Point){return false;});
    }
    // Exact simple source witness, NOT a simulation of real engine turn order.
    void next_input()
    {
        float loss=cap::distance(body.pos,external)<=1?10.0f:0.0f;
        if(!body.selected_reward && !body.selected_respawning && cap::distance(body.pos,selected)<=1)loss+=40;
        body.hp-=loss;++body.epoch;body.move=true;
    }
    cap::Result<Point> observe_capture(Point destination,bool staging=true)
    {
        CaptureActor actor{body};const auto expected=body.pos;
        return cap::authority(groups,0,expected,destination,actor,[&]{return body.epoch;},
            [&](int){return !body.reveal?cap::Life::Unknown:body.selected_respawning?cap::Life::Respawning:
                body.selected_reward?cap::Life::Reward:cap::Life::Alive;},
            [&]{return body.enemy_visible;},[&](Point p){return legal(p);},[]{return true;},staging);
    }
    void prepare_full_word(Owner requested)
    {
        owner=requested;
        const std::array<walk::Point,6> ingress{{{1,2},{2,2},{3,2},{4,2},{5,2},{6,2}}};
        check(history.can_retain_prefix(ingress,[&](walk::Point p){return quiet(pp(p));}),
            "existing Trail preflights complete future ordinary-walk storage");
        std::vector<Point> word{{1,2},{2,2},{3,2},{4,2},{5,2},{6,2},{5,2},{4,2},{3,2},{2,2},{1,2}};
        ledger.emplace(std::move(word),5,2,key(),24);
        for(unsigned phase=0;phase<64 && ledger->status()==pot::Status::NeedCompute;++phase)
        {
            ledger->advance(field,2);
            check(ledger->last_work()<=2,"retained pure charge construction obeys its declared unit limit");
            next_input(); // every pure preparation phase stays at actual P0
        }
        check(ledger->status()==pot::Status::Ready && body.pos==start && quiet(body.pos),
            "full ingress/waits/refusal/reverse word publishes at actual quiet origin");
    }
    pd::Result<Point> paid_leg()
    {
        OrdinaryActor actor{body};const auto before=body.pos;
        const auto leg=ledger?ledger->leg(key(),before,history.status()==walk::Status::Known):std::nullopt;
        if(!leg)return {};
        auto result=pd::transit_authority(leg->required_hp,before,leg->destination,actor,
            [&]{return body.epoch;},[&](Point p){return legal(p);},[]{return true;});
        if(result.moved)
        {
            check(history.record(wp(before),wp(body.pos),quiet(before),quiet(body.pos))==walk::Status::Known,
                "only actual accepted ordinary edge advances Trail");
            check(ledger->consume_applied(before,body.pos,key()),
                "only actual edge plus revised actual Trail consumes forward Ledger leg");
        }
        return result;
    }
    std::vector<Point> actual_reverse() const
    {std::vector<Point> out;for(const auto p:history.make_return(wp(body.pos)))out.push_back(pp(p));return out;}
    pd::Result<Point> paid_return()
    {
        OrdinaryActor actor{body};const auto before=body.pos;const auto reverse=actual_reverse();
        auto result=pd::authority(field,std::span<const Point>{reverse},true,actor,
            [&]{return body.epoch;},[&](Point p){return legal(p);},[]{return true;},true);
        if(result.moved)check(history.record(wp(before),wp(body.pos),quiet(before),quiet(body.pos))==walk::Status::Known,
            "freshly accepted reverse edge updates actual Trail instead of fabricating a phase return");
        return result;
    }
};

// The root's alternative staged contract is distinct from the full Ledger:
// retain target+actual Trail, freshly price ONE next edge + complete reverse +
// two landing waits + refusal from CURRENT HP, and advance a finite geometry/
// observation cursor or deliberately return. Incurred damage is sunk. This
// reference exercises that existing Work prefix composition without changing it.
std::vector<Point> legacy_work_prefix_reference()
{
    Scene s;s.owner=Owner::LegacyWork;
    for(unsigned steps=0;steps<5 && s.body.pos!=frontier;++steps)
    {
        const Point destination{s.body.pos.x+1,s.body.pos.y};
        const auto word=walk::prefix_word(s.history,wp(s.body.pos),wp(destination),
            [&](walk::Point p){return s.quiet(pp(p));});
        std::vector<Point> path;for(const auto p:word)path.push_back(pp(p));
        OrdinaryActor actor{s.body};const auto before=s.body.pos;
        const auto moved=pd::authority(s.field,std::span<const Point>{path},true,actor,
            [&]{return s.body.epoch;},[&](Point p){return s.legal(p);},[]{return true;},true,
            std::optional<Point>{destination},2);
        check(moved.status==pd::Status::Ready && moved.safe_return && moved.moved,
            "actual legacy Work prefix composition admits the next paid edge/waits/refusal/reverse");
        if(!moved.moved)break;
        check(s.history.record(wp(before),wp(s.body.pos),s.quiet(before),s.quiet(s.body.pos))==walk::Status::Known,
            "legacy reference records only accepted ordinary support");
        s.next_input();
    }
    check(s.body.pos==frontier && s.body.capture_moves==0 && s.owner==Owner::LegacyWork,
        "unchanged legacy Work capability can cross the positive external field without a Capture phase wrapper");
    return s.body.actual_edges;
}

std::vector<Point> capture_observation_to_reward()
{
    Scene s;s.prepare_full_word(Owner::ObservationIngress);
    const auto unknown=s.observe_capture(s.body.pos);
    check(unknown.status==cap::Status::NeedObservation && unknown.availability==cap::Life::Unknown && !unknown.safe_input,
        "actual capture adapter gives Unknown no Step/Hold authority");
    const auto initial=tt::capture_handoff::decide({true,true,false,s.quiet(s.body.pos),false,true,true,false,false});
    check(initial==tt::capture_handoff::Decision::ObservationIngress,"P0 handoff nominates initial ordinary ingress only");
    const auto first=s.paid_leg();
    check(first.status==pd::Status::Ready && first.safe_return && first.moved && s.body.pos==charged && !s.quiet(s.body.pos),
        "actual prepaid first leg lands in POSITIVE external all-source support");
    check(s.actual_reverse()==std::vector<Point>({charged,start}) && s.history.nodes().size()==2,
        "charged landing retains the actual reverse edge and original P0 anchor");
    s.next_input();
    const auto before=s.body.pos;const auto revision=s.history.revision();
    const auto still_unknown=s.observe_capture(s.body.pos);
    check(still_unknown.availability==cap::Life::Unknown && !still_unknown.moved && s.body.capture_moves==0 &&
        s.history.revision()==revision && s.actual_reverse()==std::vector<Point>({charged,start}),
        "next input Unknown observation neither spends movement nor resets owner/history");
    check(tt::capture_handoff::decide({true,true,false,false,false,true,true,false,false})==
        tt::capture_handoff::Decision::KeepGuardedCapture,
        "existing P0-only capture handoff deliberately does NOT authorize charged continuation");
    const auto second=s.paid_leg();
    check(s.owner==Owner::ObservationIngress && second.moved && before==charged && s.body.pos==Point{3,2} && !s.quiet(s.body.pos),
        "separate fresh Ledger authority preserves paid leg2 under unchanged Unknown owner");
    check(s.actual_reverse()==std::vector<Point>({{3,2},charged,start}),
        "second charged leg retains all actual reverse support");
    for(unsigned steps=0;steps<3 && s.body.pos!=frontier;++steps)
    {
        s.next_input();const auto observed=s.observe_capture(s.body.pos);
        check(observed.availability==cap::Life::Unknown && !observed.moved,"intermediate lifecycle query is still Unknown and grants no move");
        const auto next=s.paid_leg();
        check(next.moved,"every later ingress input obtains its own fresh paid leg");
        if(!next.moved)break;
    }
    check(s.ledger->at_frontier() && s.quiet(s.body.pos) && s.body.hp==170 && s.body.capture_moves==0,
        "complete paid ingress reaches quiet observation frontier without assuming selected life or capture progress");
    const auto ingress_edges=s.body.actual_edges;
    // Main visibility premise: ACTUAL arrival in the connected bush reveals
    // selected(8,2), rather than an unchanged pose becoming visible by a timer.
    s.body.reveal=true;
    const auto seen=s.observe_capture(s.body.pos);
    check(seen.status==cap::Status::Ready && seen.availability==cap::Life::Reward && seen.safe_input &&
        !seen.moved && !seen.holding,"actual reveal permits quiet staging but not a fictitious source Hold");
    s.next_input();const auto second_observation=s.observe_capture(s.body.pos);
    check(second_observation.status==cap::Status::Ready && second_observation.safe_input && !second_observation.moved,
        "second declared observation input re-reads actual Reward at quiet staging");
    s.next_input();
    auto result=s.observe_capture({7,2},false);
    check(result.status==cap::Status::Ready && result.availability==cap::Life::Reward && result.safe_input && result.moved,
        "only fresh Reward at ZERO EXTERNAL support authorizes actual selected-group capture ingress");
    if(result.status==cap::Status::Ready && result.availability==cap::Life::Reward && result.safe_input)s.owner=Owner::Capture;
    const auto from=frontier;check(s.history.record(wp(from),wp(s.body.pos),s.quiet(from),s.quiet(s.body.pos))==walk::Status::Known,
        "capture move also records only its actual edge");
    s.next_input();const auto step=s.observe_capture(selected,false);
    check(step.status==cap::Status::Ready && step.safe_input && step.moved && s.body.pos==selected,
        "next capture edge needs a new actual Reward guard");
    check(s.history.record({7,2},wp(s.body.pos),s.quiet({7,2}),s.quiet(s.body.pos))==walk::Status::Known &&
        s.actual_reverse()==std::vector<Point>({selected,{7,2},frontier}),
        "each capture edge retains actual reverse history through the ownership transition");
    s.next_input();const auto hold=s.observe_capture(selected,false);
    check(s.owner==Owner::Capture && hold.status==cap::Status::Ready && hold.safe_input && hold.holding && !hold.moved,
        "capture Hold independently re-establishes actual Reward; no receipt is invented");
    return ingress_edges;
}

void refusal_and_actual_return()
{
    Scene s;s.prepare_full_word(Owner::ObservationIngress);check(s.paid_leg().moved,"refusal branch first edge accepted");
    s.next_input();const auto reverse=s.actual_reverse();const auto revision=s.history.revision();
    const auto origin=s.ledger->current_origin();s.body.hidden_refusal=true;
    const auto refused=s.paid_leg();
    check(refused.status==pd::Status::NeedObservation && !refused.moved && s.body.pos==charged && s.body.move &&
        s.history.revision()==revision && s.actual_reverse()==reverse && s.ledger->current_origin()==origin,
        "refused OFF-RETURN forward edge spends no allowance and consumes neither actual Trail nor Ledger");
    s.owner=Owner::ReturnBeforeRevalue;s.ledger->invalidate(); // forward proof only
    s.next_input(); // explicitly spend the one charged refusal interval already budgeted
    check(s.body.hp==180 && s.actual_reverse()==std::vector<Point>({charged,start}),
        "new return owner retains paid historical reverse across a real charged interval");
    const auto returned=s.paid_return();
    check(returned.status==pd::Status::Ready && returned.moved && s.body.pos==start && s.quiet(s.body.pos),
        "after refusal actual fresh return command reaches P0 before any task reset/revaluation");
    if(returned.moved)s.owner=Owner::Acquire;
    check(s.owner==Owner::Acquire && s.history.nodes().size()==1,
        "phase completion follows actual return rather than declaring displacement by reset");
}

void changed_forward_support_and_alive_handoff()
{
    Scene s;s.prepare_full_word(Owner::ObservationIngress);check(s.paid_leg().moved,"Alive branch first edge accepted");
    s.next_input();++s.graph;
    check(!s.ledger->leg(s.key(),s.body.pos,true),"changed forward geometry rejects the old ingress Ledger identity");
    // Conditional fresh-personal-reveal branch: e.g. a separately legal own ward
    // revealed the selected source this input. Its legality/fuel is not modelled.
    s.body.reveal=true;s.body.selected_reward=false;
    const auto alive=s.observe_capture(s.body.pos);
    check(alive.status==cap::Status::Ready && alive.availability==cap::Life::Alive && !alive.safe_input && !alive.moved,
        "fresh Alive provides observation only and invalidates the stale Capture hypothesis");
    s.owner=Owner::ReturnBeforeRevalue;
    check(s.actual_reverse()==std::vector<Point>({charged,start}),
        "new lifecycle/task owner does not erase still-supported actual reverse history");
    const auto returned=s.paid_return();
    check(returned.moved && s.body.pos==start && s.body.capture_moves==0,
        "Alive revaluation executes paid actual egress under unchanged all-source field and fresh point guards");
    if(returned.moved)s.owner=Owner::Revalue;
    check(s.owner==Owner::Revalue,"Work revaluation is entered only after actual quiet return");
}

void blocked_return_is_not_invented()
{
    Scene s;s.prepare_full_word(Owner::ObservationIngress);check(s.paid_leg().moved,"blocked-return first edge accepted");
    s.next_input();s.owner=Owner::ReturnBeforeRevalue;s.body.block_return=true;
    const auto revision=s.history.revision();const auto blocked=s.paid_return();
    check(blocked.status==pd::Status::NeedObservation && !blocked.moved && s.body.pos==charged &&
        s.owner==Owner::ReturnBeforeRevalue && s.history.revision()==revision,
        "a blocked reverse tile does not fabricate an escape, phase completion or alternate path");
    s.next_input();s.body.block_return=false; // ACTUAL changed occupancy premise
    check(s.paid_return().moved && s.body.pos==start,"changed free return tile is rechecked before actual return");
    Scene displaced;displaced.prepare_full_word(Owner::ObservationIngress);displaced.paid_leg();
    displaced.body.pos={2,3}; // an external displacement, not an ordinary edge
    check(displaced.actual_reverse().empty(),"actual-origin mismatch withdraws historical return support");
}
void charged_reward_still_needs_real_return()
{
    Scene s;s.prepare_full_word(Owner::ObservationIngress);s.paid_leg();s.next_input();
    s.body.reveal=true; // separate fresh reveal premise, not inferred from time
    const auto reward=s.observe_capture(s.body.pos);
    check(reward.availability==cap::Life::Reward && reward.status==cap::Status::NeedObservation &&
        reward.current_damage==10 && reward.next_damage==10 && !reward.safe_input && !reward.moved,
        "fresh Reward removes selected group only; positive EXTERNAL support still forbids Capture Step/Hold");
    s.owner=Owner::ReturnBeforeRevalue;
    check(s.paid_return().moved && s.body.pos==start && s.body.capture_moves==0,
        "fresh Reward at externally charged pose retains real paid return instead of inventing safe capture");
}
void unresolved_frontier_is_not_capture()
{
    // Different visibility premise from the connected-bush positive witness:
    // this proposed observation frontier does NOT actually reveal the source
    // (e.g. another occluder remains). No later clock flips the oracle to known.
    Scene s;s.prepare_full_word(Owner::ObservationIngress);
    for(unsigned steps=0;steps<5 && s.body.pos!=frontier;++steps)
    {const auto next=s.paid_leg();check(next.moved,"unresolved frontier retains fresh paid ingress");if(!next.moved)break;s.next_input();}
    const auto unresolved=s.observe_capture(s.body.pos);
    check(unresolved.availability==cap::Life::Unknown && !unresolved.safe_input && !unresolved.moved &&
        !s.ledger->next_destination() && s.actual_reverse()==std::vector<Point>({frontier}),
        "finite exhausted ingress cannot invent visibility, another leg or capture Hold at an unresolved frontier");
    s.owner=Owner::Unknown;
    check(s.quiet(s.body.pos) && s.body.capture_moves==0,"unsupported observer family ends at actual quiet support without claiming production");
}

namespace cs=tt::capture_service;
cs::Point cp(Point p){return {p.x,p.y};}
struct DispatchBackend
{
    Scene& scene;bool enemies{},route_ready{true},route_capture{true},has_headroom{true},lose_support{};
    cs::GeometryCursor geometry{};
    int forward_calls{},return_calls{},contact_reads{},candidate_attempts{};
    cs::Frame frame()
    {
        const auto observed=scene.observe_capture(scene.body.pos);
        cs::Frame frame;frame.epoch=scene.body.epoch;frame.group=0;frame.pos=cp(scene.body.pos);
        frame.alive=scene.body.alive;frame.channeling=scene.body.channel;frame.quiet=scene.quiet(scene.body.pos);
        frame.enemy_known=true;frame.enemy=enemies || scene.body.enemy_visible;frame.read=cs::Read::Fresh;
        if(observed.availability==cap::Life::Alive)frame.life=cs::Life::Alive;
        else if(observed.availability==cap::Life::Reward)frame.life=cs::Life::Reward;
        else if(observed.availability==cap::Life::Respawning)frame.life=cs::Life::Respawning;
        frame.safe_capture=observed.status==cap::Status::Ready && observed.safe_input;
        frame.receipt=observed.gems>0;
        frame.route_ready=route_ready && route_capture;frame.distance=selected.x-scene.body.pos.x;
        frame.needs_geometry=!frame.route_ready && scene.body.pos!=selected;frame.geometry=geometry;
        frame.graph=scene.graph;frame.field=scene.field_generation;return frame;
    }
    bool fresh(const cs::Frame& frame) const
    {return frame.epoch==scene.body.epoch && frame.pos==cp(scene.body.pos);}
    bool headroom() const {return has_headroom;}
    cs::Contact refresh_contact(const cs::Frame& frame)
    {
        ++contact_reads;if(!fresh(frame))return cs::Contact::Stale;
        return enemies || scene.body.enemy_visible?cs::Contact::Revalue:cs::Contact::Clear;
    }
    std::array<Point,2> candidates(const cs::Frame& frame) const
    {return {{{frame.pos.x+1,frame.pos.y},{frame.pos.x+1,frame.pos.y+1}}};}
    cs::Step convert(const pd::Result<Point>& result,int distance=-1)
    {
        if(result.status==pd::Status::Ready && result.moved)
            return {cs::StepStatus::Applied,cp(scene.body.pos),distance,scene.quiet(scene.body.pos)};
        if(result.status==pd::Status::Stale)return {cs::StepStatus::Stale};
        if(result.status==pd::Status::NeedCompute)return {cs::StepStatus::NeedCompute};
        if(result.status==pd::Status::Ready && !result.can_move)return {cs::StepStatus::Spent};
        return {result.status==pd::Status::NeedObservation?cs::StepStatus::Refused:cs::StepStatus::Denied};
    }
    cs::Step attempt(const cs::Frame& frame,Point destination)
    {
        ++candidate_attempts;const auto before=scene.body.pos;
        const auto tail=scene.history.make_return(wp(before),wp(destination));
        if(tail.empty())return {};
        std::vector<Point> word{before};for(const auto p:tail)word.push_back(pp(p));
        OrdinaryActor actor{scene.body};
        const auto moved=pd::authority(scene.field,std::span<const Point>{word},true,actor,
            [&]{return scene.body.epoch;},[&](Point p){return scene.legal(p);},[&]{return has_headroom;},true,
            std::optional<Point>{destination},2);
        if(moved.moved)
        {
            check(frame.pos==cp(before),"shared dispatcher binds the actual pre-command pose");
            check(scene.history.record(wp(before),wp(scene.body.pos),scene.quiet(before),scene.quiet(scene.body.pos))==walk::Status::Known,
                "runtime-style paid next edge records actual reverse support before ownership continues");
            if(lose_support)return {cs::StepStatus::SupportLost,cp(scene.body.pos)};
        }
        return convert(moved,selected.x-scene.body.pos.x);
    }
    cs::Step advance(const cs::Frame& frame)
    {++forward_calls;return cs::guarded_advance(frame,*this);}
    cs::Step retreat(const cs::Frame& frame)
    {
        const auto contact=refresh_contact(frame);
        if(contact==cs::Contact::Stale)return {cs::StepStatus::Stale};
        if(contact!=cs::Contact::Clear)return {cs::StepStatus::NeedContact};
        ++return_calls;const auto before=scene.body.pos;const auto path=scene.actual_reverse();
        if(path.size()<2)return {};
        OrdinaryActor actor{scene.body};
        const auto moved=pd::authority(scene.field,std::span<const Point>{path},true,actor,
            [&]{return scene.body.epoch;},[&](Point p){return scene.legal(p);},[&]{return has_headroom;},true);
        if(moved.moved)check(scene.history.record(wp(before),wp(scene.body.pos),scene.quiet(before),scene.quiet(scene.body.pos))==walk::Status::Known,
            "shared return owner records only an actually accepted reverse edge");
        if(moved.moved && lose_support)return {cs::StepStatus::SupportLost,cp(scene.body.pos)};
        if(moved.status==pd::Status::NeedObservation)
        {
            const auto revealed=refresh_contact(frame);
            if(revealed==cs::Contact::Stale)return {cs::StepStatus::Stale};
            if(revealed!=cs::Contact::Clear)return {cs::StepStatus::NeedContact};
        }
        return convert(moved);
    }
};

void actual_dispatcher_preserves_charged_capability()
{
    Scene scene;DispatchBackend backend{scene};cs::State owner;
    auto result=cs::dispatch(owner,backend.frame(),backend);
    check(result==cs::Outcome::Handled && owner.mode==cs::Mode::Approach && scene.body.pos==charged &&
        !scene.quiet(scene.body.pos) && scene.actual_reverse()==std::vector<Point>({charged,start}),
        "ACTUAL shared dispatcher admits initial P0 edge and retains the charged observation owner");
    scene.next_input();const auto first_hp=scene.body.hp;
    result=cs::dispatch(owner,backend.frame(),backend);
    check(result==cs::Outcome::Handled && owner.mode==cs::Mode::Approach && scene.body.pos==Point{3,2} &&
        scene.body.hp==first_hp && backend.forward_calls==2 && scene.body.capture_moves==0,
        "ACTUAL shared dispatcher executes paid leg2 under unchanged Unknown from current HP, without paying sunk damage twice");
    for(unsigned steps=0;steps<3 && scene.body.pos!=frontier;++steps)
    {scene.next_input();result=cs::dispatch(owner,backend.frame(),backend);if(result!=cs::Outcome::Handled)break;}
    check(scene.body.pos==frontier && scene.quiet(scene.body.pos) && scene.body.hp==170,
        "shared bounded-next-edge owner crosses the entire external support region on a finite descending geometry cursor");
    scene.next_input();scene.body.reveal=true;
    check(cs::dispatch(owner,backend.frame(),backend)==cs::Outcome::LegacyCapture,
        "fresh Reward hands ownership to the REAL Capture guard and never issues a capture move itself");
    auto step=scene.observe_capture({7,2},false);
    check(step.moved && step.safe_input && scene.history.record(wp(frontier),wp(scene.body.pos),true,false)==walk::Status::Known,
        "real fresh Reward guard executes first Capture edge with actual history");
    scene.next_input();check(cs::dispatch(owner,backend.frame(),backend)==cs::Outcome::LegacyCapture,"next input independently returns to fresh Capture authority");
    step=scene.observe_capture(selected,false);
    check(step.moved && scene.history.record({7,2},wp(scene.body.pos),false,false)==walk::Status::Known,
        "second real Capture edge preserves its reverse support");
    scene.next_input();const auto held=scene.observe_capture(selected,false);
    check(held.safe_input && held.holding && !held.moved,"fresh Reward Hold is an actual capture guard result, not observation progress");
    // Explicit engine-receipt oracle, not a timer/hold-derived simulated payout.
    scene.body.gems=11;scene.body.selected_reward=false;scene.body.selected_respawning=true;
    auto receipt=cs::dispatch(owner,backend.frame(),backend);
    check(receipt==cs::Outcome::Handled && owner.mode==cs::Mode::Returning && owner.reason==cs::Reason::Bank &&
        scene.body.pos==Point{7,2},"actual bag resource plus fresh Respawning read starts real release/return before Bank ownership");
    // Actual post-phase release premise: after leaving the occupied source, the
    // selected NPC can respawn. The unchanged all-source field already priced it.
    scene.body.selected_respawning=false;scene.next_input();
    receipt=cs::dispatch(owner,backend.frame(),backend);
    check(receipt==cs::Outcome::Bank && scene.body.pos==frontier && scene.quiet(scene.body.pos) && scene.body.gems==11,
        "receipt preserves actual reverse support across source reappearance and enters Bank only after accepted quiet return");
}

void actual_dispatcher_returns_before_revaluation()
{
    Scene scene;DispatchBackend backend{scene};cs::State owner;
    cs::dispatch(owner,backend.frame(),backend);scene.next_input();
    const auto reverse=scene.actual_reverse();scene.body.hidden_refusal=true;
    const auto refused=cs::dispatch(owner,backend.frame(),backend);
    check(refused==cs::Outcome::Refuted && scene.body.pos==start && backend.return_calls==1 &&
        scene.history.nodes().size()==1 && reverse==std::vector<Point>({charged,start}),
        "shared dispatcher keeps reverse support after a refused forward edge and ACTUALLY returns before refuting Capture");
    Scene alive;DispatchBackend alive_backend{alive};cs::State alive_owner;
    cs::dispatch(alive_owner,alive_backend.frame(),alive_backend);alive.next_input();
    alive.body.reveal=true;alive.body.selected_reward=false;
    check(cs::dispatch(alive_owner,alive_backend.frame(),alive_backend)==cs::Outcome::Revalue &&
        alive.body.pos==start && alive_backend.return_calls==1 && alive.body.capture_moves==0,
        "actual Alive selects Work revaluation ONLY after the shared dispatcher executes actual paid egress");
    Scene changed;DispatchBackend changed_backend{changed};cs::State changed_owner;
    cs::dispatch(changed_owner,changed_backend.frame(),changed_backend);changed.next_input();++changed.graph;
    check(cs::dispatch(changed_owner,changed_backend.frame(),changed_backend)==cs::Outcome::Refuted && changed.body.pos==start,
        "changed forward graph revokes forward permission while fresh actual reverse remains executable");
    Scene reward;DispatchBackend reward_backend{reward};cs::State reward_owner;
    cs::dispatch(reward_owner,reward_backend.frame(),reward_backend);reward.next_input();reward.body.reveal=true;
    check(cs::dispatch(reward_owner,reward_backend.frame(),reward_backend)==cs::Outcome::Refuted &&
        reward.body.pos==start && reward.body.capture_moves==0,
        "shared dispatcher never upgrades fresh Reward plus positive external support into an unsafe Capture hold");
}

void actual_dispatcher_refuses_false_progress()
{
    Scene scene;DispatchBackend backend{scene};cs::State owner;
    backend.route_ready=false;
    check(cs::dispatch(owner,backend.frame(),backend)==cs::Outcome::Geometry && scene.body.accepted==0,
        "initial P0 geometry preparation owns no movement/capture authority");
    backend.route_ready=true;cs::dispatch(owner,backend.frame(),backend);scene.next_input();
    backend.enemies=true;const auto before=scene.body.accepted;
    check(cs::dispatch(owner,backend.frame(),backend)==cs::Outcome::Contact && scene.body.accepted==before,
        "fresh visible enemy yields threat ownership instead of treating Unknown's omitted enemy field as a negative");
    backend.enemies=false;scene.body.block_forward=true;scene.body.block_return=true;
    check(cs::dispatch(owner,backend.frame(),backend)==cs::Outcome::Handled && owner.mode==cs::Mode::Returning && scene.body.pos==charged,
        "blocked forward and return edges retain the real charged return owner without declaring arrival");
    scene.next_input();
    check(cs::dispatch(owner,backend.frame(),backend)==cs::Outcome::NeedResolver && scene.body.pos==charged,
        "unchanged repeated obstruction exhausts the bounded refusal class instead of inventing repeated progress");
    scene.next_input();scene.body.block_return=false;
    check(cs::dispatch(owner,backend.frame(),backend)==cs::Outcome::Refuted && scene.body.pos==start,
        "an actually changed return occupancy is freshly guarded before the real reverse move");
    Scene unprimed;unprimed.body.pos=charged;DispatchBackend unprimed_backend{unprimed};cs::State unprimed_owner;
    check(cs::dispatch(unprimed_owner,unprimed_backend.frame(),unprimed_backend)==cs::Outcome::NeedResolver &&
        unprimed_backend.forward_calls==0 && unprimed.body.accepted==0,
        "an unprimed charged Unknown pose cannot borrow the initial P0 admission or invent reverse history");
}
void refused_move_refreshes_contact_before_any_second_command()
{
    Scene scene;DispatchBackend backend{scene};cs::State owner;
    cs::dispatch(owner,backend.frame(),backend);scene.next_input();
    const auto reverse=scene.actual_reverse();const auto attempts=scene.body.attempts;
    const auto candidates=backend.candidate_attempts;const auto returns=backend.return_calls;
    scene.body.hidden_refusal=true;scene.body.reveal_enemy_on_refusal=true;
    const auto old=backend.frame();
    check(!old.enemy && scene.body.move,"real adapter counterexample starts with a fresh negative enemy read and available M");
    check(cs::dispatch(owner,old,backend)==cs::Outcome::Contact && scene.body.enemy_visible && scene.body.pos==charged &&
        scene.body.attempts==attempts+1 && backend.candidate_attempts==candidates+1 && backend.return_calls==returns &&
        scene.actual_reverse()==reverse && owner.mode==cs::Mode::Approach,
        "REAL shared adapter stops after Move(false) reveals enemy, preserving Trail without another forward or reverse command");
    check(cs::dispatch(owner,backend.frame(),backend)==cs::Outcome::Contact && scene.body.attempts==attempts+1,
        "fresh contact ownership remains ahead of NPC-only continuation after the revealing refusal");
    // An actual external contact resolver removes the visible actor; no timer
    // or local wait counter is used as evidence that the enemy disappeared.
    scene.body.enemy_visible=false;scene.body.hidden_refusal=false;scene.body.reveal_enemy_on_refusal=false;
    check(cs::dispatch(owner,backend.frame(),backend)==cs::Outcome::Handled && scene.body.pos==Point{3,2},
        "a new actual clear contact read can re-admit the retained paid observation edge");
    Scene reward;DispatchBackend reward_backend{reward};cs::State reward_owner;
    for(unsigned steps=0;steps<5;++steps)
    {cs::dispatch(reward_owner,reward_backend.frame(),reward_backend);reward.next_input();}
    reward.body.reveal=true;
    const auto first_capture=reward.observe_capture({7,2},false);
    check(first_capture.moved && reward.history.record(wp(frontier),wp(reward.body.pos),true,false)==walk::Status::Known,
        "Reward refusal witness retains an actual prior Capture edge from quiet support");
    reward.next_input();reward.body.capture_refusal=true;reward.body.reveal_enemy_on_refusal=true;
    const auto reward_frame=reward_backend.frame();const auto reward_attempts=reward.body.attempts;
    check(cs::dispatch(reward_owner,reward_frame,reward_backend)==cs::Outcome::LegacyCapture,
        "real Reward continuation still requires its separate fresh capture actuator");
    const auto denied=reward.observe_capture(selected,false);
    check(denied.status==cap::Status::NeedObservation && !denied.moved && reward.body.enemy_visible &&
        reward_backend.refresh_contact(reward_frame)==cs::Contact::Revalue && reward.body.attempts==reward_attempts+1 &&
        reward_backend.return_calls==0 && reward.body.pos==Point{7,2},
        "real Capture Step(false) also invalidates the enemy-negative support before any NPC-only fallback command");
    check(cs::dispatch(reward_owner,reward_backend.frame(),reward_backend)==cs::Outcome::Contact &&
        reward.body.attempts==reward_attempts+1,"fresh enemy contact blocks further Capture and paid-return commands after Reward refusal");
}
void unavailable_reads_and_accepted_support_loss_are_bounded()
{
    Scene quiet;DispatchBackend quiet_backend{quiet};cs::State quiet_owner;
    auto quiet_read=quiet_backend.frame();quiet_read.read=cs::Read::Unavailable;
    check(cs::dispatch(quiet_owner,quiet_read,quiet_backend)==cs::Outcome::Handled && quiet.body.accepted==0,
        "unavailable quiet source read grants no physical authority");
    quiet.next_input();quiet_read=quiet_backend.frame();quiet_read.read=cs::Read::Unavailable;
    check(cs::dispatch(quiet_owner,quiet_read,quiet_backend)==cs::Outcome::Refuted && quiet.body.accepted==0,
        "a second distinct unavailable quiet read retires this owner instead of an unbounded observer loop");
    Scene scene;DispatchBackend backend{scene};cs::State owner;
    cs::dispatch(owner,backend.frame(),backend);scene.next_input();
    auto unavailable=backend.frame();unavailable.read=cs::Read::Unavailable;
    const auto before=scene.body.accepted;
    check(cs::dispatch(owner,unavailable,backend)==cs::Outcome::Handled && owner.deferrals==1 && scene.body.accepted==before,
        "one charged unavailable observer input consumes one of the two explicitly funded observation waits");
    check(cs::dispatch(owner,unavailable,backend)==cs::Outcome::Handled && owner.deferrals==1,
        "same-epoch redispatch cannot manufacture another wait interval");
    scene.next_input();unavailable=backend.frame();unavailable.read=cs::Read::Unavailable;
    check(cs::dispatch(owner,unavailable,backend)==cs::Outcome::Refuted && scene.body.pos==start,
        "second unavailable observation input deliberately returns from fresh current HP instead of repeating the observer forever");
    Scene blocked;DispatchBackend blocked_backend{blocked};cs::State blocked_owner;
    cs::dispatch(blocked_owner,blocked_backend.frame(),blocked_backend);blocked.next_input();
    blocked.body.reveal=true;blocked.body.selected_reward=false;blocked_backend.has_headroom=false;
    check(cs::dispatch(blocked_owner,blocked_backend.frame(),blocked_backend)==cs::Outcome::Handled && blocked_owner.deferrals==1,
        "return NeedCompute is an accounted physical deferral, not free renewed admission");
    blocked.next_input();
    check(cs::dispatch(blocked_owner,blocked_backend.frame(),blocked_backend)==cs::Outcome::NeedResolver && blocked.body.pos==charged,
        "second return computation withdrawal names a resolver without inventing an accepted edge");
    Scene lost;DispatchBackend lost_backend{lost};cs::State lost_owner;lost_backend.lose_support=true;
    check(cs::dispatch(lost_owner,lost_backend.frame(),lost_backend)==cs::Outcome::Handled && lost_owner.mode==cs::Mode::Returning &&
        lost_owner.expected==cp(charged) && lost.body.accepted==1 && lost_backend.return_calls==0,
        "accepted forward edge plus SupportLost ends the input at its actual new pose without re-entering an actuator");
    lost.next_input();lost_backend.lose_support=false;
    check(cs::dispatch(lost_owner,lost_backend.frame(),lost_backend)==cs::Outcome::Refuted && lost.body.pos==start,
        "next-input support resolution uses actual remaining reverse capability");
    Scene returning;DispatchBackend returning_backend{returning};cs::State returning_owner;
    cs::dispatch(returning_owner,returning_backend.frame(),returning_backend);returning.next_input();
    cs::dispatch(returning_owner,returning_backend.frame(),returning_backend);returning.next_input();
    returning.body.reveal=true;returning.body.selected_reward=false;returning_backend.lose_support=true;
    check(cs::dispatch(returning_owner,returning_backend.frame(),returning_backend)==cs::Outcome::Handled && returning.body.pos==charged &&
        returning_owner.expected==cp(charged) && returning_backend.return_calls==1,
        "accepted reverse edge plus SupportLost cannot invoke the general resolver in the same input");
}
void charged_owner_precedes_quiet_optional_resource_gate()
{
    Scene scene;DispatchBackend backend{scene};cs::State owner;
    cs::dispatch(owner,backend.frame(),backend);scene.next_input();
    // Source-level interval witness, not a native fuel-cost qualification:
    // runtime budget(work) requires >14000+work, Capture preflight uses16000,
    // optional Dash uses26000. At37000 only the mandatory paid edge qualifies.
    constexpr unsigned remaining=37000,tail=14000;
    const bool paid_headroom=remaining>tail+16000,optional_dash_headroom=remaining>tail+26000;
    bool optional_called=false;cs::Outcome result=cs::Outcome::NeedResolver;
    if(cs::before_optional(owner,true,scene.quiet(scene.body.pos)))
    {backend.has_headroom=paid_headroom;result=cs::dispatch(owner,backend.frame(),backend);}
    else {optional_called=true;check(optional_dash_headroom,"optional gate would withdraw at this modeled resource interval");}
    check(paid_headroom && !optional_dash_headroom && !optional_called && result==cs::Outcome::Handled && scene.body.pos==Point{3,2},
        "ACTUAL caller seam selects retained charged Approach before optional Dash can withdraw at its higher resource gate");
    check(cs::before_optional(owner,true,false) && !cs::before_optional(owner,true,true) && !cs::before_optional(cs::State{},false,false),
        "mandatory charged Capture ordering does not turn legacy Work or quiet preparation into that owner");
    owner.mode=cs::Mode::Returning;
    check(cs::before_optional(owner,false,false),"actual retained Return survives a phase handoff and precedes optional gates");
}
// The shared request geometry is real production code. Native bitboard import
// and distance-field completion below are explicit fixture oracles; this pure
// composition does NOT qualify native cost, field connectivity or publication.
struct CapturePreparation
{
    struct Assembly
    {
        int group{},reach{1};std::size_t phase{},empty_group{},empty_member{},root_target{},root_cell{};
        std::vector<std::uint8_t> visible_bits,living_bits,goal_bits,empty_bits;std::vector<Point> targets;
    };
    Scene& scene;std::optional<Assembly> assembly{};bool pending{},allow_progress{true};
    unsigned seeds{},geometry_calls{},imports{},native_calls{};
    cs::GeometryCursor cursor() const
    {
        if(assembly)return {1,assembly->phase,assembly->empty_group,assembly->empty_member,assembly->root_target,assembly->root_cell};
        return {pending?2u:0u,0,0,0,0,0};
    }
    cs::Outcome input(cs::State& owner,DispatchBackend& backend)
    {
        backend.geometry=cursor();const auto frame=backend.frame();
        const auto result=cs::dispatch(owner,frame,backend);
        const auto flow=result==cs::Outcome::RewardGeometry || result==cs::Outcome::Geometry?cs::Flow::Geometry:
            result==cs::Outcome::Contact?cs::Flow::Contact:cs::Flow::Handled;
        if(cs::caller(flow)!=cs::Caller::Geometry)return result;
        check(result==cs::Outcome::RewardGeometry && owner.preparation_epoch==scene.body.epoch &&
            owner.expected==cp(scene.body.pos) && frame.safe_capture && frame.life==cs::Life::Reward && !frame.enemy,
            "REAL caller Geometry handoff carries a fresh per-input Reward staging guard, never a static P0 relabel");
        if(!assembly && !pending)
        {
            assembly.emplace();++seeds;
            assembly->visible_bits.assign(9,0xff);assembly->living_bits.assign(9,0);
            tt::request_detail::packed_set(assembly->living_bits,static_cast<std::size_t>(external.y*12+external.x));
            return result; // real global observation seed is supplied as a fixture premise
        }
        if(assembly)
        {
            if(assembly->phase<3)
            {
                struct World {int width{12},height{6};const std::vector<Group>& groups;std::vector<Point> altars;};
                const World world{12,6,scene.groups,{}};++geometry_calls;
                const auto geometry=tt::request_detail::advance_geometry(*assembly,world,
                    tt::request_detail::GoalKind::Capture,[&]{return allow_progress;});
                check(geometry!=tt::request_detail::GeometryStatus::Invalid,"actual retained Capture request accepts its frozen masks and group geometry");
                if(geometry==tt::request_detail::GeometryStatus::Complete)
                    check(tt::request_detail::packed_test(assembly->goal_bits,selected.y*12+selected.x) &&
                        tt::request_detail::packed_test(assembly->empty_bits,selected.y*12+selected.x) &&
                        !tt::request_detail::packed_test(assembly->empty_bits,external.y*12+external.x),
                        "REAL Capture request roots selected Reward members and preserves the visible living external NPC");
            }
            else if(allow_progress && assembly->phase==3){++imports;assembly->phase=4;}
            else if(allow_progress && assembly->phase==4){++imports;assembly.reset();pending=true;}
            return result;
        }
        if(allow_progress){++native_calls;pending=false;backend.route_ready=true;}
        return result;
    }
};
void actual_work_edge_to_reward_perch(Scene& scene,DispatchBackend& backend,cs::State& owner)
{
    scene.body.selected_reward=false;
    for(unsigned steps=0;steps<5;++steps)
    {cs::dispatch(owner,backend.frame(),backend);scene.next_input();}
    const auto tail=scene.history.make_return(wp(frontier),walk::Point{7,2});
    std::vector<Point> word{frontier};for(auto point:tail)word.push_back(pp(point));OrdinaryActor actor{scene.body};
    const auto entered=pd::authority(scene.field,std::span<const Point>{word},true,actor,
        [&]{return scene.body.epoch;},[&](Point p){return scene.legal(p);},[]{return true;},true);
    check(entered.moved && scene.history.record(wp(frontier),wp(scene.body.pos),true,false)==walk::Status::Known,
        "a REAL paid ordinary Work edge establishes actual reverse support at the selected group's positive-support perch");
    // Actual clear/lifecycle oracle; this fixture does not invent an attack,
    // damage receipt or game event from the movement itself.
    scene.body.selected_reward=true;scene.body.reveal=true;scene.next_input();backend.route_ready=false;
    check(scene.body.pos==Point{7,2} && !scene.quiet(scene.body.pos) && backend.frame().safe_capture,
        "actual Reward excludes selected dead sources while immutable all-source support remains positive");
}
void reward_staging_retains_request_and_capture_continuation()
{
    Scene scene;DispatchBackend backend{scene};cs::State owner;actual_work_edge_to_reward_perch(scene,backend,owner);
    CapturePreparation preparation{scene};const auto accepted=scene.body.accepted;
    for(unsigned input=0;input<5;++input)
    {
        check(cs::before_optional(owner,true,scene.quiet(scene.body.pos)),"mandatory charged owner is selected before optional Dash on every Reward preparation input");
        check(preparation.input(owner,backend)==cs::Outcome::RewardGeometry && scene.body.accepted==accepted,
            "multiple real shared-dispatch/caller inputs retain Reward geometry instead of returning/refuting the immutable-positive perch");
        scene.next_input();
    }
    check(preparation.seeds==1 && preparation.geometry_calls==1 && preparation.imports==2 && preparation.native_calls==1 &&
        backend.route_ready && owner.mode==cs::Mode::RewardPreparation,
        "retained request/cursor advances once through actual geometry and explicit native oracles without reseeding on later owner inputs");
    check(cs::dispatch(owner,backend.frame(),backend)==cs::Outcome::LegacyCapture,
        "only completed geometry plus a NEW actual Reward guard reaches the capture actuator");
    const auto step=scene.observe_capture(selected,false);
    check(step.moved && scene.history.record({7,2},wp(scene.body.pos),false,false)==walk::Status::Known,
        "newly prepared Reward geometry is followed by a REAL fresh Capture Step with actual reverse history");
    scene.next_input();const auto hold=scene.observe_capture(selected,false);
    check(hold.safe_input && hold.holding && !hold.moved && scene.body.gems==0,
        "later actual Capture Hold grants no invented receipt or timer progress");
    scene.body.gems=11;scene.body.selected_reward=false;scene.body.selected_respawning=true;
    check(cs::dispatch(owner,backend.frame(),backend)==cs::Outcome::Handled && owner.reason==cs::Reason::Bank && scene.body.pos==Point{7,2},
        "actual receipt and Respawning read preserve the return owner after the staged Capture continuation");
    scene.body.selected_respawning=false;scene.next_input();
    check(cs::dispatch(owner,backend.frame(),backend)==cs::Outcome::Bank && scene.body.pos==frontier && scene.quiet(scene.body.pos),
        "receipt reaches Bank only after actual guarded reverse egress despite selected-source reappearance");
}
void reward_preparation_withdrawal_and_resource_refusal()
{
    Scene unknown;DispatchBackend unknown_backend{unknown};cs::State unknown_owner;
    actual_work_edge_to_reward_perch(unknown,unknown_backend,unknown_owner);CapturePreparation unknown_preparation{unknown};
    unknown_preparation.input(unknown_owner,unknown_backend);unknown.next_input();unknown.body.reveal=false;
    check(unknown_preparation.input(unknown_owner,unknown_backend)==cs::Outcome::Refuted && unknown.body.pos==frontier &&
        unknown_preparation.geometry_calls==0 && unknown_preparation.seeds==1,
        "actual Unknown withdraws Reward preparation and ACTUALLY returns before further geometry or revaluation");
    Scene alive;DispatchBackend alive_backend{alive};cs::State alive_owner;
    actual_work_edge_to_reward_perch(alive,alive_backend,alive_owner);CapturePreparation alive_preparation{alive};
    alive_preparation.input(alive_owner,alive_backend);alive.next_input();alive.body.selected_reward=false;
    check(alive_preparation.input(alive_owner,alive_backend)==cs::Outcome::Revalue && alive.body.pos==frontier && alive.body.capture_moves==0,
        "actual Alive retires the zero-residual Capture quote only after real reverse egress");
    Scene enemy;DispatchBackend enemy_backend{enemy};cs::State enemy_owner;
    actual_work_edge_to_reward_perch(enemy,enemy_backend,enemy_owner);CapturePreparation enemy_preparation{enemy};
    enemy_preparation.input(enemy_owner,enemy_backend);enemy.next_input();enemy.body.enemy_visible=true;
    const auto prior=enemy.body.accepted;
    check(enemy_preparation.input(enemy_owner,enemy_backend)==cs::Outcome::Contact && enemy.body.accepted==prior &&
        enemy_preparation.geometry_calls==0 && cs::caller(cs::Flow::Contact)==cs::Caller::FreshObservation,
        "fresh enemy withdrawal routes Contact directly to the actual fresh observer, not geometry or an NPC-only return command");
    Scene refused;DispatchBackend refused_backend{refused};cs::State refused_owner;
    actual_work_edge_to_reward_perch(refused,refused_backend,refused_owner);CapturePreparation refused_preparation{refused};
    refused_preparation.input(refused_owner,refused_backend);refused_preparation.allow_progress=false;refused.next_input();
    refused_preparation.input(refused_owner,refused_backend);refused.next_input();
    refused_preparation.input(refused_owner,refused_backend);refused.next_input();
    check(refused_preparation.input(refused_owner,refused_backend)==cs::Outcome::Refuted && refused.body.pos==frontier &&
        refused_preparation.seeds==1 && refused_preparation.geometry_calls==2,
        "two unchanged pure-stage NeedCompute inputs cause explicit actual return without resetting the query or lowering any resource gate");
    check(cs::caller(cs::Flow::Geometry)==cs::Caller::Geometry && cs::caller(cs::Flow::Dispatcher)==cs::Caller::FreshObservation,
        "the REAL mandatory caller seam distinguishes authorized geometry from generic observation/contact handoff");
}
}
int main()
{
    const auto work=legacy_work_prefix_reference();
    const auto capture=capture_observation_to_reward();
    check(work==capture,"desired Capture observation owner preserves the same admitted multi-input paid movement capability as Work");
    refusal_and_actual_return();changed_forward_support_and_alive_handoff();blocked_return_is_not_invented();charged_reward_still_needs_real_return();unresolved_frontier_is_not_capture();
    actual_dispatcher_preserves_charged_capability();actual_dispatcher_returns_before_revaluation();actual_dispatcher_refuses_false_progress();
    refused_move_refreshes_contact_before_any_second_command();unavailable_reads_and_accepted_support_loss_are_bounded();charged_owner_precedes_quiet_optional_resource_gate();
    reward_staging_retains_request_and_capture_continuation();reward_preparation_withdrawal_and_resource_refusal();
    std::printf("capture service capability preservation: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
