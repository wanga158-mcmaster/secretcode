#pragma once
#include <algorithm>
#include <span>
namespace mb::freecenter {
struct Point { int x{}, y{}; };
inline int distance(Point a, Point b) { return std::max(a.x>b.x?a.x-b.x:b.x-a.x, a.y>b.y?a.y-b.y:b.y-a.y); }
struct Enemy { Point pos; float value{}, retaliation{}; };
struct Camp { Point pos; float value{}; bool sleeping_danger{}; };
struct Score { float gain{}, lost_camp{}, added_retaliation{}, net{}; bool eligible{}, new_danger{}; };
// Conservative first arm: retain every original enemy victim and the chosen cast/slot.
// Additional damage is not treated as free: lost camp contribution and new retaliation cost.
inline Score evaluate(Point base, Point candidate, int radius, std::span<const Enemy> enemies, std::span<const Camp> camps) {
    Score s; bool extra=false;
    for (const auto& e:enemies) {
        const bool old_hit=distance(base,e.pos)<=radius, new_hit=distance(candidate,e.pos)<=radius;
        if(old_hit&&!new_hit) return s;
        if(new_hit&&!old_hit) { extra=true; s.gain+=e.value; s.added_retaliation+=e.retaliation; }
    }
    if(!extra) return s;
    for(const auto& c:camps) {
        const bool old_hit=distance(base,c.pos)<=radius, new_hit=distance(candidate,c.pos)<=radius;
        if(old_hit&&!new_hit) s.lost_camp+=c.value;
        if(new_hit&&!old_hit&&c.sleeping_danger) s.new_danger=true;
    }
    s.net=s.gain-s.lost_camp-s.added_retaliation;
    s.eligible=!s.new_danger&&s.net>0.0f;
    return s;
}
}
