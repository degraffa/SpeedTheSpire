# Stage B Task Ledger

Execution tracker for [stage-b-design.md](stage-b-design.md) (the frozen Stage
B spec — **this file never overrides it**; on any conflict, the design doc wins
and this file gets fixed). [stage-a-design.md](stage-a-design.md) remains
frozen and in force for everything it covers. Target milestones: **M2** (oracle
bridge live, gate G4), **M3** (S1 rules complete, gate G6), **M4** (S1
verified, gate G7) — InitialPlan's milestone table, S1 = Ironclad / Act 1 /
A20.

**This file holds only what is still open.** Completed task blocks live
verbatim in [stage-b-log.md](stage-b-log.md) and appear here as one-line index
entries. The rules every task obeys live once in
[conventions.md](conventions.md). Picking up an open task means reading
`conventions.md` + this file's entry — not the archive.

## Orchestrator protocol

- Statuses: `[ ]` todo · `[~]` in progress · `[x]` done · `[!]` blocked.
  Update the checkbox and the **Log** line of a task when its state changes.
  When a task lands, its full block moves to
  [stage-b-log.md](stage-b-log.md) under an `<a id="…">` anchor and is
  replaced here by a one-line index entry; any forward-looking obligation its
  Log leaves behind goes into **Deferred obligations** below **and** into the
  owning task's `**Inherited:**` line — never only into the archived Log.
- A task is **done only when its Acceptance block passes** — run the commands,
  don't infer. Tests land in the same change as the code they verify.
  Registry YAML is code: entries land with their tier-2 tests in one commit.
- Respect `Deps:`. Tasks with disjoint deliverables and satisfied deps may run
  in parallel (parallel-safe groups are marked ∥).
- **Gates** (G4–G7, continuing Stage A's numbering) are stop-the-line: nothing
  past a gate starts until the gate task is `[x]`. Phase B3/B4 content work
  additionally requires **both** G4 and G5 (design §3.2: bridge first, then
  registry migration, then mass content).
- Build/test commands, provenance rules, the precedence chain, git discipline
  and hygiene: [conventions.md](conventions.md). Nothing in this file
  overrides it.
- Layout: engine as in Stage A; registry sources `registry/*.yaml`; codegen
  `tools/registry_gen/`; bridge `tools/oracle_bridge/` (fork source under
  `communicationmod-oracle/`, driver under `driver/`, translator under
  `translator/`); distributional tests `tools/dist_check/`; reports
  `tools/verify_report/` + `docs/verification/`.

## Working agreements

Moved, in full, to **[conventions.md](conventions.md)** — the single
authoritative copy of statuses, git discipline, the canonical reference
reading order, the precedence chain and its live-override evidence bar,
hygiene, and the build commands. It is binding on every task in this ledger.
The only ledger-local specifics are the protocol bullets above.

## Deferred obligations

Forward-looking obligations that completed tasks recorded in their Logs and
handed to a future task. **This table is the live carrier** — the archived
Logs are not read during normal execution, so an obligation that is not here
is invisible. When you land a task, discharge every row that names it (or
re-own the row explicitly) and delete the row in the same commit.

Owners marked `[x]` are tasks that have **already landed without recording a
discharge** — they need re-owning by the orchestrator, not silent closure.
`UNASSIGNED` means the deferring Log named no owner at all.

| Obligation | Deferred by | Owner task | Detail |
|---|---|---|---|
| In-combat card-CHOOSE potion bodies: Elixir, Attack/Skill/Power/Colorless Potion, Gambler's Brew, Liquid Memories | B3.23 | B3.4 `[x]` | B3.23 named B3.4 as the verb owner; B3.4 landed CHOOSE-in-combat but recorded no potion un-deferral. **PARTIALLY DISCHARGED** on `discharge` (Blessing of the Forge, commit `d710d50`) — it needed only the already-live `CHOOSE_CARD{upgrade}` kind, so it left this row; the potions still listed above need real hand-select-screen / `MAKE_CARD` work and stay open. As of the same branch they are also **fail-loud**: `potion_use_implemented` keeps every still-deferred potion off the legal-action mask (the potion-legality commit on `discharge`, immediately following `d710d50`) instead of letting a USE silently burn the slot |
| Mummified Hand onUseCard POWER → cardRandomRng 0-cost | B3.25 | B3.7 `[x]` | no POWER CardType at B3.25 time; B3.7 landed the POWER cards but recorded no discharge. **DISCHARGED** on `discharge`, commit `add41ed` — implemented with the cardQueue exclusion and the just-played-card exclusion, no draw on the empty-candidate path |
| Frozen Egg's POWER-card upgrade-on-obtain branch (documented inert) | B3.25 | B3.7 `[x]` | same cause, same gap. **DISCHARGED** on `discharge`, commit `dc6f626` |
| Recursive-play opcode (design R14) — Mayhem | B3.2 | B3.11 | **the opcode now exists**: B3.8 landed `PLAY_CARD` = 34 as the *general* verb, and Mayhem reuses `{op: PLAY_CARD, play: [from_draw_top]}` **unchanged** — this row is the authoring, not the verb. Also unblocks the Duplication and Distilled Chaos potions |
| Per-power counter storage — Panache (every-5th), The Bomb (3-turn) | B3.2 | B3.11 | B3.2 added no `PowerSlot` counter field; Combust (B3.7) and Rampage (B3.5) were solved locally |
| **Dead Branch** `onExhaust` | B3.26 | UNASSIGNED — needs the unfiltered all-red combat card pool | `DeadBranch.onExhaust` (`DeadBranch.java:259-266`) queues a `returnTrulyRandomCardInCombat()` copy into hand. That draw is **unfiltered** over the whole colour pool — commons + uncommons + rares (`AbstractDungeon.java:964-979`), not the ATTACK-only pool `RANDOM_ATTACK_TO_HAND` uses — so it needs a **second generated combat pool** and is **`cardRandomRng`-visible**: implementing it moves the stream. Pandora's Box (B3.27) waits on the same pool. Inertness is asserted today by `relic_rares_shop_test`, so implementing it fails a test rather than silently changing behaviour |
| **Purity and Forethought** (colorless uncommons, `CardId` **109** and **101** reserved) | B3.10 split | UNASSIGNED — **same owner as the Gambling Chip row below**, whoever makes the optional multi-select change | reached from a *card* batch rather than a relic, which is what makes it worth its own row: the blocker is no longer a single deferred relic but a **third** independent consumer. Purity discards zero-to-all; Forethought needs a new draw-**bottom** `ChoiceKind` **and** a `freeToPlayOnce` `CardFlag` (bits 10–15 are free; card-limbo allocated bit 8 to `EXHAUST_ON_USE_ONCE` and the dequeue fix-forward allocated transient Double Tap X-energy bit 9), and Forethought+ is itself zero-to-all. `choice_requires_user` (`interp_cards.cpp:718-728`) hard-codes a mandatory fixed count, `ActionVerb` has no confirm/skip, and the translator explicitly defers `can_pick_zero` (`translate.cpp:757`). **Do not let a reward-screen or shop skip button become this change by accident** — it is a public `ActionMask` surface change and wants its own task |
| **Gambling Chip** `atTurnStartPostDraw` | B3.26 | UNASSIGNED — needs an OPTIONAL multi-select `CHOOSE_CARD` | `GamblingChip.java:426-453` opens a hand-select screen discarding **zero-to-all** chosen cards, then draws exactly as many as were discarded. Every existing `ChoiceKind` selects a **mandatory fixed count**, so this needs an optional multi-select with an explicit confirm — which **changes the public `ActionMask` surface** the observation and translator layers join on, and is therefore not a local relic change. Only `atTurnStartPostDraw` is bound (`atBattleStartPreDraw` has no dispatch site). Inertness asserted by `relic_rares_shop_test` |
| **Sling of Courage** `atBattleStart` | B3.26 | UNASSIGNED — **whoever adds an elite/boss room marker to `CombatState`** | `Sling.atBattleStart` (`Sling.java:1030-1038`) grants Strength 2 when `getCurrRoom().eliteTrigger` is set. `eliteTrigger` is per-**ROOM** state the run layer sets when an elite encounter begins, and `CombatState` carries no elite marker; producing one is a run-layer change. **Twin of the Slaver's Collar row below** — same missing marker, so both wait on the blocker, not on each other. Inertness asserted by `relic_rares_shop_test` |
| **Orange Pellets** `onUseCard` | B3.26 | UNASSIGNED — needs a new opcode | `OrangePellets.java:1218-1250`: once an ATTACK, a SKILL and a POWER have all been played, it queues `RemoveDebuffsAction(player)`, which removes **every** DEBUFF-type power on the player, **enumerated when the action resolves**. No opcode expresses that — `REMOVE_POWER` names one `PowerId` chosen at *queue* time. The three latches and their `at_turn_start` clear are live; only the removal is deferred. Inertness asserted by `relic_rares_shop_test` |
| Ten `energyMaster` relics (Fusion Hammer, Velvet Choker, Runic Dome, Cursed Key, Busted Crown, Ectoplasm, Sozu, Philosopher's Stone, Coffee Dripper, Mark of Pain) **and Snecko Eye's `masterHandSize += 2`** | B3.27 | UNASSIGNED — next `action_queue.cpp` owner | there is no `energyMaster` / `gameHandSize` field, and the single consumer is the recharge/draw line inside `start_of_turn`. Each is deferred **whole**, with asserted inertness, because every partial would desync `miscRng` or `relicRng` |
| `dispatch_relics_at_pre_battle` at the **run** entry (`run_advance.cpp` `enter_combat`) | B3.27 | UNASSIGNED — next `run_advance.cpp` owner | one line; it is wired only in `advance.cpp`'s `combat_begin` today, so a run-layer combat gives Snecko Eye no Confusion |
| Slaver's Collar `beforeEnergyPrep` | B3.27 | UNASSIGNED — **whoever adds an elite/boss room marker to `CombatState`** | `SlaversCollar.beforeEnergyPrep` (`SlaversCollar.java:46-57`), called by name from `AbstractPlayer.preBattlePrep` (`:1589-1591`): `++energyMaster` when the room's `eliteTrigger` is set **or** any monster is `EnemyType.BOSS`; `onVictory` undoes it. `CombatState` carries no elite/boss room marker. **Twin of the Sling of Courage row above** — same blocker, so neither row owns the other. Row, pool slot and `relicRng` draw are live |
| Warped Tongs | B3.27 | UNASSIGNED — needs a new opcode | `UpgradeRandomCardAction` is `shuffleRng`-consuming; `CHOOSE_CARD` RANDOM+UPGRADE is a different stream and a different filter |
| Pandora's Box / Tiny House / Astrolabe / Empty Cage / Calling Bell `onEquip`; Cursed Key + N'loth's Mask chest hooks; Sacred Bark potency; Fusion Hammer / Coffee Dripper campfire locks; Sozu / Golden Idol at non-combat claim sites | B3.27 | B4.7 / B4.9 / B4.10-13 | **Chest hooks DISCHARGED by B4.7:** Cursed Key and N'loth's Mask are live and tested, including boss gates and acquisition order. The source audit also corrected the inherited claim: `RewardItem.applyGoldBonus` explicitly excludes `TreasureRoom`, and an ordinary chest has no potion reward, so Golden Idol and Sozu have no chest share to implement. Their event-screen shares remain with B4.10-13. **B4.5's shares are discharged** at combat rewards. The five `onEquip` bodies, Sacred Bark, campfire locks, and Busted Crown/Ectoplasm energy halves stay in their own rows. |
| Philosopher's Stone `onSpawnMonster` | B3.27 | UNASSIGNED — split/spawn owner | |
| Purged replay copies leak a card-pool row | B3.8 | UNASSIGNED | same as the existing POWER-card path; bounded (~40 of 160 rows worst case). Freeing the row would race a queued `DAMAGE_RAMPAGE` stamping that index |
| Windows CI job | build effort | UNASSIGNED | a proposed workflow exists but is **unverified** (Actions cannot run locally). **Pin the LLVM version**: the googletest `/WX-` workaround exists because clang 22 added a warning gtest trips over, and a newer runner clang could add another |
| `replay` generalized to seed a sim replay from any translated `RunState` | B1.6 | B4.4 `[x]` | B1.6 scoped itself to "adapter + format only" and named B4.4; B4.4's Log records run-combat equivalence but no `replay` generalization |
| Emit `kIroncladAttackPool` **and the three B4.5 reward pools** in CardLibrary HashMap **library order** instead of registry-id order | B3.6 | B4.5's oracle capture | documented interim deviation; **one** `gen.py` fix pins all four pools at once. Blocked on the same manual capture that blocks B4.5 itself — the runbook's §4 documents exactly how the capture pins them |
| Matryoshka (chest relic) | B3.25 | B4.7 `[x]` | **DISCHARGED:** two-use non-boss hook, 75/25 relicRng branch, reward insertion, counter `2→1→-2`, and boss no-op are live and tested |
| The Courier (shop relic) | B3.25 | B4.8 | floor≤48 && !in_shop gate live, effect deferred |
| Eternal Feather (rest-room heal) | B3.25 | B4.9 | row live, effect deferred |
| Translator `eventList`/`shrineList`/`specialOneTimeEventList` membership bitsets | B1.5, B4.3 | B4.10 | storage exists since B4.3; needs `events.yaml` + the canonical list order (B4.10-B4.13) |
| ?-room `eventRng` duplicate roll | B4.4 | B4.10 | B4.4 names B4.10 as owner |
| Translator `screen_state` content (event / shop / grid / map screens) | B1.5, B4.3 | B4.8, B4.10, B4.14 | structurally consumed today (a new/renamed key still fails loud), not mapped. **The reward slice is DISCHARGED** by B4.5, which also made CARD_REWARD/COMBAT_REWARD content-validated — the `reward_type` name is now enumerated and an unknown one fails loud, where previously anything passed |
| `b14_accept2` obtain-race capture-fidelity triage | B1.3 | B5.2 | flagged explicitly by B1.3; B1.4's acceptance is unaffected |
| Infernal-Blade-generated Blood for Blood cost model (`cost_now` only; end-of-turn reset restores 4, not the game's reduced base) | B3.6 | G7 | judged unreachable — "revisit if G7 ever hits it" |
| Bottled trio bottling at acquisition (run-layer acquisition-choice machinery + a per-master-deck-instance innate flag) | B3.25 | UNASSIGNED — named "B4-owner" | rows + deck-content gates live so pools and B4.7 chests are complete |
| Akabeko (Vigor power row) | B3.24 | UNASSIGNED — "card-batch consumers" | no S1 Ironclad card grants Vigor, so no card batch will pick the power row up |
| Pen Nib double-damage `PenNibPower` | B3.24 | UNASSIGNED — "card-batch consumers" | the attack counter is already live; only the damage doubling is missing |
| Boot (`onAttackToChangeDamage` DAMAGE-pipeline modifier) | B3.24 | UNASSIGNED | deliberately deferred to keep the frozen float-exact damage pipeline untouched |
| Red Skull `onNotBloodied` −3 heal-cross | B3.24 | UNASSIGNED | needs a heal-cross hook that does not exist yet |
| Art of War / Ancient Tea Set (cross-turn / cross-room energy flags) | B3.24 | UNASSIGNED | needs state beyond `RelicSlot.counter` |
| Preserved Insect (elite-room HP scaling) | B3.24 | UNASSIGNED | elite rooms exist since B4.4; no discharge recorded |
| Fire Potion `applyEnemyPowersOnly` / THORNS typing | B3.23, B3.2 | UNASSIGNED | B3.2's DAMAGE damage-TYPE item was discharged by the potion-support-powers follow-up; B3.23 records this piece as "its own item" |
| Snecko Oil cost-randomization potion body | B3.23 | UNASSIGNED | the "cost randomization" verb has no owner |
| Fairy in a Bottle out-of-combat revive | B3.23 | UNASSIGNED | flagged native at B3.23, no owner named |
| Translator: power `misc` fields other than player-owned Combust | B3.7 | UNASSIGNED | the fix-forward mapped only Combust `hpLoss`; every other power `misc` stays deferred |
| Translator: `monster_move_history` beyond 3 entries | B1.5, B4.3 | UNASSIGNED | "stays deferred to its owning task" — none named |
| Translator: real `act_boss` | B1.5, B4.3 | UNASSIGNED | the boss registry now exists — B3.20/B3.21/B3.22 landed all three Act-1 bosses, so the blocker is gone — but still no owner is named |
| Bit-exact oracle for the raw monsterRng monster / elite / boss lists | B3.12 | UNASSIGNED | B3.12 pinned the algorithm + determinism, not a golden list; B4.4's floor-0 triple pins stream state only. Natural home is B5.2's campaign automation |
| `RETAIN` `CardFlag` end-of-turn sweep | B3.1 | UNASSIGNED — "first content consumer" | ETHEREAL (B3.5/B3.6) and INNATE (B3.9) discharged; no S1 Ironclad card uses Retain |
| `lagavulin_init_awake` has no production caller | B3.19 | UNASSIGNED — Lagavulin Event owner | implemented and tested; the event that would build `Lagavulin(false)` does not exist |
| Gremlin move-99 escape (`EscapeAction` body **and** the `deathReact`/`escapeNext` trigger, landed together) | B3.16 | UNASSIGNED — Act-2 owner | unreachable in Act 1: `escapeNext()` has no caller in the decompiled tree; the only `deathReact()` call is `BanditBear.java:131` |
| `registry/cards.yaml`'s documented `native: <bool>` field is a **silent no-op** | B3.10 scope read | UNASSIGNED — next `emit/cards.py` or schema owner | `cards.yaml:20` documents the field, but `emit/cards.py` **never reads it** and `loader.py:34-70` accepts unknown keys silently, so `native: true` on a card row does nothing and reports nothing. Powers, relics, potions and monsters all have a native path; **cards do not**. This is conventions §8's "a comment asserting X" class running in reverse — documentation asserting a capability the code lacks — and it is a live trap: a future card batch will reasonably reach for it. Either implement it, delete the doc line, or make the loader reject the key |
| `CardDef` has no upgraded-**target** column | B3.10 scope read | UNASSIGNED — `ActionMask` / observation owner | Blind+ and Trip+ change `this.target` to `ALL_ENEMY` (`Blind.java:48`, `Trip.java:43`). The *effect* is encodable via an `ALL_ENEMY` step target in the upgraded program, so B3.10a is not blocked — but card-level `needs_target` stays single-target, which is an **`ActionMask` deviation, not a state one**: the mask offers a target choice the game would not. Cheap to miss because every test of the effect passes |
| **Nine pre-existing out-of-range Java citations, repo-wide** | integration-15 citation audit | UNASSIGNED | `RupturePower`, `DexterityPower`, `FrailPower`, `Clash`, `HeavyBlade`, `Torii`, `TungstenRod`, `LizardTail`, `MagicFlower` each carry a `File.java:line` citation that does not resolve in `D:\STS_BG_Mod\SlayTheSpireDecompiled`. Found while fixing the two citations integration-15's own merge broke, and **deliberately left alone** — they predate that integration and are a separate repo-wide condition, not something those branches introduced. Fixing them is comment/provenance-only and needs each cited method **re-read in full**, not line-shifted: the two integration-15 fixed both had *correct prose and wrong numbers*, so a mechanical offset would have looked right and been wrong |
| `a20.yaml` row 4 cites `tackleDmg = 10`, a dead `DamageInfo` | integration-11 | B4.15 follow-up | literally accurate (`SlimeBoss.java:94-96`) but `tackleDmg` is `damage.get(0)` and SlimeBoss never reads it — only `damage.get(1)` at `:137`/`:144`. `monsters.yaml` is right to omit it. Not a document conflict |

## Shared namespaces — allocation now in force

A conventions §7 **rule-of-two** observation, recorded here rather than inside
any one task block because both namespaces are contended across concurrent
worktrees: contention for them **stopped one task dead** and produced a
**silent off-limits edit** in another. A worktree cannot see its siblings, so
"take the next free number" is not a safe local decision — the orchestrator
allocates first.

**1. Registry ids are append-only, and gaps are legal.** The loader
(`tools/registry_gen/stsgen/loader.py`) requires an id that is an integer,
`>= 1`, unique and appended — it does **not** require contiguity, and no
emitted table is a dense id-indexed array, so a gap costs nothing at runtime.
Several gaps are live today and every one of them is correct: `MonsterId` 14 is
unallocated (B3.18 took 12/13, B3.19 took 15), and `PowerId` ids have long run
ahead of the row count (`ANGER` is 33, `ANGRY` is 40). **Never renumber to close
a gap.** Renumbering an existing id is stop-the-line (conventions §5, design
§4.4) and would cost a design-doc change-log entry plus a schema-version bump —
never worth a cosmetic tidy. Re-derive the live id list from `registry/*.yaml`,
never from this paragraph. (Note the one place contiguity *is* required and is a
different thing: `relics.yaml` `pool_order` within a tier.)

**`PowerId` 47, 54–58 and 60–72 are PERMANENT gaps.** 47 was reserved by B3.21,
which took `MODE_SHIFT` 45 and `SHARP_HIDE` 46 and then did not need a third;
**B3.8 correctly refused to backfill it** and appended its six powers at 48–53.
54–58 were left behind in the same way — B3.27 appended `CONFUSION` at 59 rather
than filling them. **60–72 are the third instance**: B3.15 appended `ENTANGLE`
73 / `SPORE_CLOUD` 74 out of the block allocated to it (the rationale is
recorded in `powers.yaml`'s own B3.15 header comment) rather than reaching down
into the free window below it. Every task holding a reservation in these three
windows has landed, so none of them will ever be issued. Filling a permanent gap
is an id renumber in all but name: it changes which power a stored
`PowerSlot.power_id` byte means, which is exactly what the append-only rule
exists to prevent. Leave them empty.

**Allocated blocks belong in this file, not only in a brief.** All three gaps
above exist because a block was handed to a task that then needed fewer ids than
it reserved — which is correct and costs nothing. What is *not* free is leaving
the allocation only in the dispatching brief: a worktree cannot see its siblings,
the brief is gone once the wave lands, and the next reader is left with an
unexplained thirteen-wide hole and no way to tell a reservation from an error.
Record the block here when it is allocated.

**2. `MonsterIntent` values in `tools/registry_gen/stsgen/vocab.py` are a
shared namespace and need orchestrator allocation before use.** The generator
rejects an unknown intent, so a monster task needing a new telegraph *must*
edit `vocab.py` — a file otherwise off-limits to content tasks. B3.19 hit
exactly that and took the edit. Claim the number in the task brief; do not
pick one locally.

| Intent | Value | Owner |
|---|---|---|
| `SLEEP` | 9 | B3.19 — landed |
| `STUN` | 10 | B3.19 — landed |
| `DEFEND` | 11 | B3.16 — landed (also The Guardian's Charge Up: one Intent constant, two users) |
| `ATTACK_BUFF` | 12 | B3.21 — landed |
| `ESCAPE` | 13 | **allocated — B3.15 remainder** (Looter's `Intent.ESCAPE` telegraph) |
| *(reserve)* | 14 | **allocated — B3.15 remainder**, contingency only; report it if used |
| *(free)* | 15–16 | **unallocated.** Claim one in a task brief before use |

### Wave-A allocations — 2026-07-26, three concurrent worktrees

Published here rather than only in the dispatching briefs, per the rule above.
**Every block is exclusive to its task**; a task that needs fewer ids than it
holds leaves the rest a permanent gap (that is fine and costs nothing — see the
`PowerId` paragraph). A task that needs *more* stops and asks the orchestrator;
it does **not** take the next free number, because its siblings are invisible to
it.

| Namespace | B3.15 remainder (`b315-looter`) | B4.5 (`b45-rewards`) | B3.10a | B3.10b |
|---|---|---|---|---|
| `CardId` | — | — | **92, 93, 95, 97, 99, 100, 102, 103, 105, 106, 107, 108, 110, 111** | **94, 96, 98, 104** |
| `PowerId` | **75–76** | — | **77** | **78** (+79–80 reserve) |
| `MonsterId` | **26** (LOOTER) | — | — | — |
| `MonsterIntent` | **13** (+14 reserve) | — | — | — |
| Opcode | **40–42** | **43–44** | **45–48** | **49–52** |
| `ChoiceKind` | — | **6–7** | — | — |

**The `CardId` blocks are deliberately interleaved, not contiguous.** B3.10's
twenty cards are numbered 92–111 in `CardLibrary.addColorlessCards` **library
(alphabetical) order** (`CardLibrary.java:799-834`), and that order is worth
preserving because the colorless pool B3.10b needs is emitted from it — getting
it right here avoids the documented interim deviation `kIroncladAttackPool`
still carries. So the split leaves **interior gaps** that B3.10b and the
choice-surface owner fill later. This is legal and explicitly fine: `loader.py`
enforces id **uniqueness**, never monotonicity. Reserved interior ids: **101**
(Forethought) and **109** (Purity) — see the optional-multi-select obligation row.

**What was actually spent** (recorded 2026-07-26, after the wave landed — the
unspent ids below are now **permanent gaps and must never be backfilled**):

| Task | Spent | Left as a permanent gap |
|---|---|---|
| B3.15 remainder | `MonsterId` 26, `PowerId` 75 `THIEVERY`, `MonsterIntent` 13 `ESCAPE`, opcode 40 `ESCAPE` | `PowerId` 76, `MonsterIntent` 14, opcodes 41–42 |
| B4.5 | *(none — needed no new registry id)* | opcodes 43–44, `ChoiceKind` 6–7 |
| B3.10a | 14 `CardId`s, `PowerId` 77 `NO_BLOCK`, opcodes 45–48 `DAMAGE_DRAW_PILE`/`CONDITIONAL_DRAW`/`RESHUFFLE_ALL`/`MADNESS` | — (spent its block exactly) |
| card-limbo | opcode 53 `USE_CARD` | — |

`USE_CARD` does not consume the numerically earlier 49–52: that block remains
the exclusive live reservation for open task B3.10b. Nor does it backfill
41–44, which became permanent gaps when their owners landed. The append-only
allocation therefore advances from the highest live reservation to 53.

**`MonsterState.flags` is a two-region `uint32_t` — allocate from the right
region.** It was a `uint16_t` and ran out (`kMonsterFlagEscaped` took the last
bit); it was widened to 32 bits on 2026-07-26, owner-approved, as
`SCHEMA_VERSION` 4 → 5 (design §11 v0.1.5). **The width was the smaller half of
the fix.** Bits had been handed out **linearly**, a fresh bit per monster type,
though no monster is two types at once — nine Act-1 types burned all sixteen
bits while the worst single type (The Guardian) needs five. Widening alone would
have re-exhausted the word in Act 2. The rule now:

- **Bits 0–23 — TYPE-SCOPED and reusable.** A bit means whatever the owning
  monster type says. Two types may share a bit **only** if no single monster is
  both. Existing constants keep their historical values; do not re-base them.
- **Bits 24–31 — GLOBAL, scarce, orchestrator-allocated.** For flags that
  *type-agnostic* code reads. Today there is exactly one: `kMonsterFlagEscaped`
  (bit 24), read by `monster_dead_or_escaped()` from the queue, interpreter,
  card-play, damage, power-hook, advance and Spore Cloud paths.
- **The power caveat.** A bit consumed by a *power's* native body is scoped to
  that power's possible **owner set**, not to one monster type — the CurlUp and
  Ritual latch sites key on `PowerId`, not `monster_id`. A type reusing such a
  bit must also never be able to own that power.

**When widening a flag field, hunt the truncating writes.** The u16→u32 change
found several `static_cast<uint16_t>` flag writes that would have **silently
cleared the whole global region** — quietly erasing `Escaped` — on every write.
The compiler cannot help: narrowing through an explicit cast is what the cast
asks for. They were removed across eight source files and two tests.

**Three shared-file rules this wave, two of which a brief got wrong once:**

1. **`tools/registry_gen/stsgen/vocab.py` holds two different namespaces and they
   have different rules.** `MONSTER_INTENTS` is **exclusive to the B3.15
   remainder** this wave. `OPCODES` (`vocab.py:17-119`) is **shared and
   append-only within each task's allocated block** — every wave-A task holds an
   opcode block, so a blanket "vocab.py is off limits" makes the allocation
   unspendable. Name the *namespace*, never the file.
2. **Adding an opcode is a three-file change, not one.** Besides `vocab.py`'s
   `OPCODES`, it requires `tools/registry_gen/stsgen/steps.py` — whose
   `assert _GROUPED == set(OPCODES)` hard-fails on an unclassified opcode, so
   **every** opcode-holding task touches it — and `include/sts/engine/cards.hpp`'s
   drift `static_assert`. Brief all three together (the B3.17 precedent).
3. **`registry/powers.yaml` is shared**, append-only within disjoint blocks — the
   B3.8 ∥ B3.27 precedent. The hazard is not the YAML, which appends at different
   places: it is the **count guards**, which every adding branch must move to
   compile, so each carries a *different* correct-for-itself value and the merge
   can land either silently. That is integration-14's defect exactly. The
   integrator **re-derives the union from the regenerated manifest**; no branch's
   number is authoritative.

**Count-guard site inventory** — re-derived 2026-07-26, and longer than the
brief that first quoted it claimed. Move every site in a family together:

| Constant | Sites |
|---|---|
| `kPowersCount` | `interp_block.cpp:40`, `interp_damage.cpp:63`, `:106`, `:157`, **`tests/registry_gen_test.cpp:407`** |
| `kCardsCount` | `tests/registry_gen_test.cpp:403`, `tests/registry_gen_standalone.cpp:25`, **`src/engine/relic_pools.cpp:207`** |

`relic_pools.cpp:207`'s assert message asks a **real question** — whether the new
rows change Bottled Tornado's gate — so it is answered, not bumped. (For B3.10's
twenty: none is BASIC or POWER-type, so the gate is unaffected.)

## Landed non-task work

Changes on `master` that are **not** ledger tasks, so they have no task block to
archive and no `#log` anchor. They are indexed here for the same reason task
entries are: conventions §2 requires that history and this ledger never
disagree, and a fix or a toolchain change that nothing records is invisible to
the next session.

- **Queued-card dequeue revalidation and dead/escaped split** `[x]` — commit
  `9484f70` identified the missing pre-hook gate but its hp-only implementation
  and Log were superseded by the immediate independent-audit fix-forward. The
  resolver and `legal_actions` now share the full in-scope `canUse` authority
  (status/curse relic escape hatches, turn, energy/autoplay, Entangle, Normality,
  Velvet Choker, Clash and `cardPlayable`). Existing YAML now generates the
  exact CardTarget kind: only `ENEMY` takes the successful-gate post-hook
  null/dead/escaping suppression; `SELF_AND_ENEMY` proceeds. Failed autoplay
  receives exact no-trigger `UseCardAction` semantics, including at terminal.
  Autoplay no longer mutates `cost_now`; X-cost draw-top plays preserve energy
  and a Double Tap purge copy carries its captured `energyOnUse` transiently
  without touching persistent card misc. No YAML/ID/opcode/state-layout,
  fixture, or golden change. Frozen §5.3 and §12 were mechanically corrected.
  [Archive log.](stage-b-log.md#card-dead-target)
- **Run-layer escape obligation + Smoke Bomb flag-parity audit** `[x]` —
  commit `82d497a` plus its immediate fix-forward (this ledger change).
  Discharges and deletes both deferred rows that named the next
  `run_advance.cpp` owner: the run-layer half of escape (outcome, stolen-gold
  settlement, and `live_target`) and the stale file-header monster roster.
  `RunCombatOutcome::MUGGED = 4` is append-only over values 0–3; value 5 stays
  deliberately reserved for the currently unreachable un-mugged monster-escape
  shape. The reward layer distinguishes the screen choice from
  `MonsterGroup.haveMonstersEscaped`: mugged outranks smoked for the screen
  (`AbstractRoom.update`, AbstractRoom.java:335-338), while an all-escaped
  plain combat suppresses its gold and zeroes its potion chance but still rolls
  cards. A killed Looter's clamped accrual returns as a first-position
  `STOLEN_GOLD` item; an escaped Looter's does not
  (`Looter.die`, Looter.java:159-174; `DamageAction.stealGold`,
  DamageAction.java:98-114; `AbstractRoom.addStolenGoldToRewards`,
  AbstractRoom.java:619-626; `RewardItem.applyGoldBonus` /
  `RewardItem.claimReward`, RewardItem.java:112-129,255-273).
  Independent review caught one rejected-branch defect before landing:
  `step_potion` opened the right MUGGED/SMOKE_BOMB screen but bypassed
  `SmokeBomb.use`, leaving `kCombatFlagPlayerEscaped` clear. The fix calls the
  native body before relic hooks and slot destruction, matching
  `PotionPopUp.updateInput`'s `potion.use` → `relic.onUsePotion` →
  `destroyPotion` order (PotionPopUp.java:234-239) and
  `SmokeBomb.use`'s smoked/isEscaping writes (`SmokeBomb.java:37-48`).
  Named regression assertions cover both ordinary Smoke Bomb and the
  mugged+smoked two-flag state while preserving MUGGED screen precedence.
  No persistent-state layout, registry id, opcode, fixture, or golden-vector
  change; this is deferred-obligation non-task work, so there is no task block
  to archive.
- **Combat start: turn 1 must not run the end-of-round pass** `[x]` — commit `821bffd`, merged at `9dea548`, landed in `56248c5`. `combat_begin` and `enter_combat` both primed turn 1 with `turn_has_ended = 1` and pumped, routing through `start_of_turn` → `dispatch_at_end_of_round` **before the player's first turn**, so every end-of-round hook on a power present at combat start fired once for free. **The game cannot reach that branch**: `AbstractRoom.java:236-243` sets the flag and then queues `GainEnergyAndEnableControlsAction`, which clears it (`:35`) — the queue is never empty while that item is pending, so the step-6 test is false by the time it is reached. **Measured**: a sleeping Lagavulin had **16 block on turn 1 instead of 8** (monster block never decays, so it also gained +8 every later turn). Fixed at **both** entry points via a shared `begin_first_turn` that reuses the same `start_of_turn` with a `TurnStart` parameter; **two by-construction guards scan both files**, so a one-sided regression fails rather than drifts. All 20 committed fixtures replayed **zero-diff**, proving the spurious pass was inert for every piece of landed content. The post-draw *powers* twin was reported and deliberately left — it is the `fix-postdraw-gate` row in the obligations table.
- **`MonsterState.flags` widened u16 → u32, schema v4 → v5** `[x]` — commit `2684548`, landed in `a32e84c`. **Owner-directed** (2026-07-26), recorded in the frozen spec's change log as design §11 **v0.1.5**. The width was the smaller half of the fix: bits had been allocated **linearly**, one fresh bit per monster type, though **no monster is two types at once** — nine Act-1 types consumed all sixteen bits while the worst single type (The Guardian) needs five, so widening alone would have re-exhausted the word in Act 2. The two-region policy is in **Shared namespaces** above; `kMonsterFlagEscaped` moved bit 15 → **24**, every type-scoped bit kept its historical value. **A latent hazard the widening exposed:** several flag writes were `static_cast<uint16_t>`, which after widening would have **silently cleared the entire global region** — quietly erasing `Escaped` — on every write; the compiler cannot catch it, because narrowing through an explicit cast is exactly what the cast asks for. Removed across eight source files and two tests. `sizeof(MonsterState)` 112 → 116 and `sizeof(CombatState)` 3896 → 3928, **measured by compiled `offsetof`/`sizeof` probes, not predicted**, under the 8192 ceiling v0.1.4 raised. **All 20 combat fixtures regenerated — the single sanctioned exception to this project's never-modify-a-committed-fixture rule**, owner-approved; nothing under `tests/golden/` outside `combat_fixtures/` moved. The B3.12/B4.3 single-zero-run-insertion proof shape **was not available and was not faked**: widening an *interior* field moves every later offset and deletes an old alignment pad, so an equivalent per-field proof was produced from probe-derived offsets over 20 fixtures / 112 records.
- **Combat start: turn 1 must not run the post-draw power pass** `[x]` — commit `f05ad8a`, merged at `a5afbf9`, landed in `06c4fa0`. The **sibling** of the end-of-round gate above and the second half of the same divergence, discharging the obligation integration-14 left. `start_of_turn`'s end-of-round pass, Ice Cream energy branch and block decay were all gated to `kSubsequentTurn`; `dispatch_at_start_of_turn_post_draw` was not, so it also ran while priming turn 1. **The game has no counterpart there**: `AbstractRoom.update`'s turn-1 block calls `applyStartOfTurnRelics` (`:253`), `applyStartOfTurnPostDrawRelics` (`:254`), `applyStartOfTurnCards` (`:255`), `applyStartOfTurnPowers` (`:256`) and `applyStartOfTurnOrbs` (`:257`) — and **no `applyStartOfTurnPostDrawPowers` line at all**; `GameActionManager.java:363` (step 6) is the whole game's only caller. The two halves are **not** a pair — the relic half really is on both sides, which is why only the power half moved. Inert for all landed content (Brutality and Demon Form are the only binders, and both require playing their card), so **no test could see it**: the two new `combat_start_test` cases construct the state with the power already present, and the fix was **demonstrated RED before green** — re-arming the dispatch failed exactly those two and nothing else. Fixtures replayed unchanged.
- **Citation audit + Pantograph's inverted DEFERRED marker** `[x]` — commit `39876f0`, in `06c4fa0`. Comment/provenance only: no executable line, registry value or generated-table value changed. Two defects, **both invisible to git because neither branch conflicted** — the conventions §8 class, and a direct instance of "a conflict-free merge is not evidence of correctness". (1) `AbstractMonster.die()` was cited as `:741-750` at nine new on-death sites; `die(boolean)` is at `:925` and `:741-750` is render code, so **the merge replaced master's already-correct `:933-937` with a wrong one** — re-read and corrected to `isDying` `:927` / powers' `onDeath` `:928-932` / relics' `onMonsterDeath` `:933-937`. (2) `EntanglePower` was cited as `:50-53` at eight sites in a **47-line file**, i.e. resolving to nothing — corrected to `:15-47` / `:17` / `:20-29` / `:31-46`. The described *behaviour* was right throughout in both cases; only the line numbers were wrong. (3) `relics.yaml`'s Pantograph row carried a `DEFERRED` marker **on live, tested code** — the same bug signal running the other way — retired after verifying the native body and its four tests; both premises of the deferral are dead (`enemy_type` column + `MonsterDef::is_boss()`, and three BOSS rows exist). The other ~39 `DEFERRED` markers in that file are legitimate and were not touched.
- **Build / toolchain effort** `[x]` — commits `44c1e11` (FP contract), `b481db2` + `c568cad` (ctest parallelism, job gate, shared ccache), `93e7a7d` + `c6913f2` (native Windows), merged at `28eab81`, landed in `8235477`. **The floating-point contract is pinned** (`-ffp-contract=off`, `cmake/StsFloatingPoint.cmake`): the frozen oracles were **not** captured under contraction only because baseline x86-64 has no FMA instruction — one `-march=native` away from silently changing damage numbers — and `fp_contract_test` now fails if contraction returns. `ctest` runs parallel; a machine-wide job gate bounds concurrent build parallelism across worktrees; ccache is shared across worktrees via `CCACHE_BASEDIR` (measured 0 % → 50 % cross-worktree hit rate). **A native Windows target was added** — clang-cl 22.1.8, presets `win-debug` / `win-release` / `win-asan` — and it is **byte-identical to Linux**: the 20 fixture traces hash to `ccdc4432…` under GCC 13.3, Clang 18.1.3 and clang-cl alike (debug, LTO release, asan). **WSL is therefore optional, including for sanitizers**: ASan *and* UBSan both work under clang-cl. `cl.exe` silently ignores `/fsanitize=undefined` (warning D9002, exit 0), which is why clang-cl is **required** here rather than merely preferred. The Windows CI job is proposed but unverified — see the obligations table.

---

## Phase B0 — Bridge groundwork

- **B0.1** `[x]` CommunicationMod source pin + protocol survey — upstream CommunicationMod pinned at v1.2.1 (`70ca84b1`), MIT, vendored source-only; `PROTOCOL.md` gives all 141 `GameStateConverter` fields a disposition; `start` seed proven to be the base-35 display string · [log](stage-b-log.md#b01)
- **B0.2** `[x]` Stock-jar bridge bring-up + environment audit — `echo_driver.py` + scriptable ModTheSpire launch; 205-state seeded A20 capture replays clean; `start` == the game's own seeded-run UI byte-identically; ~0.36 states/s stock baseline; profile-unlock audit PASS (do not “fix” `IRONCLADUnlockLevel`) · [log](stage-b-log.md#b02)

---

## Phase B1 — The oracle fork, driver, translator (Gate G4 = M2)

- **B1.1** `[x]` Fork build pipeline — `build_fork.ps1`: JDK-8, no-Maven, deterministic jar (sha `6AB875C8…`); fork == stock on 206/206 normalized states; modid `CommunicationMod-oracle` · [log](stage-b-log.md#b11)
- **B1.2** `[x]` Oracle state block — the `oracle` JSON block behind fork flag `oracleBlock`, carrying 14 RNG streams `{counter,s0,s1}` + 2 pity + 3 event-pity floats + `purgeCost` + 3 remaining-pool lists + 5 relic-pool orders + move history; `relicRng.counter`==5 at init; floors 1-3 == `floor_stream` bit-for-bit · [log](stage-b-log.md#b12)
- **B1.3** `[x]` Rendering-strip / fast-forward patches — 3 individually-toggleable strip families; 20 seeds / 996 records A/B: 12/20 byte-identical, 8/20 differ only in enumerated presentation fields, 0 semantic leaks; sustained **32.0 act/s** (≈89× baseline) · [log](stage-b-log.md#b13)
- **B1.4** `[x]` Campaign driver — `campaign_driver.py` + `orchestrator.py` + `validate_artifacts.py`; unattended 10-seed A20 campaign 10/10, 574 actions, survived an induced mid-campaign game kill; artifacts schema-valid · [log](stage-b-log.md#b14)
- **B1.5** `[x]` Translator (JSON → binary schema) — `tools/oracle_bridge/translator/` (nlohmann, tools-only) JSONL → RunState/CombatState with a fail-loud disposition table; 7 `translator_test` cases; 147/147 · [log](stage-b-log.md#b15)
- **B1.6** `[x]` Diff-harness run-level + oracle adapter — trace format v2 (`state_kind` discriminator), `SCHEMA_VERSION` 1→2 with v1 compat-read (zero fixture regeneration), `diff_run_states` field groups, `CommunicationModOracleAdapter`; +16 tests, 163/163 ×3 · [log](stage-b-log.md#b16)
- **G4** `[x]` **Gate: oracle bridge live (M2)** — tag `g4-bridge-live` — 20 seeds / 996 records: **zero unknown-FIELD errors** (94 unknown content ids tallied), 75 floor-entry RNG cross-checks vs `floor_stream`/`map_stream`, strip equivalence + 32.0 act/s re-confirmed on the final build (sha `04477E4E…`); 166/166 ×3 · [log](stage-b-log.md#g4)

---

## Phase B2 — Registry system + skeleton migration (Gate G5) — ∥ with Phase B1

- **B2.1** `[x]` Registry schema + codegen tool — `registry/*.yaml` for all 8 domains + `tools/registry_gen/gen.py` (PyYAML) emitting id enums / effect-program tables / game_id tables / manifest; deterministic; 137/137 · [log](stage-b-log.md#b21)
- **B2.2** `[x]` Skeleton migration onto the registry — hand tables deleted (no dual system), engine re-exports the generated enums/tables; Stage A's 131 tests + all 20 fixtures pass with **zero test edits and zero fixture regeneration**; 140/140 · [log](stage-b-log.md#b22)
- **G5** `[x]` **Gate: registry live** — tag `g5-registry-live` — 140/140 through the generated path in debug+asan+release; manifest == the skeleton counts (9 rows); CI installs `python3-yaml` and runs the generator · [log](stage-b-log.md#g5)

---

## Phase B3 — Combat content closure (all tasks additionally gated on G4 + G5)

Each content task ships: registry entries with provenance (every cited
`use()`/ctor/`getMove` **read in full** before encoding), any new
powers/opcodes those entries need (append-only numbering), tier-2 table tests
per entry, a directed tier-3 script added to the campaign script library, and
— once the run layer exists — at least one oracle spot-diff exercising the
batch. Until run-level replay lands (B4.4), oracle spot-diffs for combat
content use combat-state comparison inside bridge runs whose context the
translator can seed (B1.6), or are deferred to the batch's campaign debut and
noted in the Log.

- **B3.1** `[x]` Interpreter/card-mechanics extensions — opcodes `MAKE_CARD`=9 / `SET_COST`=10, ALL_ENEMY+RANDOM_ENEMY targeting, `CardFlag` bits (EXHAUST/ETHEREAL/INNATE/UNPLAYABLE/RETAIN/XCOST), X-cost, two-row upgrade plumbing; +17 tests, 191/191 ×3 · [log](stage-b-log.md#b31)
- **B3.2** `[x]` Power-hook framework completion — 14 hook points dispatched in the frozen §5.2-5.5 per-hook source order, `powers.yaml` `hooks:`/`native:` schema, opcode `LOSE_HP`=11, framework powers ids 4-12; +12 tests, 213/213 ×3 · [log](stage-b-log.md#b32)
- **B3.3** `[x]` Red commons — attacks — 13 red common attacks, card ids 11-23, + Wound (id 24, first STATUS); opcodes `DAMAGE_BLOCK`=15 / `DAMAGE_STR_MULT`=16 / `DAMAGE_PER_STRIKE`=17; +35 tests, 308/308 ×3 · [log](stage-b-log.md#b33)
- **B3.4** `[x]` Red commons — skills — 5 skills, card ids 6-10, + LoseStrength power 13; CHOOSE-in-combat lives in the action queue — opcodes `CHOOSE_CARD`=12 / `PLAY_TOP_DRAW`=13 / `REMOVE_POWER`=14; +18 tests · [log](stage-b-log.md#b34)
- **B3.5** `[x]` Red uncommons — attacks — 11 uncommon attacks, card ids 40-50, opcodes 21-24; Searing Blow encodes its multi-upgrade in the existing `CardInstance.upgrade` count (no schema change); +18 tests, 405/405 ×3 · [log](stage-b-log.md#b35)
- **B3.6** `[x]` Red uncommons — skills — 17 uncommon skills (source enumeration moved **Rage** here from B3.7), card ids 51-67, powers `NO_DRAW`=24 / `FLAME_BARRIER`=25, opcodes 30-33, `ChoiceKind::DUPLICATE`=4, `CardType::POWER`=4; 4 stop-the-line Java corrections fixed forward; +35 tests, 464/464 ×3 · [log](stage-b-log.md#b36)
- **B3.7** `[x]` Red uncommons — power cards — 8 uncommon power cards, card ids 68-75, powers 26-27, POWER-card play path (apply + remove from every pile, no discard); plus a fix-forward importing Combust `hpLoss` from oracle power `misc`; 526/526 debug + asan · [log](stage-b-log.md#b37)
- **B3.8** `[x]` ∥ Red rares — 16 rare cards, ids 76-91; powers 48-53 (**Corruption needed no row** — id 11 already existed, and `PowerId` 47 was left the gap B3.21 reserved); opcodes 34-39 incl. **`PLAY_CARD`, the general recursive-play verb** (B3.11's Mayhem reuses it unchanged); `ChoiceKind::EXHAUST_TO_HAND`=5, `CardFlag::PURGE_ON_USE`; discharges `healing: true` on Feed/Reaper (`kIroncladAttackPool` 25 → 28, both heals through `heal_player_with_relics`), Barricade's block-decay branch on the `kSubsequentTurn` side, and the recursive-play opcode · [log](stage-b-log.md#b38)
- **B3.9** `[x]` Status + curses — 4 statuses + 11 curses, card ids 25-39; opcodes `LOSE_HP_PER_HAND`=18 / `DISCARD_HAND`=19 / `REDUCE_POWER`=20, native `FRAIL`=21; end-of-turn order rewritten; all 20 combat fixtures regenerated from the checked-in generator; 368/368 · [log](stage-b-log.md#b39)

### B3.10 — **SPLIT 2026-07-26 into B3.10a / B3.10b + a deferred pair**
The enumeration was verified against source before the split and is **exactly
the twenty the original entry listed** — no card moved between tasks the way
B3.6 gained Rage. `CardLibrary.addColorlessCards` (`CardLibrary.java:799-834`)
orders them alphabetically, and ids 92–111 map 1:1 onto that order.

The split is a **scope** finding, not a scoping preference: ten of the twenty
need no new machinery, and the other ten each need a *distinct* verb. Conventions
§5 says a task whose real scope turns out materially larger than its ledger entry
gets the split proposed first, so the dispatched agent stopped without
committing rather than take unallocated ids or land a fragment. That was correct.

- **B3.10a** `[x]` ∥ Colorless uncommons — the fourteen reachable without new choice machinery, card ids 92-111 **sparse** (interior gaps pinned empty by a named test), opcodes 45-48, power 77 `NO_BLOCK`. Split out after the whole-B3.10 agent read every `use()` and **stopped without committing** — ten of the twenty need a distinct verb each, ~2.5× the allocated budget. **`NO_BLOCK` overrides `modifyBlockLast`, not `modifyBlock`** — the game’s only overrider of that hook — so `interp_block.cpp` gained a genuine **second pass**, demonstrated RED before green and pinned by one named test (the Log says so, because the other seven block cases pass under the wrong shape). Deep Breath exposed the **card-in-limbo** divergence; the local compensation and general deferral recorded at landing are now discharged by `card-limbo` below · [log](stage-b-log.md#b310a)
- **card-limbo** `[x]` Played-card limbo / queued `UseCardAction` filing — discharges B3.10a's engine-wide obligation, removes the Headbutt / Deep Breath / Havoc local compensations, allocates engine-emitted opcode 53 without consuming B3.10b's 49–52 reservation, and uses append-only `CardFlag` bit 8 for Java's one-play `exhaustOnUseOnce` lifetime · [log](stage-b-log.md#card-limbo)

### B3.10b `[ ]` Colorless uncommons — the four needing a colorless pool
**Deps:** B3.10a (shares `cards.yaml`; branch off it, do not run beside it)
**Deliverables:** Dark Shackles, Discovery, Enlightenment, Jack of All Trades —
ids **94, 96, 98, 104**, opcodes **49–52**, power **78** (Dark Shackles'
`GainStrengthPower`, "Shackled").
**The real deliverable is the colorless card pool.** Discovery and Jack of All
Trades draw from `srcColorlessCardPool` =
`AbstractDungeon.returnTrulyRandomColorlessCardInCombat` (`:981-995`) — **all 35
colorless uncommons + rares minus HEALING** (Bandage Up), so 34 candidates. No
colorless pool exists: `kIroncladAttackPool` is `color == "RED"`-gated and
`kPoolableCurses` is the only other. The pool builder in `emit/cards.py:275-287`
is the template, and membership **self-completes when B3.11 lands** — the same
precedent B3.8 set. Discovery additionally needs a choice source that is a
**generated 3-card set**, where `ChoiceSource` is `HAND`/`DISCARD`/`EXHAUST`
today.
**Acceptance:** tier-2 per card; Discovery `cardRandomRng` draw accounting;
directed script.
**Log:** —

### B3.11 `[ ]` ∥ Colorless rares
**Deps:** B3.2 · **Provenance:** cards/colorless RARE (15)
**Deliverables:** registry entries for the 15 (Apotheosis, Chrysalis, Hand of
Greed, Magnetism, Master of Strategy, Mayhem, Metamorphosis, Panache, Sadistic
Nature, Secret Technique, Secret Weapon, The Bomb, Thinking Ahead,
Transmutation, Violence — verify from source).
**Acceptance:** tier-2 per card; directed script.
**Inherited:** the recursive-play opcode (Mayhem) and per-power counter storage for
Panache (every-5th) and The Bomb (3-turn) — deferred by B3.2 (no new `PowerSlot`
field was added there). **The opcode itself now exists:** B3.8 landed `PLAY_CARD` = 34
as the *general* verb, and Mayhem reuses `{op: PLAY_CARD, play: [from_draw_top]}`
unchanged — what is left here is the authoring, not the verb.
**Log:** —

- **B3.12** `[x]` Multi-monster combat + encounter framework — `encounters.yaml` with 20 Act-1 encounters and their miscRng composition programs; `resolve_composition`/`generate_monster_lists`/`spawn_group`/`dispatch_monster_turn`; **schema 3→4** (`kMonsterCap` 5→7, `sizeof(CombatState)` 3672→3896); 20 fixtures regenerated with a byte-level zero-diff-in-meaning proof; +13 tests, 286/286 ×3 · [log](stage-b-log.md#b312)
- **B3.13** `[x]` Monsters: Cultist + louses — monster ids 2-4 (Cultist, LouseNormal, LouseDefensive) + power `CURL_UP`=20; committed independent XS128 fixtures, 32 seeds × 20 turns per monster; 359/359 · [log](stage-b-log.md#b313)
- **B3.14** `[x]` Monsters: small/medium slimes — monster ids 5-8 (Spike/Acid Slime S+M) + `MonsterIntent::ATTACK_DEBUFF`=6; XS128 fixtures ×4; 413/413 integrated · [log](stage-b-log.md#b314)

- **B3.15** `[x]` Monsters: slavers + Looter + Fungi Beast — landed in **two deliberate halves**. Half one (`f24b8db`): monster ids 23-25 (Blue/Red Slaver, Fungi Beast), powers `ENTANGLE`=73 / `SPORE_CLOUD`=74, the first `Hook::ON_DEATH` dispatch; **Entangled is a legality predicate, not a power hook**, and Spore Cloud's `isDying`-before-the-power-walk ordering means the first of two Fungi Beasts releases 2 Vulnerable and the last releases none. The Looter was **withheld rather than improvised**: the engine's `hp > 0` liveness signal could not express the game's `isDying || isEscaping`, and `pump_step` recomputes the phase from it every iteration so there was no alternative seam — four obligation rows that looked like four problems were **one**. Half two (`a2a60df`): monster id 26, `PowerId` 75 `THIEVERY`, `MonsterIntent` 13 `ESCAPE`, opcode 40 `ESCAPE`, and **every** "in the fight" read converted against its Java citation across eight files — **no `CombatState` field, no schema bump**, 20 fixtures unchanged. Un-parked its groups by construction with no `run_advance.cpp` edit · [log](stage-b-log.md#b315)

- **B3.16** `[x]` ∥ Monsters: gremlin gang — monster ids 16-20, `PowerId::ANGRY`=40, `MonsterIntent::DEFEND`=11; six independent 32-seed × 20-turn XS128 fixtures incl. a 4-gremlin battery pinning the Tsundere's `aiRng` block-target pick; **move 99 (ESCAPE) is unreachable in Act 1** and left unmodelled with both halves recorded for Act 2; Angry's `damageAmount > 0` guard reads **post-block** damage; registering the five init fns un-parked the encounter with no `run_advance.cpp` edit · [log](stage-b-log.md#b316)
- **B3.17** `[x]` Monsters: large slimes + split — monster ids 9-10 (large slimes), `PowerId::SPLIT`=22, opcodes 25-29 (`CANNOT_LOSE`/`CAN_LOSE`/`SUICIDE`/`SPAWN_MONSTER`/`SET_MOVE`); the Java-exact split framework; 441/441 ×3 · [log](stage-b-log.md#b317)
- **B3.18** `[x]` Elites: Gremlin Nob + Sentries — monster ids 12 GREMLIN_NOB / 13 SENTRY (both ELITE) + `PowerId::ANGER`=33; Artifact needed **no** new row (B3.2's id 4, the nullify already at the APPLY_POWER site — Sentry only grants the stack); 3 independent 32-seed × 20-turn fixtures pin the Nob's A18 history tree and the Sentry in an even and an odd slot; registering the two init fns un-parked the Gremlin Nob and 3 Sentries encounters by construction; Sentry's animation-only `damage()` is an explicit empty `on_monster_damaged` case, not a `default:`; union 641/641 ×3 · [log](stage-b-log.md#b318)
- **B3.19** `[x]` Elite: Lagavulin — monster id 15 LAGAVULIN (ELITE), native sleep/wake machine; **no new power id** (Metallicize was already id 5 — now the first MONSTER-owned power to bind an end-of-turn hook, and the generator's duplicate-name check caught the re-add); `MonsterIntent` SLEEP=9 / STUN=10; `on_monster_damaged` gains `hp_lost` so absorbed damage cannot wake it; armour stands at 8/16/24 because monster block never decays in this build; un-parked the Lagavulin encounter by construction; union 641/641 ×3 · [log](stage-b-log.md#b319)
- **B3.20** `[x]` Boss: Slime Boss — monster id 11 + `MonsterIntent::STRONG_DEBUFF`=8; fixed 150 HP, Goop→Prep→Slam cycle, exact-half split chaining into B3.17's large slimes; 521/521 debug + asan · [log](stage-b-log.md#b320)
- **B3.21** `[x]` ∥ Boss: The Guardian — monster id 21, native offensive/defensive mode machine, powers MODE_SHIFT=45 / SHARP_HIDE=46 (**47 deliberately unused, never to be backfilled**), intents DEFEND=11 / ATTACK_BUFF=12; the mode threshold **grows by 10 at every Defensive-Mode entry** (40, 50, 60 … at A20 — driven for three real flips, not computed), and mode state is **not** derived from `ModeShiftPower.amount` because `isOpen` is set synchronously while the power is only queued; found and fixed en route: **Spot Weakness paid out nothing against a telegraphed `ATTACK_BUFF`** · [log](stage-b-log.md#b321)
- **B3.22** `[x]` ∥ Boss: Hexaghost — monster id 22; **the orbs are not entities**, so the only combat-relevant state is the scalar `orbActiveCount` (0-6) in three spare `MonsterState.flags` bits with a fourth for `burnUpgraded` — **no `CombatState` field, so no `SCHEMA_VERSION` bump and no fixture regeneration**; Divider = `player.currentHealth / 12 + 1` locked at the ACTIVATE turn; `getMove` never reads its rolled `num`, so the draw count is pinned; no new powers, intents or `cards.yaml` edit. **All three Act-1 BOSS encounters are now live** · [log](stage-b-log.md#b322)
- **B3.23** `[x]` Potions — `potions.yaml` with 33 Ironclad-pool rows in pool order; `potions.hpp/.cpp` `use_potion`; trap-14 rejection-sampling identity roll (draw-count pinned); `potion_slot_count(A20)`==2; +19 tests, 232/232 · [log](stage-b-log.md#b323)
- **B3.24** `[x]` Relics: starter + commons — 34 relic rows (Burning Blood + 33 commons), ids 1-34; a distinct `RelicHook` framework dispatching in **acquisition order** (trap 8) with DATA/native bindings; +18 tests, 250/250 · [log](stage-b-log.md#b324)
- **B3.25** `[x]` Relics: uncommons — 30 rows, ids 36-65; the canonical pre-shuffle UNCOMMON `pool_order` recovered by inverting the JDK shuffle against 3 live captures; `RelicHook` `ON_MONSTER_DEATH`=14 / `ON_SHUFFLE`=15; power `NEXT_TURN_BLOCK`=23; Paper Phrog retires stage-a A4.1's “unreachable” note; 454/454 ×3 · [log](stage-b-log.md#b325)
- **B3.26** `[x]` ∥ Relics: rares + shop — 46 rows (28 RARE + 17 SHOP + Odd Mushroom SPECIAL), ids 66-111; powers BUFFER=28 / INTANGIBLE=29, `RelicHook::ON_BLOCK_BROKEN`=16, an `at_turn_start_post_draw` relic dispatch phase, and one `heal_player_with_relics` seam so Magic Flower cannot be forgotten at a heal site; RARE/SHOP `pool_order` recovered by **inverting the JDK shuffle** against ten live captures and validated by reproducing B3.25's UNCOMMON order; discharges Odd Mushroom ×1.25, Calipers, Ice Cream (no-relic path byte-identical) and the RARE+SHOP `pool_order` rows; **Prismatic Shard is a deliberate no-op** with an exact, live pool slot · [log](stage-b-log.md#b326)
- **B3.27** `[x]` ∥ Relics: boss (Neow pool) + event-specials — 31 rows (22 BOSS ids 112-133 + 9 Act-1 SPECIALs ids 134-142), power CONFUSION=59; BOSS `pool_order` recovered by the same shuffle inversion and **validated, not fitted** — the same inversion reproduces the committed COMMON/UNCOMMON/RARE/SHOP orders id-for-id; Snecko Eye drove a new `AT_PRE_BATTLE` relic dispatch with its `cardRandomRng` accounting pinned against an independently derived stream; `gain_gold` is now the single run-layer gold door; discharges the BOSS `pool_order` rows and the translator's all-tier `relicPools` un-deferral · [log](stage-b-log.md#b327)

---

## Phase B4 — The run layer (Gate G6 = M3)

- **B4.1** `[x]` Map path generation — header-only `map_gen.hpp` re-expressing MapGenerator bit-for-bit on `mapRng` (incl. the H5 `getCommonAncestor` bug, proven load-bearing); edges match the oracle **node-for-node for all 20 seeds**; stop-the-line finding: `setEmeraldElite` DOES fire in S1; 174/174 ×3 · [log](stage-b-log.md#b41)
- **B4.2** `[x]` Room-type assignment — header-only `map_rooms.hpp`: quotas (elite ×1.6 at asc≥1), trap-12 raw-XS128 `Collections.shuffle`, placement rules, fixed rows, the emerald draw; room symbols **and** the post-`generateMap` `{counter,s0,s1}` triple match the oracle for all 20 seeds; 201/201 ×3 · [log](stage-b-log.md#b42)
- **B4.3** `[x]` RunState population + additive fields (schema v2) — `sizeof(RunState)` 1648→2184, **schema 2→3**; pity floats/purgeCost/potion slots/membership bitsets/relic-pool storage added; map reoriented to game-native 15×7 (rename only); combat relic mirror (`sizeof(CombatState)` 3504→3672); 20 fixtures regenerated with a byte-level insertion proof; 273/273 ×3 · [log](stage-b-log.md#b43)
- **B4.4** `[x]` Run-level advance + room lifecycle — `RunController` + `run_begin` / `next_room_transition` (floor++ then reseed, trap 7); NEOW/MAP/COMBAT/REWARD/RUN_OVER phases in one heterogeneous batch; USE_POTION at both layers; combat spawn + fold-back; +19 tests, 387/387 ×3 · [log](stage-b-log.md#b44)

### B4.5 `[!]` Combat rewards — **code landed; BLOCKED on the manual oracle capture**
**Blocker:** the Acceptance below requires an oracle spot-diff over **≥ 3 bridge
runs**, and per CLAUDE.md the game is **launched by hand** — no agent can run it.
Everything else landed and is green; the task stays open because §1 says a task
is done only when its Acceptance **passes**, and this one has not been run.
**To close it,** follow the committed runbook
`tools/oracle_bridge/driver/b45_reward_spotdiff.md`: pick 3 seeds (it names which
to avoid and why), launch via `orchestrator.py --campaign-id b45_rewards --policy
random-legal --fresh`, `translate_cli` the artifacts, then diff the **post-claim
`RunState`** per its field table. Expect the deck column to expose the
CardLibrary library-order deviation; the captured offers are what pins the
one-line `gen.py` fix, after which it re-diffs to zero.

**Landed** — commit `4f0544a`, merged at `e222dc2`, landed in `e6ec9ce`:
`combat_rewards.{hpp,cpp}` (assembly at reward-screen open), the `RewardScreen`
phase in `RunController` (**transient — no new storage, no schema bump**, as the
Acceptance demanded), the CHOOSE claim flow, `kIronclad{Common,Uncommon,Rare}Pool`
in `emit/cards.py`, and the translator's reward slice — now **content-validated**,
where previously any `reward_type` name passed. Verified at integration that the
three new pools are correctly RED-gated: **20/36/16 unchanged** after B3.10a added
14 colorless cards to `cards.yaml`, which is the "confirm colorless is unreachable
from a combat reward" deliverable proven rather than argued.
**Three findings the brief and design §5.6 did not carry** — see the change log
for the frozen-doc ruling: **elite card-rarity widths are 10/40** (set in
`MonsterRoomElite`'s *constructor*, not by its `getCardRarity` override, which is
Elite-Swarm-only) and **boss rewards are unconditionally RARE**
(`MonsterRoomBoss.java:40-42`), so boss rewards also reset pity on every card and
never draw the upgrade boolean; **Prayer Wheel** grants a second plain-room card
reward (`CombatRewardScreen.java:89-94`), unmentioned anywhere; and **Smoke Bomb
consumes the battle-over draws** (gold, elite relic pop, potion roll + ratchet),
where B4.4 had modelled escape as a stream no-op — a fix-forward pinned by a
named test.
**Deps:** B4.4, B3.3-B3.9 (the RED reward pool) · **Spec:**
design §5.6 · **Provenance:** AbstractRoom.java:291-296, 314-325, 580-617,
108-109, 148-177; AbstractDungeon.java:1423-1498, 1597-1624
**Deps note** (amended 2026-07-25 from a read-only scout; every citation
verified — see the change log): **B3.10 and B3.11 removed.** The combat
card-reward pool is **RED-only** (`Ironclad.getCardPool` →
`CardLibrary.addRedCards`, `CardLibrary.java:1157`); colorless reaches the
player through the shop and Neow, and the only caller of
`getColorlessRewardCards()` is `RewardItem(CardColor)` →
`SensoryStone.java:121`, an Act-3 event. **B3.8 stays and is promoted from a
coverage dep to a hard mechanical blocker:** `cards.yaml` has **zero RARE
rows**, so `rareCardPool` would be empty and `getCard(RARE)` would index an
empty list — and RARE is reachable as soon as pity reaches 2.
**Deliverables:** gold rolls (boss=miscRng ±5 ×0.75@A13, elite/normal=
treasureRng — trap 18), potion drop (40 % + blizzardPotionMod ratchet, trap
family), card rewards (3 cards, `cardRng.random(99)+cardBlizzRandomizer`
against thresholds **`< 3` / `< 40`** — widths 3/37/60, `AbstractRoom.java:158,
167`, confirmed at `AbstractDungeon.java:1606-1615`; coding the widths as
thresholds is wrong by 3 points on every reward — pity reset/growth,
no-duplicate re-roll — read the dupe loop at task, upgrade chance 0 in Act 1
— **the `randomBoolean` draw still happens** (`Random.java:79-82`); only
`c.rarity != RARE` short-circuits it, `AbstractDungeon.java:1470`), **confirm
colorless is unreachable from a combat reward** with the citation above
(re-scoped from "colorless handling"), reward-screen CHOOSE flow incl. skip.
**Acceptance:** tier-2: pity dynamics across scripted reward sequences match
hand-derivation; stream attribution named tests (trap 13, 18); oracle
spot-diff: ≥ 3 bridge runs' reward screens zero-diff through the differ —
where "reward screens zero-diff" means the **post-claim `RunState`** (gold,
potions, deck, pity, counters). **No new storage and no schema bump.**
Diffing the *offer* would need tools-side differ work or `RunState` growth
(an unplanned `SCHEMA_VERSION` bump is stop-the-line, conventions §5) and is
explicitly **not** what this acceptance asks for.
**Inherited — four of five DISCHARGED:** Question Card / Singing Bowl / White
Beast Statue (B3.25) — implemented and tested. Reward-screen `screen_state`
translation (B1.5/B4.3) — done for the reward slice, and hardened: the
`reward_type` name is now enumerated and fails loud. The **master-deck door**
(hook audit) — every reward card obtains through `add_card_to_master_deck`, with
the requested Ceramic-Fish-gold guard test; `remove_master_deck_card`
legitimately gains no caller, because the game has no reward-screen removal.
**Busted Crown**'s reward count and the **Black Star** elite relic (B3.27) — live
at the combat-reward claim, as are Golden Idol ×1.25 and Sozu's potion block;
their chest/event-screen shares stay with those screens' owners.
**HANDED ON:** the CardLibrary library-order pin (B3.6) — blocked on the same
manual capture that blocks this task, and now covering all four pools.
**Log:** — (task is not done: the oracle spot-diff in its Acceptance has not been
run; the code is landed and green, see the blocker above)

- **B4.6** `[x]` Relic pools + acquisition — `relic_pools.hpp/.cpp`: 5 unconditional relicRng shuffles (JDK-LCG route), front/end pop, 50/33/17 tier roll, canSpawn re-check + Circlet fallback, acquisition in trap-8 order with pickup effects; 3-seed live-oracle pool + `(s0,s1,counter)` match; 428/428 ×3 · [log](stage-b-log.md#b46)

### B4.7 `[ ]` Treasure rooms
**Deps:** B4.6 · **Spec:** design §5.6; §10 trap 16 · **Provenance:**
AbstractDungeon.java:499-508; AbstractChest.java:54-102; Small/Medium/
LargeChest.java:18-22
**Deliverables:** chest size roll, single-roll gold+tier (trap 16), gold
amount ×(0.9,1.1), relic grant via B4.6, the fixed treasure row (map row 8).
**Acceptance:** tier-2: chest tables vs. hand-derivation across the roll
range; trap-16 named test; oracle spot-diff ≥ 2 treasure floors.
**Pending oracle spot-diff — expected shape (design §11 v0.1.6):** the capture
**will** carry one extra trailing `SAPPHIRE_KEY` reward row after the base
relic on every Act-1 chest open (`isFinalActAvailable && !hasSapphireKey`
holds, AbstractChest.java:95-96; `AbstractRoom.addSapphireKey`,
AbstractRoom.java:545-547). It is **expected, not a divergence**: it consumes
no RNG, the sim models no key row, and the translator already classifies the
type as known and ignores `rewards[].link`. The spot-diff must therefore
(a) compare reward rows ignoring that trailing key row, and (b) **claim the
base RELIC, never the key** — claiming the relic marks the linked key row
`isDone`/`ignoreReward` (RewardItem.java:298-300), whereas claiming the key
does the reverse (RewardItem.java:317-322) and would cost the run its relic,
diverging every downstream floor. The one legitimate absence is an N'loth's
Mask open with no Matryoshka bonus: `removeOneRelicFromRewards` deletes the
first RELIC row **and** the row immediately after it when that row is its
`relicLink` (AbstractRoom.java:549-557), taking the key with the base relic.
Any other missing key row, or a key row on a *non*-treasure reward screen, IS
a divergence.
**Inherited — DISCHARGED in code:** Matryoshka (chest relic; floor≤40
canSpawn gate was already live), plus the **Cursed Key** and **N'loth's Mask**
chest hooks deferred by B3.27. All three now have exact non-boss bodies,
boss gates, counter/RNG/acquisition-order tests, and source-order coverage.
**Defensive fix-forward:** one strict descriptor/capacity authority now gates
mask, open, and step; fallible copy-commit reward/hook transactions make
malformed or over-cap forced opens byte-stable in Debug and Release, including
duplicate imported Matryoshkas and near-full public hook calls. The authority
additionally preflights master-deck slots for acquisition-ordered Cursed Keys
under first-Omamori charge depletion (a full-deck curse aborts the whole open
byte-stably instead of being silently dropped), and its descriptor domain is
derived at compile time from `treasure_chest_for_rolls`, so non-constructible
size/tier pairs (SMALL+RARE, LARGE+COMMON) are rejected and the table cannot
drift from the generator. `open_treasure_chest`'s vestigial `misc_rng`
parameter is gone — the open path reads `treasureRng` only
(AbstractChest.java:72), and `miscRng` is first touched at claim time.
**Log:** [implementation and remaining oracle blocker](stage-b-log.md#b47)
(the task stays unchecked until its required live-game spot-diff can run)

### B4.8 `[ ]` Shop
**Deps:** B4.5, B4.6, B3.23 · **Spec:** design §5.6 · **Provenance:**
ShopScreen.java:100-136, 227-292, 340-428, 601-661; Merchant.java (stock
build — read at task)
**Deliverables:** stock generation (5 colored + 2 colorless w/ 0.3 rare
chance, 3 relics incl. end-pop + SHOP tier slot, 3 potions), pricing (base ×
jitter, colorless ×1.2, A16 ×1.1, sale card /2), purge (75 + 25 ramp,
persistent), purchase/purge as CHOOSE flow, merchantRng draw-order exactness.
**Acceptance:** tier-2: full stock + prices for a fixed merchantRng state
match hand-derivation draw-for-draw; purge ramp persists across two shops;
oracle spot-diff of a shop floor (stock, prices, sale index) zero-diff.
**Inherited:** The Courier (floor≤48 && !in_shop gate already live, effect is a
documented no-op) — deferred by B3.25. Shop `screen_state` translation — deferred by
B1.5/B4.3.
**Log:** —

### B4.9 `[ ]` Rest sites
**Deps:** B4.4, B3.26 (Girya/Peace Pipe/Shovel options) · **Spec:** design
§5.6 · **Provenance:** RestOption.java:25; CampfireUI.java:81-107; smith
grid flow (read at task)
**Deliverables:** rest (30 % max-HP heal, the frozen no-ascension-effect
negative), smith (upgrade grid CHOOSE), relic-added options for implemented
relics, the fixed rest row (14) + no-rest-row-13 rule already in B4.2.
**Acceptance:** tier-2: heal amount, smith upgrade writes the upgrade bit
via registry rows; option availability matrix (no upgradable cards → no
smith).
**Inherited:** Eternal Feather (rest-room heal) — deferred by B3.25. The **Fusion
Hammer** and **Coffee Dripper** campfire-option locks — deferred by B3.27 (rows live,
bodies inert).
**Log:** —

### B4.10 `[ ]` Event framework + ?-room resolution
**Deps:** B4.4 · **Spec:** design §5.6; §10 traps 17/19 · **Provenance:**
EventHelper.java:88-211; AbstractDungeon.java:1864-1990, 1340-1358; stage-a
§3.4's eventRng-duplicate quirk
**Deliverables:** the ?-room roll (float table, pity growth/reset — float
arithmetic, trap 19), shrine-vs-event split (0.25), pool draw + removal
bookkeeping (RunState bitsets from B4.3), event dialog framework (options as
CHOOSE, conditional options, one-shot flags), Juzu/Tiny-Chest hooks for the
relics that alter the table.
**Acceptance:** tier-2: pity float sequences match hand-derivation
bit-for-bit across 20 ?-rooms; pool-removal bookkeeping vs oracle §2.5 lists
for ≥ 3 seeds; trap-19 named test.
**Inherited:** the ?-room `eventRng` duplicate roll — deferred by B4.4. Un-defer the
translator's `eventList`/`shrineList`/`specialOneTimeEventList` membership bitsets
(RunState storage has existed since B4.3) — deferred by B1.5/B4.3, jointly with
B4.11-B4.13's `events.yaml` rows and the canonical list order. The event-screen half
of B3.27's inert boss/special relics (**Golden Idol** ×1.25 gold, **Sozu**'s potion
block, **Sacred Bark** potency, and the five deferred `onEquip` bodies — Pandora's
Box, Tiny House, Astrolabe, Empty Cage, Calling Bell) — deferred by B3.27, each
deferred whole because a partial would desync `miscRng` or `relicRng`.
**Log:** —

### B4.11 `[ ]` ∥ Exordium events I
**Deps:** B4.10 · **Provenance:** events/exordium: Big Fish, The Cleric,
Dead Adventurer, Golden Idol, Golden Wing, World of Goop (each read in full;
A15 branches per event)
**Deliverables:** the 6 events as native logic + `events.yaml` metadata
(conditions, option tables, A15 columns); Dead Adventurer's escalating
encounter, Golden Idol's relic+curse branches.
**Acceptance:** tier-2 per event (every option's state delta, A15 variants);
directed script per event.
**Log:** —

### B4.12 `[ ]` ∥ Exordium events II
**Deps:** B4.10 · **Provenance:** events/exordium: Liars Game, Living Wall,
Mushrooms, Scrap Ooze, Shining Light
**Deliverables:** the 5 events (Mushrooms' combat + Parasite, Scrap Ooze's
escalating relic odds, Shining Light's upgrade-random-two).
**Acceptance:** as B4.11.
**Log:** —

### B4.13 `[ ]` ∥ Shrines + one-time specials
**Deps:** B4.10 · **Provenance:** events/shrines: Match and Keep, Golden
Shrine, Transmorgrifier, Purifier, Upgrade Shrine, Wheel of Change + the
AbstractDungeon.java:1340-1358 one-time list filtered to Act-1 reachability
(floor/hp/gold gates, AbstractDungeon.java:1949-1980 — record the excluded
ones with their gate evidence in the Log)
**Deliverables:** the 6 shrines + reachable specials (Accursed Blacksmith,
Bonfire Elementals, Duplicator, Fountain of Cleansing, Lab, N'loth?, The
Woman in Blue, … per the filter) with transform/remove/upgrade mechanics
(cardRandomRng vs miscRng attribution read per event).
**Acceptance:** tier-2 per event; transform draw-stream attribution named
tests; Match-and-Keep's card dealing vs oracle spot-check.
**Log:** —

### B4.14 `[ ]` Neow
**Deps:** B4.4, B4.6, B3.27 (boss pool) · **Spec:** design §5.6; §10 trap 17
· **Provenance:** NeowEvent.java:62, 163, 289, 349-371; NeowReward.java:
68-128, 190-368
**Deliverables:** the four-category blessing (fresh `Random(seed)` — trap
17), category tables incl. cat-2 drawback-first roll order, all payout
activations (cards via NeowEvent.rng, relics/potions via relicRng/potionRng
pools), cat-3 boss swap, mini-blessing path flagged out-of-scope if
unlock-gated (fully-unlocked profile ⇒ full blessing; record the check).
**Acceptance:** tier-2: option sets + payouts for fixed seed match
hand-derivation draw-for-draw; oracle spot-diff of the Neow screen across
≥ 10 seeds zero-diff (options AND post-choice state).
**Inherited:** Neow `screen_state` translation — deferred by B1.5/B4.3.
**Log:** —

- **B4.15** `[x]` A20 run-setup modifiers + negative freezes — `registry/a20.yaml` populated to one row per ascension level 1..20 (`id == level`), each IMPLEMENTED or N/A-for-S1-with-reason, machine-checked by `A20Manifest.EveryRowCarriesScopeProvenanceAndAnS1Status`; run-setup order corrected to **A11 → (A5) → A14 → A6 → A10 → starting deck**, so A14's max-HP loss precedes A6's 90 % rewrite and an A20 Ironclad is **68/75, not 72/75** (matches the G4 oracle capture); Ascender's Bane lands at master-deck **index 0**, ahead of the five Strikes, routed through `add_card_to_master_deck`; retires the A6/A10/A14 deferred-obligation row; union 641/641 ×3 · [log](stage-b-log.md#b415)

### G6 `[ ]` **Gate: S1 rules complete (M3)** — tag `g6-s1-content`
**Deps:** all B3.*, all B4.*
Checklist (evidence linked in Log):
- [ ] 100 % tier-2 registry coverage: every manifest row has named passing
      tests (scripted check, `tools/verify_report/`).
- [ ] Sim-only soak: 1,000-seed random-policy full Act-1 runs complete (win,
      die, or legal-action exhaustion — never an assert/illegal state) in
      debug + asan.
- [ ] Oracle spot campaign: ≥ 20 full-run seeds, Neow through boss reward,
      **zero un-triaged diffs** through the run-level differ.
- [ ] All Stage A tests + fixtures still green (schema bumps accounted).
Then: update CLAUDE.md "Current state".
**Log:** —

---

## Phase B5 — Verification campaigns + S1 exit (Gate G7 = M4)

- **B5.1** `[x]` ∥ Sim self-replay fuzz soak — deterministic random-legal +
  four E0 heuristic policies, replay-twice per-step/final controller hashing,
  `STSFUZZ v1` mismatch reproducers + pre-crash journals, shardable coverage
  reports, and `soak.sh`; acceptance release sweep **10,808,430 actions /
  10,000 seeds / 250,000 cases**, zero failures, plus ASan-clean **2,500 /
  250,000 cases = 1.00 %** disjoint sample; 959/959 ×3 ·
  [log](stage-b-log.md#b51)

### B5.2 `[ ]` ∥ Oracle campaign automation
**Deps:** B1.4, B4.4 · **Spec:** design §7.1-7.3
**Deliverables:** campaign orchestration (seed sharding, nightly schedule,
resume, artifact root fixed per §7.3, translation + diffing pipeline run as
one command); triage queue (divergence → reproducer → this ledger/change-log
workflow); promoted-reproducer corpus layout.
**Acceptance:** one unattended overnight campaign (≥ 50 seeds) runs
end-to-end (game → JSONL → traces → diff reports) without intervention;
throughput and diff counts land in a generated report.
**Inherited:** triage the `b14_accept2` obtain-race capture-fidelity gap — deferred by
B1.3 (B1.4's acceptance is unaffected).
**Log:** —

### B5.3 `[ ]` ∥ Tier-4 distributional suite
**Deps:** G6 · **Spec:** design §3.4 (analytic-first scope)
**Deliverables:** `tools/dist_check/`: the §3.4 analytic expectation set
(encounter weights, compositions, rarity+pity dynamics, potion ratchet,
chest tables, ?-room pity, relic/potion tier rolls, map quotas) with
chi-square/exact tests over ≥ 10k sim seeds; the ≥ 200-run oracle-harvested
spot comparison.
**Acceptance:** all analytic tests pass at pre-registered significance
(document the correction for multiple comparisons in the tool README —
choose and justify); oracle spot set shows no flagged aggregate; failures
are stop-the-line divergences, not tuning targets.
**Log:** —

### B5.4 `[ ]` Verification report + CI corpus
**Deps:** B5.2 · **Spec:** design §7.4-7.5, §7.1(1)
**Deliverables:** `tools/verify_report/` (diffs per million actions,
divergence inventory, per-registry-row oracle-sighting + tier coverage join
via `game_id`); latest report committed under `docs/verification/`; the
curated 50-seed CI corpus (compressed translated traces +
zero-diff-replay gtest wired into the existing CI matrix).
**Acceptance:** CI runs the 50-seed replay smoke in seconds and fails on an
injected synthetic divergence (proven once, then reverted); report
regenerates deterministically from campaign artifacts.
**Log:** —

### B5.5 `[ ]` Throughput floors
**Deps:** G6 · **Spec:** design §8(4); InitialPlan §0.2 floors
**Deliverables:** benchmark additions: full-combat/sec/core (random policy)
and full-run/sec whole-machine on `bench_advance`'s pattern; methodology
notes (random-policy stand-in for 25-sim MCTS, per design §8).
**Acceptance:** release-preset numbers recorded here: ≥ 50k combat
steps/sec/core, ≥ 300 combats/sec/core, ≥ 0.4 runs/sec whole-machine — or a
stop-the-line design-doc amendment with profiling evidence (fast-but-wrong
is death; slow-but-honest gets a Stage C plan).
**Log:** —

### G7 `[ ]` **Gate: S1 verified (M4)** — tag `g7-s1-verified`
**Deps:** B5.1-B5.5, G6
The design §8 bar, checked literally (evidence linked in Log):
- [ ] ≥ 1,000,000 oracle-diffed actions across ≥ 2,000 seeds, zero
      un-triaged diffs (campaign reports).
- [ ] ≥ 10M sim-side fuzz actions, zero nondeterminism/asserts (B5.1).
- [ ] 100 % tier-2 registry coverage (manifest check, re-run at gate).
- [ ] Every a20.yaml row verified per design §8(3).
- [ ] Throughput floors hold (B5.5 numbers).
- [ ] Tier-4 suite green (B5.3).
Then: update CLAUDE.md "Current state"; open Stage C planning (perf
hardening) and the S2 scope conversation as fresh planning exercises.
**Inherited:** revisit the Infernal-Blade-generated Blood for Blood cost model — B3.6
models it via `cost_now` only, so an end-of-turn reset restores 4 rather than the
game's reduced base; judged unreachable, “revisit if G7 ever hits it”.
**Log:** —

---

## Parallelism map

```
B0.1 ─┬─▶ B1.1 ─▶ B1.2 ─┬─▶ B1.3 ──────────┐
B0.2 ─┘                 └─▶ B1.4 ─▶ B1.5 ─▶ B1.6 ─▶ G4     (B0.1 ∥ B0.2)
B2.1 ─▶ B2.2 ─▶ G5                                          (Phase B2 ∥ Phase B0/B1)

G4 + G5 ─▶ B3.1 ─▶ B3.2 ─┬─▶ B3.3 … B3.11 (card batches ∥)
                         ├─▶ B3.12 ─▶ B3.13 … B3.22 (monster batches ∥;
                         │            B3.17 ─▶ B3.20; B3.14 ─▶ B3.17)
                         ├─▶ B3.23 (potions)
                         └─▶ B3.24 ─▶ B3.25 ∥ B3.26 ∥ B3.27 (relics)

G4 + G5 ─▶ B4.1 ─▶ B4.2 ─▶ B4.3 ─▶ B4.4 ─┬─▶ B4.5 ─▶ B4.8
   (B4.1-B4.3 ∥ with Phase B3)           ├─▶ B4.6 ─▶ B4.7, B4.8
                                         ├─▶ B4.9
                                         ├─▶ B4.10 ─▶ B4.11 ∥ B4.12 ∥ B4.13
                                         ├─▶ B4.14 (also needs B4.6, B3.27)
                                         └─▶ B4.15
all B3.* + all B4.* ─▶ G6
B4.4 ─▶ B5.1 ∥ B5.2 (campaigns start before G6; DoD volume accrues)
G6 ─▶ B5.3 ∥ B5.5 ; B5.2 ─▶ B5.4 ; B5.1-B5.5 ─▶ G7
```

## Change log

- 2026-07-26 — **run-layer escape deferred obligations discharged; rejected
  Smoke Bomb branch fixed before landing.** The implementation commit
  `82d497a` closed the outcome / stolen-gold / liveness seam and replaced the
  stale `run_advance.cpp` roster comment with the dispatch-table mechanism.
  Independent review then found that the run-layer Smoke Bomb interception
  selected the right reward screen without executing the already-live native
  potion body, so the independent player-escaped flag was absent — most visibly
  when mugged and smoked were both true. The fix-forward preserves the Java
  `use` → relic hook → slot destruction order and adds assertions for both
  ordinary smoke and mugged+smoked precedence. The two satisfied obligation
  rows were deleted; the full provenance and non-task archive entry are under
  **Landed non-task work** above.
- 2026-07-26 — **card-limbo independent-audit fix-forward.** Java keeps
  `exhaust` and `exhaustOnUseOnce` separate and clears only the latter after
  `UseCardAction` filing; append-only `CardFlag` bit 8 now carries that
  one-play lifetime for Havoc and Corruption, so a Spoon-saved or Exhumed
  normally non-exhausting card does not exhaust forever. Terminal lethal
  damage keeps `UseCardAction` in `clearPostCombatActions`, so pending filing
  now performs its Strange Spoon RNG and `onExhaust` fan-out before the
  engine's established immediate halt. No schema or fixture/golden change.
  [Archive log.](stage-b-log.md#card-limbo)
- 2026-07-26 — **the played-card limbo obligation is discharged.**
  `resolve_card_play` now moves the source to `CombatState.limbo`, and an
  engine-emitted queued `USE_CARD` action files it only after its own effects,
  matching `AbstractPlayer.useCard` / `UseCardAction`. Opcode **53** is the
  append-only allocation: B3.10b still exclusively owns live reservation
  49–52, while 41–44 remain permanent gaps and are not backfilled. No schema
  version or committed fixture/golden changed. [Archive log.](stage-b-log.md#card-limbo)
- 2026-07-26 — **`MonsterState.flags` widened to `uint32_t` with a two-region
  allocation policy (`master` at `a32e84c`).** Owner-directed after the wave-A
  reconciliation recorded that the `uint16_t` was full. Recorded in the frozen
  spec as design §11 **v0.1.5**; the **Shared namespaces** section's
  "flags is now FULL" paragraph is replaced by the allocation rule.
  - **The diagnosis mattered more than the width.** Widening alone would have
    re-exhausted the word in Act 2, because the exhaustion was caused by
    **linear** allocation — a fresh bit per monster type, when no monster is two
    types at once. Type-scoped reuse (bits 0–23) plus a small scarce global
    region (24–31) makes 32 bits sufficient for the whole game; the worst single
    type needs five.
  - **A hazard the change exposed rather than introduced:** several flag writes
    were `static_cast<uint16_t>`. After widening, each would have **silently
    cleared the whole global region**, erasing `Escaped`. No compiler diagnostic
    is possible — narrowing through an explicit cast is what the cast requests.
    Removed across eight source files and two tests. Generalised in the
    namespaces section as: **when widening a flag field, hunt the truncating
    writes.**
  - **A refinement to the policy, found in implementation:** a bit consumed by a
    *power's* native body is scoped to that power's possible **owner set**, not
    to a single monster type — the CurlUp and Ritual latch sites key on
    `PowerId`, not `monster_id`. Reuse requires the reusing type can never own
    that power.
  - **The fixture proof was honest about not matching precedent.** B3.12 and
    B4.3 both proved their bumps with a single-zero-run insertion; that shape is
    **unavailable** when an *interior* field widens, because every later offset
    moves and an old alignment pad disappears. Rather than force the analogy, an
    equivalent per-field proof was produced from offsets taken by probes compiled
    against both old and new headers, verifying every meaning-carrying byte
    preserved in order across 20 fixtures / 112 records. **This is the one
    sanctioned exception to never modifying a committed fixture** — owner-
    approved, and nothing under `tests/golden/` outside `combat_fixtures/` moved.
- 2026-07-26 — **wave A landed: B3.15 `[x]`, B3.10a `[x]`, B4.5 `[!]`
  (`master` at `e6ec9ce`).** Three tasks dispatched in parallel on disjoint
  files; two integration passes, `integration-16` (B3.15 + B3.10a) and
  `integration-17` (B4.5). Union green on `debug`/`asan`/`release` with **zero
  NOT_BUILT**, manifest **regenerated** rather than summed: cards 105 / powers 44
  / monsters 25 / relics 142 / potions 33 / events 0 / encounters 20 / a20 20 /
  **total 389**. No committed fixture or golden vector was modified, deleted or
  renamed at any point in the wave — only added to.
  - **Design §5.6 ruling: the elite/boss card-rarity widths are an OMISSION, not
    a conflict — no frozen-doc amendment.** B4.5 found that elite widths are
    **10/40** and boss rewards are **unconditionally RARE**, neither of which
    §5.6 mentions, and escalated instead of amending. Ruling, verified against
    the source rather than the report: §5.6's sentence cites
    `AbstractRoom.java:108-109` — literally the **base** fields
    `baseRareCardChance = 3` / `baseUncommonCardChance = 37` — and `:148-177`,
    the base method. It describes the base room and cites the base fields; it
    **nowhere asserts the values are room-invariant**. `MonsterRoomElite`
    overrides those same fields *in its constructor* (`:34-35`) and
    `MonsterRoomBoss` overrides the method (`:40-42`). The doc therefore covers
    less than the game, which is a gap in coverage, not a statement contradicted
    — §4's stop-the-line bar is not met. Worth recording *why* this was nearly
    misread: `MonsterRoomElite.getCardRarity` **looks** like the mechanism and is
    not — it is gated on `ModHelper.isModEnabled("Elite Swarm")`, a daily mod
    irrelevant to S1, and otherwise defers to `super`. Reading the override alone
    gives the wrong answer; the constructor is the mechanism.
  - **integration-16's only defect came from the integration step itself.**
    Everything checked statically was clean — the union counts, a single
    definition of `monster_dead_or_escaped`, the merged two-pass block function
    against `AbstractCard.applyPowersToBlock:2291-2307`, the two independent
    writers of `tgt` (they cannot collide: `RESHUFFLE_ALL` reads a card-pool
    index behind a `>= kCardPoolCap` sentinel, `roll_random_target` yields a
    monster index only for `CardDef.random_target`), and both branches'
    namespace edits. The defect was **task ids written into a header by the
    conflict resolution** — present on neither branch, so no branch's green run
    could have caught it, and invisible to a clean merge. Only running the suite
    on the merged tree found it (`NoTaskIds`, all three presets). A second
    instance was introduced by the orchestrator's own fix for the stale
    `atDamageFinalReceive` claim, and caught the same way. Both now cite the
    mechanism instead: a `File.java:line` goes stale **visibly**, a task id
    silently. Fixed in `b2a3606`, which also discharges that stale-claim
    obligation — both merged branches touched `interp_damage.cpp`, making this
    integration the "next owner" the row named.
  - **integration-17's risk was semantic, not textual.** Only three files
    overlapped by name, but B4.5 added pool *builders* reading `cards.yaml` while
    B3.10a added 14 cards *to* it — an interaction git cannot see. Verified from
    the regenerated table that the pools are correctly RED-gated: **20/36/16
    unchanged**, `kIroncladAttackPool` still 28. That is B4.5's "confirm
    colorless is unreachable from a combat reward" deliverable **proven rather
    than argued**.
  - **B4.5 is `[!]`, not `[x]`.** All its code landed and is green, but its
    Acceptance requires an oracle spot-diff over ≥ 3 bridge runs and the game is
    launched by hand. §1 says done means the Acceptance **passes**, so it stays
    open with the blocker named and a committed runbook. Its agent stopped
    exactly there rather than claiming the task.
  - **Allocation outcome recorded, and `MonsterState.flags` is now FULL.**
    `kMonsterFlagEscaped = 0x8000` took the last bit of that `uint16_t`; the next
    monster needing one faces either a type-scoped overlap or a schema bump, and
    that decision must not be taken locally inside a content task. Unspent
    allocations (opcodes 41–44, `ChoiceKind` 6–7, `PowerId` 76, `MonsterIntent`
    14) are now permanent gaps. Also recorded: **adding an opcode is a
    three-file change** — `vocab.py`, `steps.py`'s partition assert and
    `cards.hpp`'s drift assert — which no brief had said.
- 2026-07-26 — **B3.10 split, and two brief errors the wave's own agents
  caught.** The dispatched B3.10 agent **stopped without committing** and
  proposed a split instead: the enumeration was right, but ten of the twenty
  cards each need a *distinct* new verb, ~2.5× the allocated opcode budget. It
  declined to land the clean fourteen-card subset too, on the grounds that doing
  so would spend all four opcodes on four of the ten verbs and consume interior
  ids under an allocation about to be revised. Both calls were correct and are
  conventions §5's "propose the split first". Split into **B3.10a** (fourteen
  cards, opcodes 45–48, power 77), **B3.10b** (four cards needing the colorless
  pool, opcodes 49–52, power 78), and **Purity + Forethought** deferred to the
  optional-multi-select owner as a new obligation row.
  - **Two orchestrator brief errors, both found by reading the tree.** (1) The
    briefs named `vocab.py` off limits when `OPCODES` lives in it and *every*
    wave-A task holds an opcode block — the allocation was unspendable as
    written. The rule now names the **namespace**, not the file:
    `MONSTER_INTENTS` exclusive, `OPCODES` shared and append-only per block.
    (2) The count guards were briefed as **four** `kPowersCount` sites; there are
    **five** — `tests/registry_gen_test.cpp:407` was missed, and a branch moving
    only four would fail or, worse, merge silently stale. The full inventory for
    both `kPowersCount` and `kCardsCount` is now in the allocations section, with
    the note that `relic_pools.cpp:207`'s assert asks a real question and is
    answered, not bumped. Both corrections were sent to the two agents still
    running.
  - **`NO_BLOCK` overrides `modifyBlockLast`, not `modifyBlock`** — the game's
    only overrider of that hook, and `applyPowersToBlock` runs the two as
    **separate full passes**. The engine's single-pass `modify_block` yields `N`
    where the game yields `0` whenever a Dexterity sits later in the list. This
    turns Panic Button from a data row into a structural `interp_block.cpp`
    change, and is exactly the kind of thing that would have merged green and
    been wrong.
  - Three further rows added from the same read: the deferred Purity/Forethought
    pair; `cards.yaml`'s documented-but-never-read `native:` field (§8's comment
    class **inverted** — docs asserting a capability the code lacks, and cards
    are the one domain with no native path); and `CardDef`'s missing upgraded-
    **target** column, an `ActionMask` deviation that every effect test would
    pass straight through.
- 2026-07-26 — **ledger/history reconciliation after integration-15 (`master` at
  `34c3e96`, pushed).** A conventions §2 incident of the same shape as the
  integration-12/-13/-14 entry below, and a **narrower, more instructive one**:
  the previous reconciliation missed *tasks*, this one missed a **partial
  landing**. Integration-15 landed `f24b8db` (B3.15's three monsters), `f05ad8a`
  (the post-draw gate) and `39876f0` (the citation audit) — none of the five
  commits since `d3500dc` touched `docs/`, so this ledger still showed B3.15
  untouched, the post-draw obligation "IN PROGRESS", and Pantograph deferred to a
  task that had already found it implemented. **The push was correct and the code
  is sound; only the record lagged.**
  - **Verified before writing, not inferred.** A clean `debug` build of
    `34c3e96` (`STS_JOBS=6 tools/wsl_run.sh debug`) reports **100 % passed, zero
    failed**, independently corroborating integration-15's six-preset claim; the
    count itself is deliberately not restated here — re-derive it with `ctest -N
    | tail -1`, per §8. Manifest
    **regenerated** by `tools/registry_gen/gen.py` rather than summed: cards 91 /
    powers 42 / monsters 24 / relics 142 / potions 33 / events 0 / encounters 20
    / a20 20 / **total 372**. The four `kPowersCount` guards (1 in
    `interp_block.cpp`, 3 in `interp_damage.cpp`) were checked against the
    regenerated value and are consistently at 42 — B3.15 moved all four
    together, so this is **not** a repeat of integration-14's silently-stale
    count assert.
  - **B3.15 is recorded as PARTIALLY LANDED and stays `[ ]`.** Its Acceptance
    names "escape terminal state distinct from kill", which no landed code
    satisfies, so §1's "done only when its Acceptance block passes" keeps it
    open; the landed half is recorded in a **Landed so far** block instead of a
    Log. Its `∥` marker is **dropped**: the remainder owns
    `src/engine/action_queue.cpp` and is no longer parallel-safe.
  - **One new obligation row replaces three separate blockers.** The Looter,
    enemy self-escape, Smoke Bomb's combat body and the stolen-gold return were
    three rows pointing at different-sounding problems; B3.15 proved they are
    **one predicate** — `hp > 0` where the Java reads `isDying || isEscaping`
    (`MonsterGroup.java:90-95,117-122`) — with no alternative seam, because
    `pump_step` recomputes the phase from it every iteration. The predicate now
    has its own row at the head of the table and the other three point at it.
  - **Two rows discharged and deleted.** The post-draw gate row (landed as
    `f05ad8a`; moved to **Landed non-task work** with its provenance, alongside a
    new bullet for the citation audit) and **Pantograph's boss heal** — which was
    never deferred at all by the time B3.15 reached it: the body, the
    `native: true` row and four `RelicHooksPantograph` tests were already live,
    and only the `DEFERRED` markers were stale. **A deferral marker sitting on
    live code is conventions §8's bug signal inverted**, and it is the second
    kind this wave produced.
  - **Two rows added, two narrowed.** Added: the liveness predicate; and the
    **nine pre-existing out-of-range Java citations** the audit found and
    correctly left alone. Narrowed: the un-park row (Blue Slaver, Red Slaver, 2
    Fungi Beasts and Exordium Wildlife un-parked by construction — **only Looter
    and Exordium Thugs remain**) and the `run_advance.cpp` stale-roster comment,
    which got staler precisely *because* the un-park gate needs no edit there.
  - **`PowerId` 60–72 are recorded as a third permanent gap.** B3.15 appended
    `ENTANGLE` 73 / `SPORE_CLOUD` 74 out of its allocated block rather than
    reaching into the free window below it — correct, and the same shape as 47
    and 54–58. The **allocation itself was never written down here**, only in the
    dispatching brief, leaving an unexplained thirteen-wide hole for the next
    reader; the namespaces section now says allocated blocks belong in this file,
    which is the rule that hole exists to teach.
- 2026-07-25 — **the `gen.py` `SET_COST` step-authoring obligation is CLOSED**,
  and B3.26's four deliberately-inert relics **get rows**. Orchestrator
  decisions on the two items the reconciliation entry below surfaced rather than
  applied; both were verified against the tree again before being written here.
  - **`SET_COST` row deleted.** The obligation assumed `SET_COST` would
    eventually need YAML step-authoring, with "the first card consumer" as its
    owner. Both named candidates are spent: Corruption (**B3.8**) and Snecko Eye
    (**B3.27**) both landed with **native** cost rewrites —
    `PowerId::CORRUPTION` (`native: true`, `on_use_card` / `on_card_draw`) and
    `PowerId::CONFUSION` (`native: true`, `on_card_draw`). Independently,
    `SET_COST` has sat in `ENGINE_EMITTED_OPS` (`stsgen/steps.py:71-74`) since
    **`b291d8f`** (2026-07-24) — **deliberately unauthorable**, enforced by a
    generation-time assert that partitions every opcode into exactly one
    capability group. The obligation is therefore not merely unowned, it is
    **contrary to a standing design decision**: authoring `SET_COST` from YAML
    is something the generator is built to refuse. **Closed rather than
    re-owned.** If a future card genuinely needs an authorable cost rewrite, the
    change is to move the opcode between groups in `steps.py` — a deliberate
    authorability decision carrying its own assert — **not** this row.
  - **Four rows added for B3.26's inert relics** — Dead Branch, Gambling Chip,
    Sling of Courage, Orange Pellets — each carrying the blocker as the registry
    provenance records it, and each noting that `relic_rares_shop_test` asserts
    the inertness today, so implementing one fails a test rather than silently
    changing behaviour. Code-side asserted inertness is **not** a substitute for
    a row: a passing assertion tells you the relic is still inert, not that
    anyone intends to implement it, and this table's own header says an
    obligation that is not in it is invisible once the deferring block is
    archived. The dangling "same owner as Sling of Courage" reference on the
    Slaver's Collar row was the proof — it named an owner that did not exist.
    **Both** that row and the new Sling of Courage row now point at the shared
    **blocker** (no elite/boss room marker on `CombatState`) and cross-reference
    each other only as twins, so neither defines the other's owner.
- 2026-07-25 — **ledger/history reconciliation after integration-12, -13 and
  -14 (`master` at `8235477`).** Six tasks — **B3.26** (`860ab73`), **B3.16**
  (`574ded0`), **B3.21** (`8b237e8` + `741d90f`), **B3.8** (`603cac2`),
  **B3.22** (`7232d01`) and **B3.27** (`5a9a541`) — were on `master` while this
  ledger still showed them `[ ]`, which conventions §2 calls an incident. All
  six blocks are now archived verbatim in [stage-b-log.md](stage-b-log.md)
  (`#b326`, `#b316`, `#b321`, `#b38`, `#b322`, `#b327`) with their Logs filled,
  and appear here as one-line index entries. Two landed changes that are **not**
  ledger tasks — the combat-start end-of-round fix (`821bffd`, in `56248c5`) and
  the build/toolchain effort (in `8235477`) — get the new **Landed non-task
  work** section, because neither has a task block to archive and neither was
  recorded anywhere the next session would look.
  Master's suite is green on **six** presets: `debug` / `asan` / `release` (WSL,
  GCC 13.3) and `win-debug` / `win-asan` / `win-release` (clang-cl 22.1.8), with
  zero NOT_BUILT lines across 51 test binaries. Landed manifest, **regenerated**
  by `tools/registry_gen/gen.py` rather than summed: cards 91 / powers 40 /
  monsters 21 / relics 142 / potions 33 / events 0 / encounters 20 / a20 20 /
  **total 367**.
  Ten obligation rows were discharged and deleted (Odd Mushroom ×1.25; Calipers;
  Ice Cream / `EnergyManager.recharge`; RARE+SHOP `pool_order`; BOSS
  `pool_order`; translator `relicPools`; `healing: true` on Feed/Reaper;
  Barricade's block-decay branch; the recursive-play opcode for Double Tap; and
  the combat-start end-of-round row). Ten were added, one narrowed and three
  corrected. Three corrections have the **ledger as the losing document**
  (conventions §4):
  - **The un-park row is narrowed to B3.15 alone, not closed.** B3.16, B3.21 and
    B3.22 discharged their share **by construction** — the gate is
    `monster_init_fn(id) == nullptr`, so registering an init fn un-parks the
    group with no shared code site edited. B3.15's slavers / Looter / Fungi
    Beast are the last group still parking.
  - **The `act_boss` row's blocker is gone.** It said a boss registry was needed
    and that "B3.21/B3.22 land Act-1 bosses"; all three Act-1 bosses (B3.20,
    B3.21, B3.22) have landed. The row stays open because it still has no owner,
    but the stated blocker is now false and is corrected.
  - **The shared-namespace paragraph's "28-row power table" was stale**, and a
    bare row count there is the same liability conventions §8 documents for test
    counts. Replaced with the invariant it was trying to state (ids run ahead of
    the row count, re-derive the list from `registry/*.yaml`), plus the new
    permanent-gap record for `PowerId` 47 and 54–58.
- 2026-07-25 — **proposed, NOT applied: close the `gen.py` `SET_COST`
  step-authoring obligation.** *(Superseded: the orchestrator **accepted** this;
  the row is deleted and the closure recorded in the topmost entry above. Left
  here as written, because the proposal and its verification are the record of
  why the obligation stopped existing.)* Recorded here for the orchestrator to accept or
  reject; this pass corrected the row's *facts* but did not delete it, because
  an obligation needs explicit re-owning rather than silent closure. The row
  named two candidates and **both have now landed without needing it**: B3.8's
  Corruption rewrites cost natively in the power (`powers.yaml` CORRUPTION,
  `native: true`, hooks `on_use_card` / `on_card_draw`), and B3.27's Snecko Eye
  does the same through `PowerId::CONFUSION` (`native: true`, `on_card_draw`) —
  so the brief's "B3.27/Snecko Eye remains the candidate" had already become
  false by the time this pass ran, and was not written into the row.
  Independently, `tools/registry_gen/stsgen/steps.py` has classified `SET_COST`
  as **`ENGINE_EMITTED_OPS` — deliberately never authorable from YAML** since
  the codegen refactor `b291d8f` (2026-07-24), because its `src` operand is a
  runtime card-pool index no YAML author can name; a generation-time assert
  partitions every opcode into exactly one capability group, so the decision is
  enforced, not merely documented. If the orchestrator agrees that "authoring"
  is answered by "deliberately unauthorable", the row should be **deleted** with
  that citation. What must **not** happen is the row quietly surviving with a
  candidate that cannot arrive.
- 2026-07-25 — **proposed, NOT applied: split B4.5 into B4.5a / B4.5b.**
  Recorded here for the orchestrator to accept or reject; this ledger pass did
  not split or renumber anything, because a task's real scope turning out
  larger than its entry is a stop-and-surface trigger (conventions §5) and
  renumbering is not a scout's call. The seam is B3.8. **B4.5a** — gold rolls,
  potion drop + the `blizzardPotionMod` ratchet, the elite relic-tier roll,
  reward assembly order, the CHOOSE/skip flow, pity arithmetic, and routing
  acquisition through `add_card_to_master_deck` — is startable **today,
  without B3.8**: none of it draws a card. **B4.5b** — the `getCard(rarity)`
  draw, the three generated pool tables, the CardLibrary library-order pin,
  the no-dupe end-to-end test and the oracle spot-diff — is gated on B3.8 by
  the hard blocker recorded in B4.5's Deps note (an empty `rareCardPool`
  indexed on the first RARE roll). The value of the split is that B4.5a
  unblocks B4.8 (Shop) work that only needs the reward *plumbing*.
- 2026-07-25 — **B4.5 amended from a read-only scout; every citation verified
  against `D:\STS_BG_Mod\SlayTheSpireDecompiled` before it was written here.**
  Six changes, all to an open task's brief, none to a frozen document:
  1. **Deps** — B3.10 and B3.11 removed, B3.3-B3.11 becomes B3.3-B3.9. The
     combat card-reward pool is RED-only (`Ironclad.getCardPool` →
     `CardLibrary.addRedCards`, `CardLibrary.java:1157`), and the only caller
     of `getColorlessRewardCards()` is `RewardItem(CardColor)` →
     `SensoryStone.java:121`, an Act-3 event. This *agrees with* frozen design
     §5.1's "the 72 red non-basics are the reward pool; colorless enters via
     the shop's 2 colorless slots and Neow" — the ledger was the drifted
     document, not the design doc.
  2. **Deps** — B3.8 promoted from a coverage dep to a **hard mechanical
     blocker**: `cards.yaml` has zero RARE rows, so `rareCardPool` is empty
     and `getCard(RARE)` indexes an empty list the first time pity reaches 2.
  3. **Deliverables** — "colorless handling" re-scoped to "confirm colorless
     is unreachable from a combat reward, with the citation".
  4. **Deliverables** — `vs 3/37/60` reworded to thresholds **`< 3` / `< 40`**
     (widths 3/37/60), `AbstractRoom.java:158, 167`, confirmed at
     `AbstractDungeon.java:1606-1615`. Coding the widths as thresholds is
     wrong by 3 points on **every** reward. Design §5.6's "against 3/37/60" is
     the widths and is not contradicted.
  5. **Deliverables** — "upgrade chance 0 in Act 1" now records that the
     `randomBoolean` draw **still happens** (`Random.java:79-82`); only
     `c.rarity != RARE` short-circuits it (`AbstractDungeon.java:1470`). A
     stream-attribution trap, not a value change.
  6. **Acceptance** — "reward screens zero-diff through the differ" is now
     explicitly the **post-claim `RunState`** (gold/potions/deck/pity/
     counters): no new storage, no schema bump. Diffing the *offer* would need
     tools-side differ work or `RunState` growth, and an unplanned
     `SCHEMA_VERSION` bump is stop-the-line (conventions §5).
  Also **discharged by construction and deleted from the Deferred obligations
  table**: *"Card-pool removal bookkeeping storage, if the dupe loop needs
  it"* (deferred by B4.3). The dupe loop re-rolls without mutating, because
  `CardGroup.getRandomCard(boolean)` is a pure indexed read
  (`CardGroup.java:502-508`) — there is nothing to book-keep.
- 2026-07-25 — **ledger/history reconciliation after the three-branch union
  (`5f96ec4`).** B3.18 (`3ce0467`, merged `db9f6a7`), B4.15 (`d13d29e`,
  `372168d`) and B3.19 (`8396190`, `cd3e7fa`) were on `master` while this
  ledger still showed them `[ ]` — history and the ledger disagreeing, which
  conventions §2 calls an incident. All three blocks are now archived verbatim
  in [stage-b-log.md](stage-b-log.md) (`#b318`, `#b319`, `#b415`) with their
  Logs filled, and appear here as one-line index entries. The union built
  green on debug, asan **and** release. Landed manifest, regenerated by
  `tools/registry_gen/gen.py` rather than summed — the three branches' own
  claimed totals of 234 / 251 / 232 were mutually inconsistent: cards 75 /
  powers 28 / monsters 14 / relics 65 / potions 33 / events 0 / encounters 20
  / a20 20 / **total 255**.
  Three corrections ride along, each with the ledger as the losing document
  (conventions §4):
  - **B4.15's Deliverables line gave the wrong run-setup order.** It said
    "(A6 90 % HP, A10 curse, A11 slot, A14 −5 — exact order per
    `dungeonTransitionSetup`)". The real order is **A11 → (A5) → A14 → A6 →
    A10 → starting deck**: `AbstractPlayer.<init>` (`:211-213`) runs before
    the dungeon exists, and inside `dungeonTransitionSetup`
    (`AbstractDungeon.java:2562-2604`) the max-HP loss (`:2591-2593`)
    **precedes** the 90 %-of-max rewrite (`:2594-2596`), so an ascension-20
    Ironclad is **68/75, not 72/75** — confirmed against the committed G4
    oracle capture. Frozen design §6 is a table of modifiers with citations,
    not an application order, and its own line numbers agree; nothing frozen
    needed changing. The archived block keeps the original wording verbatim;
    the correction lives in its Log and here.
  - **The master-deck-door obligation row's "zero production callers" is now
    false.** B4.15 routes the A10 curse through `add_card_to_master_deck`, so
    that function has exactly one production caller (with a named test pinning
    that its `onObtainCard` pass is empty there). `remove_master_deck_card`
    and `dispatch_relics_on_obtain_card` still have none, and the obligation
    on B4.5 is unchanged.
  - **The un-park obligation row is narrowed, not closed.** B3.18 and B3.19
    discharged their share **by construction** — the gate is
    `monster_init_fn(id) == nullptr`, so registering an init fn un-parks the
    group with no shared code site edited. Remaining owners: B3.15, B3.16,
    B3.21, B3.22. The A6/A10/A14 row is deleted outright, discharged by B4.15.
  A new **Shared namespaces** subsection sits under the obligations table,
  recording that registry ids are append-only **with legal gaps that must
  never be renumbered** (`MonsterId` 14 is unallocated; `PowerId::ANGER` is 33
  against a 28-row table), and that `MonsterIntent` in
  `tools/registry_gen/stsgen/vocab.py` is a shared namespace needing
  orchestrator allocation before use — a conventions §7 rule-of-two
  observation, written because contention for it stopped one task dead and
  produced a silent off-limits edit (B3.19's `vocab.py` addition) in another.
- 2026-07-25 — **terminal-outcome coverage is now pinned by a test**,
  `FixtureOracle.CorpusCoversBothTerminalOutcomes`. The corpus must contain at
  least one fight ending in monster death and at least one ending in player
  death, and both are classified from the **replayed terminal `CombatState`** —
  the engine driven through each fixture's own recorded actions — never from a
  file name. Renaming a file cannot satisfy it; neutering a script cannot pass
  it.
  This is the actual lesson of the two fixture corrections below. The zero-diff
  oracle compares two implementations against each other and is silent whenever
  they agree, and two implementations agree very readily about an action that
  does nothing. The coverage claim therefore lived nowhere executable — in a
  file name and a markdown row — and went stale without anything turning red.
  A guarantee that nothing asserts is a guarantee that will drift.
  Two smaller corrections ride along, both found with `--dumpall` and neither
  changing any trace: fixt20's coverage description claimed the monster "gains
  Strength", which r18's Chomp/Thrash/Chomp sequence never does — it never
  Bellows, so it stays Vulnerable-only, and it is now described as the
  Vulnerable-only counterpart to the overlap fixtures. And `--dumpall` is now
  documented in the generator's USAGE block, since the derivation notes name it
  as their source of truth.
- 2026-07-25 — **an out-of-range hand slot is now a hard generation failure**
  in `tools/fixture_gen/gen_combat_fixtures.cpp`, the twin of the affordability
  abort added the day before and the mechanism behind the fixt16 correction
  recorded below. A `Play(n)` whose slot is outside the current hand aborts
  generation, naming the fixture, the action index, the slot and the hand size,
  and writes nothing.
  Worth stating why this needed a check at all, because it is *not* the fixt13
  case. An unaffordable play produced a state the game cannot reach, and the
  `advance()` legality guard eventually contradicted it. An out-of-range play
  produces a state the game reaches perfectly well — `queue_card_play` refuses
  the index, `advance()` no-ops the action, the reference simulator mirrors that
  no-op, and the trace is **correct**. The zero-diff check therefore cannot ever
  fire on one: both implementations agree, accurately, that nothing happened.
  What is wrong is not the trace but the fixture's *claim* — its name, and the
  coverage table's row — and no amount of differential testing looks at those.
  The only place the defect is visible is authoring time, which is where the
  check now lives.
  Safe to add because the corpus has exactly one violator and it was fixed
  first: generation reports **20/20** with the check in place, and every one of
  the 20 traces is byte-identical to before it. Demonstrated by reintroducing
  the old fixt16 script, observing the abort (exit 3, no trace written), and
  reverting.
- 2026-07-25 — **a second frozen Stage A golden vector was corrected** — again
  beyond the zero-diff-in-meaning regeneration conventions §5 sanctions, so it
  is recorded here rather than only in a task Log.
  `tests/golden/combat_fixtures/fixt16_r29_monster_death.trace` was named for
  monster death and **did not kill the monster**. Its script ended
  `… End(), Play(1), Play(1), Play(3)`; turn 2 deals a five-card hand, two plays
  leave three cards, and the final `Play(3)` therefore named an **empty slot**.
  `queue_card_play` (`src/engine/card_play.cpp`) rejects an index `>= hand_count`
  and `advance()` no-ops the action, the reference simulator mirrored that no-op,
  and the trace ended at `WAITING_ON_USER` with the monster alive on 17/43. The
  description was wrong in the same way — it claimed "then 3 Strikes" where the
  realized turn-2 plays were Defend then Strike.
  **Root cause — the same hand-index drift as fixt13, and the same blind spot.**
  The scripts were authored against a generator that did not yet discard the hand
  at end of turn, so every turn-2 slot silently came to name a different card.
  Nothing failed: an out-of-range play is a *documented no-op* on both sides, so
  the two independent implementations agreed that nothing happened and the
  zero-diff check passed. The corpus advertised monster-death coverage it did not
  have — a coverage claim that went stale in silence, which is the more general
  defect here than either individual script.
  **Fix.** fixt16 was re-authored empirically against
  `gen_combat_fixtures --dump`: turn 1 is unchanged (Bash 8 + Vulnerable, then a
  Vulnerable-boosted Strike 9, monster 43→26); turn 2 plays the hand it is
  actually dealt — Pommel 13, whose DRAW 1 pulls the second Strike into hand,
  then Strike 9 and Strike 9 — reaching `COMBAT_OVER` with the monster at 0/43
  inside the 3-energy budget. The killing blow stays a single-effect **Strike**
  so the pump halts with an empty queue. Only fixt16's trace changed; the other
  19 were proven byte-identical by sha256. Two follow-on commits on the same
  branch close the mechanism behind the defect rather than this one instance of
  it — see the two entries above.
  **Also corrected: the derivation notes.** The previous change left a scoped
  accuracy warning naming fixt01, fixt08 and fixt18 as having wrong printed
  arithmetic. Re-deriving every entry against `--dumpall` showed the warning
  itself understated the problem — **fixt12, fixt15, fixt19 and fixt20 were wrong
  too** — and that the coverage table had four independent errors (it credited
  fixt15 with a Pommel Strike it never plays, credited fixt20 with a
  Strength/Vulnerable overlap its monster never reaches, and missed both a Bash
  and three reshuffles). Every entry and every table row is now re-derived from
  the traces and the warning is gone. **No trace was wrong — only the prose
  about it**, which is why this is a notes fix and not a second vector
  correction.
- 2026-07-25 — **a frozen Stage A golden vector was corrected**, which goes
  beyond the zero-diff-in-meaning regeneration conventions §5 sanctions, so it
  is recorded here rather than only in a task Log.
  `tests/golden/combat_fixtures/fixt13_r21_triple.trace` asserted a state the
  game cannot reach. Its script played Bash (2) + Strike (1) + Pommel Strike (1)
  in a single turn — **4 energy against the Ironclad's 3**
  (`new EnergyManager(3)`, Ironclad.java:68) — and the trace duly recorded
  `player_energy == -1`. The game has no path to that value:
  `AbstractCard.hasEnoughEnergy` (AbstractCard.java:888) refuses the third card
  before it is ever used, and `EnergyPanel.useEnergy` clamps at zero
  (EnergyPanel.java:71-74).
  **Root cause — the cross-check was independent in the wrong dimension.**
  `tools/fixture_gen/gen_combat_fixtures.cpp` subtracted the card's cost with no
  affordability test, and the engine's `advance()` did not gate playability
  either, so the reference simulator and the engine agreed on an illegal state.
  The two implementations were genuinely independent in damage math, pile
  mechanics and RNG, and that is what the fixture corpus was built to check —
  but neither expressed the *playability* precondition, so no amount of
  differential testing could see it. The `advance()` legality guard on
  `advance-guard` added that precondition to one side and the disagreement
  surfaced immediately, as a `FixtureOracle` failure.
  **Fix.** The affordability rule is re-derived from the Java **in the
  generator** (not delegated to the engine's `legal_actions()`, which would
  re-couple the oracle to the code under test) and is a **hard generation-time
  abort** naming the fixture, action index, card, cost and available energy — an
  unplayable script now fails at authoring instead of becoming a golden vector.
  All 20 scripts were audited against it: 19 pass, fixt13 was the only failure.
  fixt13 was rescripted to Bash + Pommel on turn 1 and the third attack on turn
  2, preserving all three attacks, all three damage figures and the same final
  monster HP; only fixt13's trace changed and the other 19 were proven
  byte-identical.
  **Two related findings, surfaced and NOT acted on** (both are corpus
  decisions): (1) `fixt16_r29_monster_death` no longer kills the monster — its
  final action names hand slot 3 of a three-card hand, an out-of-range index
  both implementations no-op, so the corpus does not actually cover monster
  death despite the coverage table claiming it; (2)
  `tests/golden/combat_fixtures/derivation_notes.md` still described the
  skeleton as having no end-of-turn hand discard, which is false, so its
  multi-turn narration names cards that are no longer in hand. The false
  mechanic statement and a scoped accuracy warning were fixed in this change;
  re-deriving the affected per-fixture arithmetic (fixt01, fixt08, fixt18) and
  re-authoring fixt16 remain open.
- 2026-07-24 — **document restructure (no mechanic changed).** This ledger was
  2,576 lines; the 33 completed task blocks (146,303 B of Log text) moved
  byte-for-byte to [stage-b-log.md](stage-b-log.md) and are represented here by
  one-line index entries. Forward-looking obligations buried in those Logs were
  swept into the new **Deferred obligations** table and mirrored as
  `**Inherited:**` lines on the 19 open tasks that own them. The "Working
  agreements" section moved in full to [conventions.md](conventions.md), now
  the single authoritative copy of the rules previously duplicated across this
  ledger, `stage-b-orchestrator-prompt.md` and `CLAUDE.md`; where those three
  had drifted, the strictest reading won (each reconciliation is listed in
  `conventions.md`'s own change note). Byte-identity of every archived Log was
  proven mechanically before the commit. No open task's Deps / Deliverables /
  Acceptance text was altered.
- 2026-07-24 — B3.20 deliverable corrected after the execution agent's
  stop-the-line source check. The ledger said Slime Boss's Goop Spray inserts
  Slimed into the hand, but `SlimeBoss.takeTurn()` queues
  `MakeTempCardInDiscardAction` (3 copies below A19, 5 at A19+) and that action
  emits `ShowCardAndAddToDiscardEffect`. The ledger is the losing document;
  "hand insertion" is mechanically corrected to "discard-pile insertion."
- 2026-07-24 — **recorded retroactively** (the correction landed with B3.6 on
  2026-07-24 but no change-log entry was written at the time; added here during
  the Stage A archive pass). B3.6 deliverable corrected after the execution
  agent's stop-the-line source check. Grepping every `cards/red` constructor for
  `CardRarity.UNCOMMON` × `CardType.SKILL` yields **17** members — B3.6's
  deliverable list of 16 **plus Rage**, because `Rage.java:31` is
  `CardType.SKILL`, while this ledger's B3.7 deliverable list misfiled Rage
  under the uncommon POWER cards. The ledger is the losing document; Rage is
  mechanically moved to **B3.6** and struck from B3.7. B3.7's own source
  enumeration then found eight RED/UNCOMMON/POWER cards (Combust, Dark Embrace,
  Evolve, Feel No Pain, Fire Breathing, Inflame, Metallicize, Rupture) and
  skipped Rage, so both Logs agree.
- 2026-07-22 — B1.1 acceptance amended (ledger is the losing document; fixed
  in the same change per the working agreement). "Reproduces B0.2's captured
  run byte-for-byte" is unsatisfiable as written: the B0.2 capture is
  timing-contaminated (queued stepper `end`-resends produced a game history —
  5 monster hits by "turn 3" — that no clean state-paced replay can recreate;
  root cause in the B1.1 Log). Replaced with the controlled equivalent: two
  stock-jar runs of the derived effective script must be byte-identical
  (determinism control), the fork run must be byte-identical to stock on the
  same script, and the run must anchor to the B0.2 capture (Neow floor-0
  state, pre-contamination 18-state prefix, floor/screen trajectory). The
  B1.3 strip-equivalence and G4 acceptance style (fork-vs-fork A/B on
  identical scripts) is unaffected.
- 2026-07-17 — v0.1 created from stage-b-design.md v0.1 (§2-§8 → phases
  B0-B5). Task counts: 57 tasks + 4 gates (G4-G7, continuing Stage A's gate
  numbering; G4=M2, G6=M3, G7=M4). Card/monster/relic batch enumerations
  marked "enumerate at task start" are counted from the CardRarity/RelicTier
  greps recorded in design §5 — the named example lists in B3.x are
  orientation, not authority; the source enumeration at task start wins.
