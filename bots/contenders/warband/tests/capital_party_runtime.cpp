#define WB_PARTY_PROGRAM_NATIVE_HOST_FREE
#include "capital_party_runtime.h"
#include <cstdio>
#include <memory>
namespace r=wb::capital::party_runtime;namespace n=wb::capital::party_program_native;namespace q=wb::capital::party_program;
namespace p=wb::capital::party;namespace w=wb::capital::party_wire;
unsigned checks{},failures{};
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(false)
q::Contract contract(){q::Contract c;c.id=9;c.life=3;c.group=2;c.start=10;c.commit_by=20;c.arrive_by=40;c.work_by=90;c.capture_by=110;
    c.bank_by=150;c.renew_by=180;c.abort_by=190;c.camps={41,42,43};c.camp_count=3;c.capture_mask=7;c.max_refusals=2;c.max_no_progress=12;
    c.domain={1,2,3,4,5,6,10,180,q::Risk::ConditionalModel};
    for(unsigned i=0;i<3;++i){auto& m=c.members[i];m.id=static_cast<int>(4+i);m.role=static_cast<p::Role>(i);m.rendezvous={2+static_cast<int>(i),2};
        m.station={6+static_cast<int>(i),7};m.capture={6+static_cast<int>(i),6};m.release={6+static_cast<int>(i),10};m.altar={2,10};m.spawn={2+static_cast<int>(i),12};m.renewal=m.spawn;}
    // The melee approach to the final member ends at the centre source tile;
    // this fixed public assignment leaves the two ranged capture goals free.
    c.members[0].capture={7,6};c.members[1].capture={6,6};return c;}
n::Admission admission(){return {{true,true,true,true,true,true,{true,true,true}},true,true};}
struct World {
    q::Contract c=contract();std::array<p::Body,3> bodies{};p::Group group{};std::array<std::uint16_t,4> words{};int round{8};unsigned capture_ticks{};
    r::Life life{r::Life::Alive};std::uint64_t score{};unsigned commands{},heals{},basics{},writes{},banks{},submit_ticks{2};std::array<unsigned,3> shield_cd{},heal_cd{};
    std::array<unsigned,3> attack_type{0,1,0};std::array<int,3> attack_range{1,3,1};
    World(){for(unsigned i=0;i<3;++i){auto& b=bodies[i];b.id=c.members[i].id;b.role=static_cast<p::Role>(i);b.position=c.members[i].rendezvous;
            b.hp=b.max_hp=1000;b.alive=b.move=b.attack=b.cast=true;b.heal_ready=b.shield_ready=true;b.heal_range=4;b.heal_amount=20;b.shield_amount=30;}
        bodies[0].hp=800;group.id=c.group;group.count=3;group.observed_now=true;
        for(unsigned i=0;i<3;++i)group.camps[i]={c.camps[i],40,true,true,{6+static_cast<int>(i),6},1,true};}
    bool all_capture()const{for(const auto& b:bodies){bool on=false;for(unsigned j=0;j<group.count;++j)on|=b.position==group.camps[j].position;if(!b.alive||!on)return false;}return true;}
    void next(){++round;for(auto& b:bodies){b.move=b.attack=b.cast=true;
            const auto role=static_cast<unsigned>(b.id-4);if(shield_cd[role])--shield_cd[role];if(heal_cd[role])--heal_cd[role];b.shield_ready=!shield_cd[role];b.heal_ready=!heal_cd[role];
            if(b.channel==p::Channel::Submit){if(b.channel_remaining<=1){score+=b.bag;b.bag=0;b.channel=p::Channel::None;++banks;}else --b.channel_remaining;}
            if(b.position==c.members[static_cast<unsigned>(b.id-4)].spawn)b.hp=b.max_hp;}
        if(life==r::Life::Reward){if(all_capture()){if(++capture_ticks==5){life=r::Life::Respawning;for(auto& b:bodies){b.bag+=3;b.gold+=30;}}}else capture_ticks=0;}
        // Actual bounded fixture NPC actions precede all owner snapshots.
        for(unsigned i=0;i<group.count;++i)if(group.camps[i].alive){int target=-1,dist=99;for(unsigned j=0;j<3;++j){const int d=p::distance(bodies[j].position,group.camps[i].position);
                if(d<=1&&(d<dist||(d==dist&&(target<0||j<static_cast<unsigned>(target))))){target=static_cast<int>(j);dist=d;}}
            if(target>=0)bodies[static_cast<unsigned>(target)].hp-=1;}}
};
struct Host {
    World& world;unsigned role;std::array<std::uint8_t,256> blocked{},visible{},occupied{};r::Planning planning{};r::MonitorSample sample{};
    q::Revision samples{},events{},tokens{};unsigned queries{},raw_count{},fail_on{},throw_on{};bool policy_known{true},price_known{true},independent_ok{true},renewal_actual{true};
    bool observer_ok{true},bad_suffix{},changed_foes{},measure_now{},measure_price{},unbounded_unknown{},reject_cut{},change_cut_model{};
    std::uint64_t last_price_word{};unsigned prices{},empty_captures{},fuel_value{100000};bool throw_abort{};std::vector<n::Word> price_shapes;
    Host(World& wld,unsigned index):world(wld),role(index){visible.fill(1);blocked[10*16+2]=1;}
    int owner(){++queries;return world.c.members[role].id;}int round(){++queries;return world.round;}unsigned fuel(){++queries;return fuel_value;}
    q::Revision event(){return ++events;}q::Epoch epoch()const{return {world.c.members[role].id,world.round,static_cast<q::Revision>(world.round*10+role+1),0,1,1,1,1,1,world.c.life};}
    static bool price(void* ctx,const q::Frame& f,const q::Offer& o,const n::Word& word,unsigned suffix,n::Price& out){auto& h=*static_cast<Host*>(ctx);++h.prices;
        CHECK(f.source==h.sample.source&&o.contract==h.world.c.id&&word.source==o.source&&suffix<=word.count);h.last_price_word=word.token;
        if(h.measure_price||h.unbounded_unknown)h.price_shapes.push_back(word);
        if(!h.price_known||(h.bad_suffix&&suffix)||(h.reject_cut&&word.count==1))return false;out={};out.model=17;out.domain=h.world.c.domain.id;
        if(h.measure_price){out.measure_now=true;out.exit_tail=25000;if(h.change_cut_model&&word.count==1)++out.model;return true;}
        if(h.unbounded_unknown)return true;out.bounded=true;
        out.guard.fill(100);out.command.fill(100);out.readback.fill(1000);out.exit_tail=25000;return true;}
    r::Callbacks callbacks(){return {this,&price,nullptr,nullptr};}
    bool observe(q::Epoch e,const q::Contract& c,q::Frame& out){if(!observer_ok)return false;out={};out.source=e;out.source.sample=++samples;if(changed_foes)++out.source.foes;
        out.fresh=out.priority_contract_current=out.capital_obligation_clear=out.independent_interpreter_ready=out.group_life_known=true;
        out.self=world.bodies[role];out.self.team_score=world.score;out.self.capital_revision=1;out.self.at_spawn=out.self.position==c.members[role].spawn;
        out.self.at_station=out.self.position==c.members[role].station;out.self.at_capture=out.self.position==c.members[role].capture;
        out.self.altar_adjacent=p::distance(out.self.position,c.members[role].altar)<=1;out.self.on_any_source=false;
        for(unsigned j=0;j<world.group.count;++j)out.self.on_any_source|=out.self.position==world.group.camps[j].position;
        for(unsigned i=0;i<3;++i)out.peers[i]={world.bodies[i],true};out.peers[role]={out.self,true};out.group=world.group;
        planning={};planning.packet_current=planning.catalogue_complete=true;planning.width=planning.height=16;planning.blocked=blocked;planning.visible=visible;occupied.fill(0);
        for(unsigned i=0;i<3;++i)occupied[static_cast<unsigned>(world.bodies[i].position.y*16+world.bodies[i].position.x)]=1;
        for(unsigned i=0;i<world.group.count;++i)if(world.group.camps[i].alive)occupied[static_cast<unsigned>(world.group.camps[i].position.y*16+world.group.camps[i].position.x)]=1;
        planning.occupied=occupied;planning.move_speed=1;planning.attack_range=world.attack_range[role];planning.submit_range=1;planning.source_count=3;
        planning.attack_type_known=world.attack_type[role]<=1;planning.melee=world.attack_type[role]==0;
        for(unsigned i=0;i<3;++i){planning.sources[i]={c.camps[i],c.group,world.group.camps[i].position,1,1,!world.group.camps[i].alive};planning.next_priority[i]=3-static_cast<int>(i);planning.priority_known[i]=true;}
        planning.casts={role==0?p::Cast::Shield:role==1?p::Cast::Heal:p::Cast::Reset,p::Cast::None};planning.cast_ready={out.self.cast&&(role==0?out.self.shield_ready:role==1?out.self.heal_ready:true),true};
        auto& pi=planning.policy;pi.source=out.source;pi.grant=8;pi.risk=q::Risk::ConditionalModel;pi.horizon_round=e.round+1;
        pi.fresh=policy_known;pi.priority_excluded=pi.station_refusals_closed=pi.other_champions_excluded=pi.foes_covered=true;
        pi.stun_covered=pi.locks_covered=pi.ongoing_effects_covered=pi.relocation_covered=pi.capital_obligation_clear=pi.economy_events_closed=true;pi.draft_ordinal={0,1,2};
        pi.renewal_observed=renewal_actual&&out.self.at_spawn&&out.self.hp==out.self.max_hp&&out.self.bag==0&&world.score==9&&world.life==r::Life::Respawning?1:0;
        out.self.npc_quiet=r::quiet(planning,out.self.position);out.source_released=out.self.npc_quiet&&!out.self.on_any_source;
        out.altar_unlocked_known=out.altar_unlocked=true;out.priority_contract_current=out.capital_obligation_clear=policy_known;out.independent_interpreter_ready=independent_ok;out.nominated_target=-1;
        sample={out.source,world.life,9,90,0,world.all_capture(),policy_known,true,policy_known};return true;}
    bool admit(q::Epoch e,const q::Contract& c){q::Frame out;return independent_ok&&observe(e,c,out)&&r::policy_current(planning.policy,out,c);}
    bool nominate(const q::Contract& c,const q::Offer& o,const q::Frame& f,n::Word& word,q::Quote& quote){const bool ready=r::nominate(c,o,f,planning,callbacks(),++tokens,word,quote,measure_now);
        if(ready&&measure_now&&measure_price){CHECK(!word.price.bounded);if(word.count)CHECK(word.count==1&&word.price.measure_now);
            else {CHECK(!word.price.measure_now);if(o.kind==q::Kind::Capture)++empty_captures;}}return ready;}
    bool monitor(const q::Frame& f,r::MonitorSample& out){if(sample.source!=f.source)return false;out=sample;return true;}
    void read_words(std::array<std::uint16_t,4>& out){out=world.words;}
    bool write_word(unsigned offset,std::uint16_t value,n::Fence fence){if(!fence())return false;const auto count=queries;world.words[offset/2]=value;++world.writes;CHECK(queries==count);
        if(throw_abort&&offset>0&&w::decode_record(value).phase==w::Phase::Abort){throw_abort=false;throw 9;}return true;}
    bool independent(const n::Independent& grant,const q::Frame& f){return independent_ok&&grant.source==f.source&&grant.complete&&grant.funded&&grant.accepted;}
    bool independent_grant(const q::Frame& f,n::Independent& out){out={f.source,4,5,true,true,independent_ok};return independent(out,f);}
    bool tail_complete(const q::Frame& f){return renewal_actual&&planning.policy.source==f.source&&planning.policy.renewal_observed&&f.capital_obligation_clear;}
    bool guard(const q::Offer& o,const n::Word& word,unsigned step,const q::Frame& f,n::Guard& out){if(step>=word.count||planning.policy.source!=f.source)return false;
        if(word.price.measure_now&&(!measure_now||word.count!=1||step))return false;
        const auto& command=word.steps[step];bool legal=f.self.alive&&!f.self.stunned&&f.self.channel==p::Channel::None;
        if(command.kind==n::PrimitiveKind::Basic){const auto* camp=n::camp(f,command.target);legal&=f.self.attack&&camp&&camp->alive&&p::distance(f.self.position,camp->position)<=planning.attack_range;}
        else if(command.kind==n::PrimitiveKind::Move)legal&=f.self.move&&p::distance(f.self.position,command.destination)==1&&!r::occupied(planning,command.destination);
        else if(command.kind==n::PrimitiveKind::Heal)legal&=f.self.cast&&world.bodies[0].hp<world.bodies[0].max_hp;
        else if(command.kind==n::PrimitiveKind::Shield)legal&=f.self.cast;
        else if(command.kind==n::PrimitiveKind::Submit)legal&=f.self.bag&&f.self.altar_adjacent;
        const bool work=o.kind==q::Kind::WorkTank||o.kind==q::Kind::Strike||o.kind==q::Kind::Heal;auto terminal=f.self.position;
        for(unsigned i=step;i<word.count;++i)if(word.steps[i].kind==n::PrimitiveKind::Move)terminal=word.steps[i].destination;
        n::Price paid;const bool priced=price(this,f,o,word,step,paid);out={f.source,word.token,step,legal,r::survival(world.c,f,planning,terminal,work),true,priced};return true;}
    bool execute(const n::Primitive& command,n::Fence fence,bool& accepted){if(!fence())return false;const auto count=queries;++raw_count;++world.commands;accepted=raw_count!=fail_on;
        if(accepted){auto& b=world.bodies[role];switch(command.kind){case n::PrimitiveKind::Move:b.position=command.destination;b.move=false;if(world.attack_type[role]==1)b.attack=false;break;
            case n::PrimitiveKind::Basic:for(unsigned i=0;i<world.group.count;++i)if(world.group.camps[i].id==command.target){auto& camp=world.group.camps[i];camp.hp=std::max(0.f,camp.hp-10);camp.alive=camp.hp>0;}
                b.attack=false;++world.basics;break;
            case n::PrimitiveKind::Shield:b.shield+=30;b.cast=false;b.shield_ready=false;world.shield_cd[role]=2;break;
            case n::PrimitiveKind::Heal:world.bodies[0].hp=std::min(world.bodies[0].max_hp,world.bodies[0].hp+20);b.cast=false;b.heal_ready=false;world.heal_cd[role]=2;++world.heals;break;
            case n::PrimitiveKind::Submit:b.channel=p::Channel::Submit;b.channel_remaining=world.submit_ticks;break;default:CHECK(false);break;}
            world.group.whole_clear=true;for(unsigned i=0;i<world.group.count;++i)world.group.whole_clear&=!world.group.camps[i].alive;
            if(world.group.whole_clear&&world.life==r::Life::Alive)world.life=r::Life::Reward;}
        CHECK(queries==count);if(raw_count==throw_on)throw 7;return true;}
    bool effects(const q::Frame& before,const q::Frame& after,const n::Primitive& command,bool accepted,const q::Lease& lease,std::span<q::Actual> out,unsigned& count){count=0;if(!accepted)return true;
        auto add=[&](q::ReceiptKind kind,int target,float from,float to){auto& a=out[count++];a={};a.kind=kind;a.event=event();a.contract=world.c.id;a.life=world.c.life;a.word=lease.word;a.actor=before.self.id;a.group=world.c.group;a.target=target;a.before=from;a.after=to;a.observed_actual=a.accepted=true;};
        if(command.kind==n::PrimitiveKind::Basic){for(unsigned i=0;i<before.group.count;++i){const auto& a=before.group.camps[i];const auto* b=n::camp(after,a.id);if(b&&a.hp>b->hp)add(q::ReceiptKind::CampDamage,a.id,a.hp,b->hp);}}
        else if(command.kind==n::PrimitiveKind::Heal){const auto* a=n::peer(before,command.target);const auto* b=n::peer(after,command.target);if(a&&b&&b->hp>a->hp)add(q::ReceiptKind::Heal,a->id,a->hp,b->hp);}
        else if(command.kind==n::PrimitiveKind::Shield&&after.self.shield>before.self.shield)add(q::ReceiptKind::Shield,before.self.id,before.self.shield,after.self.shield);return true;}
};
struct Driver {
    World world;std::array<std::unique_ptr<Host>,3> hosts;std::array<std::unique_ptr<r::Runtime<Host>>,3> runtimes;
    explicit Driver(bool measure=false,bool paid_exit=false){n::Options options;options.allow_measure_now=measure;options.allow_paid_exit_handoff=paid_exit;
        for(unsigned i=0;i<3;++i){hosts[i]=std::make_unique<Host>(world,i);hosts[i]->measure_now=hosts[i]->measure_price=measure;
            runtimes[i]=std::make_unique<r::Runtime<Host>>(*hosts[i],world.c,world.c.members[i].id,admission(),options);}}
    void input(unsigned i){(void)runtimes[i]->head(hosts[i]->epoch());for(unsigned k=0;k<3;++k){const auto result=runtimes[i]->step();if(hosts[i]->measure_now&&hosts[i]->measure_price)CHECK(result.accepted_steps<=1);
            if(result.attempted&&result.accepted_steps)break;
            if(result.status==n::Status::Prestart||result.status==n::Status::NoOffer||result.status==n::Status::NeedModel||result.status==n::Status::Refused)break;}}
    void tick(){for(unsigned i=0;i<3;++i)input(i);world.next();}
    void until(q::Phase target,unsigned role=0){for(unsigned i=0;i<200&&runtimes[role]->session().program().phase()!=target;++i)tick();CHECK(runtimes[role]->session().program().phase()==target);}
};
struct PrefixFixture {
    World world;Host host;n::Session<Host> session;n::Word word{};q::Quote quote{};q::Offer offer{};
    static n::Options options(bool admitted){n::Options out;out.allow_measure_now=admitted;return out;}
    PrefixFixture(unsigned role,bool admitted):host(world,role),session(host,world.c,world.c.members[role].id,admission(),options(admitted)){
        CHECK(session.begin(host.epoch())==n::Status::Ready);
        if(role==0)CHECK(session.propose(true).accepted);else {world.words[0]=w::encode_header(world.c.start,static_cast<unsigned>(world.c.group)).word;CHECK(session.arm()==n::Status::Prestart);}
        world.round=10;CHECK(session.begin(host.epoch())==n::Status::Ready);CHECK(session.observe()==n::Status::Ready);CHECK(prepare());CHECK(session.execute(offer,quote,word).accepted);
        world.round=11;remote(w::Phase::Arrived);CHECK(session.begin(host.epoch())==n::Status::Ready);CHECK(session.observe()==n::Status::Ready);CHECK(session.program().phase()==q::Phase::Enter);
        for(unsigned i=0;i<3;++i)world.bodies[i].position=world.c.members[i].station;
        world.round=12;remote(w::Phase::Entered);CHECK(session.begin(host.epoch())==n::Status::Ready);CHECK(session.observe()==n::Status::Ready);CHECK(session.program().phase()==q::Phase::Work);}
    void remote(w::Phase phase){for(unsigned i=0;i<3;++i)if(i!=host.role)world.words[i+1]=w::encode_record(world.round,phase,true).word;}
    bool prepare(){const auto nominated=session.nominate();offer=nominated.offer;return nominated.status==q::Status::Ready&&host.nominate(world.c,offer,session.actual(),word,quote);}
    void measure(){host.measure_now=host.measure_price=true;host.price_shapes.clear();}
    n::Result run(){return session.execute(offer,quote,word);}
};
void full_cycle(unsigned ticks,bool measure=false){Driver d(measure);d.world.submit_ticks=ticks;for(unsigned i=0;i<180;++i){d.tick();
        bool closed=true;for(const auto& rt:d.runtimes)closed&=!rt->session().wire_owned();if(closed&&d.world.round>10)break;}
    CHECK(d.world.heals>0&&d.world.basics>=12&&d.world.banks==3&&d.world.score==9);
    for(unsigned i=0;i<3;++i){const auto& rt=*d.runtimes[i];const auto done=rt.session().program().completion();
        if(rt.session().program().phase()!=q::Phase::Complete)std::printf("cycle role %u phase %u failure %u round %d\n",i,static_cast<unsigned>(rt.session().program().phase()),static_cast<unsigned>(done.failure),d.world.round);
        CHECK(rt.session().program().phase()==q::Phase::Complete);CHECK(!rt.session().wire_owned()&&!rt.session().needs_reconciliation());
        CHECK(rt.custody().source_known&&rt.custody().bank_known&&rt.custody().credited==3);CHECK(done.quantities[i].received_gems==3&&done.quantities[i].banked_gems==3);
        CHECK(done.members[i].positive_work&&done.members[i].cleanup_ack);CHECK(!done.complete_observed);CHECK(done.lifecycle_attested);}
    CHECK(d.runtimes[1]->session().program().completion().quantities[1].useful_heal>0);
    if(measure){CHECK(d.hosts[0]->empty_captures+d.hosts[1]->empty_captures+d.hosts[2]->empty_captures>0);
        for(unsigned i=0;i<3;++i)CHECK(d.hosts[i]->planning.policy.renewal_observed&&d.runtimes[i]->session().program().completion().members[i].renewed);}
    CHECK(w::decode_record(d.world.words[1]).phase==w::Phase::CleanupDone&&w::decode_record(d.world.words[2]).phase==w::Phase::CleanupDone&&w::decode_record(d.world.words[3]).phase==w::Phase::CleanupDone);}
void unknown_and_abort(){World world;Host host(world,0);host.policy_known=false;r::Runtime<Host> unclaimed(host,world.c,4,admission());auto head=unclaimed.head(host.epoch());CHECK(!head.wire_owned&&!head.claimed&&head.status==n::Status::NoOffer);CHECK(!unclaimed.step().attempted&&world.writes==0);
    host.policy_known=true;host.independent_ok=false;r::Runtime<Host> missing_fallback(host,world.c,4,admission());CHECK(!missing_fallback.head(host.epoch()).claimed);host.independent_ok=true;
    auto invalid=world.c;invalid.capture_mask=1;r::Runtime<Host> rejected(host,invalid,4,admission());CHECK(!rejected.head(host.epoch()).wire_owned);
    Driver d;d.until(q::Phase::Work);d.hosts[0]->price_known=false;n::Result withdrawn;const auto physical=d.world.commands;
    for(unsigned i=0;i<6&&w::decode_record(d.world.words[1]).phase!=w::Phase::Abort;++i){(void)d.runtimes[0]->head(d.hosts[0]->epoch());withdrawn=d.runtimes[0]->step();
        if(w::decode_record(d.world.words[1]).phase!=w::Phase::Abort)d.world.next();}
    CHECK(withdrawn.accepted&&w::decode_record(d.world.words[1]).phase==w::Phase::Abort);
    for(unsigned i=0;i<4;++i){const auto blocked=d.runtimes[0]->step();CHECK(!blocked.attempted&&blocked.status==n::Status::Aborting);}CHECK(d.world.commands==physical);
    for(unsigned i=0;i<200;++i){d.tick();if(!d.runtimes[0]->session().wire_owned())break;}
    CHECK(d.runtimes[0]->session().program().phase()==q::Phase::Handoff);CHECK(!d.runtimes[0]->session().wire_owned());CHECK(d.world.score==0);}
void receipt_history(){World world;Host h(world,0);q::Frame f;CHECK(h.observe(h.epoch(),world.c,f));r::Custody custody;std::array<q::Actual,2> receipts{};
    auto s=h.sample;s.life=r::Life::Reward;s.all_capture=s.capture_history_closed=true;CHECK(custody.observe(h,world.c,f,s,receipts)==0);
    ++f.source.round;++f.source.sample;f.self.bag=3;f.self.gold=30;s.source=f.source;s.life=r::Life::Respawning;CHECK(custody.observe(h,world.c,f,s,receipts)==1);CHECK(custody.source_known);
    f.self.channel=p::Channel::Submit;f.self.channel_remaining=1;++f.source.round;++f.source.sample;s.source=f.source;CHECK(custody.observe(h,world.c,f,s,receipts)==0);
    ++f.source.round;++f.source.sample;f.self.channel=p::Channel::None;f.self.bag=0;f.self.team_score=3;s.source=f.source;CHECK(custody.observe(h,world.c,f,s,receipts)==1&&receipts[0].kind==q::ReceiptKind::BankPayout&&custody.credited==3);
    r::Custody gap;f.self.bag=f.self.gold=f.self.team_score=0;s.life=r::Life::Reward;CHECK(gap.observe(h,world.c,f,s,receipts)==0);
    f.source.round+=2;++f.source.sample;f.self.bag=3;f.self.gold=30;s.source=f.source;s.life=r::Life::Respawning;CHECK(gap.observe(h,world.c,f,s,receipts)==0&&gap.broken&&!gap.bank_known&&!gap.source_known);
    r::Custody no_cause;f.self.bag=f.self.gold=0;s.life=r::Life::Reward;s.capture_history_closed=false;CHECK(no_cause.observe(h,world.c,f,s,receipts)==0);
    ++f.source.round;++f.source.sample;f.self.bag=3;f.self.gold=30;s.source=f.source;s.life=r::Life::Respawning;CHECK(no_cause.observe(h,world.c,f,s,receipts)==0&&no_cause.broken&&!no_cause.source_known);}
void primitive_failures(){Driver refused;refused.until(q::Phase::Work);auto& h=*refused.hosts[0];h.fail_on=h.raw_count+1;const auto before=refused.world.commands;
    for(unsigned i=0;i<6&&refused.world.commands==before;++i){refused.input(0);refused.world.next();}CHECK(refused.world.commands==before+1&&!refused.runtimes[0]->session().needs_reconciliation());
    Driver interrupted;interrupted.until(q::Phase::Work);auto& throwing=*interrupted.hosts[0];throwing.throw_on=throwing.raw_count+1;bool threw=false;
    try{for(unsigned i=0;i<6&&throwing.raw_count<throwing.throw_on;++i){interrupted.input(0);if(throwing.raw_count<throwing.throw_on)interrupted.world.next();}}catch(int){threw=true;}
    CHECK(threw&&interrupted.runtimes[0]->session().needs_reconciliation());
    interrupted.world.next();auto head=interrupted.runtimes[0]->head(throwing.epoch());CHECK(!head.pending);CHECK(interrupted.runtimes[0]->session().program().failure()==q::Failure::UnknownEffect);
    CHECK(!interrupted.runtimes[0]->session().program().completion().quantity_history_known[0]);
    Driver future;future.until(q::Phase::Work);future.hosts[0]->renewal_actual=false;for(unsigned i=0;i<180;++i){future.tick();if(!future.runtimes[0]->session().wire_owned())break;}
    CHECK(future.runtimes[0]->session().program().phase()==q::Phase::Handoff);CHECK(!future.runtimes[0]->session().program().completion().members[0].renewed);}
void bounds_and_ties(){World wld;Host h(wld,0);q::Frame f;CHECK(h.observe(h.epoch(),wld.c,f));auto x=h.planning;
    f.self.position={6,7};for(unsigned i=0;i<3;++i){f.peers[i].body.position={6+static_cast<int>(i),7};f.peers[i].body.hp=100;}f.peers[0].body=f.self;
    CHECK(r::survival(wld.c,f,x,f.self.position,true));f.peers[1].body.hp=0;CHECK(!r::survival(wld.c,f,x,f.self.position,true));
    f.peers[1].observed_now=false;CHECK(!r::survival(wld.c,f,x,f.self.position,true));CHECK(r::survival(wld.c,f,x,f.self.position,false));
    f.peers[1].observed_now=true;f.peers[1].body.hp=100;f.self.hp=50;f.peers[0].body=f.self;x.sources[0].damage=60;x.sources[1].next_dead=x.sources[2].next_dead=true;
    CHECK(!r::survival(wld.c,f,x,f.self.position,true));x.next_priority[1]=4;CHECK(r::survival(wld.c,f,x,f.self.position,true));
    p::Point next;CHECK(r::next_step(x,{1,1},{3,3},next));h.visible[3*16+3]=0;CHECK(!r::next_step(x,{1,1},{3,3},next));
    std::array<std::uint64_t,6> raw{};raw[4]=1ull<<9;CHECK(r::raw_bit(raw,3,{9,9}));CHECK(!r::raw_bit(raw,3,{1,9}));}
void measure_now_prefixes(){
    PrefixFixture native_off(0,false);native_off.measure();CHECK(native_off.prepare());CHECK(native_off.word.count==1&&!native_off.word.price.bounded&&native_off.word.price.measure_now);
    CHECK(native_off.run().status==n::Status::NeedModel&&native_off.host.raw_count==0);
    PrefixFixture hook_off(0,true);hook_off.host.measure_price=true;CHECK(!hook_off.prepare()&&hook_off.host.raw_count==0);
    PrefixFixture unknown(0,true);unknown.host.measure_now=unknown.host.unbounded_unknown=true;CHECK(!unknown.prepare()&&unknown.host.raw_count==0);
    PrefixFixture rejected(0,true);rejected.measure();rejected.host.reject_cut=true;CHECK(!rejected.prepare()&&rejected.host.price_shapes.size()==2&&rejected.host.raw_count==0);
    PrefixFixture changed(0,true);changed.measure();changed.host.change_cut_model=true;CHECK(!changed.prepare()&&changed.host.raw_count==0);
    PrefixFixture shield(0,true);shield.measure();CHECK(shield.prepare());CHECK(shield.host.price_shapes.size()==2&&shield.host.price_shapes[0].count==2&&shield.host.price_shapes[1].count==1);
    CHECK(shield.word.steps[0].kind==n::PrimitiveKind::Shield&&shield.word.selected_cast==p::Cast::Shield&&shield.word.camp_target==-1);
    CHECK(shield.run().accepted_steps==1&&shield.host.raw_count==1);const auto shield_done=shield.session.program().completion();
    CHECK(shield_done.quantities[0].shield==30&&shield_done.quantities[0].work==0&&!shield_done.members[0].positive_work&&!shield_done.members[0].payout);
    CHECK(shield.session.telemetry().measured_word.count==1&&shield.session.telemetry().guards[0].known&&shield.session.telemetry().commands[0].known&&shield.session.telemetry().readbacks[0].known);
    CHECK(shield.session.telemetry().guard_overruns==0&&shield.session.telemetry().command_overruns==0&&shield.session.telemetry().readback_overruns==0);
    PrefixFixture heal(1,true);heal.measure();CHECK(heal.prepare());CHECK(heal.host.price_shapes.size()==2&&heal.host.price_shapes[0].count==2&&heal.host.price_shapes[0].camp_target>=0);
    CHECK(heal.host.price_shapes[1].count==1&&heal.host.price_shapes[1].camp_target==-1&&heal.word.camp_target==-1&&heal.quote.camp_target==-1&&heal.word.selected_cast==p::Cast::Heal);
    const auto hp=heal.world.bodies[0].hp;const auto camp_hp=heal.world.group.camps[0].hp;CHECK(heal.run().accepted_steps==1&&heal.host.raw_count==1);
    CHECK(heal.world.bodies[0].hp>hp&&heal.world.group.camps[0].hp==camp_hp&&heal.session.program().completion().quantities[1].work==0);
    PrefixFixture bounded(1,true);bounded.host.measure_now=true;CHECK(bounded.prepare()&&bounded.word.count==2&&bounded.word.price.bounded&&!bounded.word.price.measure_now);
    CHECK(bounded.run().accepted_steps==2&&bounded.host.raw_count==2&&bounded.session.program().completion().quantities[1].work>0);
    PrefixFixture refused(0,true);refused.measure();CHECK(refused.prepare());refused.host.fail_on=1;CHECK(refused.run().status==n::Status::Refused&&refused.host.raw_count==1);
    CHECK(refused.session.program().completion().quantities[0].shield==0&&!refused.session.needs_reconciliation());
    PrefixFixture interrupted(0,true);interrupted.measure();CHECK(interrupted.prepare());interrupted.host.throw_on=1;bool threw=false;try{(void)interrupted.run();}catch(int){threw=true;}
    CHECK(threw&&interrupted.session.needs_reconciliation()&&interrupted.host.raw_count==1);interrupted.world.next();CHECK(interrupted.session.begin(interrupted.host.epoch())==n::Status::Pending);
    CHECK(interrupted.session.reconcile_pending().unknown_effect&&!interrupted.session.needs_reconciliation()&&interrupted.host.raw_count==1);
}
void observation_cost_fences(){q::Epoch request{4,12,121,0,1,1,1,1,1,3};auto actual=request;actual.sample=7;actual.effects=2;
    const auto ok=r::observation_cost(request,actual,90000,78000,{12,12,12,12},true);CHECK(ok.known&&ok.observed&&ok.before-ok.after==12000&&ok.source==actual);
    for(unsigned i=0;i<4;++i){auto rounds=std::array<int,4>{12,12,12,12};rounds[i]=13;const auto stale=r::observation_cost(request,actual,90000,78000,rounds,true);CHECK(!stale.known&&stale.source==q::Epoch{});}
    CHECK(!r::observation_cost(request,actual,20000,90000,{12,12,12,12},true).known);auto other=actual;++other.input;
    CHECK(!r::observation_cost(request,other,90000,78000,{12,12,12,12},true).known);
    const auto failed=r::observation_cost(request,{},90000,78000,{12,12,12,12},false);CHECK(failed.known&&!failed.observed&&failed.source==q::Epoch{});
    const r::ObservationCost cleared{};CHECK(!cleared.known&&!cleared.observed&&cleared.source==q::Epoch{});
}
void measure_empty_control(){World world;Host h(world,0);q::Frame frame;CHECK(h.observe(h.epoch(),world.c,frame));q::Offer offer{frame.source,world.c.id,1,q::Kind::PublishCommit};n::Word word;q::Quote quote;
    h.price_known=false;CHECK(!r::nominate(world.c,offer,frame,h.planning,h.callbacks(),9,word,quote));const auto calls=h.prices;
    CHECK(r::nominate(world.c,offer,frame,h.planning,h.callbacks(),10,word,quote,true));CHECK(!word.count&&!word.price.bounded&&!word.price.measure_now&&h.prices==calls);
    offer.kind=q::Kind::HoldChannel;frame.self.channel=p::Channel::Submit;CHECK(r::nominate(world.c,offer,frame,h.planning,h.callbacks(),11,word,quote,true));
    CHECK(!word.count&&!word.price.bounded&&!word.price.measure_now&&h.prices==calls);
    frame.self.channel=p::Channel::None;CHECK(!r::nominate(world.c,offer,frame,h.planning,h.callbacks(),12,word,quote,true));
    offer.kind=q::Kind::Capture;offer.destination=world.c.members[0].capture;frame.self.position=offer.destination;frame.self.at_capture=true;frame.group.whole_clear=true;
    for(unsigned i=0;i<frame.group.count;++i){frame.group.camps[i].alive=false;frame.group.camps[i].hp=0;}
    const auto capture_calls=h.prices;CHECK(r::nominate(world.c,offer,frame,h.planning,h.callbacks(),13,word,quote,true));
    CHECK(!word.count&&!word.price.bounded&&!word.price.measure_now&&h.prices==capture_calls&&frame.self.bag==0);
    CHECK(!r::nominate(world.c,offer,frame,h.planning,h.callbacks(),14,word,quote));
    frame.fresh=false;CHECK(!r::nominate(world.c,offer,frame,h.planning,h.callbacks(),15,word,quote,true));frame.fresh=true;
    frame.self.at_capture=false;CHECK(!r::nominate(world.c,offer,frame,h.planning,h.callbacks(),16,word,quote,true));frame.self.at_capture=true;
    frame.group.camps[0].observed_now=false;CHECK(!r::nominate(world.c,offer,frame,h.planning,h.callbacks(),17,word,quote,true));frame.group.camps[0].observed_now=true;
    frame.group.camps[0].id=99;CHECK(!r::nominate(world.c,offer,frame,h.planning,h.callbacks(),18,word,quote,true));frame.group.camps[0].id=world.c.camps[0];
    World rejected_world;Host rejected_host(rejected_world,0);rejected_host.measure_now=true;rejected_host.price_known=false;r::Runtime<Host> rejected(rejected_host,rejected_world.c,4,admission());
    (void)rejected.head(rejected_host.epoch());CHECK(rejected.step().accepted);rejected_world.round=10;(void)rejected.head(rejected_host.epoch());CHECK(rejected.step().status==n::Status::NeedModel&&!rejected.session().program().completion().members[0].committed);
    World admitted_world;Host admitted_host(admitted_world,0);admitted_host.measure_now=true;admitted_host.price_known=false;n::Options options;options.allow_measure_now=true;
    r::Runtime<Host> admitted(admitted_host,admitted_world.c,4,admission(),options);(void)admitted.head(admitted_host.epoch());CHECK(admitted.step().accepted);
    admitted_world.round=10;(void)admitted.head(admitted_host.epoch());CHECK(admitted.step().accepted&&admitted.session().program().completion().members[0].committed);
    CHECK(admitted_host.raw_count==0&&admitted.session().telemetry().measured_word.count==0&&!admitted.session().telemetry().guards[0].known);
}
void actual_attack_types(){
    for(unsigned role=0;role<3;++role){PrefixFixture d(role,false);auto& body=d.world.bodies[role];body.position=role==1?p::Point{2,7}:p::Point{4,7};body.cast=false;body.heal_ready=false;
        CHECK(d.session.observe()==n::Status::Ready);CHECK(d.prepare());CHECK(d.word.steps[0].kind==n::PrimitiveKind::Move);
        CHECK(d.word.count==(role==1?1u:2u));if(role!=1)CHECK(d.word.steps[1].kind==n::PrimitiveKind::Basic);
        const auto hp=d.world.group.camps[0].hp;const auto result=d.run();CHECK(result.accepted_steps==d.word.count&&d.host.raw_count==d.word.count);
        CHECK(role==1?d.world.group.camps[0].hp==hp:d.world.group.camps[0].hp<hp);
        if(role==1){CHECK(!d.session.actual().self.attack);CHECK(!d.prepare());}}
    PrefixFixture changed_range(1,false);changed_range.world.attack_range[1]=1;changed_range.world.bodies[1].position={4,7};changed_range.world.bodies[1].cast=false;changed_range.world.bodies[1].heal_ready=false;
    CHECK(changed_range.session.observe()==n::Status::Ready);CHECK(changed_range.prepare()&&changed_range.word.count==1);CHECK(changed_range.run().accepted_steps==1&&!changed_range.session.actual().self.attack);
    PrefixFixture unknown(2,false);unknown.world.attack_type[2]=2;unknown.world.bodies[2].position={4,7};unknown.world.bodies[2].cast=false;
    CHECK(unknown.session.observe()==n::Status::Ready);CHECK(unknown.prepare()&&unknown.word.count==1&&unknown.word.steps[0].kind==n::PrimitiveKind::Move);
}
void requested_abort_and_paid_exit(){
    World world;Host h(world,0);n::Options options;options.allow_measure_now=options.allow_paid_exit_handoff=true;r::Runtime<Host> untouched(h,world.c,4,admission(),options);
    untouched.request_abort();const auto queries=h.queries;const auto before=untouched.head(h.epoch());CHECK(untouched.stopping()&&!before.claimed&&!before.wire_owned&&h.queries==queries&&world.writes==0);
    CHECK(!untouched.step().attempted&&h.raw_count==0);
    Driver d(true,true);d.until(q::Phase::Work);const auto physical=d.world.commands;
    for(unsigned i=0;i<3;++i){d.hosts[i]->policy_known=false;d.hosts[i]->price_known=false;d.runtimes[i]->request_abort();}d.hosts[0]->independent_ok=false;
    for(unsigned i=0;i<4;++i)d.tick();CHECK(d.runtimes[0]->session().program().phase()==q::Phase::Abort&&d.runtimes[0]->session().wire_owned());
    CHECK(!d.runtimes[0]->session().actual().source_released&&!d.runtimes[0]->session().actual().self.npc_quiet&&d.world.commands==physical);
    d.hosts[0]->independent_ok=true;for(unsigned i=0;i<12;++i){d.tick();bool closed=true;for(const auto& rt:d.runtimes)closed&=!rt->session().wire_owned();if(closed)break;}
    for(unsigned i=0;i<3;++i){const auto& rt=*d.runtimes[i];const auto done=rt.session().program().completion();CHECK(rt.stopping()&&rt.session().program().phase()==q::Phase::Handoff&&!rt.session().wire_owned());
        CHECK(done.members[i].handoff&&!done.members[i].released&&!done.members[i].payout&&!done.members[i].renewed&&done.quantities[i].received_gems==0&&done.quantities[i].banked_gems==0);}
    CHECK(d.world.commands==physical);const auto writes=d.world.writes;for(unsigned i=0;i<3;++i){d.runtimes[i]->request_abort();d.input(i);}CHECK(d.world.writes==writes);
    Driver ordinary(true,false);ordinary.until(q::Phase::Work);ordinary.hosts[0]->policy_known=false;ordinary.runtimes[0]->request_abort();for(unsigned i=0;i<5;++i)ordinary.tick();
    CHECK(ordinary.world.round<ordinary.world.c.abort_by&&ordinary.runtimes[0]->session().program().phase()==q::Phase::Abort&&ordinary.runtimes[0]->session().wire_owned());
}
void abort_retry_and_pending(){
    Driver low(true,true);low.until(q::Phase::Work);auto& h=*low.hosts[0];auto& rt=*low.runtimes[0];(void)rt.head(h.epoch());rt.request_abort();const auto writes=low.world.writes;h.fuel_value=1;
    CHECK(rt.step().status==n::Status::NeedFuel&&rt.stopping()&&low.world.writes==writes);h.fuel_value=100000;low.world.next();CHECK(rt.head(h.epoch()).status==n::Status::Ready);
    CHECK(rt.step().accepted&&w::decode_record(low.world.words[1]).phase==w::Phase::Abort&&rt.stopping());
    Driver interrupted(true,true);interrupted.until(q::Phase::Work);auto& host=*interrupted.hosts[0];auto& stopping=*interrupted.runtimes[0];(void)stopping.head(host.epoch());stopping.request_abort();host.throw_abort=true;
    bool threw=false;try{(void)stopping.step();}catch(int){threw=true;}CHECK(threw&&stopping.session().needs_reconciliation());const auto issued=interrupted.world.writes;
    interrupted.world.next();(void)stopping.head(host.epoch());CHECK(!stopping.session().needs_reconciliation()&&stopping.session().own_abort_visible());
    CHECK(stopping.step().status==n::Status::Handoff&&interrupted.world.writes==issued&&stopping.session().wire_owned());
    Driver pending(true,true);pending.until(q::Phase::Work);auto& p_host=*pending.hosts[0];auto& p_rt=*pending.runtimes[0];p_host.throw_on=p_host.raw_count+1;threw=false;
    try{for(unsigned i=0;i<6&&p_host.raw_count<p_host.throw_on;++i){pending.input(0);if(p_host.raw_count<p_host.throw_on)pending.world.next();}}catch(int){threw=true;}
    CHECK(threw&&p_rt.session().needs_reconciliation());p_rt.request_abort();const auto raw=p_host.raw_count;pending.world.next();(void)p_rt.head(p_host.epoch());
    CHECK(!p_rt.session().needs_reconciliation()&&p_rt.session().program().failure()==q::Failure::UnknownEffect);CHECK(p_rt.step().accepted&&p_host.raw_count==raw);
    pending.world.next();(void)p_rt.head(p_host.epoch());CHECK(p_rt.step().status==n::Status::Handoff&&p_host.raw_count==raw&&!p_rt.session().program().completion().quantity_history_known[0]);
}
int main(){full_cycle(1);full_cycle(2);full_cycle(1,true);unknown_and_abort();receipt_history();primitive_failures();bounds_and_ties();measure_now_prefixes();observation_cost_fences();measure_empty_control();actual_attack_types();requested_abort_and_paid_exit();abort_retry_and_pending();std::printf("party runtime %u checks %u failures\n",checks,failures);return failures?1:0;}
