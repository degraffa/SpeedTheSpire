# PublicView field audit — v3 (T0.1 + T0.2 + S2.13)

The field-by-field completeness proof for `encode_public_view`
([../include/sts/engine/public_view.hpp](../include/sts/engine/public_view.hpp),
[../src/engine/public_view.cpp](../src/engine/public_view.cpp)), against the
information contract of [training-plan.md](training-plan.md) §1/§2.1: public =
derivable by a perfect-memory player from the revealed action–observation
history; the boundary hides RNG *realizations*, never rules.

**How to read this file.** Every `CombatState` and `RunController` member has a
row, and — since T0.2 — so does every member of every transient struct
`RunController` owns (§8), of the mask channel (§9) and of `KnowledgeState`
(§10). The **Class** column is the information classification the T0.5
total-byte tripwire consumes; the **v1** column says what the encoder does with
it (the column keeps its name; a cell reading `→ field` is current as of v2).

**Completeness is the deliverable, and it is not test-enforced.** A field this
table forgets is *twin-invariant* — the T0.5 twin suite compares two states
that both lack it, so it passes. Only this table catches an omission. The
converse failure, carrying something hidden, IS caught by the twin suite, which
is why the doubtful direction here is always "mask it and write down why".

**Since T0.5 the Class column has an executable twin**:
[`include/sts/engine/byte_class.hpp`](../include/sts/engine/byte_class.hpp)
carries the same classification as a table of byte ranges that must tile
`sizeof(RunController)` exactly, checked by `static_assert` at build time and by
`tests/tripwire_test.cpp` at run time. It cannot tell whether a class is
*right* — that stays a review question, and this file stays the reviewed
artifact — but a field added to `RunController`, `RunState` or `CombatState`
with no row now fails the build instead of silently vanishing from the
observation. **Keep the two in step: a row here and a row there, in the same
change.** The header's per-row `note` is the one-line form of this file's Notes
cell, so a disagreement between them is a documentation conflict
(conventions §4).

**Class vocabulary** (one value per row):

- `public` — carried at face value; the player has observed it or can derive
  it from observed history plus public rules.
- `public-cond` — public except under a declared observability transform
  (today: Runic Dome intent hiding, plan §2.3).
- `derived` — a deterministic function of other public state / public history;
  carrying the raw bytes would add no information, so the view omits or
  re-derives them. Not hidden: a leak test may treat these as public.
- `hidden` — an RNG realization (or a container whose *arrangement* is one)
  that the player has not observed. Never carried raw.
- `padding` — declared pad bytes; no information content.

**v1 status vocabulary:** `→ field` (encoded now), `reserved` (a zero-filled
reserved field exists for it), `excluded` (deliberately not carried — the Notes
say why). No `T0.2` cell remains: the column is complete as of v2, and v3's
single tail-appended field (`event_flags_hi`) has its own row.

Companion pieces at the bottom: the **flags bit audits** (both `flags` words
are carried whole, so every allocated bit is classified) and the
**schema-evolution note** (which changes are additive vs breaking).

---

## 1. CombatState → PublicView (combat section; valid iff `combat_active == 1`)

| CombatState member | Class | v1 | Notes |
|---|---|---|---|
| `kSchemaVersion` | — | excluded | Compile-time constant, no per-instance storage. `PublicView` carries its own independent `public_view_version` (see the header's VERSIONING note). |
| `phase` | public | → `combat_phase` | Screen identity; the player sees whose turn it is. |
| `pad_header` | padding | excluded | |
| `turn` | public | → `turn` | Displayed turn counter. |
| `flags` | public | → `combat_flags` | Carried whole; every allocated bit is public/derived — see §3 bit audit. |
| `player_hp` | public | → `player_hp` | |
| `player_max_hp` | public | → `player_max_hp` | |
| `player_block` | public | → `player_block` | |
| `player_energy` | public | → `player_energy` | |
| `stance` | public | → `stance` | 0 = None (Ironclad is stanceless; field exists for later characters). |
| `cards_played_this_turn` | public | → `cards_played_this_turn` | Own observed plays. |
| `player_power_count` | public | → `player_power_count` | |
| `pad_player` | padding | excluded | |
| `player_powers[24]` | public | → `player_powers[24]` | Full list — the `ObsBuffer` stub carried none. Per-slot classification in §2 (PowerSlot). |
| `card_pool[160]` | derived | excluded | Engine bookkeeping: pool *indices* are not information; every card's public content reaches the view **by value** through the pile projections below. A pool row referenced by no pile is dead storage. |
| `hand[10]` | public | → `hand[10]` | Engine order — the hand is on screen. Projected as `PvCard` values (§2). |
| `draw[128]` | public contents / **hidden order** | → `draw[128]`, canonically sorted | The multiset is perfect-memory-derivable; the *arrangement* is a shuffle realization. Sorted ascending (card_id, upgrade, cost_now, flags) so hidden order cannot reach the bytes — the T0.5 twin-equality property. KnowledgeState-constrained known positions are T0.2/T0.3 (projected as constraints, not by unsorting). |
| `discard[128]` | public | → `discard[128]` | Engine order is public: every arrival was an observed event and the game lets the player inspect the discard in order-relevant ways only via effects that reveal it; the order here is the fold of observed arrivals. |
| `exhaust[128]` | public | → `exhaust[128]` | Engine order; every exhaust was observed. |
| `limbo[8]` | public | → `limbo[8]` | Mid-resolution holding zone; arrivals observed. Normally empty at a WAITING_ON_USER boundary, carried for the mid-resolution CHOOSE screens where it is not. |
| `hand_count` / `draw_count` / `discard_count` / `exhaust_count` / `limbo_count` | public | → same names | |
| `monster_count` | public | → `monster_count` | |
| `combat_gold` | public | → `combat_gold` | Gold gains are displayed as they happen (Hand of Greed flare). |
| `pad_monsters[2]` | padding | excluded | Declared by T0.5. It was the compiler's implicit alignment gap before `monsters[]` until the classification tripwire named it; `CombatState` is memcmp'd and byte-hashed, which is exactly the conventions §8 incident. Adding the member moved no offset and no size. |
| `monsters[7]` | see §2 | → `monsters[7]` | Per-field classification in §2 (MonsterState). |
| `action_queue[64]` + `action_head/tail/count` + `pad_actionq` | derived | excluded | Resolution transients. At every decision boundary their contents are a deterministic function of the observed public action history (the pump is deterministic given public plays); carrying them adds no information. The pending-screen context a consumer does need arrives through the mask channel (§9) — `PublicView.ResolutionQueuesReachTheViewOnlyThroughTheMaskChannel` pins both halves: no queue byte reaches a data field, and a pending resolution DOES move the mask. |
| `pre_turn_actions[16]` + `pre_turn_head/tail/count` | derived | excluded | Same as above; may be non-empty at WAITING_ON_USER (queued start-of-next-turn hooks), but every entry is the consequence of an observed play/trigger. |
| `turn_has_ended` | derived | excluded | Pump bookkeeping; deterministic at every boundary. |
| `card_queue[16]` + `card_queue_count` + `pad_cardq` | derived | excluded | Pending plays the player queued; deterministic from public history. |
| `monster_queue[5]` + `monster_queue_count` + `monster_attacks_queued` | derived | excluded | Turn-order bookkeeping; deterministic from public state. |
| `relics[40]` + `relic_count` | public | → `relics[40]`, `relic_count` | The combat relic mirror. Relics + displayed counters are the plan §2.1 always-block; while `phase == COMBAT` the encoder reads THIS live mirror, not `RunState.relics`, because in-combat counter ticks (Kunai, Ink Bottle) land here until the end-of-combat fold-back and the mirror is what the screen shows. Pinned by `PublicViewRun.RelicCountersComeFromTheCombatMirrorInCombat`. |
| `pad_relics[7]` | padding | excluded | |
| `pending_potion_count` + `pending_potion[5]` | public | excluded | The in-combat belt-obtain accumulator (Entropic Brew's queued `ObtainPotionAction`s, `Opcode::OBTAIN_POTION`), in what was `pad_rng_align[6]` -- declared by T0.5 (`pad_relics` rounds the relic mirror out but does not reach the 8-aligned stream block, so six bytes were implicit). PUBLIC but not separately encoded, for the `pending_obtain` reason: the run layer drains it onto `RunState.potions` (which IS encoded) at the same command boundary, so no observable `PublicView` is taken while it is non-zero. |
| `monster_hp_rng` / `ai_rng` / `shuffle_rng` / `card_random_rng` / `misc_rng` | hidden | excluded | The five floor-scoped stream states are exactly the realizations the contract hides. Resampled by T0.4. |

## 2. Projected element types

### PowerSlot → PvPower (player and monster power lists)

| PowerSlot member | Class | v1 | Notes |
|---|---|---|---|
| `power_id` | public | → `power_id` | |
| `amount` | public | → `amount` | The oracle-visible stack number. |
| `counter` | public | → `counter` | Second oracle-visible number (The Bomb / Panache damage). For Vulnerable/Weak/Frail it is the justApplied latch — derivable (the application was observed) and 0 at every WAITING_ON_USER boundary anyway. |
| `pad0` | padding | excluded | |

### CardInstance → PvCard (all pile projections)

| CardInstance member | Class | v1 | Notes |
|---|---|---|---|
| `card_id` | public | → `card_id` | |
| `upgrade` | public | → `upgrade` | |
| `cost_now` | public | → `cost_now` | Live cost after observed modifiers. |
| `flags` | public | → `flags` | Every CardFlag bit latches a consequence of an observed public event (a Forethought grant, a this-turn cost write, an authored EXHAUST); FREE_TO_PLAY_ONCE is load-bearing information `cost_now` does not carry. SAVED_BASE_COST's payload bits encode an observed permanent cost write. |
| `misc` | derived | excluded | Mid-resolution scratch (AUTOPLAY_X_ENERGY payload on a purge-on-use replay copy); a deterministic consequence of the observed play, never information. |

### MonsterState → PvMonster

| MonsterState member | Class | v1 | Notes |
|---|---|---|---|
| `monster_id` | public | → `monster_id` | |
| `hp` | public | → `hp` | HP bars are displayed. |
| `max_hp` | public | → `max_hp` | Displayed; the spawn roll is revealed at spawn. |
| `block` | public | → `block` | The stub's biggest gap; displayed every frame. |
| `flags` | public | → `flags` | Carried whole; every allocated bit latches an observed event — see §4 bit audit, and the schema-evolution rule for future bits. |
| `move_history[3]` | public | → `move_history[3]` | Past moves were observed as they resolved — visible even under Runic Dome. |
| `intent` | public-cond | → `intent` (0 under Runic Dome) | The telegraphed NEXT move. Suppressed at this write site exactly as `encode_observation` does (the game's two rendering guards, AbstractMonster.java:258/:749, touch no game state). Sentinel 0 is unambiguous: move ids are never 0. |
| `power_count` | public | → `power_count` | True live count. |
| `pad0` | **hidden** (mixed-use) | excluded | Per-type scratch that can hold an **unrevealed construction roll** — the Louse bite damage, rolled at spawn and revealed only by the first bite/telegraph. Other types' uses (Gremlin Wizard charge, Lagavulin nibbles, Guardian HP baseline, Red Slaver entangle latch, Hexaghost divider base) are derivable from public `move_history`/HP, but the byte is excluded wholesale because one producer is hidden. Revealed rolls surface through the T0.2 KnowledgeState projection (plan §2.2), never by leaking the raw byte. |
| `pad1[2]` | padding | excluded | |
| `powers[24]` | public | → `powers[24]` | FULL list — closes the stub's 4-of-24 truncation. Per-slot classification above. |

PvMonster additionally carries `occupied` (derived: `slot < monster_count`)
and explicit `pad0[2]` (always zero).

## 3. CombatState.flags — bit audit (word carried whole)

| Bits | Meaning | Class | Why public |
|---|---|---|---|
| 0 | retired (ex-Frail latch) | — | Never set; reads 0. |
| 1 | `kCombatFlagCannotLose` | derived | Set/cleared by split machinery the player watches happen. |
| 2 | `kCombatFlagMugged` | public | The thief's escape is observed. |
| 3 | `kCombatFlagPlayerEscaped` | public | The player's own Smoke Bomb. |
| 4 | `kCombatFlagCentennialPuzzleUsed` | public | The relic flash is displayed; also derivable from the first observed HP loss. |
| 5 | `kCombatFlagRedSkullActive` | derived | Own HP vs 50% of own max HP. |
| 6–7 | unallocated | — | Zero. |
| 8–15 | Combust hpLoss counter | derived | +1 per observed Combust play. |
| 16–18 | Fairy-in-a-Bottle armed count | public | The player's own belt. |
| 19 | released unspent | — | Zero. |
| 20 | `kCombatFlagEliteRoom` | public | Room kind is public from the map / the event dialog that set it. |
| 21–23 | Orange Pellets type latches | public | Own observed plays this turn. |
| 24 | `kCombatFlagArtOfWarAttackPlayed` | public | Own observed plays this turn. |
| 25–31 | unallocated | — | Zero. |

**Rule for future bits:** a new `CombatState.flags` bit must add a row here
*before* it ships, because the word is carried whole. A bit whose value is a
hidden realization may not land in a carried word — it would need an encoder
mask, which is a breaking view change (see the schema-evolution note).

## 4. MonsterState.flags — bit audit (word carried whole)

Type-scoped region (0–23) and global region (24–31); every allocated bit
latches a consequence of an observed event:

| Bits | Owner / meaning | Class | Why public |
|---|---|---|---|
| 0x0001 | Cultist RitualSkip | public | Set by the observed Incantation cast. |
| 0x0002 | Louse CurlUpTriggered | public | The curl-up block gain is displayed. |
| 0x0004 | Large-slime SplitTriggered | public | The split telegraph is displayed. |
| 0x0008 / 0x0010 / 0x0020 | Lagavulin asleep / isOut / outTriggered | public | Sleep and shell state are drawn; outTriggered follows the observed wake hit. |
| 0x0040 / 0x0080 | Guardian OPEN / CloseUpTriggered | public | Mode is drawn; the flip threshold trigger is the observed mode-shift telegraph. |
| 0x0700 | Guardian shift count | public | Count of observed Defensive-Mode flips. |
| 0x3800 | Hexaghost orb count | public | The orbs are drawn. |
| 0x4000 | Hexaghost burnUpgraded | public | Follows the observed first Inferno. |
| 1<<24 | ESCAPED (global) | public | The escape was observed. |
| others | unallocated | — | Zero. |

**Rule for future bits:** same as §3 — audit-before-carry; the two-region
policy (combat_state.hpp) does not by itself guarantee publicness, this table
does.

## 5. RunController → PublicView

| RunController member | Class | v1 | Notes |
|---|---|---|---|
| `run` | see §6 | — | Row-by-row in §6. |
| `combat` | see §1 | → combat section | Encoded only while `phase == COMBAT` in v1 (`combat_active == 1`); zeroed otherwise. |
| `phase` | public | → `run_phase` | Screen identity. |
| `cur_x` | public | → `cur_x` | Own map position (`kNeowColumn` at Neow). |
| `room_type` | public | → `room_type` | The entered room's kind. |
| `combat_outcome` | public | → `combat_outcome` | How the last combat ended — observed. |
| `lists` (MonsterLists) | consumed prefix public / **suffix hidden** | → `monster_prefix` / `elite_prefix` / `boss_prefix` (prefix only) | Per-field rows in §8.7. The consumed prefix is plan §1 tracker state, carried as registry `EncounterDef.id`s; the unconsumed suffix is a monsterRng realization — excluded, resampled by T0.4's Markov continuation. `boss_list[0]` is carried regardless of the cursor: the act boss is public from the map screen. `PublicViewRun.EncounterSuffixDoesNotLeak`. |
| `monster_cursor` / `elite_cursor` / `boss_cursor` | public | → same names | The consumed-prefix lengths. The encounter the player is currently INSIDE is carried separately as `current_encounter_id` (revealed by room entry; the cursor still points at it, since it advances on room exit). |
| `pad1` | padding | excluded | |
| `emerald_x` / `emerald_y` | public | → `emerald_x` / `emerald_y` | The burning-elite node is drawn on the map. |
| `pad_emerald[2]` | padding | excluded | |
| `rewards` (RewardScreen) | public **while on screen** | → `rewards` (§8.1) | Gated on the screen actually being up (COMBAT_REWARD, or Neow's ITEM_REWARD), never on the struct being non-empty: Dead Adventurer loads this struct with the rewards the player never searched out and THEN starts its combat, so an emptiness gate would hand over unrevealed rolls. `PublicViewScreens.RewardScreenIsNotEncodedWhileItIsNotOnScreen`. |
| `rest` (RestSiteState) | public | → `rest_screen` (§8.2) | Only the screen id is stored; the campfire's option list is rebuilt per call and reaches the consumer through the mask (§9). |
| `shop` (ShopState) | public | → `shop` (§8.3) | Current-visit stock is public once the floor is entered (plan §2.4 table); gated on `phase == SHOP` so a left shop's stale stock does not follow the player. |
| `treasure_chest` (TreasureChest) | **contents hidden pre-open** / size public | → `chest_*` (§8.4) | The one masking trap named by plan §2.1: contents roll at construction; pre-open only the chest SIZE is carried. The `PublicViewChest.*` tests pin the reveal timing and the pre-open byte-equality. |
| `event` (EventDialogState) | mixed — see §8.5 | → `event` (§8.5) | Per-event scratch publicity plus a masked Match & Keep board; the board's unflipped identities are the plan §2.4 sampler row. |
| `neow` (NeowState) | public | → `neow` (§8.6) | The rolled options are displayed. |
| `pending_bottle` | public | → `pending_bottle` | The modal overlay is on screen. |
| `pending_deck_pick` | public | → the mask (§9) | Dolly's Mirror's raw-master-deck grid, the same modal overlay one byte over (equip-trio). It gets NO field of its own: its whole observable is "every master-deck row is choosable while `pending_bottle` is NONE", which `action_mask` already carries, and the enum has exactly one member so there is nothing to disambiguate. A second member would want a field, and the `PUBLIC_VIEW_VERSION` bump that goes with it. |
| `pad2[2]` | padding | excluded | One byte shorter than the `pad2[3]` this row used to name — `pending_deck_pick` above took it, which is why adding that overlay moved no hash and regenerated no twin fixture. |
| `knowledge` (KnowledgeState) | public (a record OF public reveals) | → projection (§10) | Never carried raw — its chain holds pool indices, which are engine bookkeeping. Projected as per-draw-slot order-constraint annotations plus the revealed monster construction rolls. |
| `pad_live_align[2]` | padding | excluded | The two bytes T0.5 declared as `pad_tail`; since S2.48 they are the alignment gap `stolen_live`'s 4-alignment inserts after `knowledge` (which ends 2 mod 4). `RunController` is memcpy'd and memcmp'd by the resample/twin suites, so conventions §8's rule applies. |
| `stolen_live` (StolenGoldLive) | derived | excluded | S2.48 live-purse bookkeeping — which steals / Hand-of-Greed gains the run layer has already charged to `RunState.gold`, a deterministic function of public state (steal counts, `combat_gold`, the purse). The purse itself is already public through `RunState.gold`; this member carries no realization of its own. Combat-scoped transient on the `knowledge` precedent. |

## 6. RunState → PublicView

| RunState member | Class | v1 | Notes |
|---|---|---|---|
| `kSchemaVersion` | — | excluded | Compile-time constant. |
| `run_seed` | **hidden** | excluded | Explicitly hidden by plan §2.6b — knowing it is seed-cracking. |
| `master_deck[128]` + `master_deck_count` | public | → `master_deck[128]`, `master_deck_count` | Always-block. Carried in ENGINE ORDER, not sorted: unlike the draw pile this order is not a hidden realization (it is the fold of observed acquisitions), and it is the index space the mask's `can_choose_master_deck[]` addresses — sorting would desynchronize the observation from the action space. Plan §2.1's word "multiset" is about *content*, which is complete either way. |
| `hp` / `max_hp` | public | → `run_hp` / `run_max_hp` | Always-block. Separate fields from the combat block's `player_hp`, which is zero outside combat. |
| `pad_gold_align` | padding | excluded | |
| `gold` | public | → `gold` | Always-block. |
| `ascension` | public | → `ascension` | Always-block. |
| `act` | public | → `act_reserved` | POPULATED from v2 (1..4). The declared reader rule stands: a record with `public_view_version < 2` reads 0 and maps to act 1, exact for S1 data. |
| `floor` | public | → `floor` | Always-block. |
| `relics[40]` + `relic_count` | public | → `relics[40]`, `relic_count` | Always-block: relics with displayed counters. The source is the §1 combat MIRROR while `phase == COMBAT`, this array otherwise. |
| `pad_relic` | padding | excluded | |
| `potions[5]` | public | → `potions[5]` | The belt, public in every phase. |
| `map[105]` | public | → `map[105]` | Always-block: the full current-act map (room kind + edge bitfield per node). The burning-elite node is the `emerald_x`/`emerald_y` pair, drawn on the same screen. |
| `boss_ids[4]` | public | → `boss_ids[4]` | The act boss's identity is displayed on the map screen. |
| `keys` | public | → `keys_reserved` | POPULATED from v2: the bits exist and have a live S1 writer (the campfire's Recall sets the ruby key). Zero in a v1 record already MEANS "no keys", so the reinterpretation is exact. |
| `pad_keys` | padding | excluded | |
| `event_flags` / `shop_flags` | public | → same names | One-shot *fired* bitsets — the fires were observed. |
| `event_flags_hi` | public | → `event_flags_hi` | S2.13. The FIRED bitset's SECOND word (event ids 32..63 at bit id-33, the Act-2/3 events). Same classification and same reason as `event_flags`; it exists only because ids 32..51 do not fit one `uint32_t` and neither struct could widen the first word in place. Appended at the v3 tail, i.e. **after `action_mask`** — see the version log. |
| `card_blizz_randomizer` / `blizzard_potion_mod` | public | → same names | Plan §1: pity counters are tracker state, encoded verbatim. |
| `event_pity_monster` / `event_pity_shop` / `event_pity_treasure` | public | → same names | Same; carried as `float`, the storage width, so no rounding is introduced. |
| `purge_cost` | public | → `purge_cost` | Displayed in every shop. |
| `potion_slots` | public | → `potion_slot_count` | |
| `pad_potion_slots` | padding | excluded | |
| `event_membership` / `special_membership` / `shrine_membership` | public | → same names | Remaining-pool membership: initial lists are public rules, removals were observed. |
| `pad_membership` | padding | excluded | |
| `relic_pools[5][48]` | **hidden** (unrevealed window) | excluded | The remaining order/composition of each tier's `[0, count)` window is a shuffle realization. Resampled by T0.4 (with the pop-time canSpawn corner cases). Observed pops are public via `relics[]`. |
| `relic_pool_count[5]` | derived | excluded | Initial tier size (public rule) minus observed pops. T0.2 declined the convenience copy: it is exactly re-derivable, and every carried byte is one the twin suite and the tripwire must keep honest. Surfacing it later remains an additive change. |
| `pad_relic_pools[3]` / `pad_rng_align_lo[2]` | padding | excluded | `pad_rng_align` was 6 bytes through v2; S2.13 carved four of them into `event_flags_hi` (row above), which is why no `RunState` offset moved and `SCHEMA_VERSION` did not bump. |
| `monster_rng` … `neow_rng` (9 streams) | hidden | excluded | Run-/act-/event-scoped stream states are realizations. Resampled fresh by T0.4. |
| `boss_chest` (BossChestState) | **offers hidden until `seen`** / screen bits public | → `boss_relic_choice_reserved[3]` + `chest_opened` (§7) | **S2.47 (schema v8):** moved here from `RunController` as a tail append, so the oracle translator and `diff_run_states` can see the offers (design §6 S2-G2 item 2). Same masking trap as `treasure_chest`, one room later: `BossChest`'s constructor pops all three at ROOM ENTRY (BossChest.java:35-39) and the player sees them only by opening; a SKIP closes the chest but cannot unsee them, so `seen` — not `screen` — gates the encode. `screen` / `seen` / `chose_relic` are public (the player is looking at / performed them); `pad[7]` excluded. The `BossChest.*` tests pin the reveal timing, the unopened-twin byte-equality, and the sampler's coherent offers+pool redraw. |

## 7. PublicView-only fields (no CombatState/RunController source)

| PublicView member | Source | Notes |
|---|---|---|
| `public_view_version` | constant | `PUBLIC_VIEW_VERSION` at encode time. |
| `combat_active` | derived | `rc.phase == COMBAT`. |
| `monsters[i].occupied` | derived | `i < monster_count`. |
| `boss_relic_choice_reserved[3]` | populated (was reserved) | Additive case 1, populated by the S2.11 boss chest: the three offered boss-relic ids while `phase == BOSS_TREASURE`, and ONLY once the chest has been opened (`boss_chest.seen`) — before that they are drawn-but-unseen and the field stays zero. Source moved to `run.boss_chest` at S2.47 (schema v8) with no PublicView change. This row said "still zero in v2" until S2.47 — S2.11 populated the field without updating it. |
| `second_boss_reserved` | derived | **v5 (S2.28):** the `EncounterDef` id of the SECOND Act-3 boss of an A20 double-boss run (`boss_list[1]`), carried from the moment the double-boss transition reveals it (`act == kFinalAct && boss_cursor >= 1`); 0 otherwise. Public because the player is looking at those monsters; the sampler preserves the same boss-list prefix, so the hidden twin agrees. Unlike `current_encounter_id` it survives into `RUN_OVER`, which is the state a finished run is observed in. |
| `pad_tail[3]` | padding | Always zero. Retained as a member at its v1 offset — the v2 tail appends *after* it, so no v1 offset moves. |
| `current_encounter_id` | derived | The encounter of the room being occupied, resolved from `lists` + the matching cursor while in COMBAT / COMBAT_REWARD. 0 for event combats (their monsters are on screen in the combat section) and outside such a room. |
| `rewards.active` / `shop.active` / `event.active` / `neow.active` | derived | 1 iff that screen is the one on screen. Zero elsewhere is the declared "not present" value the additive-append rule requires. |
| `event.scratch_public_mask` | derived | Which `scratch[i]` words this event declares public (§8.5). Carried so a consumer can tell "zero because masked" from "zero because zero". |
| `event.board[i].revealed` | derived | Whether a Match & Keep slot's identity is public (§8.5). |
| `draw_constraint_rank[]` / `draw_exact_pos[]` | derived (from KnowledgeState) | §10. |
| `action_mask.pad_end[]` | padding | Always zero; rounds the record back to 4-byte alignment so `PublicView` itself carries no compiler-inserted bytes. |

## 8. RunController transient structs → PublicView (T0.2)

Every struct `RunController` owns by value gets a member-level table here. The
shared rule for all of them: **a screen section is encoded only while that
screen is on screen.** A transient struct is not cleared the moment its screen
closes, and worse, one of them is *filled ahead of time* — so "non-empty"
is not a safe gate and phase is.

### 8.1 RewardScreen / RunRewardItem → `PvReward` / `PvRewardItem`

Gate: `phase == COMBAT_REWARD`, or `phase == NEOW && neow.screen == ITEM_REWARD`.

| Member | Class | v1 | Notes |
|---|---|---|---|
| `RewardScreen.items[8]` | public while on screen | → `rewards.items[8]` | Per-field below. |
| `RewardScreen.count` | public | → `rewards.count` | |
| `RewardScreen.open_card_item` | public | → `rewards.open_card_item` | Which CARD row's pick screen is up (`kNoOpenCardReward` when none). |
| `RewardScreen.pad[2]` | padding | excluded | |
| `RunRewardItem.gold` / `bonus_gold` | public | → same names | Rolled at assembly, printed on the row (Golden Idol's bonus included). |
| `RunRewardItem.id` | public | → `id` | RelicId / PotionId of the row. |
| `RunRewardItem.card_ids[4]` | public | → `card_ids[4]` | The card offer; revealed with the screen. |
| `RunRewardItem.card_upgrades[4]` | public | → `card_upgrades[4]` | |
| `RunRewardItem.kind` | public | → `kind` | RewardItemKind. |
| `RunRewardItem.card_count` | public | → `card_count` | |

### 8.2 RestSiteState → `rest_screen`

Gate: `phase == REST_SITE`.

| Member | Class | v1 | Notes |
|---|---|---|---|
| `screen` | public | → `rest_screen` | RestScreen (menu / Smith / Toke / Dream Catcher). |
| `pad[3]` | padding | excluded | |

`RestMenu` / `RestOptionEntry` are **not** `RunController` state — the campfire
menu is rebuilt from relics and run state on every `legal_actions` call. Its
information therefore reaches the consumer through §9's mask
(`can_choose_rest[]`), which is the case plan §2.1 introduces the channel for.

### 8.3 ShopState / ShopSlot → `PvShop` / `PvShopSlot`

Gate: `phase == SHOP`.

| Member | Class | v1 | Notes |
|---|---|---|---|
| `colored[5]` / `colorless[2]` / `relics[3]` / `potions[3]` | public | → same names | The whole visit's stock is built on room entry and drawn with prices (plan §2.4: "current-visit shop stock … public — copy"). |
| `sale_index` | public | → `sale_index` | The sale tag is rendered on its shelf. |
| `screen` | public | → `screen` | ShopScreenKind (floor vs purge grid). |
| `purge_available` | public | → `purge_available` | |
| `pad` / `pad2` | padding | excluded | |
| `actual_purge_cost` | public | → `actual_purge_cost` | This visit's price, displayed on the service. |
| `ShopSlot.id` | public | → `id` | `kShopRestockedUnknownCard` is itself public: the Courier's one unmodelled restock slot holds a card that IS on the shelf whose identity this engine cannot name (shop.hpp's Courier block). |
| `ShopSlot.price` | public | → `price` | Post-discount, as shown. |
| `ShopSlot.sold` | public | → `sold` | |
| `ShopSlot.upgrade` | public | → `upgrade` | The eggs' preview upgrade is on the card the player sees. |
| `ShopSlot.pad[2]` | padding | excluded | |

### 8.4 TreasureChest → `chest_*` (the masked one)

Gate: `phase == TREASURE_ROOM || phase == COMBAT_REWARD` (opening moves the
controller to the reward screen with the chest record retained; skipping or
proceeding zeroes it).

| Member | Class | v1 | Notes |
|---|---|---|---|
| `size` | public | → `chest_size` | The chest model on the floor tells the player its size before any interaction. |
| `relic_tier` | **hidden until opened** | → `chest_relic_tier` (0 pre-open) | `AbstractChest.randomizeReward` rolls the contents at CONSTRUCTION, i.e. on room entry (treasure_rooms.hpp); the player learns them only from the open action. |
| `has_gold` | **hidden until opened** | → `chest_has_gold` (0 pre-open) | Same roll. |
| `opened` | public | → `chest_opened` | The player performed the action. |

### 8.5 EventDialogState / EventBoardCard → `PvEvent` / `PvEventBoardCard`

Gate: `phase == EVENT_DIALOG || phase == ROOM_UNIMPLEMENTED` (a resolved event
with no implemented body parks there with its selection committed, and the
player is standing in that room, so the id is public).

| Member | Class | v1 | Notes |
|---|---|---|---|
| `event_id` | public | → `event.event_id` | Which event the room resolved to. |
| `screen` | public | → `event.screen` | The page the dialog is on. |
| `grid_kind` | public | → `event.grid_kind` | Which card grid is open. |
| `scratch0..3` | **mixed, per event** | → `event.scratch[4]` under `scratch_public_mask` | See the table below. |
| `board[12]` | **mixed** | → `event.board[12]` | Per-field below. |
| `EventBoardCard.card_id` / `upgrade` | **hidden until revealed** | → same, 0 unless `revealed` | The twelve cards are dealt FACE DOWN from a miscRng shuffle. |
| `EventBoardCard.taken` | public | → `taken` | A matched pair visibly leaves the board. |

**Per-event scratch publicity** (`event_scratch_public_mask`, public_view.cpp).
The switch there has no `default:`, so a new `EventId` is a diagnostic at the
site that must classify it rather than inheriting somebody else's answer.

| Event | Mask | Why |
|---|---|---|
| the twenty Act-2/3 `eventList` rows (S2.02, ids 32-51) | `0` | Identity rows: no body is linked, so nothing writes their scratch and it reads zero either way. Masked because that is the answer that stays correct if a body lands without revisiting the switch — Mind Bloom's boss shuffle and Cursed Tome's book draw are the Dead-Adventurer shape, and a leak here is invisible to every downstream twin test. S2.31–S2.33 reclassify per body. |
| `DEAD_ADVENTURER` | `0` | `scratch0` packs a miscRng JDK shuffle of {gold, nothing, relic} plus the search count; `scratch1` names the elite the fight will spring. Both are rolled at ROOM ENTRY and neither is on screen — the reward order is revealed one search at a time, the elite only when the fight starts. The search-count bits *are* public but are exactly derivable from the observed presses, so the word is masked rather than carried under a per-event transform. |
| every other event (the S1 Act-1 rows) | `0x0F` | The word is either never written (reads zero) or holds a number the dialog prints: Scrap Ooze's ramping chance/damage, World of Goop's gold, FaceTrader's gold/HP offer, We Meet Again's three offers, Match and Keep's attempts-left and currently-flipped slot. |

**Known coarsening (T0.4 owns it).** A perfect-memory player also remembers the
identities of a MISSED Match-and-Keep pair, which the game flips back face
down. The engine keeps no record of past flips, so the view under-informs there
rather than leaking. Widening it needs a per-slot "seen" latch in the event
body — which is exactly plan §2.4's "pin revealed flips, permute the rest"
sampler row.

### 8.6 NeowState → `PvNeow`

Gate: `phase == NEOW`.

| Member | Class | v1 | Notes |
|---|---|---|---|
| `option_type[4]` | public | → `neow.option_type[4]` | The four rolled blessings are the screen. |
| `option_drawback[4]` | public | → `neow.option_drawback[4]` | Printed under their options. |
| `grid_picked[3]` | public | → `neow.grid_picked[3]` | The player's own picks, in pick order. |
| `hp_bonus` | public | → `neow.hp_bonus` | Frozen at blessing time from public max HP. |
| `screen` | public | → `neow.screen` | NeowScreen. |
| `chosen` | public | → `neow.chosen` | The player's own choice. |
| `grid_mode` / `grid_needed` / `grid_done` | public | → same names | The open grid's terms. |
| `pad` | padding | excluded | |

### 8.7 MonsterLists → prefix arrays

| Member | Class | v1 | Notes |
|---|---|---|---|
| `monster_list[16]` | prefix public / suffix **hidden** | → `monster_prefix[16]` for `i < monster_cursor` | Entries are registry encounter keys; carried as `EncounterDef.id`. |
| `elite_list[10]` | same | → `elite_prefix[10]` for `i < elite_cursor` | |
| `boss_list[3]` | same, plus `[0]` always | → `boss_prefix[3]` | `boss_list[0]` is public from the map screen from act start, so it is carried whether or not the cursor has passed it. |
| `monster_list_count` / `elite_list_count` / `boss_list_count` | **hidden** | excluded | The generated list LENGTH is a property of the unrevealed suffix, not of anything observed; the cursors are the public counts. |

## 9. RunActionMask / ActionMask → the mask channel

Plan §2.1: *"The legal-action mask is an observation channel. A legality bit
computed from hidden state is a leak the observation-equality test cannot
catch."* The bytes are therefore a MEMBER of `PublicView` (`action_mask.mask`),
not a parallel object a consumer might hash and forget.

| Member | Class | v1 | Notes |
|---|---|---|---|
| every `RunActionMask` field | derived | → `action_mask.mask` (whole struct, byte for byte) | Filled by `legal_actions(rc, ...)` at encode time. Nothing here is classified individually **on purpose**: the point of carrying the bytes is that whether each bit is truly derivable from public state becomes a *tested* property (T0.5's twin-invariant-mask requirement) instead of a reviewed one. |
| `ActionMask combat` (nested) | derived | → same | Ditto for the delegated in-combat mask. |
| `PvMask.pad_end[]` | padding | always zero | Alignment only. |

`RunActionMask` is all `bool`/`uint8` today, so `alignof` is 1 and it has no
implicit padding; a `static_assert` in public_view.hpp fails if that changes,
because a padded mask would put indeterminate bytes into a hashed record.

### 9a. Known mask leak — the draw-source CHOOSE window (found by T0.5)

`ActionMask.can_choose[i]` for a **DRAW-source** choice (`ChoiceKind::DRAW_TO_HAND`
— Secret Technique / Secret Weapon, both real S1 registry rows) is computed as
`instance_has_type(s, s.draw[i], filter)` (`choice_slot_eligible`,
interp_cards.cpp). It therefore reports the card TYPES of the first `kHandCap`
draw-pile **array slots**, and draw-pile order is a shuffle realization (§1).
That is hidden information reaching an observation channel.

**The game does not do this.** `SkillFromDeckToHandAction.update` builds its
browse group with `tmp.addToRandomSpot(c)` over the filtered draw pile and opens
the grid on `tmp` (SkillFromDeckToHandAction.java:35-40, :65) — the presentation
order is deliberately randomised, so a player sees the eligible cards but not
their pile positions. The leak is our slot-indexed action space, not the rule.

**Status.** Repairing it means giving the draw-source choice its own
presentation order (and the draws that build it), i.e. an action-space change,
which is outside T0.5. Until then:

- `make_hidden_twin` PINS the draw pile whenever such a screen is open
  (`draw_choice_pending`, twin.hpp), so the leak gate is green on a defect it
  has already recorded rather than red on one it cannot fix;
- `TwinDrawChoiceLeak.MaskReadsRawDrawSlotsWhileADrawSourcedChoiceIsOpen`
  (tests/twin_test.cpp) asserts the leak **still exists**, so it turns red the
  day the action space is repaired — which is the day the pin and that test are
  both deleted;
- the belief sampler (`resample_hidden`) is deliberately NOT pinned: it must
  keep sampling the true belief. Sampler-side conditioning on this screen is a
  question for the T0.6 distributional suite, not for the leak gate.

## 10. KnowledgeState → PublicView (the projection)

`KnowledgeState` (knowledge.hpp) is a record OF public reveals, so nothing in it
is hidden — but it cannot be carried raw, because its chain holds
`CardPoolIndex` values, and pool indices are engine bookkeeping rather than
information (the same reason §1 excludes `card_pool`). It is projected instead.

| Member | Class | v1 | Notes |
|---|---|---|---|
| `chain[128]` | public (as constraints) | → `draw_constraint_rank[128]` / `draw_exact_pos[128]` | The draw pile is encoded as a canonically SORTED multiset, so order knowledge cannot be expressed by rearranging it — unsorting would put the hidden arrangement straight back into the bytes. It is expressed as two annotations parallel to `draw[]`: the 1-based rank of that card in the known relative-order chain, and its 1-based exact from-top position where it has one. The projection walks the chain top-first and binds each entry to the lowest not-yet-annotated sorted slot holding that card VALUE; identical values are interchangeable under a multiset encoding, so the tie-break is canonical. |
| `chain_count` | public | → `knowledge_chain_count` | |
| `exact_prefix` | public | → `knowledge_exact_prefix` | How many leading chain entries are position-exact. |
| `full_order` | public | → `knowledge_full_order` | Frozen Eye. Under it every slot carries an exact position, which reconstructs the true order exactly — pinned by `PublicViewKnowledge.FrozenEyeFullOrderRoundTrips`. |
| `monster_roll_known[7]` | public | → `monster_roll_known[7]` | |
| `monster_roll[7]` | public **iff known** | → `monster_roll[7]` (0 when not known) | The declared reveal channel for `MonsterState.pad0`, which stays excluded wholesale (§2). |
| `pad0` / `pad1[2]` | padding | excluded | |

The projection runs only while `phase == COMBAT`: knowledge is combat-scoped
(reset by `enter_combat`) and its annotations index the combat section's draw
pile, which is zero in every other phase.

These are the plan §3.1 order-constraint flags the training repo's card token
reads. They are also the field class this audit exists for: an omitted
constraint flag is twin-invariant, so no later test class would have caught it.

---

## Schema-evolution note (v1)

One stamp: `PUBLIC_VIEW_VERSION` (public_view.hpp), stored per instance,
independent of the engine `SCHEMA_VERSION` (an engine layout change that does
not alter what is public must not invalidate training shards, and vice versa).
**Any** change to the view's layout or field semantics bumps it, and the bump
records here which class the change falls in. Downstream loaders are
refuse-on-mismatch (training plan, T1.2); "additive" below means a mechanical,
lossless reinterpretation of older records exists, so S1-era checkpoints,
shards and eval snapshots stay forward-readable — the refusal is then lifted
by a declared migration rule, not by luck.

**Additive changes** (offsets, widths and meanings of every existing byte
unchanged; old records remain valid under the new reading):

1. **Populating a reserved field with its declared meaning** —
   `keys_reserved`, `boss_relic_choice_reserved`, `second_boss_reserved`,
   `act_reserved` (plan §2.1; lands with T4.1/S2, T5.1/S3). Old records carry
   zero there. For `keys_reserved` zero already *means* "no keys", so old
   records are semantically exact. For `act_reserved` the populated encoding
   is 1..4 and 0 is reserved for pre-population records; readers map
   version < (the populating version) ⇒ act 1, which is exact for S1 data.
2. **Appending fields at the tail** (before nothing — after `pad_tail`, which
   then absorbs into the new layout's alignment). `sizeof` grows; every
   existing offset holds; old records extend with zeros under the new reading
   *iff* zero is the declared "not present" value of each appended field —
   that declaration is part of the change.
3. **New id values inside carried fields** (new CardId/PowerId/MonsterId/
   PotionId rows): the registry is append-only (design §4.4), no layout moves;
   consumers absorb via embedding-table headroom (plan §3.1).
4. **Allocating a previously-zero bit of a carried flags word to a *public*
   meaning** (per the §3/§4 audit-before-carry rule): old records correctly
   read the bit as 0 = "never happened".

**Breaking changes** (bump + reanalyze-or-quarantine per plan §4.1's data
lifecycle rule; no in-place reinterpretation exists):

1. Any reorder, width change, or removal of an existing field; any change to
   `PvCard` / `PvPower` / `PvMonster` layout.
2. Any capacity change (`kPowerCap`, `kHandCap`, `kDrawCap`, `kDiscardCap`,
   `kExhaustCap`, `kLimboCap`, `kMonsterCap`, `kPotionCap`) — array strides
   shift every downstream offset.
3. Changing the draw pile's canonical sort key — the key is part of the
   schema (two records are comparable only under one sort).
4. Changing a sentinel or suppression convention (intent 0 under Runic Dome,
   `NONE == 0` empty slots, `occupied` semantics).
5. Re-classifying a carried byte — in particular, masking a flags bit that an
   engine change turned hidden (the §3/§4 rule exists so this is caught at
   review, not after shards shipped).
6. Widening the combat section's validity (e.g. encoding it during
   COMBAT_REWARD): existing zero bytes acquire meaning retroactively.

**Version log:**

- v1 — T0.1: combat block + v1 skeleton (the initial layout, 3760 bytes).
- v2 — T0.2: run always-block, per-phase screen sections, the RunActionMask
  channel and the KnowledgeState projection. **ADDITIVE.** Classified against
  the two rules it uses:
  - *Case 2 (tail append).* Every new field sits after the v1 `pad_tail`, so
    every v1 offset holds — `PublicViewLayout.V2TailHasNoImplicitPadding`
    asserts `offsetof(PublicView, gold) == 3760` precisely so that a future
    edit that would break this stops being silent. `sizeof` grows 3760 → 6032.
    The declared "not present" value of every appended field is **zero**, and
    it is a truthful reading of a v1 record in each case: a zero `*.active`
    byte means "that screen section is absent", a zero
    `draw_constraint_rank[i]` means "no order constraint known", a zero
    `chest_size` means "no chest", a zero `current_encounter_id` means "not in
    a list-drawn combat room", and a zeroed `action_mask` means "mask not
    carried" (v1 readers never had one). Reserved fields keep the meanings
    already declared for them.
  - *Case 1 (populating a reserved field).* `keys_reserved` and `act_reserved`
    now carry `RunState.keys` and `RunState.act`. Both reinterpretations of a
    v1 record are exact, per the existing case-1 text.
  - Two decisions recorded here because a later reader will otherwise re-litigate
    them: the **master deck is carried in engine order** (its order is public
    and it is the mask's index space — see §6), and the **mask is embedded
    rather than parallel** (§9), which makes `sizeof(RunActionMask)` part of
    this schema. That is deliberate: a mask that grows is a public-view change
    and gets reviewed like one, which is why public_view.hpp pins the total
    size to a literal as well as to a formula.
- v3 — S2.13: `event_flags_hi`, the one-shot FIRED bitset's second word
  (event ids 32..63 at bit id-33). **ADDITIVE, case 2 (tail append).**
  - *Why a second word rather than a wider `event_flags`.* S2.02 allocated
    ids 32..51 for the Act-2/3 events and S2.13 made them drawable, so the
    `uint32_t` ran out at id 31. Widening it in place is a **breaking**
    change under the rule above — it changes an existing field's width, and
    it would move `shop_flags` and everything after it — so the second word
    is the only additive route. `RunState` did the same carve on its side,
    out of declared padding, so the engine `SCHEMA_VERSION` did not move
    either.
  - *Why it sits AFTER `action_mask`.* Because the mask channel is a member,
    the "tail" of this struct is past it. Putting the new word next to
    `event_flags` would have shifted every field from `shop_flags` to the end
    of the mask — not a tail append at all. The two words are one field
    semantically and adjacent nowhere in memory; the encoder assigns them
    together and the differ compares them together.
    `PublicViewLayout.V2TailHasNoImplicitPadding` gained a row for it, and a
    new `static_assert` pins `offsetof(PublicView, action_mask) ==
    kPublicViewFixedBytes` so a future append cannot quietly move the mask.
    `sizeof` grows 6032 → 6036; `PvMask` is 4-sized and 4-aligned, so the
    word abuts it with no padding.
- v4 — S2.2F: `kMonsterCap` 7 → 23 (engine `SCHEMA_VERSION` 6 → 7).
  **BREAKING, case 2 (capacity change).** The first breaking bump this view has
  taken, and the enumerated case for it already existed — "any capacity change
  … array strides shift every downstream offset", with `kMonsterCap` named.
  - *What moves.* Three things inside this record are sized by the cap:
    `monsters[kMonsterCap]` (16 more `PvMonster`, +2624 B), the
    `monster_roll_known` / `monster_roll` pair (+32 B), and — inside the
    embedded mask channel — `RunActionMask`'s per-(card, monster) and
    per-(potion, monster) target grids (`sizeof(PvMask)` 376 → 616). The
    monster block sits in the MIDDLE of the record, so `kPublicViewFixedBytes`
    goes 5656 → 8312 and `sizeof(PublicView)` 6036 → 8932. All measured, not
    derived.
  - *Why no in-place reinterpretation exists.* A v3 record's `gold` sits at
    3760 and a v4 record's at 6384; every field after the monster block is at a
    different offset, and the mask channel — which the v2 note deliberately
    made part of this schema, precisely so a growing mask would be reviewed —
    changed size too. Per plan §4.1 the correct response is reanalyze-or-
    quarantine, not a migration rule. `PublicViewLayout.V2TailHasNoImplicitPadding`
    keeps its assert with the new boundary, so it still catches an
    *accidental* move.
  - *Why the engine needed it.* Three Act-2/3 mid-combat spawners (Gremlin
    Leader, The Collector, Reptomancer) grow their record count without bound
    as a fight lasts, and the game never removes a dead record. 23 is the
    largest cap the 8192 B `CombatState` ceiling admits — 24 measures 8304.
    The full table and the rejected alternatives are in `combat_state.hpp`.
  - *Nothing was re-classified.* `MonsterState.draw_x` (new, in what was
    `pad1`) is DERIVED — a per-encounter constant fixed by the monster's
    identity and spawn slot, both public — so it is recoverable and gets no
    `PvMonster` field. `CombatState.pending_obtain` (new, in what was
    `pad_relics`) is PUBLIC but is not separately encoded: the run layer drains
    it into `master_deck`, which IS encoded, within the same pump step, so no
    observable `PublicView` is ever taken while it is non-zero. Both carry
    `byte_class.hpp` rows saying so.
  - *Declared "not present" value:* **zero**, and it is a truthful reading of
    a v2 record — v2 predates ids 32..51 being drawable at all, so no v2
    record could have had a fire to report there.
  - `tests/golden/twin_fixtures/twins_v1.bin` was regenerated with its
    checked-in generator: the fixture stamps both `PUBLIC_VIEW_VERSION` and
    `sizeof(PublicView)`, and refuses on mismatch by design.
- v5 — S2.28: `second_boss_reserved` populated. **ADDITIVE, case 1 (populating
  a reserved field).** The field carries the `EncounterDef` id of the SECOND
  Act-3 boss of an A20 double-boss run (`boss_list[1]`), from the moment the
  double-boss transition puts the player in the second boss room
  (`act == kFinalAct && boss_cursor >= 1`), and 0 in every other state.
  - *The v4-record reinterpretation is exact*: no v4 engine could take the
    double-boss transition at all (the Act-3 bosses and the transition both
    land in v5), so every stored v4 record truthfully reads "no second boss
    revealed" — the same zero the field has carried since v1.
  - *Why it is not a leak*: the field is written only once the second boss's
    monsters are on screen. Before that moment the identity is a hidden
    realization the belief sampler permutes (`condition_boss_list`'s `keep`
    argument, the same cursor-plus-one prefix rule as the monster/elite
    lists), and the hidden-twin gate compares this field — an early populate
    FAILS the gate rather than leaking.
  - No layout change of any kind: no field moved, no size changed,
    `sizeof(PublicView)` and every offset are byte-identical to v4.
    `twins_v1.bin` was regenerated for the version stamp alone (the loader
    refuses a stamp mismatch by design, which is the tripwire working).
- v6 — S2.32: `kEventOptionCap` and `kEventBoardCap` 12 → 20, for The
  Library's twenty-card read board (TheLibrary.java:66-91: one option per
  rolled card, pick exactly one). **BREAKING — the v4 shape, not a tail
  append.** `PvEvent.board` sits mid-record, so every offset after the event
  section moves (`kPublicViewFixedBytes` 8312 → 8360), and
  `RunActionMask.can_choose_event_option` is embedded in the mask channel, so
  `sizeof(PvMask)` grows 616 → 624 and `sizeof(PublicView)` 8932 → 8988. A v5
  record cannot be reinterpreted as v6; consumers re-encode.
  - *Board classification for the new user*: The Library deals its twenty
    cards FACE UP, so every non-empty slot is `revealed` (the opposite
    reading from Match and Keep's face-down deal; both live in
    `encode_event`, and the `byte_class.hpp` board row now names the pair).
    Match and Keep's own loops moved to `kMatchBoardSize` (12), so the cap's
    spare slots can never enter its board or its resampler permutation.
  - New scratch classifications for the ten S2.32 bodies live in
    `event_scratch_public_mask` (public_view.cpp): nine print their scratch
    on the dialog (the 0x0F group); The Joust's `ownerWins` is the second
    Dead-Adventurer-shaped hidden realization and stays masked (0x01 — the
    bet itself is the player's own displayed choice).
  - `twins_v1.bin` regenerated with its checked-in generator, as at v4/v5.
