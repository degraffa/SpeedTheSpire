# B5.2 campaign exercise triage

This note records the first queue produced while exercising the B5.2
one-command pipeline. The campaign
`b52_accept_20260729_70000_70049` is **not** the acceptance campaign: its first
few seeds overlapped a separately launched oracle job before the new singleton
lock was in place. Every artifact below strict-validates, but the campaign is
retained only as workflow/triage evidence. The independent acceptance campaign
is `b52_accept_locked_20260729_71000_71049`.

| Seed | First replay result | Classification | Disposition |
|---|---|---|---|
| `STS70011` | seq 90, floor 9, `COMBAT_REWARD`: `relics[1].counter -1 -> 0`; the slot is Lantern | product divergence | Lantern's Java keeps a private `firstTurn` boolean and leaves `AbstractRelic.counter` at -1. The sim incorrectly seeds the registry counter to 0 and then uses it as the first-turn latch. Filed in the live obligations table; not accepted. |
| `STS70022` | zero-diff through seq 29, then HAND_SELECT `choose 0` names a slot absent from the sim's open screen | replay-harness/command-map gap | The compared prefix is clean; the stop is a choice-surface disagreement rather than a proved state mechanic. Filed for harness triage; not counted as replay-clean after the stop. |
| `STS70023` | seq 35, floor 2, combat: capture HP 41 vs sim HP 42 after block fell from 5 to 0 against a 6-damage Spike Slime attack | product-divergence candidate | The game consumed five block and one HP; the sim consumed the block without the last HP. No standing deviation covers this shape. Filed for an engine owner to reduce before any mechanic is accepted. |
| `STS70037` | seq 82, floor 10, Wheel of Change: sim enters `COMBAT_REWARD`, pops one uncommon relic and advances `relicRng` one command before the capture | product-divergence candidate | The captured UI has a distinct prize/result acknowledgement before the reward screen. The early pool/RNG move persists rather than reconverging. Filed for event-state-machine reduction; not accepted. |

The fresh acceptance run generated its own machine-readable queue: 40/50 runs
were clean, five stopped in the replay harness after zero-diff prefixes, and
five exposed state divergences. Its report deliberately says `Pending triage:
10`; B5.2 automates queue production and provenance, it does not silently
convert newly found mismatches into accepted mechanics.

| Seed | Queue disposition |
|---|---|
| `STS71009`, `STS71022` | replay-harness gap after a zero-diff prefix: a captured `play` has no mappable card index |
| `STS71015` | replay-harness gap after a zero-diff prefix: reward-screen potion use is not mapped |
| `STS71030`, `STS71040` | replay-harness gap after a zero-diff prefix: a shop `choose` arrives after the sim has returned to `MAP_CHOICE` |
| `STS71017` | Lantern product divergence, independently confirming STS70011 (`counter -1 -> 0`) |
| `STS71018` | Tiny House/Neow payout product-divergence candidate: from seq 5 the capture has the selected Body Slam and later 50 gold while the sim has neither |
| `STS71025` | **known standing deviation**, not a new product bug: two Looter steals change the live game's purse during combat (122→102→82), while the sim intentionally settles stolen gold only at fold-back; this is the obligations table's “Stolen-gold clamp vs in-combat gold ordering” model boundary |
| `STS71033` | Gambling Chip product divergence: the sim exposes its private activation latch through `RelicSlot.counter` (`-1 -> 1`) while the capture counter remains -1 |
| `STS71039` | Calling Bell/Neow onEquip product-divergence candidate: at seq 2 the capture has opened the curse grid and consumed the bell pools, while the sim is still at the pre-equip Neow state |

The three newly exposed product families are filed in the live obligations
table alongside Lantern. All exact source hashes, first-diff reports and traces
remain under the fixed external campaign root.
