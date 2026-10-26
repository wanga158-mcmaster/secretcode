#include "equipment.h"
#include <cassert>
#include <cmath>
int main()
{
    tt::EquipmentModel items;
    items.observe(4,.3f); items.observe(4,.3f); // duplicate family does not stack
    items.observe(2,.1f); items.observe(1,.1f); items.observe(3,.2f);
    tt::BodySpec body;
    items.apply(body);
    assert(std::abs(body.taken-.63f)<1e-6f);
    assert(std::abs(body.gold_mult-1.2f)<1e-6f);
    assert(std::abs(items.camp_scale()-1.1f)<1e-6f);
    tt::BodySpec vesper;
    vesper.gold_mult=1.33f;
    items.apply(vesper);
    assert(std::abs(vesper.gold_mult-1.53f)<1e-6f);
    tt::GroupSpec elk;
    elk.add(2400,60,4); elk.sort_members();
    const auto clear=tt::smith_clear(elk,100);
    tt::BodySpec unequipped;
    unequipped.hp=1100;
    const float before=tt::solo_damage(elk,unequipped,clear);
    tt::EquipmentModel jungle;
    jungle.observe(4,.3f);
    auto equipped=unequipped;
    jungle.apply(equipped);
    const float after=tt::solo_damage(elk,equipped,clear);
    assert(before>unequipped.hp && after<equipped.hp);
}
