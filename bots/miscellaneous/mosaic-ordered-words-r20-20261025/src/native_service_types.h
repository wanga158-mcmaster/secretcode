#pragma once
#include <array>
#include <cstdint>

namespace mosaic::native_service {
using NativeId = std::uint32_t;
struct Point {
    int x{}, y{};
    friend constexpr bool operator==(Point,Point)=default;
};
// revision changes when position/damage can change personal visibility or camp
// state. attempt changes for EVERY attempted primitive, including FALSE.
struct Epoch {
    int round{-1}; std::uint64_t revision{}, attempt{};
    friend constexpr bool operator==(Epoch,Epoch)=default;
};
constexpr bool same_world(Epoch a,Epoch b) {
    return a.round==b.round&&a.revision==b.revision;
}
enum class EffectScope : std::uint8_t { self_only, knowledge_only, position_or_damage };
enum class Life : std::uint8_t { unknown, dead, awake, asleep };
enum class GroupLife : std::uint8_t { unknown, alive, reward, respawning };
enum class AbilityKind : std::uint8_t { absent=0, reset=1, damage=2, aoe_damage=3, cc=4, heal=5, dash=6, shield=7 };
inline constexpr std::uint32_t target_camps=1, target_enemies=2, target_allies=4, target_self=8, target_ground=16;

struct SelfFact {
    Epoch epoch{}; NativeId id{}; Point pose{};
    float hp{},max_hp{},strength{},focus{},shield{},camp_damage_multiplier{1}; unsigned gold{};
    int team{},attack_range{},move_speed{},gems{},channel_time{},shield_time{},stun_time{},enhanced_time{};
    bool raw_m{},raw_a{},raw_c{},alive{},recalling{},submitting{},teleporting{},completed{};
    bool recall_started{},submit_started{};
    bool move_restricted{},attack_restricted{},cast_restricted{};
    bool stunned{},enhanced{},stun_immune{},basic_splash{},ranged{},ordinary_walked{};
    bool body_actual{},restrictions_actual{},buffs_actual{},custody_actual{},gems_actual{},gold_actual{},channel_actual{},channel_time_actual{},walked_actual{};
    constexpr bool actual(Epoch current)const {
        return epoch==current&&body_actual&&restrictions_actual&&buffs_actual&&channel_actual;
    }
    constexpr bool can_move(Epoch current)const {
        return actual(current)&&alive&&raw_m&&!stunned&&!move_restricted&&!recalling&&!submitting&&!teleporting&&!completed;
    }
    constexpr bool can_attack(Epoch current)const {
        return actual(current)&&alive&&raw_a&&!stunned&&!attack_restricted&&!recalling&&!submitting&&!teleporting&&!completed;
    }
    constexpr bool can_cast(Epoch current)const {
        return actual(current)&&alive&&raw_c&&!stunned&&!cast_restricted&&!recalling&&!submitting&&!teleporting&&!completed;
    }
};
struct CampFact {
    Epoch epoch{}; NativeId id{},group_id{}; int member_ordinal{-1},group_ordinal{-1}; Point pose{};
    Life life{Life::unknown}; float hp{},max_hp{},strength{},shield{};
    int range{},shield_time{},stun_time{}; bool stunned{},life_actual{},body_actual{},buffs_actual{};
    constexpr bool actual(Epoch current)const {return same_world(epoch,current)&&life_actual;}
    constexpr bool living(Epoch current)const {return actual(current)&&(life==Life::awake||life==Life::asleep);}
    // Unknown is a possible active source, never an asleep/dead receipt.
    constexpr bool wake_potential(Epoch current)const {return !actual(current)||life!=Life::dead;}
};
struct GroupFact {
    Epoch epoch{}; NativeId id{}; int ordinal{-1}; GroupLife life{GroupLife::unknown};
    int respawn_time{}; unsigned gem_value{},gold_value{}; bool actual{},respawn_actual{},visible{},visibility_actual{};
    constexpr bool current(Epoch now)const {return actual&&same_world(epoch,now);}
    constexpr bool capturable(Epoch now)const {return current(now)&&life==GroupLife::reward;}
};
struct AbilityDescriptor {
    AbilityKind kind{AbilityKind::absent}; std::uint32_t target_mask{};
    float range{},modifier{},radius{}; int duration{},max_cooldown{};
    bool exclusive_with_movement{},enhanced_attack{},present{},actual{};
    constexpr bool camps()const {return (target_mask&target_camps)!=0;}
    constexpr bool enemies()const {return (target_mask&target_enemies)!=0;}
    constexpr bool ground()const {return (target_mask&target_ground)!=0;}
    // Duration can stun for AoE and dash as well as the dedicated CC payload.
    constexpr bool applies_stun()const {
        return duration>0&&(kind==AbilityKind::aoe_damage||kind==AbilityKind::cc||kind==AbilityKind::dash);
    }
};
struct AbilityFact {
    Epoch epoch{}; AbilityDescriptor descriptor{}; int cooldown{}; bool cooldown_actual{};
    constexpr bool ready(const SelfFact& self,Epoch now)const {
        return descriptor.actual&&descriptor.present&&cooldown_actual&&epoch==now&&cooldown==0&&self.can_cast(now);
    }
};
struct CaptureFact {
    Epoch epoch{}; NativeId group_id{}; GroupLife life{GroupLife::unknown};
    bool life_actual{},on_spawn{},on_spawn_actual{};
    // No timer or uncontested assertion is fabricated from standing on a tile.
    constexpr bool holding_reward(Epoch now)const {
        return epoch==now&&life_actual&&life==GroupLife::reward&&on_spawn_actual&&on_spawn;
    }
};
struct BankFact {
    Epoch epoch{}; unsigned submitted{}; int gems{};
    bool custody_actual{},submitted_actual{},submitting{},channel_actual{};
    constexpr bool current(Epoch now)const {return epoch==now&&custody_actual&&submitted_actual&&channel_actual;}
};
// Fixed used data only: no world copy, funding table or cold native catalog.
struct Facts {
    Epoch epoch{}; SelfFact self{}; CampFact camp{}; GroupFact group{};
    CaptureFact capture{}; BankFact bank{}; std::array<AbilityFact,2> abilities{};
    constexpr void reject() {
        epoch.round=-1;
        self.body_actual=self.restrictions_actual=self.buffs_actual=self.custody_actual=self.gems_actual=self.gold_actual=false;
        self.channel_actual=self.channel_time_actual=self.walked_actual=false;
        camp.life_actual=camp.body_actual=camp.buffs_actual=false;camp.life=Life::unknown;
        group.actual=group.respawn_actual=group.visibility_actual=false;group.life=GroupLife::unknown;
        capture.life_actual=capture.on_spawn_actual=false;
        bank.custody_actual=bank.submitted_actual=bank.channel_actual=false;
        for(auto& ability:abilities)ability.cooldown_actual=false;
    }
    constexpr void begin(int round) {
        if(epoch.round==round)return;
        epoch.round=round;++epoch.revision;epoch.attempt=0;
        self={};camp={};group={};capture={};bank={};
        for(auto& ability:abilities){ability.cooldown_actual=false;ability.epoch=epoch;}
    }
    constexpr void attempted(EffectScope scope=EffectScope::position_or_damage) {
        ++epoch.attempt;
        if(scope==EffectScope::position_or_damage)++epoch.revision;
        self.body_actual=self.restrictions_actual=self.buffs_actual=self.custody_actual=self.gems_actual=self.gold_actual=false;
        self.channel_actual=self.channel_time_actual=self.walked_actual=false;
        capture.life_actual=capture.on_spawn_actual=false;
        bank.custody_actual=bank.submitted_actual=bank.channel_actual=false;
        for(auto& ability:abilities)ability.cooldown_actual=false;
        if(scope==EffectScope::position_or_damage){
            camp.life_actual=camp.body_actual=camp.buffs_actual=false;camp.life=Life::unknown;
            group.actual=group.respawn_actual=false;group.life=GroupLife::unknown;
        }
    }
};
}
