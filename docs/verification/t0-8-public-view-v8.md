# T0.8 — PublicView v8: publish the on-screen offers a public-information policy could not see

Evidence for the ledger block in [../training-tasks.md](../training-tasks.md).
Acceptance is stated and run under the 2026-09-03 owner directive
([../conventions.md](../conventions.md) §1): **builds + a real-run witness +
the committed oracle corpus**, never a unit test.

Base: engine `master` `949fce2`, branch `pv8`.

---

## 1. The gap, re-verified against the encoder before the design

The training repo's full-run actor (T3.6,
`SpireTrainer/docs/verification/t3-6-full-run-actor.md` §10.1–10.2) reported
that `PvMask` exposes legality bits whose on-screen content `PublicView` does
not publish. Each claim was checked against `encode_screens`
(`src/engine/public_view.cpp`) and `legal_actions`
(`src/engine/run_advance.cpp`) rather than taken on report:

| # | Claim | Verdict | Where |
|---|---|---|---|
| 1 | Neow's `CARD_REWARD`: `can_take_card[j]` legal, offer not published | **REAL** | mask: `run_advance.cpp:2714`. Encoder gate is `COMBAT_REWARD \|\| (NEOW && neow.screen == ITEM_REWARD)`; `CARD_REWARD` is a different `NeowScreen`. |
| 2 | Dream Catcher's rest-site pick: same | **REAL** | mask: `run_advance.cpp:3004` under `RestScreen::DREAM_CATCHER`. `REST_SITE` published only `rest_screen`. |
| 3 | Boss chest's claim rows legal outside `rewards.active` | **REAL, and narrower than reported** | mask: `run_advance.cpp:2839/2849` under `BossChestScreen::EQUIP_ITEM_REWARD` (Tiny House / Calling Bell). The chest's OTHER claim screen, `RELIC_SELECT` (`run_advance.cpp:2804`), is **already covered** — S2.47 publishes its three offers through `boss_relic_choice_reserved` under the `seen` gate — so v8 covers only `EQUIP_ITEM_REWARD`. |
| 4 | `can_choose_rest[i]` is index-only, no per-option kind published | **REAL** | mask: `run_advance.cpp:2979` from `build_rest_menu`. No `PublicView` field carried `RestOptionEntry.kind`. |

Nothing new is read from hidden state. All three reward surfaces project
`rc.rewards`, a **pure copy** in `resample_hidden`
(`src/engine/resample.cpp`'s "rows that are PURE COPIES" list), and the
campfire block projects `build_rest_menu(rc.run)`, a pure function of public
`RunState`. That is what makes the hidden-twin comparison in §3 able to hold
the publication honest instead of merely unreviewed.

## 2. What landed

`PUBLIC_VIEW_VERSION` **7 → 8**, **ADDITIVE** (schema-evolution case 2, tail
append). Every field is appended after `pad_v7`, i.e. past the mask channel
exactly as v3's and v7's own fields were; no v7 offset moves.

| Field | Meaning | Gate |
|---|---|---|
| `claim_rows[8]` (`PvRewardItem`), `claim_row_count`, `claim_rows_active`, `claim_rows_source` | the boss chest's equip item-reward rows | `BOSS_TREASURE && boss_chest.screen == EQUIP_ITEM_REWARD && open_card_item == kNoOpenCardReward` |
| `card_offer_ids[4]`, `card_offer_upgrades[4]`, `card_offer_count`, `card_offer_item`, `card_offer_active`, `card_offer_source` | the OPEN card pick, at the three screens `rewards` does not gate | `reward_card_item_open_legal(rc.rewards)` under Neow `CARD_REWARD` / rest `DREAM_CATCHER` / boss-chest `EQUIP_ITEM_REWARD` |
| `rest_option_kind[43]`, `rest_option_count` | `PvRestOptionKind` per campfire button, same index space as `can_choose_rest[]` | `phase == REST_SITE` |
| `pad_v8[1]` | explicit padding, always zero | — |

`sizeof(PublicView)` **8992 → 9248** (+256); `kPublicViewFixedBytes`,
`offsetof(action_mask)` and `offsetof(event_flags_hi)` are unchanged, and are
now each pinned by their own `static_assert`.

The kind alphabet is `RestOptionKind + 1` because `RestOptionKind::REST` is 0
and zero must remain the appended field's declared not-present value.

**No `byte_class.hpp` row was added, and none was needed**: v8 adds no
`RunController` byte. `rewards` and `rest` were already classified `public`;
both rows' notes are extended to record how much of them the encoder now
publishes. The table's `static_assert` that it tiles `sizeof(RunController)`
exactly still holds — it is checked at build time, so §4's builds are its
evidence.

Documentation landed in the same change: the header's version log, this repo's
[public-view-audit.md](../public-view-audit.md) (§8.1 and §8.2 field tables
plus the v8 version-log entry), and
[training-contract.md](../training-contract.md) §1 and §2.

## 3. Real-run leak witness

`tools/twin_fixtures/src/pv8_leak_probe.cpp` — a standalone program, **not** a
gtest. It exists because the GT0 leak gates (`tests/twin_test.cpp`,
`tests/tripwire_test.cpp`) are exactly the suites the 2026-09-03 directive
stopped writing, extending and running, so the property they assert had to be
re-established by something that *is* run. It is built by
`--target pv8_leak_probe` and run in the foreground.

It drives **20,000 A20 runs** (five scripted policies × 4,000 seeds through
`fuzz::run_case`'s pass A) and at **every decision** compares
`encode_public_view(state)` with `encode_public_view(make_hidden_twin(state,
rng))` byte for byte, reporting the first differing member via
`public_view_first_difference`.

### win-release (clang-cl, LTO)

```
PUBLIC_VIEW_VERSION  8
sizeof(PublicView)   9248
A20 runs             20000 (5 policies x 4000 seeds)
states compared      1811843 (1811803 swept + 40 directed)
view digest          0xd5a79b0a6c6077ca

states per phase:
  NEOW                   54237
  MAP_CHOICE             121136
  COMBAT                 1249845
  COMBAT_REWARD          257202
  RUN_OVER               39978
  REST_SITE              17446
  TREASURE_ROOM          3663
  EVENT_DIALOG           51319
  SHOP                   16918
  BOSS_TREASURE/other    99

v8 field witnesses (each must be > 0):
  card_offer (Neow CARD_REWARD)                3216
  card_offer (Dream Catcher)                   9
  card_offer (boss chest EQUIP_ITEM_REWARD)    9
  claim_rows (boss chest EQUIP_ITEM_REWARD)    18
  rest_option_kind (campfire menu)             17446
  campfire kinds published (PvRestOptionKind bitset) 0x7e

twin byte differences 0
PROBE GREEN
```

Wall clock 17.9 s. A second run of the same binary printed the **same digest**,
so the probe is reproducible rather than merely asserted.

### WSL release (GCC 13, LTO)

Identical output, **identical digest `0xd5a79b0a6c6077ca`** — 1,811,843 states,
zero differences. The two hosts agree byte for byte, which is the same
cross-compiler property the 20 combat fixtures carry.

### win-debug (clang-cl, asserts live)

`pv8_leak_probe.exe 200` (1,000 runs): 90,395 states, **zero** differences,
every witness > 0.

### Two failures, not one

The probe reports both, because they are different bugs:

1. **A byte difference** = a v8 field is a function of hidden state. Zero, on
   1.8 M states.
2. **An unwitnessed field** = a field nobody populates, which passes a twin
   comparison for free. The audit doc names this exact hole ("a field this
   table forgets is twin-invariant"), so each new field must be seen non-zero
   or the probe exits 1.

`0x7e` is bits 1–6, i.e. **all six** `PvRestOptionKind` values — REST, SMITH,
LIFT, TOKE, DIG and RECALL — were each published somewhere in the sweep.

### The directed cohort, and why it exists

Two of the four surfaces are unreachable by any scripted policy in this tree,
which `gen_twin_fixtures.cpp` already records for `RunPhase::BOSS_TREASURE` as
a whole: reaching the boss chest needs the Act-1 boss **killed** at A20, and
the equip screen additionally needs Tiny House or Calling Bell picked out of
three offers. Dream Catcher's pick needs the relic *and* a slept rest site.
Both are therefore built by directed construction — the same resolution
`tests/boss_chest_test.cpp`'s `at_boss_chest` uses, and the same standalone
probe precedent as
[s3-62-terminal-clear-repair.md](s3-62-terminal-clear-repair.md):

* the boss chest is reached through the public `next_room_transition_boss_chest`
  edge, offer 0 is overwritten with the relic under test, the chest is opened
  and the offer picked (Calling Bell's confirmation grid is stepped through),
  and the resulting claim screen and its card row are both probed — three
  seeds × two relics;
* the Dream Catcher states are built on **eight real rest-site controllers
  harvested from the sweep itself**, spread across it, so eight different
  master decks roll eight different offers.

The twin comparison run on a directed state is the same one the sweep runs, and
the report separates the two counts (1,811,803 swept + 40 directed) so neither
is mistaken for the other.

## 4. Builds

| Preset | Result |
|---|---|
| `tools/win_build.cmd win-release` (`sts_engine replay_run_diff pv8_leak_probe gen_twin_fixtures`) | clean |
| `tools/win_build.cmd win-debug` (`sts_engine replay_run_diff pv8_leak_probe`) | clean |
| `tools/wsl_run.sh --script tools/build_presets.sh release` (GCC 13, whole `all` target incl. every test binary) | `PRESETS BUILT: release` |

The WSL build is the wider one — it compiles every test target — and it is what
caught the two stale `static_assert(sizeof(PublicView) == 8992)` lines in
`tests/public_view_test.cpp`. Those were updated to 9248 and the layout-walk
list gained its v8 rows; `tests/twin_test.cpp`'s last-member expectation moved
`pad_v7` → `pad_v8`. That is **build hygiene, not test maintenance**: under the
owner directive these suites are not run as acceptance, but the tree must still
compile, and a number that has gone stale should not be left asserting a lie.
The real layout guard is now seven `static_assert`s in `public_view.hpp`, which
every preset checks.

`tests/golden/twin_fixtures/twins_v1.bin` was regenerated with its checked-in
generator (`gen_twin_fixtures`, 18 cases over 9 phases), as at v4/v5/v6/v7 —
the container stamps `PUBLIC_VIEW_VERSION` and `sizeof(PublicView)` and refuses
a mismatch by design.

## 5. Oracle corpus

`tools/corpus_replay.sh` — all three committed archives, three comparison modes,
each with its injected-divergence negative control.

| Host | `--replay` | `--costs` | `--masks` | controls |
|---|---|---|---|---|
| `win-release` | 3/3 ZERO-DIFF | 3/3 ZERO-DIFF | 3/3 ZERO-DIFF | 9/9 fail loud |
| WSL `release` | 3/3 ZERO-DIFF | 3/3 ZERO-DIFF | 3/3 ZERO-DIFF | 9/9 fail loud |

Archives: `act1_a20_50`, `three_act_a20_5`, `keys_a20_4`.

`--masks` compares **mask** bytes, and the mask did not change: v8 adds no
`RunActionMask` field and spends no new action-space index. Every v8 field is a
projection of state the mask already made legal, which is why a zero-diff
`--masks` run is exactly the expected result and not a weak one.

## 6. Hygiene

`git diff --check`, `tools/check_doc_links.sh` and `tools/check_stale_counts.sh`
— all clean (run from Git Bash on Windows).

## 7. Consumer consequence

The trainer must **bump its engine pin and step its own encoding version
(v3 → v4)** to consume these fields. Additive means a v7-stamped shard stays
readable under the declared migration rule — it does not mean a v7-era encoder
can see values that were not in the record. Shards written before the bump
simply do not contain the Neow / Dream Catcher / boss-chest / campfire values,
and a policy trained on them keeps acting by index at those screens.
