![Champion Changelog](/api/v1/patch-notes/assets/057b48a5-6561-4923-987a-bc1817acc46c)

# Ryth

- <:strength[Strength per shard]:> (+<:strength:>) : 5 → 4.5

# Vesper Vale

- <:strength[Strength]:> (<:strength:>) : 55 → 45

# Cassi

- <:focus[Focus]:> (<:focus:>) : 70 → 100

# Issac

- <:strength[Strength]:> (<:strength:>) : 40 → 45

# Stellar

- <:focus[Focus per shard]:> (+<:focus:>) : 7 → 8.5

# Prism

- <:health[Health per shard]:> (+<:health:>) : 60 → 65

# Marcella

- <:strength[Strength per shard]:> (+<:strength:>) : 5 → 4.5
- <:focus[Focus per shard]:> (+<:focus:>) : 15 → 14

# Willow

- <:focus[Focus]:> (<:focus:>) : 120 → 140

# Ren

- Stun damage (S1): 0.75 x <:focus[Focus]:> (<:focus:>) → 1.1 x <:focus[Focus]:> (<:focus:>)

# Varg

- <:strength[Strength]:> (<:strength:>) : 40 → 45

# Tavi

- Stun damage (S2): 1.4 x <:focus[Focus]:> (<:focus:>) → 1.6 x <:focus[Focus]:> (<:focus:>)

# Heart of Ruin

- <:strength[Strength per shard]:> (+<:strength:>) : 3.5 → 3
- <:focus[Focus per shard]:> (+<:focus:>) : 12 → 10

---

# Treasure Emblem

- Bonus gold (<:gold:>): 20% → 10%

# Monster Emblem

- Bonus damage to camps: 10% → 15%

---

# New emblem families

## Tribute Emblem

![Tribute Emblem](/api/v1/patch-notes/assets/a535c713-5285-4291-9026-71a74e531e51)

- Tribute time (<:cooldown:>): reduced by 33%.
- The resulting duration is rounded up, with a minimum of one turn.

## Capture Emblem

![Capture Emblem](/api/v1/patch-notes/assets/20739c2c-297e-43ad-91bf-47072c4720bd)

- Capture time (<:cooldown:>): 5 → 3 turns.
- Applies when at least one allied capturer owns a Capture Emblem as the capture begins.

## Prospector Emblem

![Prospector Emblem](/api/v1/patch-notes/assets/84fdd566-98ed-4e99-baf9-b40ca9c8c73a)

- Bonus gold (<:gold:>) and gems (<:gems:>) from clusters: +25%.
- Only the Gold Cluster and Gem Cluster parts of a camp group's rewards contribute to this bonus. Other camps in the group keep their normal rewards.

> **Sharing rewards:** Each capturer who owns a Prospector Emblem receives 25% of their own share as an additional reward, rounded down separately for gold and gems. Capturers without Prospector receive only their normal share.
> 
> For example, 20 cluster gold (<:gold:>) shared by two allies gives each 10 base gold (<:gold:>). A Prospector holder gains 2 extra gold (<:gold:>); if both own the emblem, both gain 2 extra.

## Hunter Emblem

![Hunter Emblem](/api/v1/patch-notes/assets/4e2b84db-d030-4170-b2b7-c073a46d3319)

- Additional gold (<:gold:>) per champion kill: +50.
- Awarded to the champion who gets the kill, before gold-income bonuses are applied.

## Skirmish Emblem

![Skirmish Emblem](/api/v1/patch-notes/assets/57623bb6-c3a2-40e7-b2f1-4de35a0ab4ed)

- 5% more damage to champions.

## Greed Emblem

![Greed Emblem](/api/v1/patch-notes/assets/39801623-c1eb-4075-8056-ff8f0e931c5c)

- No passive. Combines at 25 shards (750 gold) to give 35 shards worth of stats points.

# Collectors Emblem

![Collectors Emblem](/api/v1/patch-notes/assets/ff50d753-97bd-4f8b-8e04-b45c5c5748ae)

- +1% bonus damage per unique completed emblem family owned.
- Collectors counts its own family, so owning only a Collectors Emblem gives 1% bonus damage.
- Different stat variants and duplicate copies of the same family count only once. Shards do not count.

---

# System changes

Increased draft first turn fuel limit by x10!

---

# CLI 4.0.0

You can now select an opponent's historical bot version for an unranked match group, using a previous match-group UUID or website link, just like on the website:

```sh
cambc match-groups create OPPONENT_TEAM_UUID --source-group MATCH_GROUP_UUID_OR_LINK
```

You can also list your team's submitted bot versions and change which version is live:

```sh
cambc submissions list
cambc submissions list --before 10 --json
cambc submissions activate 7
cambc submissions activate latest
```

---

# Breaking SDK details

This patch uses **engine 2.0.0** and **CLI 4.0.0**. Update your local CLI and rebuild your bot with the updated SDK before testing against the new engine.

The item backpack has expanded from **42 to 84 item types**. If you maintain custom WASM bindings, `env.champion_items` keeps its name and arguments but now writes **84 little-endian `u32` counts (336 bytes)**. Its output buffer must be expanded accordingly.

# Camp capture state

The SDKs now expose capture progress alongside the existing camp-group state query. The result has `timer` and `state` fields:

| State | Meaning |
| --- | --- |
| Ally capturing | You or an ally are capturing the group. |
| Contested | Both teams are present, blocking capture. |
| Enemy capturing | The opposing team is capturing the group. |
| Capture available | The group's reward is available, but no one is capturing. |
| No capture available | The camp group is still alive or waiting to respawn. |

While either team is capturing, `timer` reports the remaining capture countdown, including the shorter countdown from a Capture Emblem. In all other states it returns the normal maximum of 5 turns.

The group must be in your current vision to view its capture state, though the champion performing the capture does not need to be in vision.

# Item enum helper

You can now select an item by family and stat, instead of looking up one of the 84 item names:

| Language | New helper |
| --- | --- |
| C++ | `cambc::get_item_enum(item_family family, item_stat stat, bool shard = true)` |
| Python | `cambc.get_item_enum(family: ItemFamily, stat: ItemStat, shard: bool = True)` |
| Rust | `cambc::get_item_enum(family: ItemFamily, stat: ItemStat, shard: bool)` |
| OCaml | `Cambc.get_item_enum : ?shard:bool -> Item_family.t -> Item_stat.t -> Item.t` |
