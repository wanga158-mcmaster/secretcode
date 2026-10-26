# Lighthouse: current native execution repair

Claim `claim-0068-0e64e77b`, engine 1.0.3 / toolchain `3837f00416ea`.
Retain experimental `specialists/lighthouse@c03ee48264ea` with
`LIGHTHOUSE_CAPTURE_DEFENSE=1 LIGHTHOUSE_ROUTE_EXECUTOR=1 LIGHTHOUSE_WARD_ROUTES=1`.
All three defaults remain **0**. No default promotion: health and useful information
attribution remain incomplete.

The old capture hold returned before combat even when a personally visible enemy
occupied another spawn of the same reward-available group. The repair reaches the
existing combat executor after the unchanged retreat check, without walking. If
combat cannot act for the queried public capture interval, departure uses the
existing danger-aware movement. Uncontested capture priority stays intact.

Ordinary and ward-route movement read the adjacent field values instead of
exporting the entire map. Ward preparation builds one complete public terrain
field at a time and revalidates the current complete eligible fork before using
the cached geometry. Complete menus use public spawn coordinates and read each
visible camp once per invocation. Their vision mask is reused only by the
immediately following pre-action fork and cleared on the next owner turn. Live
camp facts, scores and affordability do not cross turns. Fork thresholds,
visibility checks, health floors and 65k/55k ward reserves are unchanged.

The single capture-defense comparison on Great Wall B has equal observed Tavi
state at R91: baseline explicitly ends; enabled Tavi casts for 154 and attacks for
45 damage against the opposing spawn occupant. He captures 14 gems at R107/R190,
banks 28 at R227, then clears/captures the same group and banks again. This is an
execution witness; private memory, fuel and whole-world equality are not claimed.

Same-run 40-map, both-side Fundamentals coverage changes clears/banked gems from
1,326/8,658 to 2,919/31,834. Final own flagged games fall from 26 to 13, but all
56 final exhaustions occur at R1 and nine stalls remain. Both opponent stalls
are retained. Fresh frozen main-v28 Great Wall/Ambush A/B are four healthy games,
104 clears and 707 banked; nine actor instances bank, later clear/capture and bank
again. This is activity evidence, not a strength claim.

One real ward occurs on Fragment B: Tavi3 at R1569, tile (21,14), adds 80 personal
vision tiles and reveals respawning `g_407`. He banks afterward and later clears,
captures and banks again. Ward fact/change/useful counters remain zero. The
`core_enabled=false` control is healthy but already differs before this window;
it does not establish a matched local decision benefit. Useful information
attribution therefore remains unproved.

Final OFF `5570767432c7` separately equals incumbent `8eea280fa3e1`: raw SHA256
`31d93752c9356821ba67c5ea08e5ff8bf6d705e42c3176f9e893dabc4b15f2a5`, injected
`06dbaa999306abe41ff5ca410bf36107037f3545275a230ae758b82af33626e0`.
Existing policy, telemetry and control tests pass (3/3). Next work is bounded
initialization, then a native matched information-to-choice-to-payment witness.
Exact modules, health flags and native locators are in
[`lighthouse-activation/index.json`](../../../results/population/lighthouse-activation/index.json).
