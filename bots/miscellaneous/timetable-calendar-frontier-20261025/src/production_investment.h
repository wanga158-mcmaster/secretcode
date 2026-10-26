#pragma once
// Conditional opportunity valuation of the ACTUAL ordered shop operator and at
// most two retained stationary production jobs. No SDK calls or action authority.
// Unsupported means this declared family cannot be valued, never global no-profit.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace tt::production_investment {
inline constexpr std::size_t family_count=5;
inline constexpr std::size_t max_shop_calls=8;
inline constexpr std::size_t max_jobs=2;
inline constexpr std::size_t max_members=32;
inline constexpr std::size_t max_advance_units=64;
inline constexpr std::uint32_t max_calendar_span=1000000;

enum class Status { NeedCompute, Ready, Unsupported };
enum class Reason {
    None, MissingPremise, InvalidNumber, ItemShape, ShopCapacity, Geometry,
    JobShape, ResourceReuse, TeamDisplacement, NoFloatProgress, CalendarOverflow, AvailabilityExpired
};
enum class Passive { None, Monster, Jungle, Durability, Treasure, Home, Unsupported };
enum class Family { Basic, StationaryAreaCaster };
enum class Decision { Keep, InvestByDeadline, EarlierSameWork, Unsupported };
struct Points { std::int32_t health{},strength{},focus{}; };
struct Growth { float health{},strength{},focus{}; };
struct RawBody { float hp{},max_hp{},strength{},focus{}; };
struct Effects { float monster{},jungle{},durability{},treasure{},home{}; };
struct Item {
    std::uint32_t id{},cost{},combine_count{};
    Points points;
    Passive passive{Passive::None};
    float magnitude{};
    bool purchasable{};
};
struct PurchaseFamily {
    Item shard,result;
    std::uint32_t shards_owned{},result_owned{};
    bool has_result{};
};
struct ShopInput {
    // Exactly purchase()'s Monster/Jungle/Durability/Treasure/Power main-stat order.
    std::array<PurchaseFamily,family_count> families{};
    std::uint32_t gold{};
    Growth growth;
    RawBody body;
    // Maximum magnitude of EACH passive across all owned stat variants.
    Effects effects;
    // Runtime trait coefficient; e.g. Ryth's raw Strength per bonus maximum HP.
    float strength_per_bonus_health{};
    bool metadata_complete{},shop_at_declared_spawn{},stable_raw_stat_projection{};
};
struct Buy {
    std::uint32_t family{},item{},count{},cost{};
    bool combined{};
};
struct ShopQuote {
    Status status{Status::Unsupported};
    Reason reason{Reason::MissingPremise};
    RawBody projected;
    Effects effects;
    std::uint32_t gold_left{},gold_spent{};
    std::array<Buy,max_shop_calls> buys{};
    std::size_t calls{};
    std::array<std::int64_t,3> point_delta{};
    // Item arithmetic grants NO healing. Native spawn recovery owns actual HP.
    float healing_credited{};
};

namespace detail {
inline float f32(float value) { volatile float rounded=value;return rounded; }
inline float add(float a,float b) { return f32(f32(a)+f32(b)); }
inline float mul(float a,float b) { return f32(f32(a)*f32(b)); }
inline bool positive(float x) { return std::isfinite(x) && x>0; }
inline bool nonnegative(float x) { return std::isfinite(x) && x>=0; }
inline bool body_valid(const RawBody& b) {
    return nonnegative(b.hp) && positive(b.max_hp) && b.hp<=b.max_hp &&
        nonnegative(b.strength) && nonnegative(b.focus);
}
inline bool effects_valid(const Effects& e) {
    return nonnegative(e.monster) && nonnegative(e.treasure) &&
        nonnegative(e.jungle) && e.jungle<1 && nonnegative(e.durability) &&
        e.durability<1 && nonnegative(e.home) && e.home<1;
}
inline bool points_valid(Points p) {
    constexpr std::int32_t cap=1000000;
    return p.health>=-cap && p.health<=cap && p.strength>=-cap &&
        p.strength<=cap && p.focus>=-cap && p.focus<=cap;
}
inline bool activate(Effects& e,Passive passive,float magnitude) {
    if (!nonnegative(magnitude))return false;
    switch(passive) {
    case Passive::None:return true;
    case Passive::Monster:e.monster=std::max(e.monster,magnitude);break;
    case Passive::Jungle:e.jungle=std::max(e.jungle,magnitude);break;
    case Passive::Durability:e.durability=std::max(e.durability,magnitude);break;
    case Passive::Treasure:e.treasure=std::max(e.treasure,magnitude);break;
    case Passive::Home:e.home=std::max(e.home,magnitude);break;
    case Passive::Unsupported:return false;
    }
    return effects_valid(e);
}
inline bool tick(std::uint32_t a,std::uint32_t b,std::uint32_t& result) {
    if(b>std::numeric_limits<std::uint32_t>::max()-a)return false;
    result=a+b;return true;
}
} // namespace detail

// Bounded at <=(max_shop_calls+1)*family_count selections. An unaffordable FIRST
// eligible family terminates exactly as purchase(); later cheap items are not
// searched. Each call completes at most one combination, Power can repeat.
// Numeric item caps bound representation, not a claim that other items are bad.
inline ShopQuote project_shop(const ShopInput& input) {
    ShopQuote result;
    result.projected=input.body;result.effects=input.effects;result.gold_left=input.gold;
    if(!input.metadata_complete || !input.shop_at_declared_spawn ||
       !input.stable_raw_stat_projection)return result;
    if(!detail::body_valid(input.body) || !detail::effects_valid(input.effects) ||
       !detail::nonnegative(input.growth.health) || !detail::nonnegative(input.growth.strength) ||
       !detail::nonnegative(input.growth.focus) || !detail::nonnegative(input.strength_per_bonus_health))
    {result.reason=Reason::InvalidNumber;return result;}
    auto families=input.families;
    for(const auto& family:families) {
        if(!family.has_result)continue;
        // Current catalog shards carry no passive. Consuming a passive-bearing
        // ingredient needs a removal/reaggregation model and is unsupported here.
        if(family.shard.passive!=Passive::None ||
           !detail::points_valid(family.shard.points) || !detail::points_valid(family.result.points) ||
           family.shard.combine_count>1024 || !detail::nonnegative(family.shard.magnitude) ||
           !detail::nonnegative(family.result.magnitude))
        {result.reason=Reason::ItemShape;return result;}
    }
    for(;;) {
        std::size_t selected=family_count;
        for(std::size_t i=0;i<family_count;++i) {
            const auto& family=families[i];const auto& shard=family.shard;
            if(!family.has_result || (i+1<family_count && family.result_owned>0) ||
               family.result_owned==std::numeric_limits<std::uint32_t>::max())continue;
            if(!shard.purchasable || !shard.cost || !shard.combine_count ||
               family.shards_owned>=shard.combine_count)continue;
            selected=i;break;
        }
        if(selected==family_count)break;
        auto& family=families[selected];const auto& shard=family.shard;
        const auto count=std::min(result.gold_left/shard.cost,shard.combine_count-family.shards_owned);
        if(!count)break; // actual purchase() does not search a later family
        if(result.calls==max_shop_calls)
        {result.reason=Reason::ShopCapacity;return result;}
        const auto cost=count*shard.cost; // <=gold_left by construction
        result.gold_left-=cost;result.gold_spent+=cost;
        const bool combined=family.shards_owned+count==shard.combine_count;
        result.buys[result.calls++]={static_cast<std::uint32_t>(selected),shard.id,count,cost,combined};
        const std::array<std::int32_t,3> before{shard.points.health,shard.points.strength,shard.points.focus};
        const std::array<std::int32_t,3> after{family.result.points.health,family.result.points.strength,family.result.points.focus};
        for(std::size_t stat=0;stat<3;++stat) {
            result.point_delta[stat]+=static_cast<std::int64_t>(count)*before[stat];
            if(combined)
                result.point_delta[stat]+=after[stat]-static_cast<std::int64_t>(shard.combine_count)*before[stat];
        }
        if(!detail::activate(result.effects,shard.passive,shard.magnitude))
        {result.reason=Reason::ItemShape;return result;}
        if(combined) {
            family.shards_owned=0;++family.result_owned;
            if(!detail::activate(result.effects,family.result.passive,family.result.magnitude))
            {result.reason=Reason::ItemShape;return result;}
        } else family.shards_owned+=count;
    }
    const float health_delta=detail::mul(input.growth.health,static_cast<float>(result.point_delta[0]));
    result.projected.max_hp=detail::add(input.body.max_hp,health_delta);
    result.projected.strength=detail::add(detail::add(input.body.strength,
        detail::mul(input.growth.strength,static_cast<float>(result.point_delta[1]))),
        detail::mul(input.strength_per_bonus_health,health_delta));
    result.projected.focus=detail::add(input.body.focus,
        detail::mul(input.growth.focus,static_cast<float>(result.point_delta[2])));
    // Current HP is deliberately unchanged even if maximum HP grows. The
    // stationary zero-incoming calendar never uses speculative purchase healing.
    if(!detail::body_valid(result.projected))
    {result.reason=Reason::InvalidNumber;return result;}
    result.status=Status::Ready;result.reason=Reason::None;return result;
}

struct Point { int x{},y{}; };
struct Member { Point pos;float hp{};bool hp_exact{}; };
struct Job {
    Family family{Family::StationaryAreaCaster};
    std::uint64_t group{},generation{};
    std::uint32_t observed_round{},available_from{},valid_through{};
    std::array<Member,max_members> members{};
    std::size_t member_count{};
    Point worker,aim;
    int width{},height{};
    float basic_range{},cast_range{},area_radius{},cast_modifier{},queried_current_damage{};
    std::uint32_t cooldown_period{};
    // Elapsed owner-start intervals along this ACTUAL retained continuation.
    // ingress_keep must include a spent current action allowance, if any; a
    // geometric walking distance alone is not an earliest-action timestamp.
    // damage_to_capture includes any exclusion-of-movement/input handoff.
    // capture_payout includes arming+queried capture duration; payout_to_bank
    // includes actual route/channel timing, not only ceil(gems/10).
    std::uint32_t ingress_keep{},ingress_shop{},ingress_between{};
    std::uint32_t damage_to_capture{},capture_payout{},payout_to_bank{};
    // Optional same-group second generation after the first actual payout.
    bool repeats_previous{};
    std::uint32_t respawn_after_previous_payout{};
    // Initial complete class requires ZERO displaced team work. Positive costs
    // need an explicit incidence-time model; they are Unsupported, never silently
    // charged only on success or forgotten when the deadline is missed.
    std::uint32_t gems{},displaced_keep{},displaced_shop{};
    bool full_group_known{},availability_supported{},geometry_supported{};
    bool zero_incoming_through_continuation{},team_displacement_accounted{};
    bool residual_persists_while_away{};
};
struct CalendarInput {
    ShopInput shop;
    std::uint64_t query_generation{};
    std::uint32_t now{},deadline{},remaining_cooldown{},recall_to_first_shop_ready{};
    // Parent includes actual recall/arrival and one fresh input per Buy, then
    // measured spawn reentry in Job.ingress_shop. Healing overlap is not added twice.
    std::array<Job,max_jobs> jobs{};
    std::size_t job_count{};
    // For a basic, rawStrength*stable outgoing factor; for a spell,
    // rawFocus*queried modifier*stable outgoing factor; Monster is then applied
    // ONCE because SDK damage_amount() excludes target-dependent camp bonuses.
    float stable_outgoing_multiplier{1};
    std::uint32_t carried_gems{};
    bool quiet_decision{},no_pending_capture_or_channel{},stable_outgoing_supported{};
};
struct Arm {
    std::array<std::uint32_t,max_jobs> last_damage_round{},payout_round{},bank_round{};
    std::array<std::uint64_t,max_jobs> commands{};
    std::int64_t net_team_gems{};
    std::uint64_t banked_gems{};
    std::size_t jobs_banked{};
    bool complete_all{},horizon_reached{};
};
struct Result {
    // Dated nomination provenance only; later native recall must revalidate it.
    // If computation spans inputs, refresh/rebase the calendar at the real now;
    // an unchanged generation is not permission to keep an expired start time.
    std::uint64_t query_generation{};
    std::uint32_t decision_round{};
    Status status{Status::NeedCompute};Reason reason{Reason::None};
    Decision decision{Decision::Unsupported};
    ShopQuote shop;
    Arm keep,invest;
    std::int64_t same_work_time_saved{};
    std::size_t work_used{};
};

namespace detail {
inline std::int64_t distance(Point a,Point b) {
    const auto dx=static_cast<std::int64_t>(a.x)-b.x,dy=static_cast<std::int64_t>(a.y)-b.y;
    return std::max(dx<0?-dx:dx,dy<0?-dy:dy);
}
inline bool inside(Point p,int width,int height) {return p.x>=0 && p.y>=0 && p.x<width && p.y<height;}
inline bool geometry(const Job& job) {
    if(!inside(job.worker,job.width,job.height))return false;
    if(job.family==Family::StationaryAreaCaster &&
       (!inside(job.aim,job.width,job.height) ||
        static_cast<double>(distance(job.worker,job.aim))>job.cast_range))return false;
    for(std::size_t i=0;i<job.member_count;++i) {
        const auto& member=job.members[i];
        if(member.hp<=0)continue;
        if(!inside(member.pos,job.width,job.height))return false;
        const auto reach=job.family==Family::Basic?job.basic_range:job.area_radius;
        const auto origin=job.family==Family::Basic?job.worker:job.aim;
        if(static_cast<double>(distance(origin,member.pos))>reach)return false;
    }
    return true;
}
} // namespace detail

// Sliced finite simulator: one advance unit is a phase transition, ONE float32
// HP subtraction, or one skipped dead member. No ceil(HP/damage), unbounded
// per-call HP loop, allocation, SDK import, hypothetical fight or hidden job.
// The horizon bounds actions; NeedCompute preserves every cursor. Work units
// are a structural bound, not a measured native WASM fuel guarantee.
class Valuation {
public:
    explicit Valuation(const CalendarInput& input):input_(input) {
        result_.query_generation=input.query_generation;result_.decision_round=input.now;
        result_.shop=project_shop(input.shop);
        if(result_.shop.status!=Status::Ready) {fail(result_.shop.reason);return;}
        if(!input.quiet_decision || !input.no_pending_capture_or_channel ||
           !input.stable_outgoing_supported || input.carried_gems!=0 ||
           !detail::positive(input.shop.body.hp))
        {fail(Reason::MissingPremise);return;}
        if(input.job_count==0 || input.job_count>max_jobs ||
           input.deadline<input.now || input.deadline-input.now>max_calendar_span ||
           !detail::positive(input.stable_outgoing_multiplier))
        {fail(Reason::JobShape);return;}
        for(std::size_t j=0;j<input.job_count;++j) {
            const auto& job=input.jobs[j];
            if(!job.full_group_known || !job.availability_supported || !job.geometry_supported ||
               !job.zero_incoming_through_continuation || !job.team_displacement_accounted ||
               !job.residual_persists_while_away)
            {fail(Reason::MissingPremise);return;}
            if(job.displaced_keep || job.displaced_shop)
            {fail(Reason::TeamDisplacement);return;}
            if(j>0 && job.group==input.jobs[j-1].group &&
               (!job.repeats_previous || job.generation==input.jobs[j-1].generation))
            {fail(Reason::ResourceReuse);return;}
            if(job.member_count==0 || job.member_count>max_members || job.observed_round>input.now ||
               job.valid_through<input.now || job.available_from<job.observed_round ||
               !detail::nonnegative(job.basic_range) || !detail::nonnegative(job.cast_range) ||
               !detail::nonnegative(job.area_radius) || !detail::nonnegative(job.cast_modifier) ||
               (job.family==Family::StationaryAreaCaster && (!job.cooldown_period || !detail::positive(job.cast_modifier))) ||
               (job.repeats_previous && (j==0 || job.group!=input.jobs[j-1].group ||
                  job.generation==input.jobs[j-1].generation)))
            {fail(Reason::JobShape);return;}
            for(std::size_t i=0;i<job.member_count;++i)
                if(!job.members[i].hp_exact || !detail::nonnegative(job.members[i].hp))
                {fail(Reason::MissingPremise);return;}
            if(!detail::geometry(job)) {fail(Reason::Geometry);return;}
        }
        if(!detail::tick(input.now,input.remaining_cooldown,ready_at_))
        {fail(Reason::CalendarOverflow);return;}
        clock_=input.now;
    }
    Status status() const {return result_.status;}
    const Result& result() const {return result_;}
    std::size_t last_work() const {return last_work_;}
    Status advance(std::size_t units) {
        last_work_=0;units=std::min(units,max_advance_units);
        while(last_work_<units && result_.status==Status::NeedCompute) {
            ++last_work_;++result_.work_used;
            if(stage_==Stage::Start)start_job();
            else if(stage_==Stage::Work)work();
            else if(stage_==Stage::Finish)finish_job();
            else finish_arm();
        }
        return result_.status;
    }
private:
    enum class Stage { Start,Work,Finish,ArmDone };
    CalendarInput input_;
    Result result_;
    std::array<float,max_members> hp_{};
    std::size_t arm_{},job_{},member_{},alive_{},last_work_{};
    std::uint32_t clock_{},ready_at_{},work_round_{},last_damage_{};
    float damage_{};
    Stage stage_{Stage::Start};
    Arm& outcome() {return arm_?result_.invest:result_.keep;}
    void fail(Reason reason) {result_.status=Status::Unsupported;result_.reason=reason;result_.decision=Decision::Unsupported;}
    bool add(std::uint32_t a,std::uint32_t b,std::uint32_t& value) {
        if(detail::tick(a,b,value))return true;fail(Reason::CalendarOverflow);return false;
    }
    void horizon() {outcome().horizon_reached=true;stage_=Stage::ArmDone;}
    void start_job() {
        if(job_==input_.job_count) {outcome().complete_all=true;stage_=Stage::ArmDone;return;}
        const auto& job=input_.jobs[job_];
        std::uint32_t arrival=clock_,release=job.available_from;
        if(job_==0 && arm_) {
            std::uint32_t delay{};
            if(!add(input_.recall_to_first_shop_ready,static_cast<std::uint32_t>(result_.shop.calls),delay) ||
               !add(clock_,delay,arrival) || !add(arrival,job.ingress_shop,arrival))return;
        } else if(!add(clock_,job_?job.ingress_between:job.ingress_keep,arrival))return;
        if(job.repeats_previous) {
            std::uint32_t respawn{};
            if(!add(outcome().payout_round[job_-1],job.respawn_after_previous_payout,respawn))return;
            release=std::max(release,respawn);
        }
        work_round_=std::max(arrival,release);
        if(job.family==Family::StationaryAreaCaster)work_round_=std::max(work_round_,ready_at_);
        if(work_round_>input_.deadline) {horizon();return;}
        if(work_round_>job.valid_through) {fail(Reason::AvailabilityExpired);return;}
        const auto& body=arm_?result_.shop.projected:input_.shop.body;
        const auto& effects=arm_?result_.shop.effects:input_.shop.effects;
        const auto power=job.family==Family::Basic?body.strength:detail::mul(body.focus,job.cast_modifier);
        const auto& current=input_.shop.body;
        const auto current_power=job.family==Family::Basic?current.strength:detail::mul(current.focus,job.cast_modifier);
        if(detail::mul(current_power,input_.stable_outgoing_multiplier)!=job.queried_current_damage)
        {fail(Reason::MissingPremise);return;}
        damage_=detail::mul(detail::mul(power,input_.stable_outgoing_multiplier),detail::add(1.0f,effects.monster));
        if(!detail::positive(damage_)) {fail(Reason::InvalidNumber);return;}
        alive_=member_=0;
        for(std::size_t i=0;i<job.member_count;++i) {
            hp_[i]=job.members[i].hp;if(hp_[i]>0)++alive_;
        }
        if(!alive_) {last_damage_=work_round_;stage_=Stage::Finish;return;}
        stage_=Stage::Work;
    }
    void work() {
        const auto& job=input_.jobs[job_];
        if(work_round_>input_.deadline) {horizon();return;}
        if(work_round_>job.valid_through) {fail(Reason::AvailabilityExpired);return;}
        if(member_>=job.member_count) {
            member_=0;
            if(job.family==Family::StationaryAreaCaster) {
                ++outcome().commands[job_];last_damage_=work_round_;
                if(!add(work_round_,job.cooldown_period,ready_at_))return;
                if(!alive_) {stage_=Stage::Finish;return;}
                work_round_=ready_at_;
            }
            return;
        }
        const auto index=member_;
        if(hp_[index]<=0) {++member_;return;}
        const float before=hp_[index];
        const float after=std::max(0.0f,detail::f32(before-damage_));
        if(!(after<before)) {fail(Reason::NoFloatProgress);return;}
        hp_[index]=after;if(after==0)--alive_;
        if(job.family==Family::StationaryAreaCaster)++member_;
        else {
            ++outcome().commands[job_];last_damage_=work_round_;
            if(after==0)++member_;
            if(!alive_) {stage_=Stage::Finish;return;}
            if(!add(work_round_,1,work_round_))return;
        }
    }
    void finish_job() {
        const auto& job=input_.jobs[job_];auto& arm=outcome();
        std::uint32_t payout{},bank{};
        if(!add(last_damage_,job.damage_to_capture,payout) ||
           !add(payout,job.capture_payout,payout) ||
           !add(payout,job.payout_to_bank,bank))return;
        arm.last_damage_round[job_]=last_damage_;arm.payout_round[job_]=payout;
        if(bank>input_.deadline) {horizon();return;}
        if(bank>job.valid_through) {fail(Reason::AvailabilityExpired);return;}
        arm.bank_round[job_]=bank;++arm.jobs_banked;arm.banked_gems+=job.gems;
        // Constructor admits only the declared zero-displacement family.
        arm.net_team_gems+=job.gems;
        clock_=bank;++job_;stage_=Stage::Start;
    }
    void finish_arm() {
        if(arm_==0) {
            arm_=1;job_=0;clock_=input_.now;
            if(!add(input_.now,input_.remaining_cooldown,ready_at_))return;
            stage_=Stage::Start;return;
        }
        result_.status=Status::Ready;result_.reason=Reason::None;result_.decision=Decision::Keep;
        const auto& keep=result_.keep;const auto& invest=result_.invest;
        if(result_.shop.calls==0)return;
        if(invest.net_team_gems>keep.net_team_gems)result_.decision=Decision::InvestByDeadline;
        else if(invest.net_team_gems==keep.net_team_gems && invest.net_team_gems>0 &&
                invest.complete_all && keep.complete_all) {
            const auto last=input_.job_count-1;
            result_.same_work_time_saved=static_cast<std::int64_t>(keep.bank_round[last])-invest.bank_round[last];
            if(result_.same_work_time_saved>0)result_.decision=Decision::EarlierSameWork;
        }
    }
};
} // namespace tt::production_investment
