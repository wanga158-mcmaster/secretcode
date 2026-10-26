// Included inside Bot; engine-facing bounded telemetry and model lifetime.
    static std::string number(float value) { return std::isfinite(value) ? std::to_string(value) : "null"; }
    std::string group_key(int group) const { return "g_"+std::to_string(groups_[group].key); }
    void probe_stop(const char* kind,const char* reason) {
        event(kind,"breakpoint_verify","\"reason\":\""+std::string(reason)+"\"",true);
        probe_finished_=true;
    }
    void expire_prediction(const char* reason,bool confounded=false) {
        if (!prediction_active_) return;
        prediction_confounded_|=confounded;
        if (prediction_confounded_) ++predictions_confounded_; else ++predictions_expired_;
        event("observation","breakpoint_verify","\"observed_count\":"+std::to_string(prediction_basics_)
            +",\"predicted\":"+std::to_string(prediction_after_)+",\"status\":\""
            +(prediction_confounded_?"confounded":"expired")+"\",\"reason\":\""+reason+"\"");
        prediction_active_=false;
    }
    void telemetry_tick() {
        const unsigned round=cambc::round_number();
        if (last_turn_ && round>last_turn_+1) turn_gaps_+=round-last_turn_-1;
        last_turn_=round;
        if (round/32>summary_bucket_) {
            summary_bucket_=round/32;
            event("summary","counters","\"lastObservedRound\":"+std::to_string(round)
                +",\"droppedEvents\":"+std::to_string(dropped_)+",\"turn_gaps\":"+std::to_string(turn_gaps_)
                +",\"purchases\":"+std::to_string(purchases_)+",\"breakpoint_purchases\":"+std::to_string(breakpoint_purchases_)
                +",\"reserved_gold_turns\":"+std::to_string(reserved_gold_turns_)+",\"reserved_gold_total\":"+std::to_string(reserved_gold_total_)
                +",\"expired\":"+std::to_string(predictions_expired_)+",\"confounded\":"+std::to_string(predictions_confounded_)
                +",\"planning_guards\":"+std::to_string(planning_guards_)+",\"fuel_low_water\":"+std::to_string(fuel_low_water_));
        }
    }
