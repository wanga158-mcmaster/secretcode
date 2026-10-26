"""SDK hosts for the decision suite's fixtures (ported from the plan3 SDK-host runners).

A host fixture includes cambc.h with __wasm__ undefined, so every SDK import becomes an extern "C"
function that the fixture defines. host/sdk-shim.h holds the shared text of an explicit 8x8 world;
host/public-config.h holds captured public designs and catalogues as raw ABI bytes. Each builder
below writes one fixture family's generated headers into `out` by exact, count-checked seam
replacements, so a changed seam fails loudly instead of silently testing something else.
Generated files are scratch: never write them into tests/ (quoted includes would pick them up).
"""
from __future__ import annotations

import hashlib
import json
import re
import shutil
import struct
from pathlib import Path

TESTS = Path(__file__).resolve().parent
SUITE = TESTS.parent
ROOT = SUITE.parents[1]
HOST = TESTS / "host"
CATALOG = ROOT / "docs/reference/runtime-catalog.json"
GEOMETRY = ROOT / "maps/geometry.json"


def need(ok, message: str) -> None:
    if not ok:
        raise ValueError(message)


def read(path: Path) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(out: Path, name: str, text: str) -> Path:
    target = Path(out) / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8", newline="\n")
    return target


def function(text: str, name: str, body: str) -> str:
    """Replace the body of the first function `name(` with `body`."""
    at = text.index(name + "(")
    begin = text.index("{", at)
    end, depth = begin + 1, 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[:begin] + "{" + body + "\n}" + text[end:]


def body(text: str, name: str, content: str) -> str:
    """Replace the body of the function whose header is `name(...)  {` (word-bounded)."""
    match = re.search(r"\b" + re.escape(name) + r"\([^;{}]*\)\s*\{", text)
    need(match, "Missing host function " + name)
    start = match.end()
    end, depth = start, 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[:start] + "\n" + content + "\n" + text[end - 1:]


def replace1(text: str, old: str, new: str) -> str:
    need(text.count(old) == 1, "Host seam changed: " + old[:80])
    return text.replace(old, new, 1)


def rename_main(text: str, old: str, new: str) -> str:
    """A fixture reused as a header keeps its scenarios but loses its main."""
    return replace1(text, old, new)


# ----------------------------------------------------------------------------- shared inputs
def config() -> str:
    """Captured public settings/designs/catalogues (raw ABI byte tables in namespace bp_config)."""
    text = read(HOST / "public-config.h")
    return text[text.index("#pragma once"):]


def shim() -> str:
    text = read(HOST / "sdk-shim.h")
    return text[text.index('extern "C" void* __cxa_allocate_exception'):]


def base_host(indexer: bool = False) -> str:
    """The 3-group backpressure host (env P3_BP_*) or the 20-group indexer host (env P3_IDX_*)."""
    groups, env = ("20", "P3_IDX") if indexer else ("3", "P3_BP")
    text = shim()
    need("std::array<unsigned,2> group_state{2,2};" in text, "Host seam changed")
    text = text.replace("std::array<unsigned,2> group_state{2,2};", f"std::array<unsigned,{groups}> group_state{{2,1,1}};")
    text = text.replace("group_state={2,2};", "group_state={2,1,1};").replace("verify(group<2", f"verify(group<{groups}")
    # Function-local construction makes eager SDK initialization recording safe.
    old = "std::vector<unsigned> query_trace;std::vector<std::int64_t> fuel_trace;"
    need(old in text, "Host trace declaration changed")
    text = text.replace(old, "std::vector<unsigned>& bp_trace(){static auto* p=new std::vector<unsigned>;return *p;}\nstd::vector<unsigned>& query_trace=bp_trace();std::vector<std::int64_t> fuel_trace;")
    text = text.replace("int forced_champion=-1;unsigned metadata_removed=0,metadata_pairs=0,constructor_cases=0,dynamic_cases=0,nonopt_cases=0;", "int forced_champion=-1;")
    text = text.replace("bool allow_elision=true;", "")
    text = text.replace("query_trace.push_back(h);", "bp_trace().push_back(h);")
    text = text.replace("namespace {\n", "namespace {\nunsigned bp_side(){const char* value=std::getenv(\"" + env + "_SIDE\");if(!value || (std::strcmp(value,\"0\") && std::strcmp(value,\"1\")))std::abort();return unsigned(value[0]-'0');}\nint bp_axis(int value){return bp_side()?7-value:value;}\nstd::uint64_t bp_camp_mask();\nextern int cost_zero_item;\nextern bool bp_hidden_collision;\n", 1)
    if indexer:
        text = text.replace("extern bool bp_hidden_collision;", "extern bool bp_hidden_collision;\nextern const unsigned* indexer_terminal_counter;\nextern unsigned indexer_drop_after;")
        need(text.count("++fuel_reads;") == 1, "Fuel seam changed")
        text = text.replace("++fuel_reads;", "++fuel_reads;if(indexer_terminal_counter && *indexer_terminal_counter>=indexer_drop_after)fuel=50000;")
    move = "    actor.x=x;actor.y=y;actor.state&=~(1<<3);++moves;*success=1;"
    need(text.count(move) == 1, "Move failure seam changed")
    text = text.replace(move, "    if(bp_hidden_collision){*success=0;return 0;}\n" + move)
    respawn = 'if(const char* value=std::getenv("P3_IDX_RESPAWN"))out->respawn_time=std::atoi(value);' if indexer else ""
    text = function(text, "sdk_get_settings", 'record("sdk_get_settings");static_assert(sizeof(*out)==120);std::memcpy(out,bp_config::settings[0].data(),sizeof(*out));' + respawn + "return 0;")
    text = function(text, "sdk_my_team", 'record("sdk_my_team");return int(bp_side());')
    text = function(text, "sdk_bc_submitted_gems", 'record("sdk_bc_submitted_gems");verify(team<2);*out=team==bp_side()?own_score:200;return 0;')
    text = function(text, "sdk_champion_design", 'record("sdk_champion_design");verify(type>=0 && type<12);++design_reads;if(cutoff_design_read && design_reads>=cutoff_design_read)fuel=44000;static_assert(sizeof(*out)==128);std::memcpy(out,bp_config::champions[unsigned(type)].data(),sizeof(*out));return 0;')
    text = text.replace("sdk_champion_design(std::int32_t,champion_design* out)", "sdk_champion_design(std::int32_t type,champion_design* out)")
    text = function(text, "sdk_champion_ability_props", 'record("sdk_champion_ability_props");verify(id<3 && slot<2);const unsigned type=id==0?unsigned(actor.type):id==1?11u:2u;champion_design design{};std::memcpy(&design,bp_config::champions[type].data(),sizeof(design));*out=design.definition.abilities[slot];return 0;')
    text = text.replace("sdk_champion_ability_props(std::uint32_t,std::uint32_t,ability_props* out)", "sdk_champion_ability_props(std::uint32_t id,std::uint32_t slot,ability_props* out)")
    text = function(text, "sdk_consumable_info", 'record("sdk_consumable_info");verify(id==0 || id==2);static_assert(sizeof(*out)==8);std::memcpy(out,bp_config::consumables[id==0?0:1].data(),sizeof(*out));return 0;')
    text = text.replace("sdk_consumable_info(std::uint32_t,consumable_info* out)", "sdk_consumable_info(std::uint32_t id,consumable_info* out)")
    text = function(text, "sdk_item_info", 'record("sdk_item_info");verify(id<42);static_assert(sizeof(*out)==40);std::memcpy(out,bp_config::items[id].data(),sizeof(*out));const char* bad=std::getenv("' + env + '_BAD_COST");if(int(id)==cost_zero_item || (bad && id==unsigned(std::atoi(bad))))out->cost=0;return 0;')
    text = function(text, "sdk_bc_map_mask", 'record("sdk_bc_map_mask");verify(kind>=0 && kind<=3);*out=0;if(kind==3)set(out,bp_axis(2),bp_axis(3));return 0;')
    text = text.replace("sdk_bc_map_mask(std::int32_t,std::uint64_t* out)", "sdk_bc_map_mask(std::int32_t kind,std::uint64_t* out)")
    text = function(text, "sdk_camp_occupancy", 'record("sdk_camp_occupancy");*out=bp_camp_mask();return 0;')
    if indexer:
        text = function(text, "sdk_camp_group_cooldown", 'record("sdk_camp_group_cooldown");verify(group<20 && group_state[group]==3);*out=20;return 0;')
    return "// Pinned V17 SDK-host extraction; only explicit recorded payload/3-group/trace-lifetime seams replaced.\n" + text


FUEL_SEAM = 'std::int64_t sdk_fuel_remaining() { record("sdk_fuel_remaining"); ++fuel_reads;'
PREDICTION_FUEL = 'if(prediction_fuel_probe){const auto remaining=prediction_fuel_probe();if(remaining>=0){fuel_trace.push_back(remaining);return remaining;}}'
DUMMY_DESIGN = 'record("sdk_champion_design");verify(type>=0 && type<13);*out={};return 0;'
# SDK 3.0.4 caches all 13 champion designs during static initialization; the captured table has
# 12, so the 13th gets a zero placeholder that is legal only before the first fixture case.
DESIGN_12 = ("verify(type>=0 && type<12);++design_reads;",
             "verify(type>=0 && type<13);if(type==12){verify(cases==0);*out={};return 0;}++design_reads;")
RESPAWN = ("verify(group<3 && group_state[group]==3);*out=60;return 0;",
           "verify(group<3 && group_state[group]==3);*out=sortie_dynamic_respawn?int(respawn_count[group]):60;return 0;")
COLLISION = ("actor.x=x;actor.y=y;actor.state&=~((1<<3)|(1<<4));++moves;++actions;*success=1;return 0;",
             "if(bp_hidden_collision){*success=0;return 0;}actor.x=x;actor.y=y;actor.state&=~((1<<3)|(1<<4));++moves;++actions;*success=1;return 0;")
CHAMPION_PROBE = ("*out=id?enemy:actor;return 0;", "*out=id?enemy:actor;if(sortie_champion_probe)return sortie_champion_probe(id,out);return 0;")
ALIVE_PROBE = ("*out=(id?enemy:actor).state&1;return 0;", "*out=(id?enemy:actor).state&1;if(sortie_alive_probe)sortie_alive_probe();return 0;")


def eager_designs(text: str) -> str:
    return replace1(text, *DESIGN_12)


def current_varg(text: str) -> str:
    """Swap captured champion 10 (Varg) for the current official record (STR40, reset/shield kit)."""
    varg = json.loads(read(CATALOG))["result"]["champions"][10]
    need(varg["name"] == "varg" and varg["stats"]["base_strength"] == 40, "Current Varg record changed")
    need(varg["abilities"][0]["kind"] == 1 and varg["abilities"][1]["payload"] == {"range": 4, "modifier": 2.5, "duration": 5}, "Current Varg kit changed")
    raw = bytes.fromhex(varg["rawHex"])
    need(len(raw) == 128, "Champion ABI size changed")
    line = next(line for line in text.splitlines() if "> champions" in line)
    entries = list(re.finditer(r"\{\{([0-9,]+)\}\}", line))
    need(len(entries) == 12, "Captured champion table changed")
    entry = entries[10]
    return text.replace(line, line[:entry.start()] + "{{" + ",".join(map(str, raw)) + "}}" + line[entry.end():], 1)


# ----------------------------------------------------------------------------- single-header hosts
def capital_cache(out: Path) -> None:
    write(out, "backpressure-config-v1.h", config())
    text = function(base_host(), "sdk_champion_design", 'record("sdk_champion_design");verify(type>=0 && type<13);*out={};out->base_stats={8,1,1,1000,100,10,10,0,20,1};return 0;')
    write(out, "backpressure-host-v1.h", function(text, "sdk_fuel_remaining", 'record("sdk_fuel_remaining");return cache_fuel();'))


def executor(out: Path) -> None:
    # Balance metadata is unexercised by navigation; the SDK eagerly loads 13 champions.
    write(out, "backpressure-config-v1.h", config())
    write(out, "backpressure-host-v1.h", function(base_host(), "sdk_champion_design", DUMMY_DESIGN))


def sortie_forecast(out: Path) -> None:
    write(out, "backpressure-config-v1.h", config())
    text = function(base_host(), "sdk_champion_design", DUMMY_DESIGN)
    write(out, "backpressure-host-v1.h", function(text, "sdk_get_settings", "std::memcpy(out,bp_config::settings[0].data(),sizeof(*out));out->spawn_healing=.1;return 0;"))


def two_trip_observation(out: Path) -> None:
    write(out, "backpressure-config-v1.h", config())
    text = read(TESTS / "plan3-backpressure-extra-host-v1.h")
    text = function(text, 'sdk_camp_state', '''record("sdk_camp_state");verify(id<9);
                member_state_calls.push_back(id);verify(all_public_spawns_visible);
                if(int(id)==hidden_state_id)return int(error_code::api_not_in_vision);
                if(int(id)==missing_state_id)return int(error_code::api_not_found);
                verify(tile_visible(id));*out=member_states[id];maybe_change_round();return 0;''')
    text = function(text, 'sdk_camp', '''record("sdk_camp");verify(id<9);
                member_info_calls.push_back(id);verify(all_public_spawns_visible && tile_visible(id));
                if(int(id)==hidden_info_id)return int(error_code::api_not_in_vision);
                if(int(id)==missing_info_id)return int(error_code::api_not_found);
                if(member_states[id]==0)return int(error_code::api_not_found);
                *out={};out->x=bp_axis(bp_x[id]);out->y=bp_axis(bp_y[id]);
                out->health=member_health[id];out->max_health=member_max_health[id];
                out->strength=member_strength[id];out->focus=member_focus[id];maybe_change_round();return 0;''')
    text = function(text, 'sdk_map_camp_info', '''record("sdk_map_camp_info");verify(id<9);
                *out={bp_axis(bp_x[id]),bp_axis(bp_y[id]),bp_kind(id)};
                if(int(id)==public_mismatch_id)++out->x;
                if(int(id)==round_change_public_id)++current_round;return 0;''')
    text = function(text, 'sdk_map_camp_group_of', '''record("sdk_map_camp_group_of");verify(id<9);
                *out=id/3+unsigned(int(id)==public_group_mismatch_id);return 0;''')
    write(out, "two-trip-observation-host.h", text)
    text = function(base_host(), 'sdk_camp_group_state', '''record("sdk_camp_group_state");verify(group<3);
                ++group_state_calls;verify(all_public_spawns_visible);*out=group_state[group];maybe_change_round();return 0;''')
    write(out, "backpressure-host-v1.h", eager_designs(text))


def backpressure_current(out: Path) -> None:
    """Base host with the prediction fuel probe; the shared Campaign renamed as the policy reference."""
    write(out, "backpressure-config-v1.h", config())
    write(out, "backpressure-host-v1.h", eager_designs(replace1(base_host(), FUEL_SEAM, FUEL_SEAM + PREDICTION_FUEL)))
    text = read(SUITE / "common/runtime.h")
    start = text.index("class Campaign ")
    end = text.index("\n};", start) + 4
    original = text[start:end]
    renamed = re.sub(r"\bCampaign\b", "BaselineCampaign", original)
    need(renamed.replace("BaselineCampaign", "Campaign") == original, "Reference extraction changed source")
    # The shared Campaign now also needs the policy events and the sortie runtime, which the
    # forked Backpressure runtime does not include.
    includes = "".join('#include ' + _absolute("common") + name + '"\n' for name in ("policy.h", "sortie-forecast.h"))
    write(out, "backpressure-reference-v1.h", includes + "namespace plan3_runtime {\n" + renamed + "\n}\n")


def indexer_current(out: Path) -> None:
    write(out, "indexer-config-v1.h", config())
    text = eager_designs(replace1(base_host(indexer=True), FUEL_SEAM, FUEL_SEAM + PREDICTION_FUEL))
    text = text.replace('unsigned bp_side(){', 'unsigned current_actor_type(){const char* p=std::getenv("P3_IDX_ACTOR");if(!p)std::abort();if(!std::strcmp(p,"tavi"))return unsigned(cambc::champion_type::tavi);if(!std::strcmp(p,"varg"))return unsigned(cambc::champion_type::varg);std::abort();}\nunsigned bp_side(){', 1)
    text = function(text, 'sdk_champion', 'record("sdk_champion");verify(id<2);*out=id?enemy:actor;if(id==0){out->type=int(current_actor_type());out->team=int(bp_side());}return 0;')
    text = function(text, 'sdk_champion_identity', 'record("sdk_champion_identity");verify(id<2);++identity_reads;if(fail_identity_read&&identity_reads==fail_identity_read)return 1;*out=id?champion_identity{enemy.team,enemy.type}:champion_identity{int(bp_side()),int(current_actor_type())};return 0;')
    text = function(text, 'sdk_champion_ability_props', 'record("sdk_champion_ability_props");verify(id<3 && slot<2);const unsigned type=id==0?current_actor_type():id==1?11u:2u;champion_design d{};std::memcpy(&d,bp_config::champions[type].data(),sizeof(d));*out=d.definition.abilities[slot];return 0;')
    write(out, "indexer-host-v1.h", text)
    text = read(TESTS / "plan3-indexer-extra-host-v1.h")
    text = text.replace('std::int32_t sdk_champion_can_shop(std::uint32_t id,std::int32_t,', 'std::int32_t sdk_champion_can_shop(std::uint32_t id,std::int32_t consumable,', 1)
    prefix = '''
namespace {
bool all_purchases_on_spawn=true;
bool modeled_on_spawn(){return actor.x==bp_axis(0) && actor.y==bp_axis(0);}
bool modeled_can_shop(bool consumable){return !(actor.state&1) || modeled_on_spawn() || (!consumable && current_actor_type()==unsigned(cambc::champion_type::tavi));}
void sync_equipment_stats(){
    cambc::detail::champion_design d{};std::memcpy(&d,bp_config::champions[current_actor_type()].data(),sizeof(d));
    double hp=d.base_stats.base_health,str=d.base_stats.base_strength,focus=d.base_stats.base_focus;
    for(unsigned i=0;i<items.size();++i)if(items[i]){
        const auto m=cambc::item_type{static_cast<cambc::item_type::enum_type>(i)}.info();
        hp+=items[i]*m.health_points*d.base_stats.health_growth;
        str+=items[i]*m.strength_points*d.base_stats.strength_growth;
        focus+=items[i]*m.focus_points*d.base_stats.focus_growth;
    }
    actor.health=actor.max_health=hp;actor.strength=str;actor.focus=focus;
}
}
'''
    text = prefix + text
    text = function(text, 'sdk_champion_can_shop', 'record("sdk_champion_can_shop");verify(id==0 && (consumable==0 || consumable==1));*out=modeled_can_shop(consumable!=0);return 0;')
    text = function(text, 'sdk_buy_item', '''record("sdk_buy_item");verify(type<items.size() && count>0 && modeled_can_shop(false));
        const auto m=cambc::item_type{static_cast<cambc::item_type::enum_type>(type)}.info();
        verify(m.purchasable && std::uint64_t(m.cost)*count<=gold);all_purchases_on_spawn&=modeled_on_spawn();gold-=m.cost*count;items[type]+=count;
        if(m.result && m.combine_count){const unsigned n=items[type]/m.combine_count;items[type]%=m.combine_count;items[unsigned(*m.result)]+=n;}
        sync_equipment_stats();++buys;++actions;return 0;''')
    text = function(text, 'sdk_submit_gems', 'record("sdk_submit_gems");verify(x==bp_axis(2) && y==bp_axis(3) && bag>0 && (actor.state&1) && !(actor.state&(1<<2)) && std::max(std::abs(x-actor.x),std::abs(y-actor.y))<=int(cambc::constants::submit_gems_range));++submissions;++actions;actor.state|=1<<2;return 0;')
    text = function(text, 'sdk_recall', 'record("sdk_recall");verify((actor.state&1) && !(actor.state&((1<<1)|(1<<2)|(1<<9)|(1<<12))));++recalls;++actions;actor.state|=1<<1;return 0;')
    write(out, "indexer-current-host.h", text)


# ----------------------------------------------------------------------------- two-trip chain
def two_trip_runtime(out: Path) -> None:
    """Modeled 8x8 transitions for actual Campaign callbacks (plan3-two-trip-runtime-sdk.cpp)."""
    write(out, "backpressure-config-v1.h", config())
    text = replace1(base_host(), FUEL_SEAM, FUEL_SEAM + 'if(optional_fuel_probe){const auto sample=optional_fuel_probe();if(sample>=0){fuel_trace.push_back(sample);return sample;}}')
    # The pinned wrapper allocates ceil(tile_count/8), contradicting its
    # one-byte/tile @param prose. Match actual wrapper capacity.
    text = function(text, 'sdk_Bitboard_ExportLinearBuffer', '''record("sdk_Bitboard_ExportLinearBuffer");verify(size==8);
                std::memcpy(bytes,in,size);return 0;''')
    # The SDK eagerly caches self ability metadata before main. Identity
    # is fixed for the WASI instance, including that startup query.
    text = function(text, 'sdk_champion_ability_props', '''record("sdk_champion_ability_props");verify(id<3 && slot<2);
                const char* name=std::getenv("P3_BP_ACTOR");const unsigned self_type=name && !std::strcmp(name,"varg")?10u:11u;
                const unsigned type=id==0?self_type:id==1?11u:2u;champion_design design{};
                std::memcpy(&design,bp_config::champions[type].data(),sizeof(design));*out=design.definition.abilities[slot];return 0;''')
    text = function(text, 'sdk_move_to', '''record("sdk_move_to");
                verify((actor.state&1) && (actor.state&(1<<3)) && !(actor.state&((1<<1)|(1<<2)|(1<<12))));
                verify(x>=0 && x<8 && y>=0 && y<8 && std::max(std::abs(x-actor.x),std::abs(y-actor.y))==1);
                verify(!occupied_by_living_camp(x,y) && !(x==bp_axis(7) && y==bp_axis(7)) && !(x==bp_axis(2) && y==bp_axis(3)));
                actor.x=x;actor.y=y;actor.state&=~((1<<3)|(1<<4));++moves;++actions;*success=1;return 0;''')
    write(out, "backpressure-host-v1.h", text)
    text = read(TESTS / "plan3-backpressure-extra-host-v1.h")
    text = function(text, 'bp_camp_mask', '''std::uint64_t result=0;
                for(unsigned id=0;id<9;++id)if(member_states[id] && tile_visible(id))set(&result,bp_axis(bp_x[id]),bp_axis(bp_y[id]));
                return result;''')
    text = function(text, 'sdk_camp_at', '''record("sdk_camp_at");
                for(unsigned id=0;id<9;++id)if(member_states[id] && tile_visible(id) && x==bp_axis(bp_x[id]) && y==bp_axis(bp_y[id])){*out=id;return 0;}
                return int(error_code::api_not_found);''')
    text = function(text, 'sdk_camp_state', '''record("sdk_camp_state");verify(id<9);++camp_state_calls[id];
                if(id==camp_state_error_id)return camp_state_error_code;
                if(!tile_visible(id))return int(error_code::api_not_in_vision);*out=member_states[id];return 0;''')
    text = function(text, 'sdk_camp', '''record("sdk_camp");verify(id<9);
                if(!tile_visible(id))return int(error_code::api_not_in_vision);
                if(!member_states[id])return int(error_code::api_not_found);
                camp_design design{};std::memcpy(&design,bp_config::camps[unsigned(bp_kind(id))].data(),sizeof(design));
                *out={};out->x=bp_axis(bp_x[id]);out->y=bp_axis(bp_y[id]);out->health=member_health[id];
                out->max_health=design.base_stats.base_health;out->strength=design.base_stats.base_strength;out->focus=design.base_stats.base_focus;return 0;''')
    text = function(text, 'sdk_champion_can_shop', '''record("sdk_champion_can_shop");verify(id==0);
                *out=!(actor.state&1) || on_spawn() || (!consumable && actor.type==int(cambc::champion_type::tavi));return 0;''')
    text = text.replace('sdk_champion_can_shop(std::uint32_t id,std::int32_t,', 'sdk_champion_can_shop(std::uint32_t id,std::int32_t consumable,', 1)
    text = function(text, 'sdk_buy_item', '''record("sdk_buy_item");verify(type<items.size() && count && (on_spawn() || actor.type==int(cambc::champion_type::tavi)));
                const auto info=cambc::item_type{static_cast<cambc::item_type::enum_type>(type)}.info();
                verify(info.purchasable && std::uint64_t(info.cost)*count<=gold);gold-=info.cost*count;items[type]+=count;
                if(info.result && info.combine_count){const unsigned n=items[type]/info.combine_count;items[type]%=info.combine_count;items[unsigned(*info.result)]+=n;}
                sync_stats();++buys;++actions;return 0;''')
    text = function(text, 'sdk_recall', '''record("sdk_recall");verify((actor.state&1) && !(actor.state&((1<<1)|(1<<2)|(1<<12))));
                verify(current_incoming()==0);recall_timer=unsigned(cambc::constants::recall_time);actor.state|=1<<1;actor.state&=~((1<<3)|(1<<4));++recalls;++actions;return 0;''')
    text = function(text, 'sdk_submit_gems', '''record("sdk_submit_gems");verify((actor.state&1) && bag && !(actor.state&((1<<1)|(1<<2)|(1<<12))) &&
                x==bp_axis(2) && y==bp_axis(3) && std::max(std::abs(x-actor.x),std::abs(y-actor.y))<=int(cambc::constants::submit_gems_range));
                const auto ticks=plan3::tribute_completion_turns(int(bag),cambc::constants::submit_gem_duration_multiplier,cambc::constants::submit_gem_base_duration,cambc::constants::submit_gem_minimum_duration);
                verify(ticks && *ticks>0);tribute_timer=unsigned(*ticks);
                actor.state|=1<<2;actor.state&=~((1<<3)|(1<<4));++submissions;++actions;return 0;''')
    write(out, "two-trip-runtime-host.h", text)


def two_trip_direct(out: Path) -> None:
    """two_trip_runtime for the direct two-trip fixture under the current SDK (13 designs)."""
    two_trip_runtime(out)
    write(out, "backpressure-host-v1.h", eager_designs(read(out / "backpressure-host-v1.h")))


def sortie_runtime(out: Path) -> None:
    """two_trip_runtime plus sortie seams (plan3-sortie-runtime-current.cpp)."""
    two_trip_runtime(out)
    text = read(out / "backpressure-host-v1.h")
    for old, new in (DESIGN_12, RESPAWN, COLLISION, CHAMPION_PROBE, ALIVE_PROBE):
        text = replace1(text, old, new)
    write(out, "backpressure-host-v1.h", text)


def enemy(out: Path) -> None:
    """sortie_runtime plus a personally visible enemy through the real SDK enumeration
    (Citadel, Hindsight, Underwriter and Ledger-nominal actual-caller fixtures)."""
    sortie_runtime(out)
    text = read(out / "two-trip-runtime-host.h")
    text = function(text, 'sdk_num_enemies', 'record("sdk_num_enemies");*out=show_enemy?1u:0u;return 0;')
    text = replace1(text, 'sdk_enemies(std::uint32_t*,std::uint32_t,std::uint32_t* count)', 'sdk_enemies(std::uint32_t* ids,std::uint32_t capacity,std::uint32_t* count)')
    text = function(text, 'sdk_enemies', 'record("sdk_enemies");*count=show_enemy?1u:0u;if(show_enemy){verify(capacity>=1);ids[0]=1;}return 0;')
    write(out, "two-trip-runtime-host.h", text)


VARG_CAST = '''record("sdk_cast");if(actor.type==10) {
        verify(slot<2 && (actor.state&(1<<11)) && !cooldown[slot] && !(actor.state&((1<<1)|(1<<2)|(1<<12))));
        champion_design d{};std::memcpy(&d,bp_config::champions[10].data(),sizeof(d));const auto& a=d.definition.abilities[slot];
        verify((a.targets&ability_target_self) && kind==0 && id==0 && x==-1 && y==-1);
        if(slot==0){verify(a.kind==1);actor.state|=1<<4;}
        else {verify(a.kind==7 && a.payload.shield.modifier==2.5f && a.payload.shield.duration==5);
            observation_shield=actor.focus*a.payload.shield.modifier;observation_shield_ticks=unsigned(a.payload.shield.duration);}
        actor.state&=~(1<<11);cooldown[slot]=unsigned(a.cooldown);*success=1;++casts;++actions;return 0;
    }verify(actor.type==11'''
CAMP_PROBE = 'record("sdk_camp");verify(id<9);'


def _varg_two_trip_copy(out: Path) -> None:
    """Copy of plan3-two-trip-runtime-sdk.cpp with the strict current Varg reset/shield and its
    five owner-end lifetime; quoted includes from out/ reach it before tests/."""
    text = read(TESTS / "plan3-two-trip-runtime-sdk.cpp")
    text = replace1(text, 'record("sdk_cast");verify(actor.type==11', VARG_CAST)
    text = replace1(text, 'actor.health-=incoming;++monster_hits;verify(actor.health>0);', 'const double absorbed=std::min(incoming,observation_shield);observation_shield-=absorbed;actor.health-=incoming-absorbed;++monster_hits;verify(actor.health>0);')
    text = replace1(text, 'for(auto& value:stuns)if(value)--value;', 'if(observation_shield_ticks && --observation_shield_ticks==0)observation_shield=0;for(auto& value:stuns)if(value)--value;')
    write(out, "plan3-two-trip-runtime-sdk.cpp", text)
    write(out, "sortie-fixture.h", rename_main(read(TESTS / "plan3-sortie-runtime-current.cpp"), "int main() {", "void historical_sortie_fixture_main() {"))


def identity_join(out: Path) -> None:
    """Thirty-member PersonalInformation observer with ordered live-query and budget seams."""
    write(out, "backpressure-config-v1.h", config())
    text = eager_designs(base_host())
    text = replace1(text, FUEL_SEAM,
                    FUEL_SEAM + 'const auto sample=identity_budget();fuel_trace.push_back(sample);return sample;')
    text = replace1(text, 'record("sdk_vision"); *out=visible[0];return 0;',
                    'record("sdk_vision");*out=visible[0];identity_query(4,0);return 0;')
    text = replace1(text, 'record("sdk_camp_group_state"); verify(group<3);*out=group_state[group];return 0;',
                    'record("sdk_camp_group_state");verify(group<3);*out=group_state[group];identity_query(0,group);return 0;')
    text = replace1(text, 'record("sdk_camp_group_cooldown"); verify(group<3 && group_state[group]==3);*out=60;return 0;',
                    'record("sdk_camp_group_cooldown");verify(group<3 && group_state[group]==3);*out=60;identity_query(3,group);return 0;')
    write(out, "backpressure-host-v1.h", text)


def observation(out: Path) -> None:
    """Observer/ordinary Campaign host: current Varg record, camp-info probe (plan3-observation-current.cpp).
    Compile with -I tests after -I out so the fixture copies' module includes resolve."""
    two_trip_runtime(out)
    write(out, "backpressure-config-v1.h", current_varg(read(out / "backpressure-config-v1.h")))
    text = read(out / "backpressure-host-v1.h")
    for old, new in (DESIGN_12, RESPAWN, COLLISION):
        text = replace1(text, old, new)
    write(out, "backpressure-host-v1.h", text)
    text = read(out / "two-trip-runtime-host.h")
    write(out, "two-trip-runtime-host.h", replace1(text, CAMP_PROBE, CAMP_PROBE + 'if(observation_camp_probe){const auto code=observation_camp_probe(id);if(code)return code;}'))
    _varg_two_trip_copy(out)


def startup(out: Path) -> None:
    """Three-altar startup host plus the observer fixture as a header (plan3-startup-current.cpp)."""
    two_trip_runtime(out)
    write(out, "backpressure-config-v1.h", current_varg(read(out / "backpressure-config-v1.h")))
    text = read(out / "backpressure-host-v1.h")
    text = replace1(text, 'if(kind==3)set(out,bp_axis(2),bp_axis(3));', 'if(kind==3){set(out,bp_axis(2),bp_axis(3));set(out,bp_axis(7),bp_axis(0));set(out,bp_axis(6),bp_axis(7));}')
    text = replace1(text, 'record("sdk_Bitboard_GetSetBits");', 'record("sdk_Bitboard_GetSetBits");if(startup_bitset_probe){const auto code=startup_bitset_probe();if(code)return code;}')
    text = replace1(text, '!(x==bp_axis(2) && y==bp_axis(3))', '!(x==bp_axis(2) && y==bp_axis(3)) && !(x==bp_axis(7) && y==bp_axis(0)) && !(x==bp_axis(6) && y==bp_axis(7))')
    for old, new in (DESIGN_12, RESPAWN, COLLISION):
        text = replace1(text, old, new)
    write(out, "backpressure-host-v1.h", text)
    text = read(out / "two-trip-runtime-host.h")
    need(text.count(CAMP_PROBE) == 1, "Changed camp info seam")
    text = replace1(text, 'x==bp_axis(2) && y==bp_axis(3) && std::max', '((x==bp_axis(2) && y==bp_axis(3)) || (x==bp_axis(7) && y==bp_axis(0)) || (x==bp_axis(6) && y==bp_axis(7))) && std::max')
    write(out, "two-trip-runtime-host.h", text.replace(CAMP_PROBE, CAMP_PROBE + 'if(observation_camp_probe){const auto code=observation_camp_probe(id);if(code)return code;}', 1))
    _varg_two_trip_copy(out)
    write(out, "observation-fixture.h", rename_main(read(TESTS / "plan3-observation-current.cpp"), "int main() {", "void historical_observation_fixture_main() {"))


def _absolute(name: str) -> str:
    return '"' + (SUITE / name).as_posix() + "/"


def sortie_ability(out: Path) -> Path:
    """Tavi ability host with every queried design/catalogue record from the current official
    query, plus the Rows 50x50 host in out/rows (compile that one with -DP3_SORTIE_ROWS)."""
    two_trip_runtime(out)
    cat = json.loads(read(CATALOG))["result"]
    text = read(out / "backpressure-config-v1.h")
    for name, size in [("champions", 128), ("camps", 124)]:
        values = sorted(cat[name], key=lambda x: x["id"])
        need([x["id"] for x in values] == list(range(len(values))) and all(x["id"] == x["type"] for x in values), "Catalogue enum join failed " + name)
        records = [bytes.fromhex(x["rawHex"]) for x in values]
        need(all(len(x) == size for x in records), "Catalogue ABI size failed " + name)
        line = "inline constexpr std::array<std::array<unsigned char," + str(size) + ">" + "," + str(len(records)) + "> " + name + "{{" + ",".join("{{" + ",".join(map(str, x)) + "}}" for x in records) + "}};"
        text = re.sub(r"^inline constexpr .*?> " + name + r"\{\{.*$", lambda _: line, text, flags=re.M)
    settings = cat["settings"]
    layout = [("capture_time", "i"), ("camp_sleep_time", "i"), ("respawn_time", "i"), ("recall_time", "i"), ("submit_gem_duration_multiplier", "d")]
    layout += [(k, "i") for k in ("submit_gem_base_duration", "submit_gem_minimum_duration", "submit_gems_range")]
    layout += [(k, "I") for k in ("max_rounds", "winning_gems", "kill_gold", "assist_gold", "passive_gold")] + [("spawn_healing", "f")]
    layout += [(k, "I") for k in ("active_wards", "ward_charges", "shared_memory_bytes", "draft_memory_bytes", "tile_reveal_rounds")]
    layout += [("ward_cooldown", "i"), ("ward_lifetime", "i")] + [(k, "f") for k in ("ward_vision_radius", "ward_placement_radius", "control_ward_vision_radius", "control_ward_placement_radius")]
    layout += [("altar_lock_hits", "I"), ("tp_scroll_time", "i"), ("tp_crystal_time", "i"), ("reserved", "I")]
    raw = struct.pack("<" + "".join(t for _, t in layout), *(settings[k] for k, _ in layout))
    need(len(raw) == 120, "Current settings ABI mismatch")
    line = "inline constexpr std::array<std::array<unsigned char,120>,1> settings{{{{" + ",".join(map(str, raw)) + "}}}};"
    text = re.sub(r"^inline constexpr .*?> settings\{\{.*$", lambda _: line, text, flags=re.M)
    records = []
    for item in sorted(cat["items"], key=lambda x: x["id"]):
        need(item["id"] == len(records), "Item enum join")
        records.append(struct.pack("<IIiiiIfIII", *(item[k] for k in ("cost", "purchasable", "health_points", "strength_points", "focus_points", "passive", "passive_magnitude", "combine_count", "result", "combine_cost"))))
    line = "inline constexpr std::array<std::array<unsigned char,40>," + str(len(records)) + "> items{{" + ",".join("{{" + ",".join(map(str, x)) + "}}" for x in records) + "}};"
    text = re.sub(r"^inline constexpr .*?> items\{\{.*$", lambda _: line, text, flags=re.M)
    write(out, "backpressure-config-v1.h", text)
    text = read(out / "backpressure-host-v1.h")
    text = replace1(text, "verify(type>=0 && type<12);++design_reads;", "verify(type>=0 && type<13);++design_reads;")
    for old, new in (RESPAWN, CHAMPION_PROBE, ALIVE_PROBE):
        text = replace1(text, old, new)
    write(out, "backpressure-host-v1.h", text)
    text = rename_main(read(TESTS / "plan3-sortie-runtime-current.cpp"), "int main() {", "[[maybe_unused]] static int historical_sortie_fixture_main() {")
    write(out, "sortie-basic-fixture.h", replace1(text, '#include "../common/sortie-runtime.h"', '#include ' + _absolute("common") + 'sortie-runtime.h"'))
    # An isolated copy adds fault seams and a strict exclusive-walking assertion.
    text = rename_main(read(TESTS / "plan3-two-trip-runtime-sdk.cpp"), "int main(){", "[[maybe_unused]] static int main(){")
    text = replace1(text, 'record("sdk_cast");verify(actor.type==11', 'record("sdk_cast");if(sortie_cast_probe && sortie_cast_probe(slot,success))return 0;verify(actor.type==11')
    text = replace1(text, 'verify(ability.kind==4);', 'verify(ability.kind==4);verify(!(ability.attributes&ability_attribute_exclusive_with_movement) || (actor.state&(1<<3)));')
    for module in ("common", "backpressure-common", "indexer-common"):
        text = text.replace('"../' + module + "/", _absolute(module))
    write(out, "plan3-two-trip-runtime-sdk.cpp", text)
    rows = Path(out) / "rows"
    _rows_host(out, rows)
    return rows


def _rows_host(src: Path, directory: Path) -> None:
    """Actual public 50x50 Rows geometry; explicitly simulated quiet lifecycle."""
    m = next(x for x in json.loads(read(GEOMETRY))["maps"] if x["id"] == "00rows")
    need((m["width"], m["height"], m["mapSha256"]) == (50, 50, hashlib.sha256((ROOT / "maps/00rows.map26a").read_bytes()).hexdigest()), "Rows geometry/map mismatch")
    groups = sorted(m["groups"], key=lambda x: x["nativeId"])
    members = {c["nativeId"]: (c, g["nativeId"]) for g in groups for c in g["members"]}
    need(sorted(members) == list(range(54)) and [g["nativeId"] for g in groups] == list(range(9)), "Unexpected public Rows IDs")
    write(directory, "backpressure-config-v1.h", read(src / "backpressure-config-v1.h"))
    h = read(src / "backpressure-host-v1.h")
    t = read(src / "two-trip-runtime-host.h")
    c = read(src / "plan3-two-trip-runtime-sdk.cpp")
    b = read(src / "sortie-basic-fixture.h")
    h = replace1(h, 'constexpr unsigned side=8,words=1;', 'constexpr unsigned side=50,words=56;')
    h = replace1(h, 'std::array<unsigned,3> group_state{2,1,1};', 'std::array<unsigned,9> group_state{};')
    h = body(h, 'bp_axis', 'return value;')
    h = body(h, 'sdk_bc_metadata', 'record("sdk_bc_metadata");*out={50,50,7,7,8,448,6272};return 0;')
    h = replace1(h, 'bp_trace().push_back(h);', 'bp_trace().push_back(h);if(rows_meter_enabled)fuel=std::max<std::int64_t>(0,fuel-rows_sdk_query_cost);if(sortie_query_probe)sortie_query_probe(h);')
    h = replace1(h, '*out=design.definition.abilities[slot];return 0;', '*out=design.definition.abilities[slot];if(sortie_bad_kit&&id==0&&slot==1)out->attributes&=~ability_attribute_exclusive_with_movement;return 0;')
    # The meter is per SDK import, not per getFuel call or a native-cost oracle.
    h = h.replace('extern int cost_zero_item;', 'extern int cost_zero_item;extern bool rows_meter_enabled;extern unsigned rows_sdk_query_cost;extern std::int64_t fuel;extern const int bp_x[54],bp_y[54];')
    h = body(h, 'set', 'verify(x>=0 && x<50 && y>=0 && y<50);const auto at=(y/8)*8+x/8;const auto mask=std::uint64_t{1}<<((y%8)*8+x%8);if(value)bits[at]|=mask;else bits[at]&=~mask;')
    helpers = '''bool rows_test(const std::uint64_t* bits,int x,int y){return x>=0&&y>=0&&x<50&&y<50&&((bits[(y/8)*8+x/8]>>((y%8)*8+x%8))&1u);}
void rows_zero(std::uint64_t* bits){std::fill_n(bits,56,std::uint64_t{0});}
void rows_vision(std::uint64_t* bits,int x,int y){rows_zero(bits);for(int yy=0;yy<50;++yy)for(int xx=0;xx<50;++xx)if(std::max(std::abs(xx-x),std::abs(yy-y))<=8)set(bits,xx,yy);}
bool rows_spawn(int x,int y){return bp_side()?(x>=44&&x<48&&y>=44&&y<48):(x>=2&&x<6&&y>=2&&y<6);}
'''
    h = h.replace('void reset() {', helpers + 'void reset() {')
    h = replace1(h, 'group_state={2,1,1};', 'group_state.fill(3);')
    h = body(h, 'sdk_bc_map_mask', 'record("sdk_bc_map_mask");verify(kind>=0&&kind<=3);rows_zero(out);for(const int tile:rows_terrain()[unsigned(kind)])set(out,tile%50,tile/50);return 0;')
    # SDK cached masks call imports during static initialization. Construct this
    # immutable public table on its first import, before those masks are cached.
    terrain = 'const std::array<std::vector<int>,4>& rows_terrain(){static const std::array<std::vector<int>,4> value{{' + ','.join('{' + ','.join(map(str, m['terrain'][k])) + '}' for k in ['wall', 'bush', 'hole', 'altar']) + '}};return value;}\n'
    h = h.replace('namespace cambc::detail { extern "C" {', terrain + 'namespace cambc::detail { extern "C" {', 1)
    h = body(h, 'sdk_spawn_region', 'record("sdk_spawn_region");*out=team?aabb{44,44,48,48}:aabb{2,2,6,6};return 0;')
    h = body(h, 'sdk_vision', 'record("sdk_vision");std::copy(visible.begin(),visible.end(),out);return 0;')
    for name in ['sdk_ally_occupancy', 'sdk_enemy_occupancy', 'sdk_teleport_occupancy']:
        action = 'set(out,actor.x,actor.y);' if name == 'sdk_ally_occupancy' else ''
        h = body(h, name, 'record("' + name + '");rows_zero(out);' + action + 'return 0;')
    h = body(h, 'sdk_camp_occupancy', 'record("sdk_camp_occupancy");rows_zero(out);for(unsigned id=0;id<54;++id)if(member_states[id]&&tile_visible(id))set(out,bp_x[id],bp_y[id]);return 0;')
    h = body(h, 'sdk_move_to', 'record("sdk_move_to");verify((actor.state&1)&&(actor.state&(1<<3))&&!(actor.state&((1<<1)|(1<<2)|(1<<12))));verify(x>=0&&x<50&&y>=0&&y<50&&std::max(std::abs(x-actor.x),std::abs(y-actor.y))==1);verify(!occupied_by_living_camp(x,y));for(unsigned k:{0u,2u,3u})verify(std::find(rows_terrain()[k].begin(),rows_terrain()[k].end(),y*50+x)==rows_terrain()[k].end());actor.x=x;actor.y=y;actor.state&=~((1<<3)|(1<<4));++moves;++actions;rows_vision(visible.data(),x,y);*success=1;return 0;')
    h = body(h, 'sdk_camp_group_state', 'record("sdk_camp_group_state");verify(group<9);*out=group_state[group];return 0;')
    h = body(h, 'sdk_camp_group_cooldown', 'record("sdk_camp_group_cooldown");verify(group<9&&group_state[group]==3);*out=int(respawn_count[group]);return 0;')
    h = body(h, 'sdk_Bitboard_Zero', 'record("sdk_Bitboard_Zero");rows_zero(out);return 0;')
    for name in ['sdk_Bitboard_ImportRawBuffer', 'sdk_Bitboard_ExportRawBuffer']:
        content = 'std::memcpy(out,bytes,size);' if 'Import' in name else 'std::memcpy(bytes,in,size);'
        h = body(h, name, 'record("' + name + '");verify(size==448);' + content + 'return 0;')
    h = body(h, 'sdk_Bitboard_ExportLinearBuffer', 'record("sdk_Bitboard_ExportLinearBuffer");verify(size==313);std::fill_n(bytes,size,std::uint8_t{0});for(int y=0;y<50;++y)for(int x=0;x<50;++x)if(rows_test(in,x,y))bytes[(y*50+x)/8]|=std::uint8_t(1u<<((y*50+x)%8));return 0;')
    h = body(h, 'sdk_Bitboard_Test', 'record("sdk_Bitboard_Test");verify(x>=0&&x<50&&y>=0&&y<50);*out=rows_test(in,x,y);return 0;')
    for name, op in [('sdk_Bitboard_Union', '|'), ('sdk_Bitboard_Intersect', '&')]:
        h = body(h, name, 'record("' + name + '");for(unsigned i=0;i<56;++i)out[i]=a[i]' + op + 'b[i];return 0;')
    h = body(h, 'sdk_Bitboard_Not', 'record("sdk_Bitboard_Not");for(unsigned i=0;i<56;++i)out[i]=~in[i];return 0;')
    h = body(h, 'sdk_Bitboard_IsZero', 'record("sdk_Bitboard_IsZero");*out=std::all_of(in,in+56,[](auto x){return !x;});return 0;')
    h = body(h, 'sdk_Bitboard_NumSetBits', 'record("sdk_Bitboard_NumSetBits");*out=0;for(int y=0;y<50;++y)for(int x=0;x<50;++x)*out+=rows_test(in,x,y);return 0;')
    h = body(h, 'sdk_Bitboard_GetSetBits', 'record("sdk_Bitboard_GetSetBits");auto* out=static_cast<position*>(positions);*count=0;for(int y=0;y<50;++y)for(int x=0;x<50;++x)if(rows_test(in,x,y)){if(*count<capacity)out[*count]={x,y};++*count;}return 0;')
    h = body(h, 'sdk_Bitboard_ReachableMask', 'record("sdk_Bitboard_ReachableMask");verify(distance==1);rows_zero(out);for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int nx=x+dx,ny=y+dy;if(nx>=0&&nx<50&&ny>=0&&ny<50&&!rows_test(walls,nx,ny))set(out,nx,ny);}return 0;')
    for name in ['member_states', 'member_health', 'camp_state_calls', 'stuns']:
        c = c.replace(',9> ' + name, ',54> ' + name)
    for name in ['absent', 'capture_count', 'respawn_count']:
        c = c.replace(',3> ' + name, ',9> ' + name)
    c = c.replace('i<9', 'i<54').replace('id<9', 'id<54')
    c = body(c, 'tile_visible', 'return rows_test(visible.data(),bp_x[id],bp_y[id]);')
    c = body(c, 'on_spawn', 'return rows_spawn(actor.x,actor.y);')
    c = body(c, 'damage', 'verify(id<54&&member_states[id]&&tile_visible(id)&&std::isfinite(amount)&&amount>0);const unsigned g=bp_group[id];for(unsigned i=0;i<54;++i)if(bp_group[i]==g&&member_states[i])member_states[i]=1;member_health[id]=std::max(0.,member_health[id]-amount);if(!member_health[id])member_states[id]=0;bool living=false;for(unsigned i=0;i<54;++i)if(bp_group[i]==g)living|=member_states[i]!=0;if(!living){group_state[g]=2;capture_count[g]=0;}')
    c = body(c, 'sdk_map_camp_group_num_camps', 'record("sdk_map_camp_group_num_camps");verify(group<9);*out=rows_counts[group];return 0;')
    c = body(c, 'sdk_hypothetical_vision', 'record("sdk_hypothetical_vision");verify(type==actor.type&&x>=0&&x<50&&y>=0&&y<50);rows_vision(out,x,y);return 0;')
    # Actual group/native IDs and composition, never three-members-per-group arithmetic.
    t = re.sub(r'std::array<std::uint32_t,3> raw_gems[^;]+;', 'std::array<std::uint32_t,9> raw_gems{' + ','.join(str(g['gemValue']) for g in groups) + '},raw_gold{' + ','.join(str(g['goldValue']) for g in groups) + '};', t)
    for name, values in [('bp_x', [members[i][0]['x'] for i in range(54)]), ('bp_y', [members[i][0]['y'] for i in range(54)]), ('bp_type', [members[i][0]['typeId'] for i in range(54)])]:
        t = re.sub(r'constexpr int ' + name + r'\[9\]=\{[^}]+\};', 'constexpr int ' + name + '[54]={' + ','.join(map(str, values)) + '};', t)
    data = 'constexpr unsigned bp_group[54]={' + ','.join(str(members[i][1]) for i in range(54)) + '};\nconstexpr unsigned rows_counts[9]={' + ','.join(str(len(g['members'])) for g in groups) + '};\n'
    t = t.replace('int bp_kind(', data + 'int bp_kind(', 1)
    t = body(t, 'bp_key', 'int key=2500;for(unsigned i=0;i<54;++i)if(bp_group[i]==group)key=std::min(key,bp_y[i]*50+bp_x[i]);return key;')
    t = re.sub(r'std::uint64_t bp_camp_mask\(\)\s*\{.*?\n\}', '', t, flags=re.S)
    h = h.replace('std::uint64_t bp_camp_mask();', '')
    t = t.replace('id<9', 'id<54')
    t = body(t, 'sdk_map_camp_group_of', 'record("sdk_map_camp_group_of");verify(id<54);*out=bp_group[id];return 0;')
    t = body(t, 'sdk_Bitboard_DrawChebyshev', 'record("sdk_Bitboard_DrawChebyshev");verify(std::isfinite(radius)&&radius>=0);rows_zero(out);for(int yy=0;yy<50;++yy)for(int xx=0;xx<50;++xx)if(std::max(std::abs(xx-x),std::abs(yy-y))<=radius)set(out,xx,yy);return 0;')
    layout = []
    for g in groups:
        layout += [g['nativeId'], g['goldValue'], g['gemValue'], g['cooldown'], len(g['members'])]
        for v in g['members']:
            layout += [v['nativeId'], v['typeId'], v['x'], v['y']]
    t = body(t, 'sdk_map_camp_layout', 'record("sdk_map_camp_layout");constexpr std::uint32_t data[]={' + ','.join(map(str, layout)) + '};*count=sizeof(data)/sizeof(*data);if(capacity>=*count)std::copy(std::begin(data),std::end(data),out);return 0;')
    t = body(t, 'sdk_num_allies', 'record("sdk_num_allies");*out=1;return 0;')
    t = body(t, 'sdk_allies', 'record("sdk_allies");verify(capacity>=1);out[0]=0;*count=1;return 0;')
    t = body(t, 'sdk_submit_gems', 'record("sdk_submit_gems");verify((actor.state&1)&&bag&&!(actor.state&((1<<1)|(1<<2)|(1<<12))));verify(std::find(rows_terrain()[3].begin(),rows_terrain()[3].end(),y*50+x)!=rows_terrain()[3].end()&&std::max(std::abs(x-actor.x),std::abs(y-actor.y))<=int(cambc::constants::submit_gems_range));const auto ticks=plan3::tribute_completion_turns(int(bag),cambc::constants::submit_gem_duration_multiplier,cambc::constants::submit_gem_base_duration,cambc::constants::submit_gem_minimum_duration);verify(ticks&&*ticks>0);tribute_timer=unsigned(*ticks);actor.state|=1<<2;actor.state&=~((1<<3)|(1<<4));++submissions;++actions;return 0;')
    b = b.replace('std::array<unsigned,3> respawn_count', 'std::array<unsigned,9> respawn_count')
    b = body(b, 'sdk_DistanceField_Get', 'record("sdk_DistanceField_Get");verify(x>=0&&x<50&&y>=0&&y<50);*out=field[y*56+x];return 0;')
    b = body(b, 'sdk_DistanceField_ComputeToWithExpansionCount', 'record("sdk_DistanceField_ComputeToWithExpansionCount");std::fill_n(field,3136,std::uint16_t{65535});const auto f=[](unsigned at){return at/50*56+at%50;};std::array<unsigned,2500> queue{};unsigned head=0,tail=0;rows_zero(visited);*count=0;bool any=false;for(unsigned n=0;n<2500;++n){any|=rows_test(destinations,int(n%50),int(n/50));if(rows_test(roots,int(n%50),int(n/50))){verify(!rows_test(walls,int(n%50),int(n/50)));field[f(n)]=0;queue[tail++]=n;set(visited,int(n%50),int(n/50));}}while(head<tail){if(any){bool complete=true;for(unsigned n=0;n<2500;++n)if(rows_test(destinations,int(n%50),int(n/50))&&!rows_test(visited,int(n%50),int(n/50)))complete=false;if(complete)break;}const auto at=queue[head++];++*count;if(field[f(at)]>=max_distance)continue;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int x=int(at%50)+dx,y=int(at/50)+dy;if((!dx&&!dy)||x<0||y<0||x>=50||y>=50)continue;const unsigned next=unsigned(y*50+x);if(rows_test(walls,x,y)||field[f(next)]!=65535)continue;field[f(next)]=std::uint16_t(field[f(at)]+1);verify(tail<2500);queue[tail++]=next;set(visited,x,y);}}return 0;')
    # The old short-case reset/clock are replaced only in this separate binary.
    clock = '''++owner_ends;for(auto& x:cooldown)if(x)--x;if((actor.state&(1<<1))&&recall_timer)--recall_timer;if((actor.state&(1<<2))&&tribute_timer)--tribute_timer;if(on_spawn()&&actor.health<actor.max_health){actor.health=std::min(double(actor.max_health),actor.health+double(cambc::constants::spawn_healing)*actor.max_health);++healing_ticks;}++current_round;
for(unsigned g=0;g<9;++g){bool present=false,in_range=false;for(unsigned id=0;id<54;++id)if(bp_group[id]==g){present|=actor.x==bp_x[id]&&actor.y==bp_y[id];in_range|=std::max(std::abs(actor.x-bp_x[id]),std::abs(actor.y-bp_y[id]))<=3;}if(group_state[g]==2){if(!present)capture_count[g]=0;else if(!capture_count[g])capture_count[g]=unsigned(cambc::constants::capture_time)+1;else if(--capture_count[g]==1){bag+=raw_gems[g];gold+=raw_gold[g];group_state[g]=3;respawn_count[g]=50;++captures;}}if(group_state[g]==1){absent[g]=in_range?0:absent[g]+1;if(absent[g]>=unsigned(cambc::constants::camp_sleep_time))for(unsigned id=0;id<54;++id)if(bp_group[id]==g&&member_states[id])member_states[id]=2;}if(group_state[g]==3&&respawn_count[g]&&!present)--respawn_count[g];}
const double incoming=current_incoming();if(incoming){verify(!(actor.state&(1<<1)));actor.health-=incoming;++monster_hits;verify(actor.health>0);}for(auto& x:stuns)if(x)--x;if((actor.state&(1<<1))&&!recall_timer){actor.x=bp_side()?47:2;actor.y=bp_side()?47:2;actor.state&=~(1<<1);}if((actor.state&(1<<2))&&!tribute_timer){own_score+=bag;bag=0;actor.state&=~(1<<2);}actor.state&=~((1<<3)|(1<<4)|(1<<11));if(!(actor.state&((1<<1)|(1<<2))))actor.state|=(1<<3)|(1<<4)|(1<<11);rows_vision(visible.data(),actor.x,actor.y);'''
    c = body(c, 'advance_round', clock)
    # Only import/transition helpers are needed in the Rows binary. Do not parse
    # and instantiate the unrelated historical fixture scenarios a second time.
    c = c[:c.index('void arm(')] + c[c.index('void advance_round()'):c.index('Campaign* cold_campaign=')] + '}\n' + c[c.index('namespace cambc::detail {extern "C" {'):c.index('[[maybe_unused]] static int main(){')]
    c = c.replace('unsigned scenarios=0,completed_paths=0,boundary_cases=0,bootstrap_cases=0,optional_gate_cases=0;', 'unsigned scenarios=0;')
    c = re.sub(r'^unsigned (?:cold_cases=|low_health_cases=|service_cases=).*\n', '', c, flags=re.M)
    c = re.sub(r'^std::array<unsigned,2> cold_funded_paths.*\n', '', c, flags=re.M)
    b = b[:b.index('namespace {\nnamespace sr=')] + '''namespace {
namespace sr=plan3_runtime::sortie;
unsigned trace_hash(const char* name){unsigned h=2166136261u;for(;*name;++name)h=(h^unsigned(*name))*16777619u;return h;}
unsigned calls(const char* name){return unsigned(std::count(query_trace.begin(),query_trace.end(),trace_hash(name)));}
}
'''
    for name, text in [('backpressure-host-v1.h', h), ('two-trip-runtime-host.h', t), ('plan3-two-trip-runtime-sdk.cpp', c), ('sortie-basic-fixture.h', b)]:
        write(directory, name, text)


# ----------------------------------------------------------------------------- other inputs
def ledger_reference(out: Path) -> Path:
    """Copy of common/ and the Ledger-nominal fixture with ONLY the root's nominal-acquisition
    opt-in disabled; returns the copied fixture to compile against the same host."""
    base = Path(out) / "full-reference"
    if base.exists():
        shutil.rmtree(base)
    shutil.copytree(SUITE / "common", base / "common")
    for name in ("plan3-ledger-nominal-current.cpp", "plan3-two-trip-runtime-sdk.cpp"):
        (base / "tests").mkdir(parents=True, exist_ok=True)
        shutil.copyfile(TESTS / name, base / "tests" / name)
    runtime = base / "common/runtime.h"
    raw = runtime.read_bytes()
    original = b"c.nominal_acquisitions_only=controller_==plan3::Controller::ledger && !selector_;"
    need(raw.count(original) == 1, "Expected exact one-assignment caller opt-in")
    runtime.write_bytes(raw.replace(original, b"c.nominal_acquisitions_only=false;"))
    return base / "tests" / "plan3-ledger-nominal-current.cpp"


SUMMARY_MOCK = '''#pragma once
namespace summary_test_host {
inline unsigned fuel=100000,fuel_calls=0,round_calls=0,self_calls=0;
inline void reset(unsigned f){fuel=f;fuel_calls=round_calls=self_calls=0;}
}
namespace cambc {
inline unsigned fuel_remaining(){++summary_test_host::fuel_calls;return summary_test_host::fuel;}
inline unsigned round_number(){++summary_test_host::round_calls;return 2000;}
struct actor {int champion_name()const{return 0;}};
inline actor& self(){++summary_test_host::self_calls;static actor a;return a;}
inline const char* to_string(int){return "varg";}
}
'''


def summary_mock(out: Path) -> Path:
    """A synthetic cambc.h with only the queries the telemetry summary formatter makes."""
    return write(Path(out) / "mock-sdk", "cambc.h", SUMMARY_MOCK).parent
