// Included inside Bot under PLAN4_MACRO_MODE. Recording owns no game action.
plan4::attempts::Recorder attempt_recorder_;
plan4::attempts::Identity legacy_attempt_identity_{};
std::uint64_t legacy_attempt_token_ = 0;
bool legacy_attempt_open_ = false;
bool attempt_previous_channel_ = false;
int attempt_previous_round_ = -1;

plan4::attempts::Observation attempt_observation() {
    plan4::attempts::Observation result;
    const int started = int(cambc::round_number());
    const auto& me = cambc::self();
    result.actor = int(static_cast<cambc::handle_champion_t>(me));
    result.round = started;
    result.own = result.fresh = true;
    result.alive = me.is_alive();
    result.alive_known = true;
    // Optional own-body reads never spend the execution reserve. Missing
    // pre-action fields remain unknown; they cannot qualify a fit bin.
    if (cambc::fuel_remaining() >= 60000) {
        result.bag = int(me.gem_count()); result.bag_known = true;
        if (result.alive) {
            const auto position = me.position();
            result.x = position.x; result.y = position.y; result.position_known = true;
            result.hp_bits = std::bit_cast<std::uint32_t>(me.health()); result.hp_known = true;
            result.max_hp_bits = std::bit_cast<std::uint32_t>(me.max_health()); result.max_hp_known = true;
        }
    }
    const int finished = int(cambc::round_number());
    if (finished != started) {
        result = {};
        result.actor = int(static_cast<cambc::handle_champion_t>(me));
        result.round = finished; result.own = result.fresh = true;
    }
    return result;
}

void attempt_end(plan4::attempts::Outcome outcome) {
    if (attempt_recorder_.active()) attempt_recorder_.finish(outcome, attempt_observation());
    legacy_attempt_open_ = false;
}

void attempt_begin(const plan4::attempts::Identity& key, const plan4::Job* job = nullptr) {
    const auto sample = attempt_observation();
    // Keep ordinary continuity observation, but never collect again or upgrade
    // an active/recently terminal identity's original UNKNOWN snapshot.
    if (attempt_recorder_.same_attempt(key) || (sample.alive_known && !sample.alive)) {
        attempt_recorder_.begin(key, sample);
        return;
    }
    const cambc::map::camp_group* physical = nullptr;
    if (key.kind == plan4::attempts::Kind::one_camp) {
        for (const auto& entry : groups_) {
            if (int(cambc::round_number()) != sample.round || cambc::fuel_remaining() < 70000) break;
            if (int(static_cast<cambc::handle_camp_group_t>(entry.group)) == key.group_id) {
                physical = &entry.group; break;
            }
        }
    }
    // The common sampler uses self/public/personal-vision evidence only. It is
    // available to reference M0 as well as both Kernel arms. Missing budget,
    // target, clock or planned metadata leaves fields unknown, never skips a start.
    const auto preaction = plan4::attempts::collect_preaction(key, sample, job, physical);
    attempt_recorder_.begin(key, sample, preaction);
}

void attempt_planned(const plan4::Information& info) {
    using namespace plan4::attempts;
    const bool bank = macro_job_.kind == plan4::JobKind::bank;
    const bool single = macro_job_.kind == plan4::JobKind::farm && macro_job_.second_group < 0;
    if (!bank && !single) {
        attempt_end(Outcome::replaced); // Other macro types are outside v1 bins.
        return;
    }
    Identity key;
    key.kind = bank ? Kind::bank : Kind::one_camp;
    key.origin = Origin::planned; key.job_token = decision_sequence_;
    if (single && macro_job_.group >= 0 && macro_job_.group < info.group_count)
        key.group_id = info.groups[macro_job_.group].id;
    if (macro_job_.altar >= 0 && macro_job_.altar < info.altar_count)
        key.altar_id = info.altars[macro_job_.altar].id;
    legacy_attempt_open_ = false;
    attempt_begin(key, &macro_job_);
}

void attempt_legacy(plan4::attempts::Kind kind, int group, plan4::attempts::Origin origin) {
    using namespace plan4::attempts;
    // Banking the reward continues the same one-camp macro until conversion.
    if (kind == Kind::bank && legacy_attempt_open_ &&
        legacy_attempt_identity_.kind == Kind::one_camp && target_ >= 0 &&
        target_ < int(groups_.size()) && legacy_attempt_identity_.group_id ==
            int(static_cast<cambc::handle_camp_group_t>(groups_[target_].group))) return;
    if (!legacy_attempt_open_ || legacy_attempt_identity_.kind != kind ||
        legacy_attempt_identity_.group_id != group) {
        legacy_attempt_identity_ = {kind, origin, ++legacy_attempt_token_, group, -1};
        legacy_attempt_open_ = true;
    }
    attempt_begin(legacy_attempt_identity_);
}

void attempt_tribute_observed() {
    const int started = int(cambc::round_number());
    const bool channel = cambc::self().is_submitting_gems();
    attempt_previous_round_ = started;
    attempt_previous_channel_ = channel && int(cambc::round_number()) == started;
}

void attempt_poll(int sampled_round, unsigned bag, bool tribute) {
    using namespace plan4::attempts;
    auto sample = attempt_observation();
    if (sample.round == sampled_round) { sample.bag = int(bag); sample.bag_known = true; }
    attempt_recorder_.observe(sample);
    const bool current = sample.round == sampled_round;
    // A team-score rise or submit_gems() return cannot prove own conversion.
    // Require contiguous, actually observed channel resolution; the converted
    // quantity/economic tape is still unknown (capture precedes resolution).
    if (current && attempt_previous_round_ + 1 == sample.round && attempt_previous_channel_ && !tribute &&
        sample.alive_known && sample.alive && sample.bag_known && sample.bag == 0) {
        attempt_recorder_.finish(Outcome::completed, sample);
        legacy_attempt_open_ = false;
    } else if (current && sample.bag_known && sample.bag == 0 && legacy_attempt_open_ &&
               legacy_attempt_identity_.kind == Kind::bank && !tribute) {
        attempt_recorder_.finish(Outcome::unknown, sample);
        legacy_attempt_open_ = false;
    }
    attempt_previous_round_ = sample.round;
    attempt_previous_channel_ = current && sample.alive_known && sample.alive && tribute;
}

void attempt_flush() {
    // At most two logical rows at the next owner-turn entry. A begin and its
    // companion are built/checked first and passed to ONE stream write, without
    // an intervening fuel/round return that could duplicate a retried base row.
    // This is not durable atomic output or sealed full-game coverage.
    const auto started = cambc::round_number();
    int rows = 0;
    while (rows < 2 && attempt_recorder_.front()) {
        if (cambc::round_number() != started || cambc::fuel_remaining() < 80000) return;
        const auto& event = *attempt_recorder_.front();
        const bool begin = event.type == plan4::attempts::EventType::begin;
        const int required = begin ? 2 : 1;
        if (rows + required > 2) return;
        const auto& sample = begin ? event.start : event.end;
        const unsigned known = unsigned(sample.position_known) | (unsigned(sample.bag_known) << 1) |
            (unsigned(sample.hp_known) << 2) | (unsigned(sample.max_hp_known) << 3) |
            (unsigned(sample.alive_known) << 4) | (unsigned(sample.alive) << 5) |
            (unsigned(sample.own) << 6) | (unsigned(sample.fresh) << 7);
        plan4::LogRow row("attempt", PLAN4_REFERENCE_VERSION);
        row.field("schema", 1).field("actor", event.start.actor)
            .field("round", begin ? event.start.round : event.terminal_round)
            .field("event", event.event_id).field("id", event.attempt_id).field("edge", int(event.type))
            .field("kind", int(event.identity.kind)).field("origin", int(event.identity.origin))
            .field("job", event.identity.job_token).field("group", event.identity.group_id)
            .field("altar", event.identity.altar_id).field("dropped", event.dropped_events)
            .field("hasEnd", event.has_end).field("outcome", int(event.outcome))
            .field("startRound", event.start.round).field("x", sample.x).field("y", sample.y)
            .field("bag", sample.bag).field("hpBits", sample.hp_bits).field("maxHpBits", sample.max_hp_bits)
            .field("known", known).field("binKnown", 0);
        plan4::LogRow companion("attempt-bin", PLAN4_REFERENCE_VERSION);
        if (begin) {
            const auto& pre = event.preaction;
            const bool composition_known = pre.composition_known &&
                (event.identity.kind == plan4::attempts::Kind::bank ? pre.composition_count == 0 : pre.composition_count > 0);
            const unsigned mask = unsigned(pre.kit_known) | (unsigned(composition_known) << 1) |
                (unsigned(pre.context_known) << 2) | (unsigned(pre.travel_known) << 3) |
                (unsigned(pre.nominal_duration_known) << 4);
            // Schema maxima: 447 item-token bytes and 63 composition bytes.
            char items[448], composition[64];
            char* item_end = items; char* composition_end = composition;
            const auto number = [](char*& at, char* end, auto value) {
                const auto result = std::to_chars(at, end, value);
                if (result.ec != std::errc{}) return false;
                at = result.ptr; return true;
            };
            if (!pre.kit_known || !pre.item_count) *item_end++ = '-';
            else for (int n = 0; n < pre.item_count; ++n) {
                if (n) *item_end++ = ',';
                if (!number(item_end, items + sizeof(items), pre.items[n].kind)) return;
                *item_end++ = ':';
                if (!number(item_end, items + sizeof(items), pre.items[n].count)) return;
            }
            if (!composition_known || !pre.composition_count) *composition_end++ = '-';
            else for (int n = 0; n < pre.composition_count; ++n) {
                if (n) *composition_end++ = ',';
                if (!number(composition_end, composition + sizeof(composition), pre.composition[n])) return;
            }
            companion.field("schema", 1).field("actor", event.start.actor).field("round", event.start.round)
                .field("id", event.attempt_id).field("event", event.event_id).field("kind", int(event.identity.kind))
                .field("group", event.identity.group_id).field("altar", event.identity.altar_id).field("known", mask)
                .field("champion", pre.kit_known ? pre.champion : -1)
                .field("ability0", pre.kit_known ? pre.ability0 : -1).field("ability1", pre.kit_known ? pre.ability1 : -1)
                .field("range", pre.kit_known ? pre.attack_range : -1)
                .field("strengthBits", pre.kit_known ? pre.strength_bits : 0u)
                .field("focusBits", pre.kit_known ? pre.focus_bits : 0u)
                .field("itemCount", pre.kit_known ? pre.item_count : 0)
                .field("items", std::string_view(items, std::size_t(item_end - items)))
                .field("compositionCount", composition_known ? pre.composition_count : 0)
                .field("composition", std::string_view(composition, std::size_t(composition_end - composition)))
                .field("context", pre.context_known ? pre.context : -1).field("travel", pre.travel_known ? pre.travel : -1)
                .field("nominal", pre.nominal_duration_known ? pre.nominal_duration : 0);
        }
        if (!row.valid() || (begin && !companion.valid()) || cambc::round_number() != started ||
            cambc::fuel_remaining() < 65000) return;
        char output[4098]; // Two maximum-sized LogRows plus their newlines.
        std::size_t size = row.view().size();
        std::memcpy(output, row.view().data(), size); output[size++] = '\n';
        if (begin) {
            std::memcpy(output + size, companion.view().data(), companion.view().size());
            size += companion.view().size(); output[size++] = '\n';
        }
        if (cambc::round_number() != started || cambc::fuel_remaining() < 65000) return;
        std::cout.write(output, std::streamsize(size));
        attempt_recorder_.pop();
        rows += required;
    }
}
