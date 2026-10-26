#pragma once

namespace p11::contest {
// This is a physical continuation of an already selected cleared group. It
// does not choose a group or an enemy, authorize hidden facts, or grant actions.
struct Continuation {
    int group=-1;
    bool pending(int selected) const {return group>=0&&group==selected;}
    bool permitted(int selected,bool on_source,bool visible_living_contester,
                   bool reward_available,bool fresh) const {
        return pending(selected)&&!on_source&&visible_living_contester&&reward_available&&fresh;
    }
    void clear(){group=-1;}
    void departed(int selected,bool moved,bool on_source_after,bool fresh) {
        group=moved&&!on_source_after&&fresh?selected:-1;
    }
};
}
