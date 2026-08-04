# PublicView field audit — v1 (T0.1)

The field-by-field completeness proof for `encode_public_view`
([../include/sts/engine/public_view.hpp](../include/sts/engine/public_view.hpp),
[../src/engine/public_view.cpp](../src/engine/public_view.cpp)), against the
information contract of [training-plan.md](training-plan.md) §1/§2.1: public =
derivable by a perfect-memory player from the revealed action–observation
history; the boundary hides RNG *realizations*, never rules.

**How to read this file.** Every `CombatState` and `RunController` member has a
row. The **Class** column is the information classification the T0.5
total-byte tripwire consumes; the **v1** column says what T0.1's encoder does
with it. T0.2 extends this table by filling the `T0.2` rows (run-phase screen
structs, the always-block, the mask channel, the KnowledgeState projection) —
the rows are already present so T0.2 edits statuses, it does not restructure.

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

**v1 status vocabulary:** `→ field` (encoded now), `T0.2` (owed by T0.2's
extension), `reserved` (a v1 zero-filled reserved field exists for it),
`excluded` (deliberately not carried — the Notes say why).

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
| `monsters[7]` | see §2 | → `monsters[7]` | Per-field classification in §2 (MonsterState). |
| `action_queue[64]` + `action_head/tail/count` + `pad_actionq` | derived | excluded | Resolution transients. At every decision boundary their contents are a deterministic function of the observed public action history (the pump is deterministic given public plays); carrying them adds no information. The pending-screen context a consumer does need arrives through the mask channel (T0.2). |
| `pre_turn_actions[16]` + `pre_turn_head/tail/count` | derived | excluded | Same as above; may be non-empty at WAITING_ON_USER (queued start-of-next-turn hooks), but every entry is the consequence of an observed play/trigger. |
| `turn_has_ended` | derived | excluded | Pump bookkeeping; deterministic at every boundary. |
| `card_queue[16]` + `card_queue_count` + `pad_cardq` | derived | excluded | Pending plays the player queued; deterministic from public history. |
| `monster_queue[5]` + `monster_queue_count` + `monster_attacks_queued` | derived | excluded | Turn-order bookkeeping; deterministic from public state. |
| `relics[40]` + `relic_count` | public | T0.2 | The combat relic mirror. Relics + displayed counters are the plan §2.1 always-block, owned by T0.2 — **T0.2 note:** while `combat_active`, encode from THIS live mirror, not `RunState.relics` (in-combat counter ticks, e.g. Kunai, live here until fold-back). |
| `pad_relics[7]` | padding | excluded | |
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
| `cur_x` | public | T0.2 | Own map position. |
| `room_type` | public | T0.2 | The entered room's kind. |
| `combat_outcome` | public | T0.2 | How the last combat ended — observed. |
| `lists` (MonsterLists) | consumed prefix public / **suffix hidden** | T0.2 (prefix) | The consumed encounter prefix is plan §1 tracker state and encodes in T0.2; the unconsumed suffix is a realization — excluded, resampled by T0.4's Markov continuation. `boss_list[0]` is public from the map. |
| `monster_cursor` / `elite_cursor` / `boss_cursor` | public | T0.2 | The consumed-prefix lengths. |
| `pad1` | padding | excluded | |
| `emerald_x` / `emerald_y` | public | T0.2 | The burning-elite node is drawn on the map. |
| `pad_emerald[2]` | padding | excluded | |
| `rewards` (RewardScreen) | public (post-assembly) | T0.2 | Direct projection of the open reward screen. Assembled contents are revealed when the screen opens. |
| `rest` (RestSiteState) | public | T0.2 | Screen projection. |
| `shop` (ShopState) | public | T0.2 | Current-visit stock is public once the floor is entered (plan §2.4 table). |
| `treasure_chest` (TreasureChest) | **contents hidden pre-open** / size public | T0.2 | The one masking trap named by plan §2.1: contents roll at construction; pre-open the player sees chest size only. T0.2 masks until the open action. |
| `event` (EventDialogState) | public | T0.2 | Open dialog projection; Match & Keep hidden board state is a T0.4 sampler row. |
| `neow` (NeowState) | public | T0.2 | The rolled options are displayed. |
| `pending_bottle` | public | T0.2 | The modal overlay is on screen. |
| `pad2[3]` | padding | excluded | |

## 6. RunState → PublicView

| RunState member | Class | v1 | Notes |
|---|---|---|---|
| `kSchemaVersion` | — | excluded | Compile-time constant. |
| `run_seed` | **hidden** | excluded | Explicitly hidden by plan §2.6b — knowing it is seed-cracking. |
| `master_deck[128]` + `master_deck_count` | public | T0.2 | Always-block: master deck as multiset. |
| `hp` / `max_hp` | public | T0.2 | Always-block. |
| `pad_gold_align` | padding | excluded | |
| `gold` | public | T0.2 | Always-block. |
| `ascension` | public | T0.2 | Always-block. |
| `act` | public | reserved (`act_reserved`) | Constant 1 in S1; the reserved field is populated when S2 lands (additive — see the note below). |
| `floor` | public | T0.2 | Always-block. |
| `relics[40]` + `relic_count` | public | T0.2 | Always-block: relics with displayed counters (see the §1 mirror note for in-combat reads). |
| `pad_relic` | padding | excluded | |
| `potions[5]` | public | → `potions[5]` | The belt, public in every phase. |
| `map[105]` | public | T0.2 | Always-block: full current-act map incl. the emerald node. |
| `boss_ids[4]` | public | T0.2 | The act boss's identity is displayed on the map screen. |
| `keys` | public | reserved (`keys_reserved`) | Exists today (kKey* bits); zero through S1/S2-pre-keys. Population is additive. |
| `pad_keys` | padding | excluded | |
| `event_flags` / `shop_flags` | public | T0.2 | One-shot *fired* bitsets — the fires were observed. |
| `card_blizz_randomizer` / `blizzard_potion_mod` | public | T0.2 | Plan §1: pity counters are tracker state, encoded verbatim. |
| `event_pity_monster` / `event_pity_shop` / `event_pity_treasure` | public | T0.2 | Same. |
| `purge_cost` | public | T0.2 | Displayed in every shop. |
| `potion_slots` | public | → `potion_slot_count` | |
| `pad_potion_slots` | padding | excluded | |
| `event_membership` / `special_membership` / `shrine_membership` | public | T0.2 | Remaining-pool membership: initial lists are public rules, removals were observed. |
| `pad_membership` | padding | excluded | |
| `relic_pools[5][48]` | **hidden** (unrevealed window) | excluded | The remaining order/composition of each tier's `[0, count)` window is a shuffle realization. Resampled by T0.4 (with the pop-time canSpawn corner cases). Observed pops are public via `relics[]`. |
| `relic_pool_count[5]` | derived | excluded | Initial tier size (public rule) minus observed pops; carrying it would add nothing and T0.2 may still choose to surface it as a convenience — that would be an additive change. |
| `pad_relic_pools[3]` / `pad_rng_align[6]` | padding | excluded | |
| `monster_rng` … `neow_rng` (9 streams) | hidden | excluded | Run-/act-/event-scoped stream states are realizations. Resampled fresh by T0.4. |

## 7. PublicView-only fields (no CombatState/RunController source)

| PublicView member | Source | Notes |
|---|---|---|
| `public_view_version` | constant | `PUBLIC_VIEW_VERSION` at encode time. |
| `combat_active` | derived | `rc.phase == COMBAT`. |
| `monsters[i].occupied` | derived | `i < monster_count`. |
| `boss_relic_choice_reserved[3]` | reserved | S2 boss-chest relic screen (zero in v1). |
| `second_boss_reserved` | reserved | S2 A20 double-boss slot (zero in v1). |
| `pad_tail[3]` | padding | Always zero. |

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

- v1 — T0.1: combat block + v1 skeleton (this initial layout, 3760 bytes).
