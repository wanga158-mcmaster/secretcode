#include "production_walk.h"
#include <cstdio>
#include <limits>
#include <vector>

namespace {
using namespace tt::production_walk;
int failures{},checks{};
void check(bool condition,const char* label)
{
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n",label); }
}
void equal(const std::vector<Point>& actual,std::initializer_list<Point> expected,const char* label)
{ check(actual==std::vector<Point>{expected},label); }
}

int main()
{
    Trail trail{16};
    const auto initial_revision=trail.revision();
    check(trail.status()==Status::Unknown && trail.make_return({0,0}).empty(),"no anchor gives no return claim");
    check(trail.anchor_quiet({0,0})==Status::Known,"fresh quiet origin establishes an anchor");
    check(trail.revision()>initial_revision,"fresh anchor advances actual-history revision");
    const auto anchored_revision=trail.revision();
    const std::vector<Point> prospective_prefix{{0,0},{1,0},{2,0}};
    check(trail.can_retain_prefix(prospective_prefix,[](Point){return false;}) &&
          trail.revision()==anchored_revision && trail.nodes().size()==1,
          "pure capacity proof cannot advance actual-history revision or consume an edge");
    equal(trail.make_return({0,0},Point{1,1}),{{1,1},{0,0}},"first prospective diagonal returns to observed quiet origin");
    check(trail.nodes().size()==1,"unexecuted prospective step does not mutate actual history");
    check(trail.record({0,0},{1,1},true,false)==Status::Known,"successful diagonal from quiet is recorded");
    check(trail.record({1,1},{2,1},false,false)==Status::Known,"second successful edge extends actual suffix");
    check(trail.record({2,1},{3,2},false,false)==Status::Known,"third successful edge extends actual suffix");
    equal(trail.make_return({3,2}),{{3,2},{2,1},{1,1},{0,0}},"three-step suffix reverses actual diagonal and cardinal edges");
    equal(trail.make_return({3,2},Point{4,2}),{{4,2},{3,2},{2,1},{1,1},{0,0}},"new adjacent prospect prefixes actual reverse suffix");
    equal(trail.make_return({3,2},Point{2,1}),{{2,1},{1,1},{0,0}},"prospective backtrack truncates at an earlier actual node");
    equal(trail.make_return({3,2},Point{3,2}),{{3,2},{2,1},{1,1},{0,0}},"equal prospective point adds no fictitious edge");
    check(trail.make_return({3,2},Point{0,0}).empty(),"nonadjacent prospective anchor is not a teleport");
    check(trail.record({3,2},{2,1},false,false)==Status::Known,"actual backtrack keeps a valid suffix");
    equal(trail.make_return({2,1}),{{2,1},{1,1},{0,0}},"actual backtrack erases the traversed branch");
    check(trail.record({2,1},{1,0},false,false)==Status::Known,"cycle side edge is recorded");
    check(trail.record({1,0},{0,0},false,false)==Status::Known,"closing an actual cycle is admitted");
    equal(trail.make_return({0,0}),{{0,0}},"cycle erasure returns to its quiet anchor");
    check(trail.record({0,0},{1,0},false,true)==Status::Known,"new quiet arrival resets anchor");
    equal(trail.make_return({1,0}),{{1,0}},"older quiet anchor is superseded by newer quiet arrival");
    check(trail.record({1,0},{2,0},true,false)==Status::Known,"leaving quiet point starts a fresh actual suffix");
    check(trail.make_return({3,0}).empty(),"unexpected actual origin produces no return proposal");
    trail.invalidate();
    check(trail.status()==Status::Unknown && trail.make_return({2,0}).empty(),"explicit displacement or refusal revokes witness");
    check(trail.record({5,5},{6,5},false,true)==Status::Unknown,"untracked edge cannot silently restore authority on quiet arrival");
    check(trail.record({5,5},{6,5},true,false)==Status::Known,"fresh from-quiet premise permits an explicit restart");
    check(trail.record({8,8},{9,8},false,false)==Status::Unknown,"mismatched actual edge origin invalidates history");
    trail.anchor_quiet({8,8});
    check(trail.record({8,8},{8,8},true,true)==Status::Unknown,"zero-length edge is not successful walking");
    trail.anchor_quiet({8,8});
    check(trail.record({8,8},{10,8},true,false)==Status::Unknown,"dash-length edge cannot enter an ordinary walk suffix");

    Trail bounded{3};
    bounded.anchor_quiet({0,0}); bounded.record({0,0},{1,0},false,false);
    bounded.record({1,0},{2,0},false,false);
    check(bounded.make_return({2,0},Point{3,0}).empty() && bounded.status()==Status::Known,
          "unexecuted over-capacity prospect is unknown without deleting actual history");
    check(bounded.record({2,0},{3,0},false,false)==Status::Unknown && bounded.nodes().empty(),
          "actual overflow revokes the bounded witness instead of truncating its quiet anchor");
    check(bounded.record({4,0},{5,0},true,true)==Status::Known && bounded.nodes().size()==1,
          "new quiet arrival can restart a bounded trail with one anchor");
    Trail none{0};
    check(none.anchor_quiet({0,0})==Status::Unknown && none.record({0,0},{1,0},true,true)==Status::Unknown,
          "zero capacity stays typed unknown");
    check(adjacent({std::numeric_limits<int>::max(),0},{std::numeric_limits<int>::max()-1,1}) &&
          !adjacent({std::numeric_limits<int>::min(),0},{std::numeric_limits<int>::max(),0}),
          "adjacency arithmetic does not overflow extreme coordinates");

    Trail full{3};
    full.anchor_quiet({0,0});
    full.record({0,0},{1,0},false,false);
    full.record({1,0},{2,0},false,false);
    const std::vector<Point> original(full.nodes().begin(),full.nodes().end());
    const auto never_quiet=[](Point){return false;};
    const std::vector<Point> append={{2,0},{3,0}};
    check(!full.can_retain_prefix(append,never_quiet),
          "full actual trail rejects a future new nonquiet node");
    check(full.status()==Status::Known && std::vector<Point>(full.nodes().begin(),full.nodes().end())==original,
          "failed virtual replay never mutates original status or history");
    const std::vector<Point> backtrack={{2,0},{1,0},{1,1}};
    check(full.can_retain_prefix(backtrack,never_quiet),
          "virtual loop erasure frees actual storage for a later new node");
    const std::vector<Point> reset={{2,0},{3,0},{4,0}};
    check(full.can_retain_prefix(reset,[](Point point){return point==Point{3,0};}),
          "future quiet arrival resets the copied trail before capacity extension");
    check(std::vector<Point>(full.nodes().begin(),full.nodes().end())==original,
          "successful virtual loop and quiet reset leave actual history unchanged");

    Trail later_overflow{4};
    later_overflow.anchor_quiet({0,0});
    later_overflow.record({0,0},{1,0},false,false);
    later_overflow.record({1,0},{2,0},false,false);
    check(later_overflow.can_retain_prefix(append,never_quiet) &&
          !later_overflow.can_retain_prefix(reset,never_quiet) && later_overflow.nodes().size()==3,
          "whole ingress is rejected when a later edge overflows though first step fits");
    check(!full.can_retain_prefix(std::span<const Point>{},never_quiet) &&
          !full.can_retain_prefix(std::vector<Point>{{1,0},{2,0}},never_quiet),
          "empty prefix and origin differing from actual last node are rejected");
    check(!full.can_retain_prefix(std::vector<Point>{{2,0},{4,0}},never_quiet) &&
          !full.can_retain_prefix(std::vector<Point>{{2,0},{2,0}},never_quiet),
          "nonadjacent and zero-length future edges are rejected");
    check(!full.can_retain_prefix(std::vector<Point>{{2,0},{3,0},{4,0},{5,0},{6,0}},
                                 [](Point){return true;}),
          "prefix family is bounded even when every future point claims quiet");
    check(full.can_retain_prefix(std::vector<Point>{{2,0}},never_quiet),
          "one-point current prefix requires no invented movement");
    Trail unknown{3};
    check(!unknown.can_retain_prefix(std::vector<Point>{{0,0}},[](Point){return true;}) &&
          !none.can_retain_prefix(std::vector<Point>{{0,0}},[](Point){return true;}),
          "unknown actual trail and zero storage cannot claim future retention");

    // The retained check must cover capture edges AFTER a fitting ingress.
    // Actual full Trail:0,0 ->1,0 ->2,0. Ingress backtracks then reaches1,1;
    // the new capture step2,1 would require a fourth retained node and must fail.
    const auto full_revision=full.revision();
    const RetentionKey capacity_key{full_revision,100};
    Retention whole(full,{{2,0},{1,0},{1,1},{2,1}},capacity_key,4);
    check(whole.status()==RetentionStatus::NeedCompute && whole.matches(capacity_key) &&
          whole.advance(capacity_key,never_quiet,0)==RetentionStatus::NeedCompute && whole.last_work()==0,
          "complete-word storage proof starts partial and zero work publishes nothing");
    check(whole.advance(capacity_key,never_quiet,2)==RetentionStatus::NeedCompute &&
          whole.replayed_edges()==2 && whole.last_work()==2,
          "fitting backtrack ingress alone cannot publish capture-word retention");
    check(whole.advance(capacity_key,never_quiet,1)==RetentionStatus::Unknown &&
          whole.replayed_edges()==2 && whole.last_work()==1 && !whole.matches(capacity_key),
          "capture suffix overflow invalidates after every earlier ingress edge fit");
    check(full.status()==Status::Known && full.revision()==full_revision &&
          std::vector<Point>(full.nodes().begin(),full.nodes().end())==original,
          "retained failure preserves actual Trail points status and revision");

    Retention erased(full,{{2,0},{1,0},{1,1},{1,0},{0,0}},capacity_key,5);
    check(erased.advance(capacity_key,never_quiet,16)==RetentionStatus::Ready &&
          erased.replayed_edges()==4 && erased.last_work()==4,
          "later backtracking erases the virtual branch through shared actual record rules");
    const auto quiet_front=[](Point point){return point==Point{1,1};};
    Retention reset_capture(full,{{2,0},{1,0},{1,1},{2,1}},capacity_key,4);
    int virtual_quiet_calls=0;
    const auto counted_quiet=[&](Point point){++virtual_quiet_calls;return quiet_front(point);};
    for(int slice=0;slice<3;++slice)
    {
        const auto state=reset_capture.advance(capacity_key,counted_quiet,1);
        check(reset_capture.last_work()==1 && reset_capture.replayed_edges()==static_cast<std::size_t>(slice+1) &&
              (slice==2 ? state==RetentionStatus::Ready : state==RetentionStatus::NeedCompute),
              "quiet reset admits later capture while each one-edge slice remains bounded");
    }
    check(virtual_quiet_calls==6 && full.revision()==full_revision &&
          std::vector<Point>(full.nodes().begin(),full.nodes().end())==original,
          "quiet callback is evaluated twice per simulated edge without actual history mutation");

    Retention changed_walk(full,backtrack,capacity_key,3);
    changed_walk.advance(capacity_key,never_quiet,1);
    check(changed_walk.advance({full_revision+1,100},never_quiet,16)==RetentionStatus::Unknown &&
          changed_walk.last_work()==0 && changed_walk.replayed_edges()==1,
          "changed actual walk revision invalidates partial replay before further work");
    Retention changed_word(full,backtrack,capacity_key,3);
    check(changed_word.advance({full_revision,101},never_quiet,16)==RetentionStatus::Unknown &&
          changed_word.last_work()==0,
          "changed complete word or quiet-field generation cannot reuse retained simulation");
    check(erased.advance({full_revision,101},never_quiet,0)==RetentionStatus::Unknown,
          "generation change also withdraws completed retention");
    Retention wrong_base(full,backtrack,{full_revision+1,100},3);
    Retention wrong_origin(full,{{1,0},{2,0}},capacity_key,2);
    Retention empty_word(full,{},capacity_key,1);
    Retention over_word(full,backtrack,capacity_key,2);
    Retention no_history(unknown,{{0,0}},RetentionKey{unknown.revision(),100},1);
    check(wrong_base.status()==RetentionStatus::Unknown && wrong_origin.status()==RetentionStatus::Unknown &&
          empty_word.status()==RetentionStatus::Unknown && over_word.status()==RetentionStatus::Unknown &&
          no_history.status()==RetentionStatus::Unknown,
          "invalid base identity origin word capacity or actual history remains unsupported");
    Retention repeated_join(full,{{2,0},{2,0}},capacity_key,2);
    Retention nonadjacent(full,{{2,0},{4,0}},capacity_key,2);
    check(repeated_join.advance(capacity_key,never_quiet,16)==RetentionStatus::Unknown &&
          nonadjacent.advance(capacity_key,never_quiet,16)==RetentionStatus::Unknown,
          "duplicated ingress-capture join and nonadjacent edges cannot invent storage authority");
    Retention one_point(full,{{2,0}},capacity_key,1);
    check(one_point.status()==RetentionStatus::Ready && one_point.replayed_edges()==0 &&
          one_point.advance(capacity_key,never_quiet,16)==RetentionStatus::Ready && one_point.last_work()==0,
          "single current point is a complete zero-edge retention statement");

    std::vector<Point> long_word;
    for(int i=0;i<34;++i) long_word.push_back({2+i,0});
    Retention long_replay(full,long_word,capacity_key,34);
    check(long_replay.advance(capacity_key,[](Point){return true;},16)==RetentionStatus::NeedCompute &&
          long_replay.last_work()==16 && long_replay.replayed_edges()==16,
          "first sixteen-edge input retains the rest of a long complete word");
    Retention moved_replay(std::move(long_replay));
    check(long_replay.status()==RetentionStatus::Unknown && !long_replay.matches(capacity_key) &&
          moved_replay.replayed_edges()==16,
          "moving partial retention preserves the destination cursor and revokes the source");
    check(moved_replay.advance(capacity_key,[](Point){return true;},16)==RetentionStatus::NeedCompute &&
          moved_replay.last_work()==16 && moved_replay.replayed_edges()==32,
          "second input performs only its next sixteen edges without replaying the prefix");
    check(moved_replay.advance(capacity_key,[](Point){return true;},16)==RetentionStatus::Ready &&
          moved_replay.last_work()==1 && moved_replay.replayed_edges()==33 &&
          full.status()==Status::Known && full.revision()==full_revision &&
          std::vector<Point>(full.nodes().begin(),full.nodes().end())==original,
          "final edge alone publishes complete retention and leaves actual Trail unchanged");
    Trail prefix_trail{4};prefix_trail.anchor_quiet({0,0});
    prefix_trail.record({0,0},{1,0},true,false);
    const auto prefix_revision=prefix_trail.revision();
    equal(prefix_word(prefix_trail,{1,0},{2,0},[](Point){return false;}),
          {{1,0},{2,0},{1,0},{0,0}},
          "partial ingress contains its next physical edge and complete reverse actual suffix");
    equal(prefix_word(prefix_trail,{1,0},{0,1},[](Point p){return p==Point{0,1};}),
          {{1,0},{0,1}},"new quiet landing ends ingress without unnecessary reverse exposure");
    check(prefix_word(prefix_trail,{1,0},{3,0},[](Point){return false;}).empty() &&
          prefix_trail.revision()==prefix_revision && prefix_trail.nodes().size()==2,
          "nonadjacent proposal is unsupported and no future word mutates actual history");
    std::printf("production walk: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
