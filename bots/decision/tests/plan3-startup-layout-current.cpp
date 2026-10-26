#define P3_NOMINAL_HELPERS_ONLY
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "plan3-ledger-nominal-current.cpp"
#pragma clang diagnostic pop

int main() {
    setup();
    Campaign c(Controller::tempo,false,"startup-layout-fixture");
    unsigned callbacks=0;
    bool saw_boundary=false;
    for(;callbacks<32 && !c.initialized_;++callbacks) {
        fuel=100000;
        const bool prior=c.personal_.initialization_complete;
        c.initialize_campaign();
        if(!prior && c.personal_.initialization_complete) {
            saw_boundary=true;
#if PLAN3_STARTUP_LAYOUT_PHASE
            NC(c.initialization_stage_==1 && c.field_order_.empty());
            NC(c.personal_.spawn_field<0 && c.personal_.altar_fields.empty());
#else
            NC(c.initialization_stage_==2 && !c.field_order_.empty());
#endif
            NC(c.personal_.groups.size()==3 && c.personal_.observations.size()==3);
            for(unsigned i=1;i<c.personal_.groups.size();++i)
                NC(c.personal_.groups[i-1].key<c.personal_.groups[i].key);
        }
        ++current_round;
    }
    NC(saw_boundary && c.initialized_);
    // Preparation still registers every immutable group exactly once and
    // exposes no unfinished field as a known zero-distance route.
    NC(c.field_order_.size()==3 && c.personal_.altar_fields.size()==1);
    std::array<bool,3> seen{};
    for(const auto i:c.field_order_){NC(i<3 && !seen[i]);seen[i]=true;}
    for(const auto& group:c.personal_.groups)
        if(group.field>=0)NC(c.paths_.get(group.field,cambc::self().position())==StaticPaths::unknown);
    const auto groups=c.personal_.groups.size();
    NC(c.personal_.initialize_step() && c.personal_.initialize_step());
    NC(c.personal_.groups.size()==groups);
    std::printf("STARTUP_LAYOUT {\"status\":\"pass\",\"side\":%u,\"phase\":%u,\"callbacks\":%u,\"checks\":%u,\"nativeGames\":0}\n",bp_side(),unsigned(PLAN3_STARTUP_LAYOUT_PHASE),callbacks,nchecks);
}
