// Included inside Bot only for adapters opting into isolated own stock.
plan4::runtime::OwnAcquisitionCollector own_acquisition_;
int acquisition_delivered_round_ = -1;
unsigned acquisition_hold_floor_ = 50000;

void macro_acquisition_hold(bool protected_recovery = false) {
    if ((!protected_recovery && (!macro_active_ || macro_job_.kind != plan4::JobKind::farm)) ||
        !information_storage_) return;
    const auto round = cambc::round_number();
    const auto before = cambc::fuel_remaining();
    if (before < 65000) return;
    acquisition_hold_floor_ = unsigned(std::max<std::uint64_t>(50000, before - 15000));
    auto& info = *information_storage_;
    // Holding a live capture bypasses planning. Reconcile fresh clear receipts
    // here before their one-round confirmation window expires. This observer
    // never chooses a job, repairs, fights, moves, or cancels the hold.
    if (!observer_.observe_into(info, navigation_.lookup(), acquisition_hold_floor_) ||
        cambc::round_number() != round) return;
    info.work_budget = {macro_budget, &acquisition_hold_floor_};
    macro_acquisition_observe(info);
}

void macro_acquisition_observe(const plan4::Information& info) {
    if (info.round < 0 || cambc::round_number() != unsigned(info.round)) return;
    runtime_model_.observe_own_stock(own_acquisition_.frame());
    // Reconcile queued travel/clear completions before a capture advances the
    // suffix. This also refreshes active contracts on every planning snapshot.
    runtime_model_.observe_snapshot(info, online_evidence_.members());
    if (acquisition_delivered_round_ != info.round) {
        acquisition_delivered_round_ = info.round;
        if (const auto* source = own_acquisition_.capture()) {
            const bool accepted = runtime_model_.capture_receipt(info, *source);
            plan4::LogRow("capture-evidence", PLAN4_REFERENCE_VERSION)
                .field("actor", source->actor_id).field("round", source->round)
                .field("group", source->group_id).field("respawn", source->respawn_rounds)
                .field("eventStatus", int(source->event_status)).field("origin", int(source->origin))
                .field("attributed", source->actor_attribution_known).field("received", source->received_gems)
                .field("stockEpoch", source->stock_epoch).field("accepted", accepted).emit();
        }
        // Keep occurrence-only evidence visibly unattributed; a lifecycle
        // transition alone never goes through the own-stock receipt consumer.
        for (int n = 0; n < online_evidence_.capture_count(); ++n) {
            const auto& source = online_evidence_.capture(n).receipt;
            plan4::LogRow("capture-evidence", PLAN4_REFERENCE_VERSION)
                .field("actor", info.own.id).field("round", source.round)
                .field("group", source.group_id).field("respawn", source.respawn_rounds)
                .field("eventStatus", int(source.event_status)).field("origin", int(source.origin))
                .field("attributed", false).field("accepted", false).emit();
        }
    }
    // The executor already leaves a visibly captured first camp. Synchronize
    // that selection before constructing the remaining-group repair frame.
    if (macro_active_ && macro_job_.kind == plan4::JobKind::farm && !macro_second_ &&
        info.own.channel == plan4::Channel::none && info.in_progress_group_id < 0 &&
        macro_job_.group >= 0 && macro_job_.group < info.group_count &&
        macro_job_.second_group >= 0 && macro_job_.second_group < info.group_count &&
        macro_job_.second_group != macro_job_.group) {
        const auto& first = info.groups[macro_job_.group];
        if (first.visible && (first.lifecycle == plan4::Lifecycle::captured ||
                              first.lifecycle == plan4::Lifecycle::respawning)) macro_second_ = true;
    }
}
