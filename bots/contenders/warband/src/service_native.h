#pragma once

#include <cambc.h>
#include "service_micro.h"
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

// Bounded native boundary for the basic-only harmless-group service. The macro
// owns the complete group/quiet-route certificate and all economic evidence.
// This adapter adds no SDK-querying globals and makes no atomicity/fuel proof.
namespace wb::service_native {

using service_micro::Authority;
using service_micro::Position;
enum class ChannelKind { None, Recall, Submit, Tribute = Submit, Teleport };

struct Self {
    Authority authority{};
    bool observed{};
    cambc::detail::champion_info raw{};
    cambc::champion_state state{};
    cambc::buff_restrictions restrictions{};
    bool stunned{};
    int bag{}, gold{};
    bool at_spawn{};
    ChannelKind channel{ChannelKind::None};
    int channel_remaining{};
};
struct Camp {
    Authority authority{};
    int id{-1};
    bool observed{}, visible{}, alive{}, live_info{};
    cambc::camp_state state{};
    // Valid ONLY with live_info. Dead lifecycle never fabricates position/HP.
    cambc::detail::camp_state raw{};
};
struct Move {
    Authority authority{};
    bool observed{};
    Position origin{}, endpoint{};
    // Actual SDK observed-reachability membership for this one adjacent step.
    // A reachable hidden landing may still contain an unseen blocker.
    bool reachable{};
    bool legal{};
    service_micro::MoveLanding landing{service_micro::MoveLanding::Missing};
    bool refusal_prefix_supported{};
};

inline std::int64_t distance(Position a, Position b) {
    const auto dx = std::int64_t(a.x) - b.x, dy = std::int64_t(a.y) - b.y;
    const auto ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    return ax > ay ? ax : ay;
}

// No native calls: converts authenticated copied records. Unknown lifecycle is
// not dead; known dead needs no live HP. Caller supplies the WHOLE domain proof.
inline service_micro::Frame make_frame(const Self& self, const Camp* camp = nullptr,
                                      const Move* move = nullptr,
                                      bool observed_domain = false,
                                      bool domain_supported = false) {
    service_micro::Frame f;
    f.authority = self.authority;
    f.observed_self = f.observed_readiness = self.observed;
    f.position = {self.raw.x, self.raw.y}; f.hp = self.raw.health;
    f.alive = self.state.contains(cambc::champion_state::alive);
    f.stunned = self.stunned; f.channel = self.channel != ChannelKind::None;
    f.can_move = self.state.contains(cambc::champion_state::can_move)
        && !self.restrictions.contains(cambc::buff_restrictions::move);
    f.can_attack = self.state.contains(cambc::champion_state::can_attack)
        && !self.restrictions.contains(cambc::buff_restrictions::attack);
    f.can_cast = self.state.contains(cambc::champion_state::can_cast)
        && !self.restrictions.contains(cambc::buff_restrictions::cast);
    f.observed_domain = observed_domain; f.domain_supported = domain_supported;
    if (camp && camp->authority == self.authority) {
        f.observed_target = camp->observed;
        f.target_id = camp->id; f.target_visible = camp->visible;
        f.target_alive = camp->alive;
        if (camp->live_info) {
            f.target_position = {camp->raw.x, camp->raw.y};
            f.target_hp = camp->raw.health;
            f.basic_in_range = self.raw.attack_range >= 0
                && distance(f.position, f.target_position) <= self.raw.attack_range;
        }
    }
    if (move && move->authority == self.authority && move->origin == f.position) {
        f.observed_move = move->observed;
        f.move_endpoint = move->endpoint; f.move_legal = move->legal;
        f.move_landing = move->landing;
        f.refusal_prefix_supported = move->refusal_prefix_supported;
    }
    return f;
}

class Adapter {
    static constexpr std::uint64_t empirical_dispatch_reserve = 12000;
    static constexpr std::size_t camp_capacity = 32;
    Authority latest_{};
    int owner_{-1}, round_{-1};
    std::uint64_t generation_{}, revision_{};
    Self self_{};
    std::array<Camp, camp_capacity> camps_{};
    std::size_t camp_count_{};
    Move move_{};
    service_micro::Gate gate_;

    bool local(Authority a) const { return service_micro::valid(a) && a == latest_; }
    bool epoch(Authority a) const {
        return local(a) && cambc::round_number() == static_cast<unsigned>(a.round);
    }
    static int integer(std::uint32_t x) {
        if (x > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
            throw std::runtime_error("service native integer range");
        return static_cast<int>(x);
    }
    // Only expected visibility failure becomes unknown. Other API failures must
    // reach the root turn boundary's error log/yield, never successful service.
    static bool visible_status(std::int32_t code) {
        if (code == static_cast<int>(cambc::error_code::api_not_in_vision)) return false;
        cambc::detail::check(code); return true;
    }
    void retain(const Camp& camp) {
        for (std::size_t i = 0; i < camp_count_; ++i)
            if (camps_[i].id == camp.id) { camps_[i] = camp; return; }
        if (camp_count_ == camps_.size())
            throw std::runtime_error("service native selected camp capacity");
        camps_[camp_count_++] = camp;
    }
    const Camp* selected(int id) const {
        for (std::size_t i = 0; i < camp_count_; ++i)
            if (camps_[i].id == id && camps_[i].authority == latest_) return &camps_[i];
        return nullptr;
    }
    static service_micro::Decision missing(service_micro::Reason reason) {
        service_micro::Decision d; d.status = service_micro::Status::NeedObservation;
        d.reason = reason; return d;
    }
    static bool same_self(const service_micro::Frame& a, const service_micro::Frame& b) {
        return a.authority == b.authority && a.observed_self == b.observed_self
            && a.observed_readiness == b.observed_readiness && a.position == b.position
            && a.hp == b.hp && a.alive == b.alive && a.stunned == b.stunned
            && a.channel == b.channel && a.can_move == b.can_move
            && a.can_attack == b.can_attack && a.can_cast == b.can_cast;
    }
    static bool same_target(const service_micro::Frame& a, const service_micro::Frame& b) {
        if (a.observed_target != b.observed_target || a.target_id != b.target_id
            || a.target_visible != b.target_visible || a.target_alive != b.target_alive) return false;
        // Dead lifecycle is complete without querying forbidden dead live info.
        return !a.target_alive || (a.target_position == b.target_position
            && a.target_hp == b.target_hp && a.basic_in_range == b.basic_in_range);
    }

public:
    Adapter() = default; // no native calls; constructed only by enabled runtime
    Adapter(const Adapter&) = delete;
    Adapter& operator=(const Adapter&) = delete;

    Authority start_input() {
        const auto before = cambc::round_number();
        const auto own = cambc::detail::sdk_my_champion();
        const auto after = cambc::round_number();
        self_ = {}; camp_count_ = 0; move_ = {};
        if (before != after || own < 0 || after > static_cast<unsigned>(std::numeric_limits<int>::max())) {
            latest_ = {}; return latest_;
        }
        if (owner_ >= 0 && own != owner_) throw std::runtime_error("service native owner changed");
        owner_ = own;
        const int r = static_cast<int>(after);
        if (r < round_) { latest_ = {}; return latest_; }
        if (r != round_) {
            if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
                latest_ = {}; return latest_;
            }
            ++generation_; revision_ = 0; round_ = r;
        }
        if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
            latest_ = {}; return latest_;
        }
        latest_ = {owner_, round_, generation_, ++revision_}; return latest_;
    }

    // Empirical reserve only. Round is the LAST native query before the caller's
    // SDK require_self/action tail. A suspension in that tail remains possible.
    bool current(Authority a) const {
        if (!local(a) || cambc::fuel_remaining() < empirical_dispatch_reserve) return false;
        return cambc::round_number() == static_cast<unsigned>(a.round);
    }

    Self observe_self(Authority a) {
        Self s; s.authority = a;
        if (!epoch(a)) return s;
        const auto id = static_cast<std::uint32_t>(a.owner);
        cambc::detail::require_self(id);
        cambc::detail::check(cambc::detail::sdk_champion(id, &s.raw));
        s.state = cambc::champion_state::from_bits_retain(static_cast<std::uint32_t>(s.raw.state));
        std::uint8_t restrictions{};
        cambc::detail::check(cambc::detail::sdk_champion_restrictions(id, &restrictions));
        s.restrictions = cambc::buff_restrictions::from_bits_retain(restrictions);
        std::array<cambc::detail::buff_data, static_cast<std::size_t>(cambc::detail::buff_kind::count)> buffs{};
        std::uint32_t count{};
        cambc::detail::check(cambc::detail::sdk_champion_buff_data(id, buffs.data(), buffs.size(), &count));
        if (count > buffs.size()) throw std::runtime_error("service native buff count");
        for (std::uint32_t i = 0; i < count; ++i)
            if (buffs[i].type == static_cast<std::uint32_t>(cambc::detail::buff_kind::stun)) s.stunned = true;
        std::uint32_t bag{}, gold{};
        cambc::detail::check(cambc::detail::sdk_champion_gems(id, &bag));
        cambc::detail::check(cambc::detail::sdk_champion_gold(id, &gold));
        s.bag = integer(bag); s.gold = integer(gold);
        if (s.state.contains(cambc::champion_state::recalling)) s.channel = ChannelKind::Recall;
        else if (s.state.contains(cambc::champion_state::submitting_gems)) s.channel = ChannelKind::Submit;
        else if (s.state.contains(cambc::champion_state::teleporting)) s.channel = ChannelKind::Teleport;
        if (s.channel != ChannelKind::None)
            cambc::detail::check(cambc::detail::sdk_champion_channel_time(id, &s.channel_remaining));
        const cambc::vec2 at{s.raw.x, s.raw.y};
        s.at_spawn = cambc::map::is_in_bounds(at) && cambc::map::spawn_region(cambc::team::ally).is_set(at);
        s.observed = epoch(a);
        self_ = s; return s;
    }

    Camp observe_camp(Authority a, const cambc::camp& handle) {
        Camp c; c.authority = a;
        c.id = static_cast<int>(static_cast<cambc::handle_camp_t>(handle));
        if (!epoch(a)) return c;
        std::uint32_t state{};
        if (!visible_status(cambc::detail::sdk_camp_state(static_cast<std::uint32_t>(c.id), &state))) {
            retain(c); return c;
        }
        switch (state) {
        case 0: c.state = cambc::camp_state::dead; break;
        case 1: c.state = cambc::camp_state::alive; break;
        case 2: c.state = cambc::camp_state::asleep; break;
        default: throw cambc::error{cambc::error_code::api_value_out_of_range, "service native camp state"};
        }
        c.alive = state != 0; c.visible = true;
        if (c.alive) {
            if (!visible_status(cambc::detail::sdk_camp(static_cast<std::uint32_t>(c.id), &c.raw))) {
                c.visible = false; retain(c); return c;
            }
            c.live_info = true;
        }
        c.observed = epoch(a);
        if (!c.observed) c.visible = c.live_info = false;
        retain(c); return c;
    }

    const Move& last_move() const { return move_; }

    Move observe_move(Authority a, Position endpoint, bool refusal_prefix_supported = false) {
        using service_micro::MoveLanding;
        Move m; m.authority = a; m.endpoint = endpoint;
        m.refusal_prefix_supported = refusal_prefix_supported;
        move_ = m; // a failed/new observation never leaves an old endpoint live
        if (!epoch(a) || !self_.observed || self_.authority != a) return m;
        m.origin = {self_.raw.x, self_.raw.y};
        const cambc::vec2 origin{m.origin.x, m.origin.y};
        const cambc::vec2 at{endpoint.x, endpoint.y};
        if (!cambc::map::is_in_bounds(origin)) return m;
        if (!cambc::map::is_in_bounds(at) || distance(m.origin, endpoint) != 1 || self_.raw.move_speed < 1) {
            m.observed = epoch(a); m.landing = MoveLanding::KnownBlocked; move_ = m; return m;
        }
        // Exact for this first adjacent-step family: outside the origin's 3x3
        // cannot affect a <=1-step path. Include all eight neighbors (including
        // diagonal corner facts), leave origin free, and let SDK reachability
        // decide membership. This is observed blockage, not hidden emptiness.
        // ImportLinearBuffer is packed row-major LSB bits with NO row padding;
        // it is NOT the engine's chunked raw/native GetVisionBuffer format.
        std::array<std::uint8_t, 1250> packed{}; // supported map <=100x100
        const auto width = cambc::map::width(), height = cambc::map::height();
        const auto bytes = cambc::bitboard::linear_buffer_size();
        if (width <= 0 || height <= 0 || width > 100 || height > 100 || bytes > packed.size()) {
            move_ = m; return m;
        }
        bool endpoint_visible = false, endpoint_blocked = false;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue; // actual own origin must be free
            const cambc::vec2 p{origin.x + dx, origin.y + dy};
            if (!cambc::map::is_in_bounds(p)) continue;
            bool blocked = cambc::map::walls().is_set(p) || cambc::map::holes().is_set(p)
                || cambc::map::altars().is_set(p) || cambc::map::spawn_region(cambc::team::enemy).is_set(p);
            std::int32_t visible{}, champion{}, camp{}, teleport{};
            if (!blocked) {
                if (!visible_status(cambc::detail::sdk_is_visible(p.x, p.y, &visible))) {
                    move_ = m; return m;
                }
                if (visible) {
                    if (!visible_status(cambc::detail::sdk_has_champion(p.x, p.y, &champion))
                        || !visible_status(cambc::detail::sdk_has_camp(p.x, p.y, &camp))
                        || !visible_status(cambc::detail::sdk_has_teleport(p.x, p.y, &teleport))) {
                        move_ = m; return m;
                    }
                    blocked = champion || camp || teleport;
                }
            }
            if (blocked) {
                const auto bit = static_cast<std::size_t>(p.y * width + p.x);
                packed[bit >> 3] |= static_cast<std::uint8_t>(1u << (bit & 7u));
            }
            if (p.x == at.x && p.y == at.y) {
                endpoint_visible = visible != 0; endpoint_blocked = blocked;
            }
        }
        cambc::bitboard blocked{cambc::uninitialized};
        blocked.import_linear_buffer(std::span<const std::uint8_t>{packed.data(), bytes});
        const auto reachable = blocked.reachable_tiles(origin, 1);
        m.reachable = reachable.is_set(at);
        m.observed = epoch(a);
        if (m.observed) {
            m.landing = endpoint_blocked || !m.reachable ? MoveLanding::KnownBlocked
                : endpoint_visible ? MoveLanding::KnownFree : MoveLanding::MayRefuse;
            // Permission to attempt; success and actual position are only the
            // BOOL primitive result plus a fresh post-view. Refusal may retain M
            // natively, but this adapter still consumes the whole input token.
            m.legal = m.reachable && !endpoint_blocked;
        } else m.reachable = false;
        move_ = m; return m;
    }

    service_micro::Decision dispatch(Authority a, const service_micro::Frame& frame,
                                    service_micro::Request request,
                                    const cambc::camp* target = nullptr) {
        using namespace service_micro;
        if (request.kind == Kind::Cast) return gate_.issue(a, frame, request);
        if (!local(a) || !self_.observed || self_.authority != a)
            return missing(Reason::AuthorityMismatch);
        const Camp* camp = nullptr;
        if (request.kind == Kind::Basic) {
            if (!target || static_cast<int>(static_cast<cambc::handle_camp_t>(*target)) != request.target_id)
                return missing(Reason::TargetMismatch);
            camp = selected(request.target_id);
            if (!camp) return missing(Reason::MissingObservation);
        }
        const auto actual = make_frame(self_, camp, request.kind == Kind::Move ? &move_ : nullptr,
                                       frame.observed_domain, frame.domain_supported);
        if (!same_self(frame, actual)) return missing(Reason::InvalidObservation);
        if (request.kind == Kind::Basic && !same_target(frame, actual))
            return missing(Reason::InvalidObservation);
        if (request.kind == Kind::Move && (frame.observed_move != actual.observed_move
            || frame.move_endpoint != actual.move_endpoint || frame.move_legal != actual.move_legal
            || frame.move_landing != actual.move_landing
            || frame.refusal_prefix_supported != actual.refusal_prefix_supported))
            return missing(Reason::InvalidObservation);
        // Resolve cached SDK object before the final fence. No SDK getter,
        // logging or planning is inserted between current() and the primitive.
        auto& actor = cambc::self();
        const auto issued = gate_.issue(a, frame, request);
        if (!issued.issued()) return issued;
        Acceptance acceptance = Acceptance::Invalidated;
        try {
            if (request.kind == Kind::Basic) {
                if (current(a)) { actor.attack(*target); acceptance = Acceptance::Accepted; }
            } else if (request.kind == Kind::Move) {
                const cambc::vec2 endpoint{request.endpoint.x, request.endpoint.y};
                if (current(a)) acceptance = actor.move_absolute(endpoint) ? Acceptance::Accepted : Acceptance::Refused;
            }
        } catch (...) {
            // A thrown action is never success. Consume/clear the pending gate
            // before propagating unexpected API failures to the turn boundary.
            (void)gate_.complete(a, issued.attempt, {Acceptance::Refused}, {});
            throw;
        }
        if (acceptance == Acceptance::Invalidated)
            return gate_.complete(a, issued.attempt, {acceptance}, {});
        try {
            const auto newer = start_input();
            const auto after_self = observe_self(newer);
            Camp after_camp;
            if (request.kind == Kind::Basic && service_micro::valid(newer)) after_camp = observe_camp(newer, *target);
            const auto after = make_frame(after_self, request.kind == Kind::Basic ? &after_camp : nullptr);
            return gate_.complete(newer, issued.attempt, {acceptance}, after);
        } catch (...) {
            (void)gate_.complete(latest_, issued.attempt, {acceptance}, {});
            throw;
        }
    }
};

} // namespace wb::service_native
