#pragma once
#include <cstdio>
#include <string>

// One framed write per record; counts survive every owner turn, including dead turns.
class Telemetry {
    unsigned sequence_ = 0, details_ = 0, summaries_ = 0, dropped_ = 0;
    unsigned turns_ = 0, gaps_ = 0, last_ = 0, opportunities_ = 0, attempts_ = 0;
    unsigned actions_ = 0, witnesses_ = 0;
    std::string actor_;
    void write(const char* kind, const std::string& data) {
        if (cambc::fuel_remaining() < 8000) { ++dropped_; return; }
        char buffer[513];
        const int length = std::snprintf(buffer, sizeof buffer,
            "BC26T{\"s\":1,\"v\":\"%s\",\"c\":\"%s\",\"a\":\"%s\",\"r\":%u,\"q\":%u,\"k\":\"%s\",\"d\":{%s}}\n",
            profile::version_id, profile::core_id, actor_.c_str(), cambc::round_number(), sequence_ + 1,
            kind, data.c_str());
        if (length <= 0 || length > 512) { ++dropped_; return; }
        ++sequence_;
        std::fwrite(buffer, 1, static_cast<std::size_t>(length), stdout);
    }
public:
    void tick() {
        const auto round = cambc::round_number();
        if (actor_.empty()) {
            actor_ = cambc::to_string(cambc::self().champion_name());
            write("start", "\"subtype\":\"runtime_start\",\"enabled\":" + std::string(profile::core_enabled ? "true" : "false")
                + ",\"probe\":" + (profile::probe_mode ? "true" : "false")
                + ",\"nativeChampionId\":" + std::to_string(static_cast<unsigned>(static_cast<cambc::handle_champion_t>(cambc::self()))));
        }
        if (turns_ && round > last_ + 1) ++gaps_;
        ++turns_; last_ = round;
        if (summaries_ < 62 && round / 32 > (round ? (round - 1) / 32 : 0)) {
            ++summaries_;
            write("summary", "\"subtype\":\"counters\",\"turns\":" + std::to_string(turns_)
                + ",\"opportunities\":" + std::to_string(opportunities_) + ",\"attempts\":" + std::to_string(attempts_)
                + ",\"actions\":" + std::to_string(actions_) + ",\"witnesses\":" + std::to_string(witnesses_)
                + ",\"lastObservedRound\":" + std::to_string(last_) + ",\"droppedEvents\":" + std::to_string(dropped_)
                + ",\"turnGapCount\":" + std::to_string(gaps_));
        }
    }
    void event(const char* kind, const char* subtype, const std::string& fields = "", bool exit = false) {
        const std::string k{kind};
        if (k == "opportunity") ++opportunities_;
        if (k == "activation") ++attempts_;
        if (k == "action_result") ++actions_;
        if (k == "mechanism_success") ++witnesses_;
        // Reserve the final eight details for exits and conversion evidence.
        if (details_ >= 32 || (!exit && details_ >= 24)) { ++dropped_; return; }
        ++details_;
        write(kind, "\"subtype\":\"" + std::string(subtype) + "\"" + (fields.empty() ? "" : "," + fields));
    }
};
