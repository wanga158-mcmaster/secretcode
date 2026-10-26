// Declared OWN catalogue-base scenario, not an SDK-observed Snapshot. Pure
// production components/shared pure publication predicates only. The SDK
// collector, actual Life observation and private activation remain UNEXECUTED.
#define main characterization_unused_main
#include "assignment_static_characterization.cpp"
#undef main
#include "../src/assignment_fresh_binding.h"
#include "../src/assignment_bound_programme.h"
#include "../src/assignment_cash_cache.h"
#include "../src/assignment_protocol.h"
#include "../src/assignment_publication.h"
#include <cstring>

namespace pipeline {
namespace fb=tt::assignment_fresh_binding;namespace bp=tt::assignment_bound_programme;
namespace cc=tt::assignment_cash_cache;namespace cr=tt::assignment_cash_routes;
namespace ap=tt::assignment_protocol;namespace pj=tt::production_job;
namespace sg=tt::assignment_static_geometry;namespace ag=tt::assignment_geometry;namespace sm=tt::assignment_static_menu;
namespace pub=tt::assignment_publication;
struct BaseSpell {int kind,range,radius,duration;float modifier;std::uint32_t period;bool exclusive;};
struct BaseKit {const char* name;std::uint64_t actor;float hp,strength,focus,incoming,health_growth,strength_growth,focus_growth,bank_gold,health_strength;int basic_range;std::array<BaseSpell,2> spells;};
struct Reward {std::size_t literal_group;std::uint32_t gems,gold,respawn;};
// BEGIN GENERATED PIPELINE INPUTS
inline constexpr char catalogue_sha[]="7cc2c639c314d0c739ad74e0fc20ade667304276a6040c014f92f5915f0b1d87";
inline constexpr std::size_t inventory_slots=42;
inline constexpr std::uint32_t horizon=2000;
inline constexpr std::uint32_t capture_inputs=5;
inline constexpr std::uint32_t submit_minimum=1;
inline constexpr std::uint32_t submit_base=0;
inline constexpr float submit_multiplier=0.1f,spawn_healing=1.0f;
inline constexpr std::uint32_t heal_inputs=1;
inline constexpr std::array<BaseKit,2> base_kits{{
    {"marcella",1,1200.0f,45.0f,60.0f,1.0f,100.0f,5.0f,15.0f,2.0f,0.0f,1,{{{0,0,0,0,0.0f,8,false},{2,4,0,0,2.0f,8,true}}}},
    {"ryth",2,1200.0f,45.0f,100.0f,1.0f,100.0f,5.0f,10.0f,0.0f,0.00999999978f,1,{{{3,4,1,1,1.0f,8,true},{1,0,0,0,0.0f,8,false}}}},
}};
inline constexpr std::array<Reward,28> rewards{{
    {0,22,130,60},
    {1,22,100,50},
    {2,15,100,50},
    {3,26,190,60},
    {4,4,80,50},
    {5,4,60,50},
    {6,11,40,50},
    {7,19,150,60},
    {8,15,100,50},
    {9,15,110,60},
    {10,0,600,50},
    {11,8,120,50},
    {12,30,220,60},
    {13,45,300,60},
    {14,45,300,60},
    {15,30,220,60},
    {16,8,120,50},
    {17,0,600,50},
    {18,15,110,60},
    {19,15,100,50},
    {20,4,60,50},
    {21,26,190,60},
    {22,19,150,60},
    {23,11,40,50},
    {24,4,80,50},
    {25,15,100,50},
    {26,22,100,50},
    {27,22,130,60},
}};
// END GENERATED PIPELINE INPUTS

constexpr std::uint32_t original_epoch=10,publication_epoch=100;constexpr unsigned bound=1000000;
int checks{},failures{};
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL pipeline %s\n",why);}}
struct Result {
    const char* stop{"world"};int group{-1};
    std::array<int,14> status;std::array<std::uint64_t,7> work{};std::array<std::uint64_t,4> value{};
    std::array<long long,8> dates;unsigned card{};bool first_cash{};std::uint32_t channel{};
    std::array<unsigned,2> publication{};
    Result(){status.fill(-1);dates.fill(-1);}
};
struct OwnInputs {
    pj::Snapshot snapshot;
    std::array<std::uint32_t,64> inventory{};
    std::size_t inventory_size{inventory_slots};
};
struct PurePublicationData {
    std::optional<int> anchor{1};
    std::optional<OwnInputs> actual;
    std::optional<fb::Current> companion;
    std::shared_ptr<bp::Programme> model;
    std::shared_ptr<const cr::Routes> cash_routes;
    std::shared_ptr<const sg::StaticGeometry> geometry;
    std::shared_ptr<const sg::TargetGeometry> target;
    std::uint64_t candidate{};
    const cc::Cache& cash_cache;
};
struct PureOwner {std::uint64_t body{},field{1};};
// This runs exactly the production pure predicates, in the production order.
// Fresh SDK outcomes are DECLARATIONS in this fixture, never fake collectors.
unsigned publication_case(const PurePublicationData& data,const PureOwner& owner,
    const OwnInputs& actual,std::uint32_t epoch,const cr::Key& expected) {
    unsigned stages{};unsigned life_reads{};
    if(!pub::prepared(data,[&]{return data.geometry&&data.target&&data.candidate&&
        data.cash_cache.key()==expected;}))return stages;
    stages|=1u;const auto body=data.companion->key.body_revision;
    if(!pub::frozen_body(data,owner,body))return stages;stages|=2u;
    if(!pub::cash_complete(data))return stages;stages|=4u;
    const bool ready=true; // DECLARED current collector result; no SDK executed.
    if(!pub::fresh_matches(data,owner,actual,body,ready,
        [&]{return actual.snapshot.epoch==epoch;},[&]{return owner.field==1;},
        [&]{return pub::geometry_revision(data.geometry->key(),1);},
        [&]{++life_reads;return data.companion->life==pj::Life::Alive&&
            data.companion->key.generation==1&&data.companion->key.life_revision==1;}))return stages;
    stages|=8u;check(life_reads==1,"fresh Life premise is requested once after every earlier pure guard");
    if(!pub::first_cash_fits(*data.model,epoch,horizon))return stages;stages|=16u;
    const auto choice=data.model->lookup(epoch,horizon,data.companion->key);
    if(!pub::choice_usable(choice,[]{return false;}))return stages;stages|=32u;
    ap::ProgrammeGate gate;
    if(!gate.stage(epoch,horizon)||gate.admit(epoch,true,true)!=ap::ProgrammeAdmission::Started)return stages;
    return stages|64u;
}
pj::Snapshot self(const BaseKit& kit,unsigned side) {
    pj::Snapshot out;out.actor=kit.actor;out.body_revision=1000+kit.actor;out.epoch=original_epoch;
    out.own=out.complete=out.stable_outgoing=true;out.body.pos=side?pj::Point{literal_origin_b.x,literal_origin_b.y}:pj::Point{literal_origin_a.x,literal_origin_a.y};
    out.body.hp=out.body.max_hp=kit.hp;out.body.strength=out.raw_strength=kit.strength;out.body.focus=out.raw_focus=kit.focus;
    out.body.attack_range=kit.basic_range;out.body.ranged=kit.basic_range>1;out.body.splash=false;
    out.body.can_move=out.body.can_attack=out.body.can_cast=true;out.body.moved_this_turn=false;
    out.body.shield=out.body.next_basic_stun=0;out.incoming_scale=out.body.camp_damage_multiplier=kit.incoming;
    out.basic_scale=1;out.missing_health_bonus=0;out.gold=out.carried_gems=0;
    out.shop.body={kit.hp,kit.hp,kit.strength,kit.focus};out.shop.growth={kit.health_growth,kit.strength_growth,kit.focus_growth};
    out.shop.strength_per_bonus_health=kit.health_strength;out.shop_supported=false; // NoPurchase, no fabricated future shop catalogue.
    for(std::size_t i=0;i<kit.spells.size();++i) {
        const auto& source=kit.spells[i];auto& spell=out.spells[i];out.period[i]=source.period;out.cooldown[i]=0;
        if(!source.kind)continue;
        spell.kind=source.kind==1?mb::service::SpellKind::reset:source.kind==2?mb::service::SpellKind::damage:mb::service::SpellKind::aoe;
        spell.ready=true;spell.range=source.range;spell.radius=source.radius;spell.duration=source.duration;spell.exclusive=source.exclusive;
        spell.damage=source.kind==1?0:kit.focus*source.modifier;out.focus_modifier[i]=source.modifier;
    }
    return out;
}
const Reward* reward(const sg::StaticGeometry& data,int group) {
    const auto& members=data.catalogue()[static_cast<std::size_t>(group)].members;const Reward* found=nullptr;
    for(const auto& resource:rewards) {
        const auto& signature=literal_groups[resource.literal_group];if(signature.count!=members.size())continue;
        bool same=true;
        for(const auto& member:members) {
            bool present=false;
            for(std::size_t i=0;i<signature.count;++i) {
                const auto& literal=literal_members[signature.offset+i];present|=member.pos==ag::Point{literal.x,literal.y};
            }
            same&=present;
        }
        if(same){check(found==nullptr,"resource metadata joins a UNIQUE complete coordinate signature");found=&resource;}
    }
    return found;
}
pj::Job metadata(const sg::StaticGeometry& data,int group,const BaseKit& kit,const Reward& resource) {
    pj::Job out;out.group=static_cast<std::uint64_t>(group);out.generation=out.life_revision=1;
    out.geometry_revision=pub::geometry_revision(data.key(),1);out.word_revision=2000+kit.actor;
    out.life=pj::Life::Alive;out.enabler=pj::Enabler::ObserveAlive;out.observed_round=out.available_from=original_epoch;
    out.width=data.grid().width;out.height=data.grid().height;
    const auto& members=data.catalogue()[static_cast<std::size_t>(group)].members;out.member_count=members.size();
    for(std::size_t i=0;i<members.size();++i) {
        const auto& m=members[i];out.full_hp[i]=m.max_hp;
        out.members[i]={static_cast<int>(i),group,{m.pos.x,m.pos.y},m.max_hp,m.strength,m.range,true,0,false};
    }
    out.gems=resource.gems;out.gold=resource.gold;out.respawn_inputs=resource.respawn;
    out.capturers=1;out.capture_rank=0;out.gold_multiplier=1;out.bank_gold_per_gem=kit.bank_gold;
    out.capture_payout_inputs=capture_inputs;out.submit_minimum=submit_minimum;out.submit_base=submit_base;out.submit_multiplier=submit_multiplier;
    out.spawn_healing=spawn_healing;out.heal_inputs=heal_inputs;out.purchase_readback_inputs=1;
    out.purchase_when_home=false;out.bank_policy=pj::BankPolicy::ChargedHome;
    out.full_group_known=out.conditional_hp=out.life_supported=out.continuation_supported=out.external_sources_complete=true;
    out.repeat=out.common_full_generation=true;return out;
}
fb::Current current(const sg::StaticGeometry& data,const pj::Snapshot& own,const pj::Job& job) {
    fb::Current out;out.key={own.actor,job.group,job.generation,own.body_revision,job.life_revision,job.geometry_revision,job.word_revision};
    out.support=data.key();out.origin={data.key().layout,data.key().potential,own.body_revision,1,static_cast<int>(own.epoch),{own.body.pos.x,own.body.pos.y}};
    // Explicit scenario identities, NOT a claimed run of the private SDK
    // collector/body hash. Every represented fact is unchanged in later Card.
    out.inventory=101;out.wallet=102;out.effects=103;out.clock=104;out.values=105;
    out.life=job.life;out.enabler=job.enabler;out.allowances=fb::allowances(own.body);return out;
}
long long offset(std::uint32_t date){return date>=original_epoch?static_cast<long long>(date-original_epoch):-1;}
void cash_record(Result& out,const bp::Programme& model) {
    const auto& quote=model.forecast();out.value={quote.banked_gems_by_H,quote.carried_gems,quote.cycles,quote.owned_gold};
    if(const auto* cash=model.cash_plan()) {
        out.first_cash=cash->first_cash_complete;out.channel=cash->channel_ticks;
        out.dates={offset(cash->bank_arrival),offset(cash->bank_complete),offset(cash->home_arrival),offset(cash->heal_complete),
            offset(cash->departure),offset(cash->redeploy_complete),offset(quote.last_clear),offset(quote.last_bank)};
    }
}
Result run(const std::shared_ptr<const sg::StaticGeometry>& data,const BaseKit& kit,unsigned side) {
    Result out;if(!data)return out;
    OwnInputs declared;declared.snapshot=self(kit,side);
    declared.snapshot.body_revision=pub::semantic_hash(declared);
    const auto& own=declared.snapshot;const ag::Point pose{own.body.pos.x,own.body.pos.y};
    sm::Menu menu{data};sm::Request offer;offer.self=own;offer.nomination={data->key().layout,data->key().potential,own.body_revision,1,10,pose};offer.handoff_inputs.fill(1);
    std::uint32_t examined{};std::shared_ptr<const sg::TargetGeometry> target;std::uint64_t publication{};
    int group=::nearest(*data,pose);out.stop="menu";
    while(group>=0&&menu.progress().units<bound) {
        examined|=std::uint32_t{1}<<group;offer.canonical_group=group;menu.request(offer);
        while(!menu.first_ready(group,own)&&!menu.complete()&&menu.status()!=sm::Status::Unsupported&&menu.progress().units<bound) {
            menu.advance(1);if(!menu.last_work())break;
        }
        target=menu.first_ready(group,own);if(target)break;
        if(!menu.complete())break;group=::nearest(*data,pose,examined);
    }
    out.status[0]=static_cast<int>(menu.status());out.status[1]=static_cast<int>(menu.reason());out.work[0]=menu.progress().units;
    if(!target){out.stop=menu.complete()?"menu-families-exhausted":menu.status()==sm::Status::Unsupported?"menu-rejection":"menu-unit-bound";return out;}out.group=group;
    for(const auto& entry:menu.entries())if(entry.target==target)publication=entry.publication_id;
    check(publication!=0,"first real Menu target retains its actual publication identity");
    check(group==(side?16:11),"declared real spell geometry retains the measured first target group");
    const auto* resource=reward(*data,group);out.stop="input-signature-gap";if(!resource)return out;
    auto job=metadata(*data,group,kit,*resource);const auto companion=current(*data,own,job);
    sg::OriginBuilder origin_builder{data,companion.origin,sg::OriginMode::BindOnly};out.stop="origin";
    for(unsigned n=0;n<bound&&origin_builder.status()==ag::Status::NeedCompute;++n) {
        origin_builder.advance(companion.origin,1);out.work[1]+=origin_builder.last_work();
    }
    out.status[2]=static_cast<int>(origin_builder.status());out.status[3]=static_cast<int>(origin_builder.reason());
    if(!origin_builder.result()){out.stop=origin_builder.status()==ag::Status::NeedCompute?"origin-unit-bound":"origin-rejection";return out;}
    fb::Request request;request.origin=origin_builder.result();request.target=target;request.self=own;request.metadata=std::move(job);request.current=companion;
    request.start=original_epoch;request.horizon=horizon;request.handoff_inputs=offer.handoff_inputs;
    request.exact_own_state=request.exact_personal_life=request.project_omitted_clocks=true;
    fb::Builder binder{std::move(request)};out.stop="binding";
    for(unsigned n=0;n<bound&&binder.status()==fb::Status::NeedCompute;++n)binder.advance_nomination(1);
    out.work[2]=binder.work_used();out.status[4]=static_cast<int>(binder.status());out.status[5]=static_cast<int>(binder.reason());
    if(binder.status()!=fb::Status::InputReady){out.stop=binder.status()==fb::Status::NeedCompute?"binding-unit-bound":"binding-rejection";return out;}
    auto later=companion;later.origin.epoch=publication_epoch;++later.origin.query;
    out.stop="later-current";
    if(fb::same_premises(binder.binding(),later))out.card|=1u;
    binder.advance(later,0);out.status[4]=static_cast<int>(binder.status());out.status[5]=static_cast<int>(binder.reason());
    if(binder.status()!=fb::Status::InputReady)return out;out.card|=2u;
    check(binder.input()->self.epoch==original_epoch&&binder.input()->start==original_epoch&&binder.binding().origin.epoch==10,
        "later Current validation does not retag the original owned input/date/key");
    auto changed=later;++changed.wallet;check(!fb::same_premises(binder.binding(),changed),"fresh Card wallet changes remain a mandatory rejection");
    auto owned=binder.take();bp::Seed seed;seed.input=std::move(*owned);const auto& geometry=target->result();
    seed.nomination={static_cast<std::size_t>(publication),static_cast<std::uint8_t>(group),target->kit().family,
        target->perch(),geometry.capture_tile,geometry.release_tile,geometry.bank_stand,geometry.home};
    auto model=bp::Programme::make(std::move(seed));out.stop="model-allocation";if(!model)return out;out.stop="model";
    for(unsigned n=0;n<bound&&model->status()!=bp::Status::ValuationReady&&model->status()!=bp::Status::Unsupported;++n)model->advance(1);
    out.work[3]=model->work_used();out.work[4]=model->producer_work();out.work[5]=model->calendar_work();
    out.status[6]=static_cast<int>(model->status());out.status[7]=static_cast<int>(model->reason());
    out.status[8]=static_cast<int>(model->producer_status());out.status[9]=static_cast<int>(model->producer_reason());
    out.status[10]=model->has_calendar()?static_cast<int>(model->calendar_status()):-1;out.status[11]=model->has_calendar()?static_cast<int>(model->calendar_reason()):-1;
    cash_record(out,*model);if(!model->valuation_ready()){out.stop=model->status()==bp::Status::Unsupported?"model-rejection":"model-unit-bound";return out;}
    out.stop="first-cash-incomplete";if(!out.first_cash)return out;
    out.stop="cash";cc::Cache cache;
    const cr::Goals goals{geometry.bank_stand,geometry.home,target->perch(),publication*3+1,publication*3+2,publication*3+3};
    cache.ensure(data,goals);
    for(unsigned n=0;n<bound&&cache.status()==cr::Status::NeedCompute;++n)cache.advance(1);
    out.work[6]=cache.work_used();out.status[12]=static_cast<int>(cache.status());out.status[13]=static_cast<int>(cache.reason());
    if(!cache.result()){out.stop=cache.status()==cr::Status::NeedCompute?"cash-unit-bound":"cash-rejection";return out;}
    const cr::Key expected{data.get(),data->key(),goals};
    const bool cash_key=cache.key()==expected&&cache.result()->key()==expected;if(cash_key)out.card|=4u;
    PurePublicationData publication_data{1,declared,companion,model,cache.result(),data,target,publication,cache};
    const PureOwner publication_owner{companion.key.body_revision,1};
    out.publication[0]=publication_case(publication_data,publication_owner,declared,original_epoch,expected);
    auto declared_later=declared;declared_later.snapshot.epoch=publication_epoch;
    out.publication[1]=publication_case(publication_data,publication_owner,declared_later,publication_epoch,expected);
    check(out.publication[0]==127&&out.publication[1]==127,
        "the same production pure publication predicates pass original and later declared Current");
    auto changed_wallet=declared_later;++changed_wallet.snapshot.gold;
    check(publication_case(publication_data,publication_owner,changed_wallet,publication_epoch,expected)==7,
        "shared fresh predicate rejects changed wallet before Life/date/choice/Gate");
    const auto original=model->lookup(original_epoch,horizon,companion.key);if(original)out.card|=8u;
    const auto choice=model->lookup(publication_epoch,horizon,companion.key);if(choice)out.card|=16u;
    const auto* cash=model->cash_plan();const auto first_end=model->input().job.repeat?cash->redeploy_complete:cash->heal_complete;
    const bool first_fits=first_end>=original_epoch&&first_end-original_epoch<=horizon-publication_epoch;if(first_fits)out.card|=32u;
    const bool positive=choice&&choice->banked_gems_by_H>0;if(positive)out.card|=64u;
    ap::ProgrammeGate old_gate;
    check(old_gate.stage(original_epoch,horizon)&&old_gate.admit(publication_epoch,true,true)==ap::ProgrammeAdmission::Withdrawn,
        "staging the old start10 cannot transfer ownership at100; a new dated Choice is required");
    out.stop="later-lookup-rejection";if(!choice)return out;
    out.stop="first-cash-does-not-fit";if(!first_fits)return out;
    out.stop="no-banked-value";if(!positive)return out;
    ap::ProgrammeGate gate;const bool staged=gate.stage(choice->start,choice->horizon);if(staged)out.card|=128u;
    const bool admitted=gate.admit(publication_epoch,fb::same_premises(companion,later),later.life==pj::Life::Alive)==ap::ProgrammeAdmission::Started;
    if(admitted)out.card|=256u;
    // The shared pure predicate is now exercised; actual collect_self, Life
    // observation, private epoch/fuel fences and activation remain UNEXECUTED.
    out.stop=out.card==511u&&out.publication[0]==127u&&out.publication[1]==127u?
        "shared-pure-ready/SDK-unexecuted":"shared-pure-rejection";
    check(model->input().self.epoch==10&&model->input().start==10&&model->forecast().start==10,
        "even successful later lookup/Gate leave the original Input and Quote dates intact");
    return out;
}
void emit(const BaseKit& kit,unsigned side,const Result& out) {
    std::printf("PIPE {\"s\":\"%c\",\"kit\":\"%s\",\"g\":%d,\"stop\":\"%s\",\"status\":[",side?'B':'A',kit.name,out.group,out.stop);
    for(std::size_t i=0;i<out.status.size();++i)std::printf("%s%d",i?",":"",out.status[i]);
    std::printf("],\"work\":[");for(std::size_t i=0;i<out.work.size();++i)std::printf("%s%llu",i?",":"",static_cast<unsigned long long>(out.work[i]));
    std::printf("],\"q\":[");for(std::size_t i=0;i<out.value.size();++i)std::printf("%s%llu",i?",":"",static_cast<unsigned long long>(out.value[i]));
    std::printf("],\"cash\":[%d,%u],\"offsets\":[",out.first_cash?1:0,out.channel);
    for(std::size_t i=0;i<out.dates.size();++i)std::printf("%s%lld",i?",":"",out.dates[i]);
    std::printf("],\"card\":%u,\"pub\":[%u,%u]}\n",out.card,out.publication[0],out.publication[1]);
}
}
int main() {
    pipeline::check(std::strcmp(pipeline::catalogue_sha,literal_catalog_sha)==0,"declared base metadata and embedded map catalogue pins agree");
    std::printf("PIPE {\"binding\":1,\"map\":\"%s\",\"catalog\":\"%s\",\"dates\":[10,100,%u],\"declared_own\":1,\"sdk_private_publisher\":0}\n",literal_map_sha,pipeline::catalogue_sha,pipeline::horizon);
    for(unsigned side=0;side<2;++side) {
        const auto built=::compile(side);
        for(const auto& kit:pipeline::base_kits)pipeline::emit(kit,side,pipeline::run(built.data,kit,side));
    }
    const int total=::checks+pipeline::checks,failures=::failures+pipeline::failures;
    std::printf("assignment pipeline characterization (declared pure): %d checks, %d failures\n",total,failures);return failures?1:0;
}
