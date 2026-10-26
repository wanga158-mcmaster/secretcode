#include "capital_contact.h"
#include <cstdio>
using namespace wb::capital::contact;
// Uncached enumeration is the refinement oracle for the continuation cache.
// It intentionally retains duplicate words and executes every successor.
static detail::Reply reference(const Foe& foe,detail::ReplyState state,unsigned depth) {
    detail::Reply best;if(depth>=foe.opportunities||foe.hp<=0)return best;
    std::array<detail::Word,max_words> words{};
    const unsigned count=detail::words(foe,state,depth,words);
    for(unsigned i=0;i<count;++i) {
        const auto& word=words[i];auto after=state;after.distance=word.distance;
        after.enhanced=word.enhanced_after;after.stun=std::max(0,after.stun-1);
        for(unsigned slot=0;slot<2;++slot)after.cooldown[slot]=std::max(0,
            (word.slot==static_cast<int>(slot)?foe.abilities[slot].max_cooldown:after.cooldown[slot])-1);
        if(foe.cooldowns_may_refresh)after.cooldown={0,0};
        const auto suffix=reference(foe,after,depth+1);
        double damage=suffix.damage;for(unsigned h=0;h<word.count;++h)damage+=word.hits[h];
        if(damage>best.damage||(damage==best.damage&&word.count+suffix.hits>best.hits))
            {best.damage=damage;best.hits=word.count+suffix.hits;}
        best.can_stun|=word.stuns||suffix.can_stun;
    }
    return best;
}
int main() {
    unsigned checks=0,failures=0;
    const std::array kinds{AbilityKind::Reset,AbilityKind::Damage,AbilityKind::Stun,AbilityKind::Dash};
    for(unsigned mask=0;mask<128;++mask)for(int distance=0;distance<10;++distance)
    for(int cooldown=0;cooldown<3;++cooldown)for(const auto kind:kinds) {
        Foe f;f.id=1;f.hp=649.f;f.max_hp=1000.f;f.basic_damage=129.8f;
        f.ranged=mask&1;f.attack_range=f.ranged?3:1;f.move_speed=mask&2?2:1;
        f.enhanced=mask&4;f.heart=mask&8;f.hp_may_fall=mask&16;
        f.cooldowns_may_refresh=mask&32;f.first_fresh=mask&64;
        f.can_move=!f.first_fresh;f.can_cast=!f.first_fresh;f.can_attack=!f.first_fresh;
        f.abilities[0]={kind,cooldown,8,2.f,1.f,156.2f,1,false,true};
        f.abilities[1]={AbilityKind::Reset,0,8,0,0,0,0,false,false};
        for(unsigned opportunities=1;opportunities<=2;++opportunities)for(int stun=0;stun<3;++stun) {
            f.opportunities=opportunities;
            detail::ReplyState state{distance,stun,{cooldown,0},f.enhanced};
            const auto old=reference(f,state,0),now=detail::replies(f,state,0);++checks;
            if(old.damage!=now.damage||old.hits!=now.hits||old.can_stun!=now.can_stun)++failures;
        }
    }
    std::printf("capital_contact_continuation: %u differential cases, %u failures\n",checks,failures);
    return failures?1:0;
}
