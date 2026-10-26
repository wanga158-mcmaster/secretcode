// Included inside Bot; reuse existing caller clocks and guard reads only.
#ifndef PLAN4_MOVEMENT_RECEIPT
#define PLAN4_MOVEMENT_RECEIPT 0
#endif
#if PLAN4_MOVEMENT_RECEIPT
struct MovementReceiptRow {
    int calls=0,stage=0,initialized=-1,status=-1,goals=-1,goal_prefix=0;
    unsigned admission_fuel=0,cold_fuel=0,prepared_fuel=0,used=0,remaining=0,visits=0,generation_before=0,generation_after=0,goal_digest=0;
};
MovementReceiptRow movement_receipts_[3];
int movement_receipt_round_=-1,movement_receipt_actor_=-1;
bool movement_receipt_emitted_=false;
MovementReceiptRow* movement_receipt_row() {
    return movement_receipt_actor_==0 && movement_receipt_round_>=15 && movement_receipt_round_<=17
        ? &movement_receipts_[movement_receipt_round_-15] : nullptr;
}
void movement_receipt_flush() {
    if(movement_receipt_emitted_ || movement_receipt_actor_!=0 || movement_receipt_round_<18) return;
    movement_receipt_emitted_=true;
    for(int n=0;n<3;++n) {
        const auto& r=movement_receipts_[n];
        plan4::LogRow("movement-receipt",PLAN4_REFERENCE_VERSION).field("actor",0).field("round",15+n)
            .field("calls",r.calls).field("stage",r.stage).field("initialized",r.initialized).field("status",r.status)
            .field("admissionFuel",r.admission_fuel).field("coldFuel",r.cold_fuel).field("preparedFuel",r.prepared_fuel)
            .field("used",r.used).field("remaining",r.remaining).field("visits",r.visits)
            .field("generationBefore",r.generation_before).field("generationAfter",r.generation_after)
            .field("goals",r.goals).field("goalPrefix",r.goal_prefix).field("goalDigest",r.goal_digest).emit();
    }
}
#endif
