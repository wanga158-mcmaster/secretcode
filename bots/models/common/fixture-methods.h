// Included inside Bot only for an ordinary-start staged fixture.
void fixture_warmup() {
    // Acquisition uses the inherited farmer in both arms. Optional model
    // planning must not consume its physical execution reserve.
    const auto note = [&](int code, const char* reason) {
        if (fixture_warmup_note_ != code || cambc::round_number() % 32 == 0)
            std::cout << "plan4 fixture warmup-yield actor=" << int(static_cast<cambc::handle_champion_t>(cambc::self()))
                << " round=" << cambc::round_number() << " reason=" << reason
                << " fuel=" << cambc::fuel_remaining() << '\n';
        fixture_warmup_note_ = code;
    };
    if (!initialized_) {
        if (cambc::fuel_remaining() < 90000) { note(0,"common-init-admission"); return; }
        initialize(); note(1,"common-initialized"); return;
    }
    if (!movement_initialized_) {
        if (cambc::fuel_remaining() < 80000) { note(2,"movement-init-admission"); return; }
        const int width = cambc::map::width(), height = cambc::map::height();
        std::vector<unsigned char> topology(width * height, 0);
        for (const auto p : terrain_.positions()) topology[p.y * width + p.x] = 1;
        movement_.initialize(width, height, std::move(topology));
        movement_initialized_ = true;
        note(3,"movement-initialized"); return;
    }
    struct Scope { bool& flag; explicit Scope(bool& value) : flag(value) { flag = true; } ~Scope() { flag = false; } } scope(fixture_warming_);
    physical_vertices_ = model_vertices_ = 0;
    movement_pending_ = false;
    production_requested_farm_group_ = -1;
    navigation_.begin_turn();
    turn();
}

// Preparation uses the ordinary observer, menu, model and selector, while the
// fixture retains its physical checkpoint until the current plan is admitted.
void fixture_prepare(bool (*commit)(void*), void* context) {
    if (!commit || !initialized_ || !movement_initialized_) return;
    struct Scope {
        Bot& bot;
        Scope(Bot& value, bool (*callback)(void*), void* raw) : bot(value) {
            bot.fixture_preparing_ = true;
            bot.fixture_prepare_round_ = int(cambc::round_number());
            bot.fixture_commit_ = callback;
            bot.fixture_commit_context_ = raw;
        }
        ~Scope() {
            bot.fixture_preparing_ = false;
            bot.fixture_prepare_round_ = -1;
            bot.fixture_commit_ = nullptr;
            bot.fixture_commit_context_ = nullptr;
        }
    } scope(*this, commit, context);
    runtime_model_.set_enabled(PLAN4_MODEL_ENABLED != 0);
    macro_abort("fixture-preparation");
    macro_tick();
    if (int(cambc::round_number()) != fixture_prepare_round_) return;
    const auto camps = cambc::vision::camps();
    const auto enemies = cambc::vision::champions(cambc::team::enemy);
    if (int(cambc::round_number()) == fixture_prepare_round_)
        macro_decide(camps, enemies);
}

bool fixture_accept_evaluation(const plan4::Information& info, const plan4::Evaluation& evaluation) {
    if (!fixture_preparing_) return true;
    const auto evidence = runtime_model_.model_evidence();
    unsigned bank_mask = 0;
    [[maybe_unused]] bool invalid_bank = false;
    if (evaluation.menu.count > 0 && evaluation.menu.count <= plan4::max_jobs) {
        for (int n = 0; n < evaluation.menu.count; ++n) {
            const auto& job = evaluation.menu.jobs[n];
            if (job.kind != plan4::JobKind::bank) continue;
            if (job.altar < 0 || job.altar >= 2) invalid_bank = true;
            else bank_mask |= 1u << unsigned(job.altar);
        }
    }
    const auto current = [&] {
        return fixture_prepare_round_ >= 0 && info.round == fixture_prepare_round_
            && int(cambc::round_number()) == fixture_prepare_round_
            && info.own.id == int(static_cast<cambc::handle_champion_t>(cambc::self()));
    };
    const auto hold = [&](int reason) {
        // This is a rejected preparation, never a model decision. Elapsed-round
        // sampling also works when four-round evaluations miss round%32==0.
        const unsigned fuel = cambc::fuel_remaining();
        if (fuel >= 16000 && info.round == int(cambc::round_number())
            && (reason != fixture_wait_reason_ || info.round - fixture_wait_round_ >= 32)) {
            unsigned accessible = 0;
            int distances[2] = {-1,-1};
            for (int n = 0; n < std::min(2, std::max(0, info.altar_count)); ++n) {
                if (info.altars[n].accessible) accessible |= 1u << unsigned(n);
                distances[n] = plan4::distance(info, info.own.position, info.altars[n].position);
            }
            std::cout << "plan4 fixture wait actor=" << info.own.id << " round=" << info.round
                << " reason=" << reason << " banks=" << bank_mask << " menu=" << evaluation.menu.count
                << " evaluated=" << evaluation.evaluated_count << " chosen=" << evaluation.chosen
                << " modelReason=" << evidence.reason << " altars=" << info.altar_count
                << " observationTruncated=" << info.observation_truncated
                << " menuTruncated=" << evaluation.menu.truncated << " fuel=" << fuel
                << " x=" << info.own.position.x << " y=" << info.own.position.y
                << " accessible=" << accessible << " distance0=" << distances[0] << " distance1=" << distances[1]
                << " navPairs=" << navigation_.completed_pairs() << " navPending=" << navigation_.pending_fields()
                << " navVisits=" << navigation_.vertex_visits() << '\n';
            fixture_wait_reason_ = reason; fixture_wait_round_ = info.round;
        }
        return false;
    };
    // First failed guard: clock, fuel, callback, body, observer/layout, altar
    // count, menu/budget, menu/evaluation count, choice, model, BANK/altar keys.
    if (!current()) return hold(1);
    if (cambc::fuel_remaining() < 40000) return hold(2);
    if (!fixture_commit_) return hold(3);
    if (!info.own.alive || info.own.maximum_health <= 0 || info.own.health <= 0
        || info.own.channel != plan4::Channel::none) return hold(4);
#ifndef PLAN4_FIXTURE_ACCEPT_EVALUATION
    if (!info.own.bag || info.own.health < info.own.maximum_health) return hold(4);
#endif
    if (info.observation_truncated) return hold(5);
    if (info.public_layout_truncated) return hold(6);
#ifndef PLAN4_FIXTURE_ACCEPT_EVALUATION
    if (info.altar_count != 2) return hold(7);
#endif
    if (evaluation.menu.truncated) return hold(8);
    if (evaluation.budget_exhausted) return hold(9);
    if (evaluation.menu.count <= 0 || evaluation.menu.count > plan4::max_jobs) return hold(10);
    if (evaluation.evaluated_count != evaluation.menu.count) return hold(11);
    if (evaluation.chosen < 0 || evaluation.chosen >= evaluation.menu.count) return hold(12);
#ifdef PLAN4_FIXTURE_ACCEPT_EVALUATION
    // A fixture may refine readiness, never the observer, menu or selector.
    // Complete/current evaluation and the final physical callback stay common.
    if (!PLAN4_FIXTURE_ACCEPT_EVALUATION(info, evaluation, evidence)) return hold(13);
#else
    if (!evidence.supported) return hold(13);
    // The Swirl paired fixture needs both actual altar keys. A duplicate BANK
    // entry or a menu with only HOLD/RENEW cannot authorize fixture release.
    if (invalid_bank || bank_mask != 3) return hold(14);
    if (info.altars[0].id < 0 || info.altars[1].id < 0 || info.altars[0].id == info.altars[1].id) return hold(15);
#endif
    if (!current()) return hold(1);
    if (!fixture_commit_(fixture_commit_context_)) return hold(16);
    // The callback performs final fresh checks before its pure acknowledgement.
    // Never select or execute from an observation that rolled while checking it.
    if (!current() || cambc::fuel_remaining() < 30000) return hold(17);
    fixture_preparing_ = false;
    return true;
}
