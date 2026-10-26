// Runtime fragment. The exit field is warmed independently of service intent.
// Its roots are actually queried home tiles with complete static camp potential
// zero. Reaching ANY complete-potential-zero tile on that route may finish camp
// egress; a home root is not a claim of future occupancy or instant healing.
void native_prepare_egress_field(native_service::Budget& budget){
    using namespace native_service;auto& s=native_service_state();
    if(!s.board_ready||s.board_edit_count)return;
    if(!s.egress_roots_prepared){
        for(const auto at:executable_home_cache()){
            const Point point{at.x,at.y};if(native_potential(point)!=0)continue;
            if(s.egress_root_count<s.egress_roots.size())s.egress_roots[s.egress_root_count++]=point;
        }
        s.egress_roots_prepared=true;
    }
    if(!s.egress_root_count){++s.unsupported;return;}
    if(!budget.checkpoint()||budget.pure_used()+3000>budget.pure_bound)return;
    if(!s.fields.begin(ServiceField::egress,walls->width(),walls->height(),{s.passable.data(),s.board_cursor},
       {s.egress_roots.data(),s.egress_root_count},native_egress_key()))return;
    const auto units=budget.pure_used()+5000<=budget.pure_bound?32u:1u;
    const auto progress=s.fields.step_guarded(ServiceField::egress,units,[&]{
        return budget.checkpoint()&&budget.pure_used()+3000<=budget.pure_bound;
    });s.prepare_units+=progress.units;budget.checkpoint();
    if(s.active_field==ServiceField::egress){s.nav_intent=native_egress_key();s.field_revision=s.fields.generation(ServiceField::egress);}
}
