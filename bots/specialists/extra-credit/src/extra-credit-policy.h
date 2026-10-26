#pragma once
namespace extra_credit {
constexpr bool owns(int canonical_index, bool cassi) {
    return canonical_index >= 0 && (canonical_index % 2 == (cassi ? 0 : 1));
}
constexpr bool may_enter(float health, float maximum, float incoming, float loss) {
    return incoming <= 0 || (maximum > 0 && health >= maximum * 0.70f && loss <= maximum * 0.20f);
}
constexpr bool retreat(float health, float maximum, bool enemy_within_eight) {
    return enemy_within_eight || (maximum > 0 && health <= maximum * 0.65f);
}
}
