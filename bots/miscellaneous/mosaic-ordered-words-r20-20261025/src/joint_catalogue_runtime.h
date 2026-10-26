// Included inside Runtime after joint_observation.h. The pure prefix below is
// also exercised directly by the host-free fixture; it is the real SDK path.
struct JointCatalogueGeometryKey {
    std::uintptr_t map_identity{};std::uint64_t map_generation{};std::size_t records{};
    int width{},height{},x{},y{},raw_speed{},job{-1},capture{-1};
    bool operator==(const JointCatalogueGeometryKey&)const=default;
};
struct JointCatalogueDependencies {
    static std::uint64_t prepare_cost(std::uint64_t enumeration,bool cached){
        const auto invalid=std::numeric_limits<std::uint64_t>::max();
        if(!enumeration)return invalid;
        if(cached)return enumeration;
        return enumeration>invalid-enumeration?invalid:enumeration+enumeration;
    }
};
struct JointCatalogueTemplate {
    joint_world::Camp camp;double base_strength{},base_weight{};
    static JointCatalogueTemplate make(const joint_catalogue::Footprint& footprint,float hp,float strength){
        JointCatalogueTemplate row{};row.camp.id=footprint.physical_id;row.camp.group=unsigned(footprint.group);
        row.camp.position={footprint.x,footprint.y};row.camp.range=footprint.range;
        row.camp.health=hp;row.camp.damage=std::max(0.0f,strength);
        row.camp.knowledge=joint_world::CampKnowledge::unknown;row.camp.alive=true;row.camp.health_known=false;
        row.base_strength=double(strength);row.base_weight=strength>0?std::ceil(double(strength))+1:0;return row;
    }
};
template<std::size_t Capacity=512> struct JointCatalogueGeometryPlan {
    JointCatalogueGeometryKey key;const joint_catalogue::Selection<Capacity>* selection{};
    std::size_t represented_count{},overflow_count{},contributor_count{},job_count{},other_count{};
    bool omitted_any{},valid{};
    std::array<std::size_t,Capacity> contributors,contributor_offsets;
    std::array<unsigned,16> job_rows,other_rows;
    JointCatalogueGeometryPlan()=default;
    JointCatalogueGeometryPlan(const JointCatalogueGeometryPlan&)=delete;
    JointCatalogueGeometryPlan& operator=(const JointCatalogueGeometryPlan&)=delete;
    void invalidate(){valid=false;selection=nullptr;}
    bool matches(const JointCatalogueGeometryKey& candidate)const{return valid&&selection&&key==candidate;}
    // Query output and templates are borrowed, ordinary live objects. The SDK
    // owner invalidates this plan BEFORE rewriting its persistent Selection.
    bool capture(const JointCatalogueGeometryKey& candidate,const joint_catalogue::Selection<Capacity>& selected,
                 std::span<const JointCatalogueTemplate> templates){
        invalidate();contributor_count=job_count=other_count=0;
        if(candidate.records!=templates.size()||selected.represented_count>16||selected.overflow_count>Capacity)return false;
        for(std::size_t n=0;n<selected.represented_count;++n){const auto index=selected.represented[n];
            if(index>=templates.size())return false;
            if(int(templates[index].camp.group)==candidate.job)job_rows[job_count++]=unsigned(n);
            else other_rows[other_count++]=unsigned(n);
        }
        for(std::size_t n=0;n<selected.overflow_count;++n){const auto index=selected.overflow[n];
            if(index>=templates.size())return false;
            if(selected.overflow_incoming[n]&&templates[index].base_strength>0){
                contributors[contributor_count]=index;contributor_offsets[contributor_count++]=n;}
        }
        key=candidate;selection=&selected;represented_count=selected.represented_count;
        overflow_count=selected.overflow_count;omitted_any=selected.omitted_any;valid=true;return true;
    }
};
struct JointCatalogueFrameResult {
    bool complete{},aggregate_present{};unsigned rows{};std::size_t overflow_seen{};
    double aggregate_damage{};std::size_t target_member{std::numeric_limits<std::size_t>::max()};
    unsigned target_source{joint_world::no_word};std::size_t mutable_reads{},eligibility_reads{};
};
struct JointCatalogueFrame {
    template<std::size_t Capacity,class Mutable,class Eligible,class Guard>
    static JointCatalogueFrameResult fill(const JointCatalogueGeometryPlan<Capacity>& plan,
        std::span<const JointCatalogueTemplate> templates,std::span<joint_world::Camp> destination,
        std::span<std::size_t> camp_members,joint_world::Point origin,int attack_range,int job,int epoch,
        Mutable&& mutable_source,Eligible&& eligible,Guard&& guard){
        JointCatalogueFrameResult result;
        if(!plan.valid||!plan.selection||plan.represented_count>destination.size()||
           plan.represented_count>camp_members.size()||plan.key.records!=templates.size()||plan.key.job!=job||
           origin.x!=plan.key.x||origin.y!=plan.key.y||!guard())return result;
        // Bounds/immutable IDs were validated once on geometry commit. Copying
        // an entire prebuilt row overwrites EVERY active field, including prior
        // observed death, shield and stun. Inactive rows remain untouched.
        for(std::size_t n=0;n<plan.represented_count;++n){const auto index=plan.selection->represented[n];
            destination[n]=templates[index].camp;camp_members[n]=index;++result.rows;}
        // Exact final old nomination: first eligible job row, otherwise first
        // eligible non-job row. No later row can change either winner, so its
        // visibility/work reads are not dependencies of this frame.
        auto find=[&](const auto& candidates,std::size_t count){
            for(std::size_t n=0;n<count;++n){const unsigned row=candidates[n];const auto index=plan.selection->represented[row];
                if(joint_world::distance(origin,templates[index].camp.position)>attack_range)continue;
                ++result.eligibility_reads;if(!eligible(index))continue;
                result.target_member=index;result.target_source=row;return true;
            }
            return false;
        };
        if(!find(plan.job_rows,plan.job_count))find(plan.other_rows,plan.other_count);
        for(std::size_t n=0;n<plan.contributor_count;++n){
            if((n&15u)==0&&!guard())return result;
            const auto index=plan.contributors[n];const auto& base=templates[index];const auto& current=mutable_source(index);
            ++result.mutable_reads;result.aggregate_present=true;
            // Each old term, and each addition, remains in the same order.
            // An epoch change changes this read; it never reuses a live weight.
            result.aggregate_damage+=current.seen==epoch?
                std::ceil(std::max(base.base_strength,double(current.strength)))+1:base.base_weight;
            result.overflow_seen=plan.contributor_offsets[n]+1;
        }
        result.overflow_seen=plan.overflow_count;result.complete=true;return result;
    }
};
#ifndef MOSAIC_JOINT_CATALOGUE_RUNTIME_PURE_ONLY
// The pure catalogue owns
// immutable geometry only; this adapter reads every mutable weight/nomination
// from Runtime at materialization time. No native query or knowledge receipt.
struct JointCatalogueRuntimeStats {
    enum class Stage:unsigned {empty,allocation,copy,building,ready,unsupported};
    Stage stage{Stage::empty};
    std::size_t total{},copied{},build_cursor{},build_steps{},build_units{},built{},clear_words{},incidences{},baseline_copied{};
    unsigned prepare_calls{},prepare_deferrals{},source_queries{},query_failures{},source_frames{},frame_failures{};
    std::uint64_t prepare_spent{},max_prepare{},max_build_step{},query_spent{},last_query{},max_query{},
        materialize_spent{},last_materialize{},max_materialize{},last_frame{},max_frame{},max_allocation{};
    std::size_t last_local{},last_overflow{},last_metadata_records{},last_contributors{};
    unsigned cache_hits{},cache_misses{},cache_prepares{},cache_failures{};bool last_cache_hit{};
    std::uint64_t baseline_spent{},max_baseline_step{},last_source_prepare{},max_source_prepare{},source_prepare_spent{};
    std::size_t materialize_mutable_reads{},materialize_eligibility_reads{};
    joint_catalogue::BuildState build_state{joint_catalogue::BuildState::idle};
    joint_catalogue::Failure failure{joint_catalogue::Failure::none};
    joint_catalogue::QueryStatus last_query_status{joint_catalogue::QueryStatus::not_ready};
    int last_epoch{-1},last_halo{};vec2 last_origin{};
};
static JointCatalogueRuntimeStats& joint_catalogue_stats(){
    static constinit JointCatalogueRuntimeStats value{};return value;
}
struct JointCatalogueRuntimeState {
    // Ordinary C++ objects, not raw storage. The vector reserves uninitialized
    // capacity and appends one live Footprint at a time. Index/Selection are
    // noncopyable; static constant initialization avoids a cold input fill.
    std::vector<joint_catalogue::Footprint> footprints;
    std::vector<JointCatalogueTemplate> templates;
    joint_catalogue::Index<> index;
    joint_catalogue::Selection<512> selection;
    JointCatalogueGeometryPlan<> geometry;
    std::uint64_t generation{},source_prepare_frame{};int source_prepare_epoch{-1};
    bool allocated{},begun{};
};
static JointCatalogueRuntimeState& joint_catalogue_state(){
    static constinit JointCatalogueRuntimeState value{};return value;
}
bool joint_catalogue_usable()const{
    const auto& catalogue=joint_catalogue_state();const auto& stats=joint_catalogue_stats();
    return stats.stage==JointCatalogueRuntimeStats::Stage::ready&&catalogue.index.ready()&&
        catalogue.index.size()==members.size();
}
// Called after the existing map/member/support setup and before Runtime.ready.
// Unsupported metadata finishes setup but does NOT publish a usable index;
// the caller's guarded branch then uses the literal old source builder.
bool joint_catalogue_prepare(){
    auto& catalogue=joint_catalogue_state();auto& stats=joint_catalogue_stats();
    const auto start=fuel_remaining();++stats.prepare_calls;
    const auto finish=[&](bool complete){const auto now=fuel_remaining();
        const auto spent=start>=now?start-now:0;stats.prepare_spent+=spent;stats.max_prepare=std::max(stats.max_prepare,spent);
        if(!complete)++stats.prepare_deferrals;return complete;};
    const auto unsupported=[&](joint_catalogue::Failure failure){
        stats.failure=failure;stats.stage=JointCatalogueRuntimeStats::Stage::unsupported;return finish(true);};
    if(stats.stage==JointCatalogueRuntimeStats::Stage::unsupported)return finish(true);
    if(stats.stage==JointCatalogueRuntimeStats::Stage::ready){
        if(catalogue.index.size()!=members.size())return unsupported(joint_catalogue::Failure::invalid_record);
        return finish(true);
    }
    if(!loaded||!walls||setup_group<groups.size()||setup_member||
       (!support_coarse&&(!support_detail.ready()||setup_support<members.size())))return finish(false);
    if(!catalogue.allocated){
        stats.total=members.size();const int width=walls->width(),height=walls->height();
        if(width<=0||height<=0)return unsupported(joint_catalogue::Failure::invalid_dimensions);
        if(std::uint64_t(width)*std::uint64_t(height)>joint_catalogue::Index<>::cell_capacity)
            return unsupported(joint_catalogue::Failure::cell_capacity);
        if(members.size()>joint_catalogue::Index<>::record_capacity)
            return unsupported(joint_catalogue::Failure::record_capacity);
        // Reuse the existing large setup-entry floor; this is not a reduced
        // gameplay floor or an inference about native allocator cost.
        if(!fuel(85000))return finish(false);
        stats.stage=JointCatalogueRuntimeStats::Stage::allocation;const auto before=fuel_remaining();
        catalogue.footprints.reserve(members.size());catalogue.templates.reserve(members.size());const auto after=fuel_remaining();
        stats.max_allocation=std::max(stats.max_allocation,before>=after?before-after:0);
        catalogue.allocated=true;stats.stage=JointCatalogueRuntimeStats::Stage::copy;
    }
    if(stats.total!=members.size())return unsupported(joint_catalogue::Failure::invalid_record);
    while(catalogue.footprints.size()<stats.total&&fuel(65000)){
        const auto before=fuel_remaining();const auto& member=members[catalogue.footprints.size()];
        const joint_catalogue::Footprint footprint{member.pos.x,member.pos.y,member.range,member.group,
            std::uint32_t(handle_camp_t(member.handle))};
        catalogue.footprints.push_back(footprint);
        catalogue.templates.push_back(JointCatalogueTemplate::make(footprint,member.base_hp,member.base_strength));
        const auto after=fuel_remaining();const auto spent=before>=after?before-after:0;
        stats.baseline_spent+=spent;stats.max_baseline_step=std::max(stats.max_baseline_step,spent);
        stats.copied=catalogue.footprints.size();
        stats.baseline_copied=catalogue.templates.size();
    }
    if(catalogue.footprints.size()!=stats.total)return finish(false);
    if(!catalogue.begun){
        if(!fuel(65000))return finish(false);
        const auto progress=catalogue.index.begin(walls->width(),walls->height(),
            std::span<const joint_catalogue::Footprint>(catalogue.footprints.data(),catalogue.footprints.size()));
        catalogue.begun=true;++catalogue.generation;catalogue.geometry.invalidate();
        stats.build_state=progress.state;stats.failure=progress.failure;
        if(progress.failed())return unsupported(progress.failure);
    }
    stats.stage=JointCatalogueRuntimeStats::Stage::building;
    // A fixed logical work quantum bounds each uninterrupted setup step.
    // Logical units are not fuel: actual costs are retained below and must be
    // qualified by native execution. Progress survives every owner input.
    constexpr std::size_t quantum=64;
    while(!catalogue.index.ready()&&fuel(65000)){
        const auto before=fuel_remaining();const auto progress=catalogue.index.buildStep(quantum);
        const auto after=fuel_remaining();stats.max_build_step=std::max(stats.max_build_step,before>=after?before-after:0);
        ++stats.build_steps;stats.build_units+=progress.units;stats.build_state=progress.state;stats.failure=progress.failure;
        stats.build_cursor=progress.records_copied;stats.clear_words=progress.clear_words_done;stats.incidences=progress.incidences_done;
        if(progress.failed())return unsupported(progress.failure);
        if(!progress.units&&!progress.ready())return unsupported(joint_catalogue::Failure::invalid_record);
    }
    if(!catalogue.index.ready())return finish(false);
    stats.stage=JointCatalogueRuntimeStats::Stage::ready;stats.build_state=joint_catalogue::BuildState::ready;++stats.built;
    return finish(true);
}
JointCatalogueGeometryKey joint_catalogue_current_key()const{
    const auto& catalogue=joint_catalogue_state();const auto& contact=joint_contact_state();
    const auto& owner=contact.world.actors[contact.world.owner];
    return {reinterpret_cast<std::uintptr_t>(&catalogue.index),catalogue.generation,members.size(),
        int(walls->width()),int(walls->height()),owner.position.x,owner.position.y,owner.move_speed,job,contact.request.capture_group};
}
// Separate unpaid dependency. The caller invokes this BEFORE the original
// measured source-materialization component. A miss must jointly fund BOTH
// unchanged enumeration bounds plus the existing first evaluation/command tail.
// A hit keeps geometry only; epoch, HP, visibility and nomination are NOT keys.
bool joint_catalogue_source_prepare(){
    auto& catalogue=joint_catalogue_state();auto& stats=joint_catalogue_stats();auto& contact=joint_contact_state();
    const auto start=fuel_remaining();++stats.cache_prepares;stats.last_cache_hit=false;stats.last_query=0;
    const auto finish=[&](bool success){const auto now=fuel_remaining();stats.last_source_prepare=start>=now?start-now:0;
        stats.source_prepare_spent+=stats.last_source_prepare;stats.max_source_prepare=std::max(stats.max_source_prepare,stats.last_source_prepare);
        catalogue.source_prepare_frame=contact.world.frame;catalogue.source_prepare_epoch=epoch;
        if(!success)++stats.cache_failures;return success;};
    if(!joint_catalogue_usable()){contact.status=JointContactStatus::unsupported;return finish(false);}
    if(contact.world.owner>=contact.world.actor_count){contact.status=JointContactStatus::capacity;return finish(false);}
    const auto key=joint_catalogue_current_key();stats.last_epoch=epoch;stats.last_origin={key.x,key.y};stats.last_halo=std::max(0,key.raw_speed);
    if(catalogue.geometry.matches(key)){
        ++stats.cache_hits;stats.last_cache_hit=true;stats.last_query=0;
        if(!joint_contact_funded(JointCatalogueDependencies::prepare_cost(contact.budget.enumeration_bound,true))){
            contact.status=JointContactStatus::partial;return finish(false);}
        return finish(true);
    }
    ++stats.cache_misses;
    const auto paired=JointCatalogueDependencies::prepare_cost(contact.budget.enumeration_bound,false);
    if(!joint_contact_funded(paired)){contact.status=JointContactStatus::partial;return finish(false);}
    catalogue.geometry.invalidate();const auto before=fuel_remaining();++stats.source_queries;
    const auto status=catalogue.index.query({key.x,key.y},key.raw_speed,key.job,key.capture,
        joint_world::camp_capacity,catalogue.selection);
    const auto queried=fuel_remaining();stats.last_query=before>=queried?before-queried:0;
    stats.query_spent+=stats.last_query;stats.max_query=std::max(stats.max_query,stats.last_query);
    stats.last_query_status=status;stats.last_metadata_records=catalogue.index.size();
    if(status!=joint_catalogue::QueryStatus::ok){++stats.query_failures;
        contact.status=status==joint_catalogue::QueryStatus::not_ready?JointContactStatus::partial:JointContactStatus::capacity;return finish(false);}
    if(!catalogue.geometry.capture(key,catalogue.selection,std::span<const JointCatalogueTemplate>(catalogue.templates))){
        contact.status=JointContactStatus::capacity;return finish(false);}
    const auto now=fuel_remaining();
    // Whole query+plan commit is one bounded component, not only the inner
    // pure query. Its following fresh materialization remains prepaid.
    if(start-now>contact.budget.enumeration_bound||!joint_contact_funded(contact.budget.enumeration_bound)){
        catalogue.geometry.invalidate();contact.status=JointContactStatus::overrun;++contact.declared_bounds_overrun;return finish(false);}
    stats.last_local=catalogue.geometry.represented_count;stats.last_overflow=catalogue.geometry.overflow_count;
    stats.last_contributors=catalogue.geometry.contributor_count;return finish(true);
}
// Called inside the original single measured enumeration component. It never
// queries or changes the retained geometry, and rereads every needed mutable
// target/weight from this actual input. No query prefix is charged again.
bool joint_indexed_sources(){
    auto& catalogue=joint_catalogue_state();auto& stats=joint_catalogue_stats();auto& contact=joint_contact_state();
    auto& observation=joint_observation_state();auto& world=contact.world;const auto start=fuel_remaining();
    const auto finish=[&](bool success){const auto now=fuel_remaining();const auto spent=start>=now?start-now:0;
        stats.last_materialize=spent;stats.materialize_spent+=spent;stats.max_materialize=std::max(stats.max_materialize,spent);
        const auto query_cost=catalogue.source_prepare_epoch==epoch&&catalogue.source_prepare_frame==world.frame?stats.last_source_prepare:0;
        stats.last_frame=query_cost+spent;stats.max_frame=std::max(stats.max_frame,stats.last_frame);
        if(success)++stats.source_frames;else ++stats.frame_failures;return success;};
    if(!joint_catalogue_usable()){contact.status=JointContactStatus::unsupported;return finish(false);}
    if(world.owner>=world.actor_count){contact.status=JointContactStatus::capacity;return finish(false);}
    if(!joint_contact_funded(contact.budget.enumeration_bound)){contact.status=JointContactStatus::partial;return finish(false);}
    if(!catalogue.geometry.matches(joint_catalogue_current_key())){contact.status=JointContactStatus::partial;return finish(false);}
    const auto& owner=world.actors[world.owner];world.camp_count=0;world.aggregate={};contact.group_count=0;
    contact.omitted_enemy_support=catalogue.geometry.omitted_any;
    observation.target_member=std::numeric_limits<std::size_t>::max();observation.target_source=joint_world::no_word;
    const auto result=JointCatalogueFrame::fill(catalogue.geometry,std::span<const JointCatalogueTemplate>(catalogue.templates),
        std::span<joint_world::Camp>(world.camps),std::span<std::size_t>(contact.camp_members),owner.position,owner.attack_range,job,epoch,
        [&](std::size_t index)->const Member&{return members[index];},
        [&](std::size_t index){const auto& member=members[index];return sees(member.pos)&&work_candidate(member);},
        [&]{return joint_contact_funded(0);});
    world.camp_count=result.rows;world.aggregate.present=result.aggregate_present;world.aggregate.damage=result.aggregate_damage;
    observation.target_member=result.target_member;observation.target_source=result.target_source;
    observation.source_overflow+=unsigned(result.overflow_seen);
    stats.materialize_mutable_reads+=result.mutable_reads;stats.materialize_eligibility_reads+=result.eligibility_reads;
    if(result.rows){observation.first_source_handle=world.camps[0].id;observation.first_source_group=world.camps[0].group;}
    if(!result.complete){contact.status=JointContactStatus::partial;return finish(false);}
    world.aggregate.covers_omitted=true;world.aggregate.center=owner.position;world.aggregate.reach=int(walls->width()+walls->height());
    observation.source_count=world.camp_count;return finish(true);
}
#endif
