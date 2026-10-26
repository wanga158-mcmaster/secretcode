// Bot-side inputs of the economy planner: the static model from the SDK (rules, items, roster designs,
// groups, altars) and its distance fields, computed a fixed number per call so every instance holds the
// same fields at the same round (econ::prepare). The native harness defines static_model() itself.
#include <cambc.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <variant>
#include "econ_model.h"
#include "econ_search.h"
#include "knowledge.h"
#include "world.h"

namespace mb::econ {

namespace {

StaticModel& model_storage()
{
    static StaticModel m;
    return m;
}

int next_field = 0;

std::vector<std::unique_ptr<cambc::distance_field>>& sdk_fields()
{
    static std::vector<std::unique_ptr<cambc::distance_field>> v;
    return v;
}

int sdk_get(int p, int tile)
{
    const auto& f = sdk_fields()[static_cast<std::size_t>(p)];
    const int w = model_storage().width;
    return f ? f->get({ tile % w, tile / w }) : kFar;
}

template <class... F> struct overloaded : F... { using F::operator()...; };
template <class... F> overloaded(F...) -> overloaded<F...>;

Ability decode(const cambc::ability& a)
{
    Ability s;
    const auto& info = a.info();
    if (!info) return s;
    auto common = [&](const cambc::ability_properties& p) {
        s.cooldown = p.max_cooldown;
        s.hits_camps = p.valid_targets.contains(cambc::ability_target_mask::camps) || p.valid_targets.contains(cambc::ability_target_mask::ground);
    };
    std::visit(overloaded{
        [&](const cambc::ability_attack_reset& v) { common(v); s.kind = ab_reset; },
        [&](const cambc::ability_damage& v) { common(v); s.kind = ab_damage; s.modifier = v.modifier; },
        [&](const cambc::ability_aoe_damage& v) { common(v); s.kind = ab_aoe; s.modifier = v.modifier; s.radius = v.radius; s.duration = v.duration; },
        [&](const cambc::ability_cc& v) { common(v); s.kind = ab_cc; s.modifier = v.modifier; s.duration = v.duration; },
        [&](const cambc::ability_heal& v) { common(v); s.kind = ab_heal; s.modifier = v.modifier; },
        [&](const cambc::ability_dash& v) { common(v); s.kind = ab_dash; s.modifier = v.modifier; s.radius = v.radius; s.duration = v.duration; },
        [&](const cambc::ability_shield& v) { common(v); s.kind = ab_shield; s.modifier = v.modifier; s.duration = v.duration; },
    }, *info);
    return s;
}

void build_rules(Rules& r)
{
    namespace k = cambc::constants;
    r.capture_time = k::capture_time;
    r.recall_time = k::recall_time;
    r.respawn_time = k::respawn_time;
    r.winning_gems = static_cast<int>(k::winning_gems);
    r.max_rounds = static_cast<int>(k::max_rounds);
    r.submit_mult = k::submit_gem_duration_multiplier;
    r.submit_base = k::submit_gem_base_duration;
    r.submit_min = k::submit_gem_minimum_duration;
    r.submit_range = k::submit_gems_range;
    r.spawn_healing = k::spawn_healing;
    r.marcella_gold_per_gem = static_cast<float>(k::passive_abilities::marcella_tribute_gold_per_gem);
    // Items come family-major: (shard, emblem) for strength, focus, health.
    const auto count = static_cast<std::uint32_t>(cambc::item_type::count);
    r.n_families = std::min<int>(kMaxFamilies, static_cast<int>(count / 6));
    for (int f = 0; f < r.n_families; ++f)
    {
        auto& fam = r.fam[f];
        bool all_shards_stat_only = true;
        for (int s = 0; s < 3; ++s)
        {
            const auto shard = cambc::item_type{ static_cast<std::uint32_t>(f * 6 + s * 2) }.info();
            const auto emblem = cambc::item_type{ static_cast<std::uint32_t>(f * 6 + s * 2 + 1) }.info();
            fam.shard_pts[s][0] = static_cast<float>(shard.health_points);
            fam.shard_pts[s][1] = static_cast<float>(shard.strength_points);
            fam.shard_pts[s][2] = static_cast<float>(shard.focus_points);
            fam.emblem_pts[s][0] = static_cast<float>(emblem.health_points);
            fam.emblem_pts[s][1] = static_cast<float>(emblem.strength_points);
            fam.emblem_pts[s][2] = static_cast<float>(emblem.focus_points);
            if (s == 0)
            {
                r.shard_cost = static_cast<int>(shard.cost);
                fam.passive = emblem.passive ? static_cast<std::uint8_t>(*emblem.passive) : 0;
                fam.magnitude = emblem.passive_magnitude;
            }
            if (!shard.purchasable) all_shards_stat_only = false;
        }
        fam.valid = all_shards_stat_only;
        switch (fam.passive)
        {
        case fp_monster: r.fam_monster = f; break;
        case fp_durability: r.fam_durability = f; break;
        case fp_treasure: r.fam_treasure = f; break;
        case fp_jungle: r.fam_jungle = f; break;
        case fp_home: r.fam_home = f; break;
        case 0:
            // Power: the emblem adds only its own stat (6); Bruiser: 5 plus one of each other stat.
            if (fam.emblem_pts[0][0] == 0 && fam.emblem_pts[0][2] == 0) { if (r.fam_power < 0) r.fam_power = f; }
            else if (r.fam_bruiser < 0) r.fam_bruiser = f;
            break;
        default: break;
        }
    }
}

Design design_of(cambc::champion_type type)
{
    const auto& c = cambc::champion_designs[static_cast<std::size_t>(type)];
    const auto st = c.base_stats();
    Design d;
    d.type = static_cast<int>(type);
    d.hp = st.base_health;
    d.str = st.base_strength;
    d.foc = st.base_focus;
    d.ghp = st.health_growth;
    d.gstr = st.strength_growth;
    d.gfoc = st.focus_growth;
    d.range = st.base_attack_range;
    d.melee = st.base_attack_range <= 1;
    d.camp_mult = st.camp_damage_multiplier > 0 ? st.camp_damage_multiplier : 1.0f;
    const auto abilities = c.abilities();
    for (int i = 0; i < 2; ++i) d.ab[i] = decode(abilities[static_cast<std::size_t>(i)]);
    namespace pa = cambc::constants::passive_abilities;
    d.passive = static_cast<std::uint8_t>(c.passive());
    switch (c.passive())
    {
    case cambc::passive_type::bonus_gold: d.passive_mag = static_cast<float>(pa::vesper_vale_bonus_gold_percent) / 100.0f; break;
    case cambc::passive_type::health_strength: d.passive_mag = pa::ryth_health_strength_ratio; break;
    case cambc::passive_type::missing_health_damage: d.passive_mag = pa::heart_of_ruin_missing_health_damage_max_bonus; break;
    default: break;
    }
    return d;
}

void build(StaticModel& m)
{
    const World& w = World::get();
    build_rules(m.rules);
    m.width = w.width;
    m.height = w.height;
    const int n = m.width * m.height;
    m.blocked_bits.assign(cambc::bitboard::linear_buffer_size(), 0);
    w.blocked.export_linear_buffer(m.blocked_bits);
    m.camp_tile.assign(static_cast<std::size_t>(n), -1);
    m.groups.clear();
    for (std::size_t gi = 0; gi < w.groups.size(); ++gi)
    {
        const auto& info = w.groups[gi];
        Group g;
        g.gold = static_cast<int>(info.gold);
        g.gems = static_cast<int>(info.gems);
        g.respawn = info.respawn;
        for (std::size_t k = 0; k < info.tiles.size(); ++k)
        {
            const auto t = info.tiles[k];
            const int type = info.types[k];
            const auto& cs = w.camp_stats[type < 6 ? type : 0];
            Member mm;
            mm.x = t.x;
            mm.y = t.y;
            mm.hp = cs.health;
            mm.str = cs.strength;
            mm.range = cs.range;
            mm.type = type;
            g.members.push_back(mm);
            const int tile = t.y * m.width + t.x;
            g.tiles.push_back(tile);
            m.camp_tile[static_cast<std::size_t>(tile)] = static_cast<std::int16_t>(gi);
        }
        finish_group(g);
        m.groups.push_back(std::move(g));
    }
    m.altars.clear();
    for (auto a : w.altars) m.altars.push_back(a.y * m.width + a.x);
    m.spawn.clear();
    for (auto s : w.ally_spawn) m.spawn.push_back(s.y * m.width + s.x);
    m.roster.clear();  // from the snapshot's types (model_for), so the draft needs no Knowledge
    m.n_points = 1 + m.G() + m.A();
    m.fields.assign(static_cast<std::size_t>(m.n_points), {});
    m.have.assign(static_cast<std::size_t>(m.n_points), 0);
    m.getter = &sdk_get;
    sdk_fields().resize(static_cast<std::size_t>(m.n_points));
    finish_distances(m);
}

} // namespace

const StaticModel* static_model()
{
    static bool built = false;
    if (!built)
    {
        build(model_storage());
        built = true;
    }
    return &model_storage();
}

const StaticModel* model_for(const Snapshot& s)
{
    StaticModel& m = const_cast<StaticModel&>(*static_model());
    bool same = m.roster.size() == s.champs.size();
    for (std::size_t i = 0; same && i < s.champs.size(); ++i) same = m.roster[i].type == s.champs[i].type;
    if (!same)
    {
        m.roster.clear();
        for (std::size_t i = 0; i < s.champs.size(); ++i)
        {
            int type = s.champs[i].type;
            if (type < 0 && i < Knowledge::get().allies.size()) type = static_cast<int>(Knowledge::get().allies[i].champion_name());
            if (type < 0 || type >= static_cast<int>(std::size(cambc::champion_designs))) type = 0;
            m.roster.push_back(design_of(static_cast<cambc::champion_type>(type)));
        }
    }
    return &m;
}

// Compute up to `max_fields` more point fields (fixed order: spawn, groups, altars) and refresh their
// distance columns. Returns true when every field exists. Call with the same quota every turn on every
// instance so plans agree. Walls and roots are bitboard operations; fields stay SDK objects read with
// get() (no linear exports).
bool prepare(int max_fields)
{
    StaticModel& m = const_cast<StaticModel&>(*static_model());
    if (next_field >= m.n_points) return true;
    const int G = m.G();
    const std::size_t bytes = cambc::bitboard::linear_buffer_size();
    auto pack = [&](const std::vector<int>& tiles, cambc::bitboard& out) {
        std::vector<std::uint8_t> bits(bytes, 0);
        for (int t : tiles) bits[static_cast<std::size_t>(t / 8)] |= static_cast<std::uint8_t>(1u << (t % 8));
        out.import_linear_buffer(bits);
    };
    static cambc::bitboard base;  // blocked tiles plus every dangerous camp tile
    if (next_field == 0)
    {
        std::vector<int> danger;
        for (const auto& g : m.groups)
            if (!g.harmless) danger.insert(danger.end(), g.tiles.begin(), g.tiles.end());
        cambc::bitboard d;
        pack(danger, d);
        base = World::get().blocked | d;
    }
    for (int done = 0; done < max_fields && next_field < m.n_points; ++done, ++next_field)
    {
        const int p = next_field;
        std::vector<int> roots;
        if (p == 0) roots = m.spawn;
        else if (p <= G) roots = m.groups[static_cast<std::size_t>(p - 1)].tiles;
        else
        {
            const int a = m.altars[static_cast<std::size_t>(p - 1 - G)], r = m.rules.submit_range, ax = a % m.width, ay = a / m.width;
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                {
                    const int x = ax + dx, y = ay + dy;
                    if (x < 0 || y < 0 || x >= m.width || y >= m.height) continue;
                    const int t = y * m.width + x;
                    if (!m.is_blocked(t) && m.camp_tile[static_cast<std::size_t>(t)] < 0) roots.push_back(t);
                }
        }
        cambc::bitboard rb, visited;
        pack(roots, rb);
        auto f = std::make_unique<cambc::distance_field>();
        if (p >= 1 && p <= G && !m.groups[static_cast<std::size_t>(p - 1)].harmless)
            f->compute(base & ~rb, rb, visited);  // the target's own tiles are open
        else
            f->compute(base, rb, visited);
        sdk_fields()[static_cast<std::size_t>(p)] = std::move(f);
        m.have[static_cast<std::size_t>(p)] = 1;
        // The spawn field opens the spawn column; every later field adds its own column.
        update_column(m, p);
    }
    return next_field >= m.n_points;
}

const cambc::distance_field* point_field(int p)
{
    auto& v = sdk_fields();
    return p >= 0 && p < static_cast<int>(v.size()) ? v[static_cast<std::size_t>(p)].get() : nullptr;
}

} // namespace mb::econ
