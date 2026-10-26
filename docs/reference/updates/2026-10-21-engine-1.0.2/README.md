# Website review and engine 1.0.2 update

Checked 21 October 2026 against public official sources for `cambc2026a`.
The repository now pins official CLI/SDK **3.0.4** and engine **1.0.2**.
The [current manifest](../../current-version.json) is authoritative; raw evidence
is summarised below; the patch notes and public review data sit next to this file.

## Published changes

[Patch 1.0.2](https://2026a.battlecode.co.uk/game/patch-notes/e251fa42-fb2a-4d9b-ab8c-2e044f21d599),
revision 7, published/updated `2026-10-21T01:31:57.918481Z`, announces:

| Champion | Change from the previous repository bundle |
| --- | --- |
| Stellar | Health 750→650; Health/shard 75→65; Focus 85→70; Focus/shard 9→7 |
| Heart of Ruin | Strength 40→30; Strength/shard 4→3.5; dash Focus multiplier 1.75→1.5 |
| Ren | Stun Focus multiplier 1.25→0.75 |
| Vesper Vale | Health 900→1000; Strength 50→55 |
| Vendetta | Health 800→1000 |
| Tavi | Health 750→900; Focus 100→110 |
| Prism | Passive Focus per carried gem 1→1.5 |

All these deltas appear in freshly queried official editor metadata and the
current handbook export. Global settings, items and camps are unchanged.

[Patch 1.0.1](https://2026a.battlecode.co.uk/game/patch-notes/7a4caf45-afc3-49d0-838a-9eb9657bafc6),
revision 7, also documents the earlier Heart growth nerf already present in 3.0.2,
and a 24-hour grace period for submissions invalidated by compilation-breaking
updates, after which they automatically lose ranked matches. This is a recorded
website rule; no account or submission was accessed.

The official [download metadata](https://2026a.battlecode.co.uk/api/v1/distribution)
publishes CLI 3.0.4 for Windows x64, Linux x64/ARM64 and macOS Intel/Apple Silicon.
Windows ARM64 remains unpublished. The Windows binary was published at
`2026-10-21T05:13:01.901Z` and verified against its advertised SHA256 and size.

## Repository changes

- Installed the verified CLI; replaced 13 changed SDK files using isolated
  official `init` output. Added full/masked `distance_field.copy_from` APIs and
  Prism's updated passive constant. Vendored sources were copied, not hand edited.
- Refreshed [rules](../../GameRules.md), [C++ API](../../BotAPICpp.md), distribution,
  patch snapshots, engine metadata and geometry. All 35 map payloads and metadata,
  CMake, and six protected Starter files are unchanged.
- Added the unique 1.0.2 replay profile while retaining old profiles. Current
  consumers use coordinated manifest identities or verified per-engine parsers.
  Historical reports retain their own pins and qualifications.
- The prior 3.0.2 environment was retired in the 21 October repository restructure.

## Other website checks

The public patch list has three entries and no next page. The event is live and
registration is open. The landing schedule lists Sprint 2 on 25 October (lock
`2026-10-24T23:59:00+01:00`), Sprint 3 on 1 November, international qualifiers
4 November, UK qualifiers 11 November, and finals 18 November at Cambridge Union.
These are current observations, not inferred changes to an earlier schedule.

The root `battlecode.co.uk` redirects to the event site. No separate official
competition source-code/changelog repository was linked by the reviewed landing
and service pages. The linked Discord channels were not accessed. Exact URLs,
schedule and coverage are in the retained
[public review](public-review.json).

## Validation and limits

The integration checks found that engine metadata used one unstarted context;
geometry used 35 bounded metadata initializations. All resources were freed and
zero gameplay rounds advanced. Engine/replay ABI checks required no game context.

The compiled native build identifier was extracted statically; the editor version
and snapshot size were queried directly. No native 3.0.4 games, fuel measurements,
strategy comparison, login, submissions or ladder actions were performed.
Earlier gameplay evidence remains specific to its original version.
