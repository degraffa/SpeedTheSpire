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
| **Combat start runs an end-of-round pass the game never runs** | B3.19 | **IN PROGRESS — `fix-combat-start`** | `combat_begin` (`advance.cpp:156-159`) and `enter_combat` (`run_advance.cpp:351-354`) prime turn 1 with `turn_has_ended = 1` and pump, routing through `start_of_turn` → `dispatch_at_end_of_round` before the player's first turn. The game cannot reach that branch: `AbstractRoom.java:236-243` sets the flag then queues `GainEnergyAndEnableControlsAction`, which clears it (`:35`). **Measured**: a sleeping Lagavulin has 16 block on turn 1 instead of 8. A fix must touch **both** entry points |
| In-combat card-CHOOSE potion bodies: Elixir, Attack/Skill/Power/Colorless Potion, Gambler's Brew, Liquid Memories | B3.23 | B3.4 `[x]` | B3.23 named B3.4 as the verb owner; B3.4 landed CHOOSE-in-combat but recorded no potion un-deferral. **PARTIALLY DISCHARGED** on `discharge` (Blessing of the Forge, commit `d710d50`) — it needed only the already-live `CHOOSE_CARD{upgrade}` kind, so it left this row; the potions still listed above need real hand-select-screen / `MAKE_CARD` work and stay open. As of the same branch they are also **fail-loud**: `potion_use_implemented` keeps every still-deferred potion off the legal-action mask (the potion-legality commit on `discharge`, immediately following `d710d50`) instead of letting a USE silently burn the slot |
| Mummified Hand onUseCard POWER → cardRandomRng 0-cost | B3.25 | B3.7 `[x]` | no POWER CardType at B3.25 time; B3.7 landed the POWER cards but recorded no discharge. **DISCHARGED** on `discharge`, commit `add41ed` — implemented with the cardQueue exclusion and the just-played-card exclusion, no draw on the empty-candidate path |
| Frozen Egg's POWER-card upgrade-on-obtain branch (documented inert) | B3.25 | B3.7 `[x]` | same cause, same gap. **DISCHARGED** on `discharge`, commit `dc6f626` |
| `healing: true` on **Feed** and **Reaper** (CardTags.HEALING) | B3.6 | B3.8 | `kIroncladAttackPool` is generated from the healing column — Infernal Blade's pool is wrong until this lands |
| Barricade block-decay branch (A3.1 left it structural) | B3.2 | B3.8 | the start-of-turn block-decay branch is a structural no-op today |
| Recursive-play opcode (design R14) — Double Tap | B3.2 | B3.8 | Havoc's `PLAY_TOP_DRAW` (B3.4) is a special case, not the general form |
| Recursive-play opcode (design R14) — Mayhem | B3.2 | B3.11 | same opcode; also unblocks the Duplication and Distilled Chaos potions |
| Per-power counter storage — Panache (every-5th), The Bomb (3-turn) | B3.2 | B3.11 | B3.2 added no `PowerSlot` counter field; Combust (B3.7) and Rampage (B3.5) were solved locally |
| Enemy self-escape + stolen-gold return rules | B4.4 | B3.15 | B4.4 names B3.15 as the explicit owner |
| Smoke Bomb combat-escape potion body | B3.23 | B3.15 | B4.4 landed the run-level half (rejects bosses, no-reward proceed); the combat-escape path is still B3.15's |
| Pantograph atBattleStart boss heal 25 | B3.25 | B3.15 | needs EnemyType/BOSS monster metadata; named "B3.15-B3.17", and B3.16/B3.17/B3.20 landed without it |
| Un-park unimplemented monster groups (they consume their B3.12 composition draws and then park) | B4.4 | B3.15, B3.16, B3.21, B3.22 | run-created combats with an unimplemented group cannot play out. **B3.18 and B3.19 discharged their share by construction** (Gremlin Nob, 3 Sentries, Lagavulin) — the gate is `monster_init_fn(id) == nullptr`, i.e. it asks the dispatch switch directly, so **no code edit is needed**: registering an init fn un-parks that group automatically. The remaining owners inherit the same free discharge |
| Odd Mushroom ×1.25 Vulnerable branch | B3.25 | B3.26 | Paper Phrog's ×1.75 twin retired at B3.25; Odd Mushroom is the rare-tier one |
| Calipers branch of the start-of-turn block decay (loses 15 instead of zeroing) | A3.1 | B3.26 | Stage A left the branch structural (`action_queue.cpp` `has_calipers = false`); B3.26's deliverables already say "Calipers — retiring A3.1's structural branch". Barricade is the separate row above (B3.8); the third branch, Blur, is Silent-only and out of S1 scope |
| Ice Cream — the only S1 consumer of the `EnergyManager.recharge()` SET-to-constant simplification | A4.3 | B3.26 | Stage A set energy to `kIroncladBaseEnergy` unconditionally each turn, exact only because no skeleton relic/power reaches the Ice Cream / Conserve branches (stage-a design §12, `action_queue.hpp`); B3.26's deliverables and acceptance already name it |
| RARE + SHOP relic `pool_order` rows | B4.6, B3.25 | B3.26 | the five-tier initializer is generic and already draws for empty tiers; only rows are missing. Also narrows B3.25's "tiers empty" scaffold assertions |
| BOSS relic `pool_order` rows | B4.6, B3.25 | B3.27 | as above |
| Translator `relicPools` un-deferral (all 5 tiers) | B1.5, B4.3, B4.6 | B3.27 | RunState storage exists since B4.3; blocked on a complete `relics.yaml` (a fail-loud join would throw). Lands once B3.26 **and** B3.27 rows exist |
| `replay` generalized to seed a sim replay from any translated `RunState` | B1.6 | B4.4 `[x]` | B1.6 scoped itself to "adapter + format only" and named B4.4; B4.4's Log records run-combat equivalence but no `replay` generalization |
| Question Card / Singing Bowl / White Beast Statue (reward-screen modifiers) | B3.25 | B4.5 | rows + canSpawn gates live, effects are documented no-ops |
| Emit `kIroncladAttackPool` in CardLibrary HashMap **library order** instead of registry-id order | B3.6 | B4.5 | documented interim deviation; a one-line `gen.py` fix once B4.5's oracle capture pins library order |
| **Route card acquisition through `add_card_to_master_deck`, never a direct `rs.master_deck[]` write** | audit 2026-07-25 | B4.5 | `add_card_to_master_deck` (`run_deck.hpp`) has **exactly one** production caller since B4.15 — the A10 Ascender's Bane insert, where the `onObtainCard` pass is provably empty and a named test pins that; `remove_master_deck_card` and the generated `dispatch_relics_on_obtain_card` still have **zero**, and the other master-deck write is the starting-deck bulk init in `run_advance.cpp`, which bypasses the door. No *relic* effect walks through it yet because rewards do not exist, so this is correct today, **but** copying that bulk-write pattern for reward cards would silently kill every `onObtainCard` relic: Ceramic Fish (9 gold), Molten / Toxic / Frozen Egg (upgrade on obtain), Darkstone Periapt (+6 max HP). Same applies to Parasite removal via `remove_master_deck_card`. A test that obtains a card through the run layer with Ceramic Fish equipped and asserts the gold gain is the cheapest guard. |
| Matryoshka (chest relic) | B3.25 | B4.7 | floor≤40 canSpawn gate live, effect deferred |
| The Courier (shop relic) | B3.25 | B4.8 | floor≤48 && !in_shop gate live, effect deferred |
| Eternal Feather (rest-room heal) | B3.25 | B4.9 | row live, effect deferred |
| Translator `eventList`/`shrineList`/`specialOneTimeEventList` membership bitsets | B1.5, B4.3 | B4.10 | storage exists since B4.3; needs `events.yaml` + the canonical list order (B4.10-B4.13) |
| ?-room `eventRng` duplicate roll | B4.4 | B4.10 | B4.4 names B4.10 as owner |
| Translator `screen_state` content (event / reward / shop / grid / map screens) | B1.5, B4.3 | B4.5, B4.8, B4.10, B4.14 | structurally consumed today (a new/renamed key still fails loud), not mapped |
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
| Translator: real `act_boss` | B1.5, B4.3 | UNASSIGNED | needs a boss registry; B3.21/B3.22 land Act-1 bosses but no owner is named |
| Bit-exact oracle for the raw monsterRng monster / elite / boss lists | B3.12 | UNASSIGNED | B3.12 pinned the algorithm + determinism, not a golden list; B4.4's floor-0 triple pins stream state only. Natural home is B5.2's campaign automation |
| `RETAIN` `CardFlag` end-of-turn sweep | B3.1 | UNASSIGNED — "first content consumer" | ETHEREAL (B3.5/B3.6) and INNATE (B3.9) discharged; no S1 Ironclad card uses Retain |
| `gen.py` step-authoring for `SET_COST` | B3.1 | UNASSIGNED — "first card consumer" | `MAKE_CARD`'s half was discharged at B3.3; candidates are B3.8 (Corruption) and B3.27 (Snecko Eye) |
| `lagavulin_init_awake` has no production caller | B3.19 | UNASSIGNED — Lagavulin Event owner | implemented and tested; the event that would build `Lagavulin(false)` does not exist |
| Gremlin move-99 escape (`EscapeAction` body **and** the `deathReact`/`escapeNext` trigger, landed together) | B3.16 | UNASSIGNED — Act-2 owner | unreachable in Act 1: `escapeNext()` has no caller in the decompiled tree; the only `deathReact()` call is `BanditBear.java:131` |
| `run_advance.cpp` file-header comment (~line 7) enumerates a stale monster roster | integration-11 | UNASSIGNED — next `run_advance.cpp` toucher | same rot as the step-(2) comment fixed in `5f96ec4`; conventions §8's "comment asserting X does not exist yet" class |
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
Two gaps are live today and both are correct: `MonsterId` 14 is unallocated
(B3.18 took 12/13, B3.19 took 15), and `PowerId::ANGER` is 33 against a
28-row power table. **Never renumber to close a gap.** Renumbering an existing
id is stop-the-line (conventions §5, design §4.4) and would cost a design-doc
change-log entry plus a schema-version bump — never worth a cosmetic tidy.
(Note the one place contiguity *is* required and is a different thing:
`relics.yaml` `pool_order` within a tier.)

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
| `DEFEND` | 11 | B3.16 |
| `ATTACK_BUFF` | 12 | B3.21 |
| *(reserved)* | 13–16 | B3.22 |

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

### B3.8 `[ ]` ∥ Red rares
**Deps:** B3.2 · **Provenance:** cards/red RARE (16)
**Deliverables:** registry entries: Barricade, Berserk, Bludgeon, Brutality,
Corruption, Demon Form, Double Tap, Exhume, Feed, Fiend Fire, Immolate,
Impervious, Juggernaut, Limit Break, Offering, Reaper. Wires the block-decay
Barricade branch A3.1 left structural, Corruption's cost/exhaust rewrite,
Feed/Reaper HP-max/heal opcodes.
**Acceptance:** tier-2 per card; Barricade block-persistence through the
frozen start-of-turn sequence; directed script.
**Inherited:** `healing: true` on **Feed** and **Reaper** (CardTags.HEALING) — deferred by
B3.6; `kIroncladAttackPool` is generated from the healing column, so Infernal Blade's
pool is WRONG until this lands. Barricade's block-decay branch and the recursive-play
opcode for Double Tap — deferred by B3.2.
**Log:** —

- **B3.9** `[x]` Status + curses — 4 statuses + 11 curses, card ids 25-39; opcodes `LOSE_HP_PER_HAND`=18 / `DISCARD_HAND`=19 / `REDUCE_POWER`=20, native `FRAIL`=21; end-of-turn order rewritten; all 20 combat fixtures regenerated from the checked-in generator; 368/368 · [log](stage-b-log.md#b39)

### B3.10 `[ ]` ∥ Colorless uncommons
**Deps:** B3.2 · **Provenance:** cards/colorless UNCOMMON (20)
**Deliverables:** registry entries for the 20 (Bandage Up, Blind, Dark
Shackles, Deep Breath, Discovery, Dramatic Entrance, Enlightenment, Finesse,
Flash of Steel, Forethought, Good Instincts, Impatience, Jack of All Trades,
Madness, Mind Blast, Panacea, Panic Button, Purity, Swift Strike, Trip —
verify enumeration from source).
**Acceptance:** tier-2 per card; Discovery/Madness cardRandomRng draw
accounting; directed script.
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
field was added there).
**Log:** —

- **B3.12** `[x]` Multi-monster combat + encounter framework — `encounters.yaml` with 20 Act-1 encounters and their miscRng composition programs; `resolve_composition`/`generate_monster_lists`/`spawn_group`/`dispatch_monster_turn`; **schema 3→4** (`kMonsterCap` 5→7, `sizeof(CombatState)` 3672→3896); 20 fixtures regenerated with a byte-level zero-diff-in-meaning proof; +13 tests, 286/286 ×3 · [log](stage-b-log.md#b312)
- **B3.13** `[x]` Monsters: Cultist + louses — monster ids 2-4 (Cultist, LouseNormal, LouseDefensive) + power `CURL_UP`=20; committed independent XS128 fixtures, 32 seeds × 20 turns per monster; 359/359 · [log](stage-b-log.md#b313)
- **B3.14** `[x]` Monsters: small/medium slimes — monster ids 5-8 (Spike/Acid Slime S+M) + `MonsterIntent::ATTACK_DEBUFF`=6; XS128 fixtures ×4; 413/413 integrated · [log](stage-b-log.md#b314)

### B3.15 `[ ]` ∥ Monsters: slavers + Looter + Fungi Beast
**Deps:** B3.12 · **Provenance:** SlaverBlue/Red.java, Looter.java,
FungiBeast.java
**Deliverables:** registry entries: entangle (Red Slaver), Looter's
gold-steal + escape (combat-end-without-death path + stolen-gold return
rules), Fungi Beast Spore Cloud (on-death Vulnerable).
**Acceptance:** tier-2 per monster; escape terminal state distinct from kill
(reward implications tested at B4.5); on-death trigger ordering.
**Inherited:** Pantograph's atBattleStart boss heal 25 (needs EnemyType/BOSS monster
metadata) — deferred by B3.25. Smoke Bomb's combat-escape body — deferred by B3.23
(B4.4 landed the run-level half). Enemy self-escape + stolen-gold return, and
un-parking unimplemented monster groups — deferred by B4.4.
**Log:** —

### B3.16 `[ ]` ∥ Monsters: gremlin gang
**Deps:** B3.12 · **Provenance:** GremlinWarrior/Thief/Fat/Tsundere/
Wizard.java; MonsterHelper.java:737-765
**Deliverables:** registry entries ×5 (Angry thorns, sneaky escape?, fat
weak, shield tsundere protect logic — native where the protect targeting
doesn't fit the table shape, per design §4.2), gang spawn order from B3.12.
**Acceptance:** tier-2 per gremlin; gang composition draws already covered by
B3.12 — here per-monster behavior incl. Tsundere's block-ally logic.
**Inherited:** un-park this gang — run-created combats currently consume the B3.12
composition draws and then park; deferred by B4.4.
**Log:** —

- **B3.17** `[x]` Monsters: large slimes + split — monster ids 9-10 (large slimes), `PowerId::SPLIT`=22, opcodes 25-29 (`CANNOT_LOSE`/`CAN_LOSE`/`SUICIDE`/`SPAWN_MONSTER`/`SET_MOVE`); the Java-exact split framework; 441/441 ×3 · [log](stage-b-log.md#b317)
- **B3.18** `[x]` Elites: Gremlin Nob + Sentries — monster ids 12 GREMLIN_NOB / 13 SENTRY (both ELITE) + `PowerId::ANGER`=33; Artifact needed **no** new row (B3.2's id 4, the nullify already at the APPLY_POWER site — Sentry only grants the stack); 3 independent 32-seed × 20-turn fixtures pin the Nob's A18 history tree and the Sentry in an even and an odd slot; registering the two init fns un-parked the Gremlin Nob and 3 Sentries encounters by construction; Sentry's animation-only `damage()` is an explicit empty `on_monster_damaged` case, not a `default:`; union 641/641 ×3 · [log](stage-b-log.md#b318)
- **B3.19** `[x]` Elite: Lagavulin — monster id 15 LAGAVULIN (ELITE), native sleep/wake machine; **no new power id** (Metallicize was already id 5 — now the first MONSTER-owned power to bind an end-of-turn hook, and the generator's duplicate-name check caught the re-add); `MonsterIntent` SLEEP=9 / STUN=10; `on_monster_damaged` gains `hp_lost` so absorbed damage cannot wake it; armour stands at 8/16/24 because monster block never decays in this build; un-parked the Lagavulin encounter by construction; union 641/641 ×3 · [log](stage-b-log.md#b319)
- **B3.20** `[x]` Boss: Slime Boss — monster id 11 + `MonsterIntent::STRONG_DEBUFF`=8; fixed 150 HP, Goop→Prep→Slam cycle, exact-half split chaining into B3.17's large slimes; 521/521 debug + asan · [log](stage-b-log.md#b320)

### B3.21 `[ ]` ∥ Boss: The Guardian
**Deps:** B3.12 · **Provenance:** TheGuardian.java (:97-107/185; mode shift
thresholds 30/35/40, Sharp Hide)
**Deliverables:** registry entry (native AI: offensive/defensive mode state
machine keyed on damage-taken threshold), Mode Shift + Sharp Hide powers.
**Acceptance:** tier-2: mode flips at the exact cumulative-damage threshold
incl. threshold growth per cycle; Sharp Hide triggers on attack plays; A19
column.
**Inherited:** un-park this boss — run-created combats currently consume the B3.12
composition draws and then park; deferred by B4.4. The translator's real `act_boss`
field also waits on a boss registry (deferred by B1.5/B4.3, no owner named).
**Log:** —

### B3.22 `[ ]` ∥ Boss: Hexaghost
**Deps:** B3.12 · **Provenance:** Hexaghost.java (:99, :137-142 —
Body/Orb components), Divider damage = f(player HP)
**Deliverables:** registry entry (native AI: orb-count state, Divider math,
Inferno upgrade of Burns, Sear/Tackle/Inflame cycle); decision recorded on
modeling orbs (monster `misc` fields vs. extra powers — CombatState additive
change needs a schema bump + fixture regeneration via checked-in generators,
design §4.4).
**Acceptance:** tier-2: Divider = player-HP-derived exactly per the cited
line; move cycle across 12+ turns matches hand-derivation; Burn upgrades at
the cited turn.
**Inherited:** un-park this boss — run-created combats currently consume the B3.12
composition draws and then park; deferred by B4.4.
**Log:** —

- **B3.23** `[x]` Potions — `potions.yaml` with 33 Ironclad-pool rows in pool order; `potions.hpp/.cpp` `use_potion`; trap-14 rejection-sampling identity roll (draw-count pinned); `potion_slot_count(A20)`==2; +19 tests, 232/232 · [log](stage-b-log.md#b323)
- **B3.24** `[x]` Relics: starter + commons — 34 relic rows (Burning Blood + 33 commons), ids 1-34; a distinct `RelicHook` framework dispatching in **acquisition order** (trap 8) with DATA/native bindings; +18 tests, 250/250 · [log](stage-b-log.md#b324)
- **B3.25** `[x]` Relics: uncommons — 30 rows, ids 36-65; the canonical pre-shuffle UNCOMMON `pool_order` recovered by inverting the JDK shuffle against 3 live captures; `RelicHook` `ON_MONSTER_DEATH`=14 / `ON_SHUFFLE`=15; power `NEXT_TURN_BLOCK`=23; Paper Phrog retires stage-a A4.1's “unreachable” note; 454/454 ×3 · [log](stage-b-log.md#b325)

### B3.26 `[ ]` ∥ Relics: rares + shop
**Deps:** B3.24 · **Provenance:** relics/ RARE + SHOP, Ironclad-obtainable
**Deliverables:** registry rows + triggers (Bird-Faced Urn, Calipers —
retiring A3.1's structural branch, Champion Belt, Charon's Ashes, Dead
Branch, Du-Vu Doll, Fossilized Helix, Gambling Chip, Ginger, Girya, Ice
Cream — retiring A4.3's EnergyManager SET simplification note, Incense
Burner, Lizard Tail, Magic Flower, Mango, Old Coin, Peace Pipe, Pocketwatch,
Prayer Wheel, Shovel, Stone Calendar, Thread and Needle, Torii, Tungsten
Rod, Turnip, Unceasing Top, Wing Boots, plus SHOP tier — enumerate).
**Acceptance:** tier-2 per relic; Ice Cream forces the energy-recharge
rewrite from SET to conditional-carry (stage-a §12 A4.3 entry's documented
boundary) — regression: all Stage A energy tests still green.
**Inherited:** Odd Mushroom's ×1.25 Vulnerable branch — deferred by B3.25 (Paper Phrog's
×1.75 twin retired there). RARE + SHOP `pool_order` rows for the generic five-tier
initializer — deferred by B4.6.
**Log:** —

### B3.27 `[ ]` ∥ Relics: boss (Neow pool) + event-specials
**Deps:** B3.24 · **Spec:** design §5.3, §5.6 (Neow cat-3 in scope) ·
**Provenance:** relics/ BOSS + SPECIAL, Ironclad-obtainable; event sources
(design §5.6)
**Deliverables:** registry rows + triggers for the Ironclad boss pool
(Black Blood, Snecko Eye — cardRandomRng cost rolls, Runic Dome — observation
impact documented, Coffee Dripper, Cursed Key, Ectoplasm, Fusion Hammer,
Mark of Pain, Philosopher's Stone, Runic Cube?, Sozu, Velvet Choker, … —
enumerate/filter) and the Act-1-event specials (Golden Idol, Neow's Lament,
Necronomicon?, … — filter by S1 event reachability, B4.11-13).
**Acceptance:** tier-2 per relic; Snecko Eye's per-draw cost roll stream +
draw-order accounting tested (trap-10 family).
**Inherited:** BOSS-tier `pool_order` rows for the five-tier initializer — deferred by
B4.6; once B3.26 **and** this task land, un-defer the translator's all-tier
`relicPools` mapping (storage has existed since B4.3).
**Log:** —

---

## Phase B4 — The run layer (Gate G6 = M3)

- **B4.1** `[x]` Map path generation — header-only `map_gen.hpp` re-expressing MapGenerator bit-for-bit on `mapRng` (incl. the H5 `getCommonAncestor` bug, proven load-bearing); edges match the oracle **node-for-node for all 20 seeds**; stop-the-line finding: `setEmeraldElite` DOES fire in S1; 174/174 ×3 · [log](stage-b-log.md#b41)
- **B4.2** `[x]` Room-type assignment — header-only `map_rooms.hpp`: quotas (elite ×1.6 at asc≥1), trap-12 raw-XS128 `Collections.shuffle`, placement rules, fixed rows, the emerald draw; room symbols **and** the post-`generateMap` `{counter,s0,s1}` triple match the oracle for all 20 seeds; 201/201 ×3 · [log](stage-b-log.md#b42)
- **B4.3** `[x]` RunState population + additive fields (schema v2) — `sizeof(RunState)` 1648→2184, **schema 2→3**; pity floats/purgeCost/potion slots/membership bitsets/relic-pool storage added; map reoriented to game-native 15×7 (rename only); combat relic mirror (`sizeof(CombatState)` 3504→3672); 20 fixtures regenerated with a byte-level insertion proof; 273/273 ×3 · [log](stage-b-log.md#b43)
- **B4.4** `[x]` Run-level advance + room lifecycle — `RunController` + `run_begin` / `next_room_transition` (floor++ then reseed, trap 7); NEOW/MAP/COMBAT/REWARD/RUN_OVER phases in one heterogeneous batch; USE_POTION at both layers; combat spawn + fold-back; +19 tests, 387/387 ×3 · [log](stage-b-log.md#b44)

### B4.5 `[ ]` Combat rewards
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
**Inherited:** Question Card / Singing Bowl / White Beast Statue (reward-screen
modifiers) — deferred by B3.25. Pin the CardLibrary HashMap **library order** for
`kIroncladAttackPool` from an oracle capture (a one-line `gen.py` fix; registry-id
order is a documented interim deviation) — deferred by B3.6. Reward-screen
`screen_state` translation — deferred by B1.5/B4.3. **Take a card into the deck
only through `add_card_to_master_deck`** (`run_deck.hpp`), never by writing
`rs.master_deck[]` directly the way the starting-deck init in `run_advance.cpp`
does — that door fires the `onObtainCard` relics (Ceramic Fish, the three eggs,
Darkstone Periapt), and its only production caller today is B4.15's A10 curse
(where the pass is provably empty), so a direct write for reward cards would
silently disable all of them; use `remove_master_deck_card` for the Parasite
side — flagged by the hook audit.
**Log:** —

- **B4.6** `[x]` Relic pools + acquisition — `relic_pools.hpp/.cpp`: 5 unconditional relicRng shuffles (JDK-LCG route), front/end pop, 50/33/17 tier roll, canSpawn re-check + Circlet fallback, acquisition in trap-8 order with pickup effects; 3-seed live-oracle pool + `(s0,s1,counter)` match; 428/428 ×3 · [log](stage-b-log.md#b46)

### B4.7 `[ ]` Treasure rooms
**Deps:** B4.6 · **Spec:** design §5.6; §10 trap 16 · **Provenance:**
AbstractDungeon.java:499-508; AbstractChest.java:54-102; Small/Medium/
LargeChest.java:18-22
**Deliverables:** chest size roll, single-roll gold+tier (trap 16), gold
amount ×(0.9,1.1), relic grant via B4.6, the fixed treasure row (map row 8).
**Acceptance:** tier-2: chest tables vs. hand-derivation across the roll
range; trap-16 named test; oracle spot-diff ≥ 2 treasure floors.
**Inherited:** Matryoshka (chest relic; floor≤40 canSpawn gate already live, effect is
a documented no-op) — deferred by B3.25.
**Log:** —

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
**Inherited:** Eternal Feather (rest-room heal) — deferred by B3.25.
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
B4.11-B4.13's `events.yaml` rows and the canonical list order.
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

### B5.1 `[ ]` ∥ Sim self-replay fuzz soak
**Deps:** B4.4 · **Spec:** design §7.1(2); stage-a §2 (replay-twice memory
guard)
**Deliverables:** `tools/fuzz/` sim-side fuzzer: random-legal + heuristic
policies (design §3.3's E0 stand-ins, implemented here) over seed sweeps;
every run replayed twice, final-state hashes compared; assert/hash-mismatch
triage output with reproducers; overnight-runnable script.
**Acceptance:** ≥ 10M actions across ≥ 10k seeds, zero nondeterminism, zero
asserts, asan-clean sample (≥ 1 % of runs under asan); numbers recorded here.
**Log:** —

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
