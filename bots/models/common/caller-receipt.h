// Included inside Bot. One opt-in actual caller window, deferred output only.
#ifndef PLAN4_CALLER_RECEIPT
#define PLAN4_CALLER_RECEIPT 0
#endif
#if PLAN4_CALLER_RECEIPT
struct CallerReceipt {
    struct Row {
        int round=-1,end_round=-1,stage=0,readiness=0,macro_result=-1,target=-1,pending=0;
        int info_round=-1,truncated=-1,layout_truncated=-1,groups=-1,menu=-1,evaluated=-1,budget=-1;
        int stop_stage=-1,stop_group=-1,stop_member=-1,model_reason=-1,model_supported=-1;
        unsigned fuel[16]{};
    } rows[4];
    Row* active=nullptr; bool emitted=false;
} caller_receipt_;
void caller_receipt_begin() {
    caller_receipt_.active=nullptr;
    if (int(static_cast<cambc::handle_champion_t>(cambc::self()))!=0) return;
    const int round=int(cambc::round_number());
    if (round<14 || round>17) return;
    auto& row=caller_receipt_.rows[round-14];row.round=round;caller_receipt_.active=&row;
    row.fuel[0]=unsigned(cambc::fuel_remaining());
}
void caller_receipt_mark(int stage) {
    if (auto* row=caller_receipt_.active) { row->stage=stage;row->fuel[stage]=unsigned(cambc::fuel_remaining()); }
}
void caller_receipt_finish() {
    if (auto* row=caller_receipt_.active) {
        row->fuel[15]=unsigned(cambc::fuel_remaining());row->end_round=int(cambc::round_number());
        row->target=target_;row->pending=movement_pending_;
        if (information_storage_) {
            const auto& info=*information_storage_;row->info_round=info.round;
            row->truncated=info.observation_truncated;row->layout_truncated=info.public_layout_truncated;row->groups=info.group_count;
        }
        const auto& stop=observer_.diagnostics();row->stop_stage=stop.stop_stage;row->stop_group=stop.stop_group;row->stop_member=stop.stop_member;
        caller_receipt_.active=nullptr;
    }
}
void caller_receipt_flush() {
    if (caller_receipt_.emitted || int(static_cast<cambc::handle_champion_t>(cambc::self()))!=0 || cambc::round_number()<18) return;
    caller_receipt_.emitted=true;
    for (const auto& r:caller_receipt_.rows) {
        plan4::LogRow row("caller-receipt",PLAN4_REFERENCE_VERSION);
        row.field("actor",0).field("round",r.round).field("endRound",r.end_round).field("stage",r.stage)
            .field("readiness",r.readiness).field("macroResult",r.macro_result).field("target",r.target).field("pending",r.pending)
            .field("infoRound",r.info_round).field("truncated",r.truncated).field("layoutTruncated",r.layout_truncated).field("groups",r.groups)
            .field("menu",r.menu).field("evaluated",r.evaluated).field("budget",r.budget)
            .field("stopStage",r.stop_stage).field("stopGroup",r.stop_group).field("stopMember",r.stop_member)
            .field("modelReason",r.model_reason).field("modelSupported",r.model_supported);
        for(int n=0;n<16;++n) row.field("f"+std::to_string(n),r.fuel[n]);
        row.emit();
    }
}
#define PLAN4_CALLER_MARK(n) caller_receipt_mark(n)
#else
#define PLAN4_CALLER_MARK(n) ((void)0)
#endif
