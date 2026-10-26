// Included inside the existing emitted model row; cached producer data only.
// This is not every private consumer predicate (in particular, not kit.exact).
{
    const auto& ab_actors = actor_evidence_.frame();
    const auto& ab_producers = online_evidence_.producers();
    const auto& ab_menu = evaluation.menu;
    const unsigned ab_producer_bad =
        (!ab_actors.roster_complete ? 1u : 0u) |
        (!ab_actors.own_observed ? 2u : 0u) |
        (ab_actors.allied_count < 1 || ab_actors.allied_count > 5 ? 4u : 0u) |
        (ab_actors.enemy_count < 0 || ab_actors.enemy_count > 5 ? 8u : 0u) |
        (ab_actors.count < 0 || ab_actors.count > 9 ? 16u : 0u) |
        (ab_producers.count < 0 || ab_producers.count > 4 ? 32u : 0u) |
        (ab_menu.single_count < 1 || ab_menu.single_count > 8 ? 64u : 0u) |
        (ab_menu.count < ab_menu.single_count || ab_menu.count > plan4::max_jobs ? 128u : 0u);
    model_row.field("abV", 1)
        .field("abBefore", actor_evidence_.collection_start_fuel())
        .field("abMinimum", actor_evidence_.minimum_fuel())
        .field("abStop", actor_evidence_.stop_reason())
        .field("abStopFuel", actor_evidence_.stop_fuel())
        .field("abRound", ab_actors.clock.round).field("abActor", ab_actors.clock.own.id)
        .field("abCount", ab_actors.count).field("abOwn", ab_actors.own_observed)
        .field("abRoster", ab_actors.roster_complete).field("abTruncated", ab_actors.truncated)
        .field("abAllies", ab_actors.allied_count).field("abEnemies", ab_actors.enemy_count)
        .field("abSingles", ab_menu.single_count).field("abMenuCount", ab_menu.count)
        .field("abProducerCount", ab_producers.count).field("abProducerBad", ab_producer_bad)
        .field("abKitSeen", kit_seen_).field("abKitRound", kit_observed_round_)
        .field("abKitEpoch", kit_epoch_);
}
