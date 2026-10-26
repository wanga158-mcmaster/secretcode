#include "conversion_observation.h"
#include <cstdio>

// SDK process startup only; facts use an observation double, not a native game.
namespace cambc::detail {
extern "C" std::int32_t sdk_get_settings(game_settings* out){*out={};return 0;}
extern "C" std::int32_t sdk_shared_memory_size(std::uint32_t* out){*out=8;return 0;}
extern "C" std::int32_t sdk_bc_metadata(metadata* out){*out={5,4,1,1,1,8,128};return 0;}
extern "C" std::int32_t sdk_consumable_info(std::uint32_t,consumable_info* out){*out={};return 0;}
extern "C" std::int32_t sdk_champion_design(std::int32_t,champion_design* out){*out={};return 0;}
extern "C" std::int32_t sdk_draft_memory_size(std::uint32_t* out){*out=0;return 0;}
extern "C" std::int32_t sdk_read_draft_memory(std::int32_t,void*,std::uint32_t){return 0;}
extern "C" std::int32_t sdk_my_champion(){return -1;}
extern "C" std::int32_t sdk_bc_map_symmetry(std::int32_t* out){*out=0;return 0;}
extern "C" std::int32_t sdk_bc_map_mask(std::int32_t,std::uint64_t* out){*out=0;return 0;}
extern "C" std::int32_t sdk_Bitboard_Zero(std::uint64_t* out){*out=0;return 0;}
extern "C" std::int32_t sdk_my_team(){return 0;}
extern "C" std::int32_t sdk_spawn_region(std::int32_t,aabb* out){*out={};return 0;}
extern "C" std::int32_t sdk_Bitboard_Set(std::uint64_t*,std::int32_t,std::int32_t,std::int32_t){return 0;}
extern "C" std::int32_t sdk_bc_get_last_error(char*,std::uint32_t){return 0;}
extern "C" std::int32_t sdk_champion(std::uint32_t,champion_info* out){*out={};return 0;}
extern "C" std::int32_t sdk_champion_ability_props(std::uint32_t,std::uint32_t,ability_props* out){*out={};return 0;}
}

namespace {
using namespace mosaic;
using Status=conversion_observation::Status;
int failures{};
void check(bool ok,const char* name){if(!ok){++failures;std::printf("FAIL %s\n",name);}}
struct Fixture {
    spatial::Index index;
    selected::Packet packet;
    conversion::History parent{7,3};
    Fixture() {
        index.build({},true);packet.stamp={7,3,2};packet.nonce=99;packet.layout=index.borrow();
        packet.complete=packet.self_complete=packet.enemies_complete=packet.origin_complete=true;
        packet.origin_stamp=packet.stamp;packet.self.pos={3,3};packet.self.hp=packet.self.max_hp=100;
        packet.self.alive=packet.self.can_move=packet.self.can_attack=packet.self.can_cast=true;
        packet.self.range=packet.self.move_speed=1;packet.self.gems=21;packet.self.strength=20;
        packet.self.basic_damage=20;packet.self.basic_damage_known=packet.self.damage_modifiers_known=true;
        packet.request.command.expected_epoch=7;packet.request.command.expected_actor=3;
        packet.request.command.expected_revision=2;packet.request.serial=77;
    }
    conversion::Request request(conversion::Kind kind=conversion::Kind::tribute_start)const {
        conversion::Request result;result.kind=kind;result.stamp=packet.stamp;result.serial=123;result.altar={4,3};return result;
    }
};
struct Observer {
    selected::Stamp stamp;
    int epoch{7},actor{3},begins{},history_reads{},timer_reads{},settings_reads{},altar_reads{},fences{};
    bool native_recall_start{},native_recall_cancel{},native_tribute_start{},native_tribute_cancel{};
    bool mismatch{},visible{true},enemy_lock{},altar_valid{true},suspend_altar{},suspend_history{},low_fuel{};
    int remaining{3};
    bool begin(selected::Stamp input){++begins;stamp=input;return same_input();}
    bool same_input()const{return stamp.epoch==epoch&&stamp.actor==actor;}
    bool fence(std::uint64_t){++fences;return !low_fuel&&same_input();}
    Status read_history(const selected::Packet&,conversion::History& history) {
        ++history_reads;if(mismatch)return Status::stale;
        history={stamp.epoch,stamp.actor,native_recall_start,native_recall_cancel,native_tribute_start,native_tribute_cancel};
        if(suspend_history)++epoch;return Status::ready;
    }
    Status read_channel(int& out){++timer_reads;out=remaining;return Status::ready;}
    Status read_settings(const conversion::Request&,conversion::Facts& facts) {
        ++settings_reads;facts.recall_duration=5;facts.submit_range=1;facts.submit_min=1;
        facts.submit_base=0;facts.submit_multiplier=0.1;return Status::ready;
    }
    Status read_altar(const conversion::Request& request,conversion::Facts& facts) {
        ++altar_reads;facts.altar=request.altar;facts.altar_complete=true;facts.altar_valid=altar_valid;
        facts.altar_visible=visible;facts.lock_complete=visible;facts.enemy_lock=enemy_lock;
        if(suspend_altar)++epoch;return Status::ready;
    }
};
Status capture(Observer& source,Fixture& fixture,const conversion::Request& request,conversion::Facts& facts) {
    return conversion_observation::collect(source,fixture.packet,request,fixture.parent,facts);
}
}

int main() {
    using namespace mosaic;
    Fixture tribute;Observer source;conversion::Facts facts;const auto tribute_request=tribute.request();
    check(capture(source,tribute,tribute_request,facts)==Status::ready&&facts.complete&&facts.packet_nonce==99&&
          facts.origin==tribute.packet.self.pos&&facts.gems==21&&source.history_reads==1&&source.timer_reads==0&&
          source.settings_reads==1&&source.altar_reads==1,
          "service facts reuse one sealed Wait packet without combat recapture and do not query an inactive channel timer");
    const auto result=conversion::evaluate(tribute.packet,facts,tribute_request);
    check(result.status==selected::Status::ready&&result.duration==3&&result.starting_bag==21&&
          result.expected_completion_epoch==10&&result.completion_is_conditional,
          "actual bag and runtime settings feed the real pure tribute evaluator with conditional completion");
    const auto counts=std::array{source.history_reads,source.timer_reads,source.settings_reads,source.altar_reads};
    for(int i=0;i<100;++i)(void)conversion::evaluate(tribute.packet,facts,tribute_request);
    check(counts==std::array{source.history_reads,source.timer_reads,source.settings_reads,source.altar_reads},
          "one hundred service evaluations add no observation imports");
    Fixture hidden;Observer hidden_source;hidden_source.visible=false;conversion::Facts hidden_facts;
    const auto hidden_request=hidden.request();
    check(capture(hidden_source,hidden,hidden_request,hidden_facts)==Status::ready&&!hidden_facts.lock_complete&&
          conversion::evaluate(hidden.packet,hidden_facts,hidden_request).status==selected::Status::incomplete,
          "hidden altar lock absence is unknown rather than interpreted as an unlocked altar");
    Fixture locked;Observer locked_source;locked_source.enemy_lock=true;conversion::Facts locked_facts;
    const auto locked_request=locked.request();
    check(capture(locked_source,locked,locked_request,locked_facts)==Status::ready&&
          conversion::evaluate(locked.packet,locked_facts,locked_request).status==selected::Status::rejected,
          "an actual enemy lock rejects tribute instead of converting the request to an unpriced lock attack");
    Fixture recalled;Observer recall_source;recall_source.native_recall_start=true;conversion::Facts recall_facts;
    const auto recall_request=recalled.request(conversion::Kind::recall_start);
    check(capture(recall_source,recalled,recall_request,recall_facts)==Status::ready&&recall_facts.history.recall_started&&
          conversion::evaluate(recalled.packet,recall_facts,recall_request).status==selected::Status::rejected&&recall_source.altar_reads==0,
          "native already-started history blocks repeating recall and recall performs no altar queries");
    Fixture parent_started;parent_started.parent.tribute_started=true;Observer clean_native;conversion::Facts merged;
    check(capture(clean_native,parent_started,parent_started.request(),merged)==Status::ready&&merged.history.tribute_started,
          "explicit parent action history is merged conservatively with fresh native state flags");
    Fixture cancel;cancel.packet.self.recalling=true;cancel.packet.self.can_move=cancel.packet.self.can_attack=cancel.packet.self.can_cast=false;
    Observer cancelling;conversion::Facts cancel_facts;const auto cancel_request=cancel.request(conversion::Kind::recall_cancel);
    check(capture(cancelling,cancel,cancel_request,cancel_facts)==Status::ready&&cancelling.timer_reads==1&&
          cancelling.settings_reads==0&&cancelling.altar_reads==0&&cancel_facts.remaining==3&&
          conversion::evaluate(cancel.packet,cancel_facts,cancel_request).status==selected::Status::ready,
          "cancellation uses the actual active-channel timer without reconstructing duration or altar state");
    Fixture stale_parent;stale_parent.parent.epoch=6;Observer untouched;conversion::Facts no_history;
    check(capture(untouched,stale_parent,stale_parent.request(),no_history)==Status::incomplete&&!no_history.complete&&untouched.begins==0,
          "missing same-input parent history never invents unused start or cancellation flags");
    Fixture crossed;Observer crossed_source;crossed_source.suspend_altar=true;conversion::Facts crossed_facts;
    check(capture(crossed_source,crossed,crossed.request(),crossed_facts)==Status::stale&&!crossed_facts.complete&&
          !crossed.packet.complete&&!crossed.packet.origin_complete&&!crossed.packet.self_complete,
          "a channel-facts clock crossing revokes both unpublished facts and the reused Wait authority");
    Fixture changed;Observer mismatch;mismatch.mismatch=true;conversion::Facts changed_facts;
    check(capture(mismatch,changed,changed.request(),changed_facts)==Status::stale&&!changed_facts.complete&&
          !changed.packet.complete&&!changed.packet.origin_complete,
          "a fresh core-state mismatch rejects the reused packet before a service certificate can be issued");
    Fixture resource_only;Observer budget;budget.low_fuel=true;conversion::Facts resource_facts;
    check(capture(budget,resource_only,resource_only.request(),resource_facts)==Status::incomplete&&!resource_facts.complete&&
          resource_only.packet.complete&&resource_only.packet.origin_complete,
          "same-input resource refusal publishes no facts while retaining independently current Wait evidence");
    Fixture old_nonce;Observer old_source;conversion::Facts old_facts;const auto old_request=old_nonce.request();
    capture(old_source,old_nonce,old_request,old_facts);++old_nonce.packet.nonce;
    check(conversion::evaluate(old_nonce.packet,old_facts,old_request).status==selected::Status::stale,
          "facts from a different packet nonce cannot authorize conversion at the same epoch and revision");
    Fixture shopping;Observer shop_source;conversion::Facts shop_facts;
    check(capture(shop_source,shopping,shopping.request(conversion::Kind::buy),shop_facts)==Status::unsupported&&
          shop_source.begins==0&&!shop_facts.complete,
          "purchase selection remains explicitly unsupported and performs no partial shop queries");
    check(conversion_observation::recall_duration(5,0)==5&&conversion_observation::recall_duration(5,0.5)==3&&
          conversion_observation::recall_duration(7,0.5)==4&&!conversion_observation::recall_duration(5,1)&&
          !conversion_observation::recall_duration(5,std::numeric_limits<double>::quiet_NaN()),
          "runtime recall scaling rounds up and rejects unsupported duration inputs");
    native_observation::ChampionRecord actual{{7,3,2},{}};
    actual.value.x=3;actual.value.y=3;actual.value.health=actual.value.max_health=100;actual.value.strength=20;
    actual.value.move_speed=actual.value.attack_range=1;actual.value.state=1|8|16|2048;
    Fixture core;
    check(conversion_observation::core_matches(actual,core.packet.self),"core comparison preserves actual allowances and geometry");
    actual.value.state|=2;
    check(!conversion_observation::core_matches(actual,core.packet.self),"new channel state invalidates a supposedly unchanged Wait record");
    std::printf("conversion observation checks: %s\n",failures?"failed":"passed");return failures?1:0;
}
