# The training contract — what the simulator publishes across the repo boundary

**Audience:** the training repo (task T1.1 onwards). This is the
consumer-facing surface of the Phase T0 information layer, certified by gate
**GT0** (tag `gt0-info-layer`,
[verification/gt0-info-layer.md](verification/gt0-info-layer.md)).

**This file cites; it does not restate.** Every mechanism below has a landed
home — a header comment, an audit row, a test name — and that home stays
authoritative. What this file adds is the *boundary* view: which of those
things a consumer may depend on, what each one promises, and what breaks the
promise. Where a number or a rule appears here it is a pointer with just
enough context to know whether you need to follow it.

The governing spec is [training-plan.md](training-plan.md) §2 (the
information layer) and §2.7 (the semantics/tensors repo boundary). The rule
that §2.7 draws, restated once because everything here follows from it:

> **The simulator owns the *view*, not the tensor encoder.** `PublicView`,
> `KnowledgeState`, the observability transforms, `resample_hidden`,
> `public_hash` and the leak/twin/sampler suites live here, because
> leak-freedom is an engine correctness property tested against engine
> internals. Dtypes, normalization, token layouts, embeddings, networks and
> search live in the training repo, because they churn weekly and must not
> pay this repo's verification ceremony.

---

## 1. `PUBLIC_VIEW_VERSION` and the stamps you must record

`PUBLIC_VIEW_VERSION` is **6**
([../include/sts/engine/public_view.hpp](../include/sts/engine/public_view.hpp)).
(It read **2** here until S2.2F: S2.13's v3 bump did not update this line. The
number lives in the header; this file quotes it, and a quoted number goes stale
exactly the way conventions §8 describes. v4 is the first BREAKING bump — see
the audit's version log — so shards and checkpoints stamped v1-v3 are
reanalyze-or-quarantine, not forward-readable. v5 — S2.28's
`second_boss_reserved` populate — is ADDITIVE over v4: no offset moved, and a
v4 record's zero there reads truthfully as "no second boss revealed". v6 —
S2.32's `kEventOptionCap`/`kEventBoardCap` 12 → 20 for The Library's
twenty-card board — is BREAKING again, the v4 shape: `PvEvent.board` sits
mid-record and `can_choose_event_option` is embedded in the mask channel, so
offsets after each move and `sizeof(PublicView)` goes 8932 → 8988; v5 shards
are reanalyze-or-quarantine under v6.)
It is a real field of every `PublicView` instance (`public_view_version`), not
just a compile-time constant, so a stored record carries its own schema
identity and a loader can refuse without out-of-band metadata.

It is **independent of the engine `SCHEMA_VERSION`** on purpose: an engine
layout change that does not alter what is public must not invalidate training
shards, and a view change must invalidate them even when the engine did not
move. Record both, plus the registry manifest hash and the sim commit, in
every shard and checkpoint header (that plumbing is T1.1's version-stamp
deliverable).

**Which changes are additive and which are breaking is a landed, enumerated
list** — see the *Schema-evolution note* at the bottom of
[public-view-audit.md](public-view-audit.md). Consumers are refuse-on-mismatch
by default (plan T1.2); "additive" is what makes a *declared migration rule*
possible, not something a loader may assume on its own.

## 2. `PublicView` — layout summary and how to read the audit table

`PublicView` is a trivially copyable POD produced by a **pull** call beside
`legal_actions()`:

```c++
void encode_public_view(const RunController& rc, PublicView& out) noexcept;
```

It is deliberately not part of `StepResult` — `StepResult` embeds an
`ObsBuffer` by value on the hot path, and this view is ~6 KB. `encode_public_view`
also runs a full `legal_actions()` internally to fill the embedded mask
(§4), so hot-path callers that only want legality keep calling
`legal_actions()` directly.

The header groups the struct into these sections, in layout order:

| Group | Contents |
|---|---|
| header | `public_view_version`, phase/validity scalars |
| combat: player | header/player scalars, then the **full** player power list |
| combat: piles | hand / **draw (canonically sorted multiset)** / discard / exhaust / limbo, as card values |
| combat: monsters | per-monster block, HP, intent, and full 24-slot power lists (the old `ObsBuffer` stub truncated at 4) |
| belt | potions — public in every phase, combat or not |
| reserved (v1) | `keys_reserved`, `act_reserved` (both **populated** since v2), `second_boss_reserved` (**populated** since v5: the A20 second Act-3 boss, 0 elsewhere), `boss_relic_choice_reserved` (**populated** since S2.11/S2.47: the three offered boss-relic ids while `phase == BOSS_TREASURE`, and only once the chest has been opened — zero before that and outside the phase) |
| *v2 tail append →* | everything below was appended; no v1 field moved |
| always-block | hp/max-hp/gold/floor/ascension, screen-flow scalars, `current_encounter_id`, masked chest fields, pity/membership counters |
| always-block: collections | master deck (**engine order** — see below), relics + displayed counters, the full current-act map incl. the emerald node, consumed encounter prefix |
| knowledge projection | `draw_constraint_rank[]` / `draw_exact_pos[]` + chain scalars (§5) |
| per-phase screens | reward / shop / event / Neow / rest / treasure, each behind its own `active` gate |
| mask channel | `PvMask` — always live (§4) |

Two layout decisions a reader will otherwise re-litigate, both recorded in the
audit's version log:

- **The draw pile is a canonically sorted multiset** — ascending
  `(card_id, upgrade, cost_now, flags)`. Its arrangement is a shuffle
  realization; sorting is what removes it. The sort key is therefore *part of
  the schema* — changing it is a breaking change, because two records are
  comparable only under one sort. (`hand`, `discard`, `exhaust` and `limbo`
  stay in engine order: those orders are observed on screen.)
- **The master deck is carried in engine order.** Its order is public, and it
  is the index space the mask's `can_choose_master_deck[]` addresses; sorting
  it would desynchronize the action space from the observation.

### How to read [public-view-audit.md](public-view-audit.md)

The audit is the *completeness proof* for the encoder, and the reason it
exists is worth internalizing before you rely on any field:

> A field the audit **forgets** is twin-invariant — the twin suite (§6)
> compares two states that both lack it, so it passes. Only the table catches
> an omission. The converse failure — carrying something hidden — *is* caught
> by the twin suite.

Every `CombatState` / `RunController` member has a row, as does every member
of every transient struct (§8), of the mask channel (§9) and of
`KnowledgeState` (§10). Two columns matter to a consumer:

- **Class** — `public` · `public-cond` (public except under a declared
  observability transform; today only Runic Dome intent hiding) · `derived` ·
  `hidden` · `padding`. Since T0.5 this column has an **executable twin** in
  [`byte_class.hpp`](../include/sts/engine/byte_class.hpp), whose rows must
  tile `sizeof(RunController)` exactly (§6).
- **v1** — `→ field` (encoded), `reserved`, or `excluded` (with the reason in
  Notes).

## 3. `public_hash` — definition and its soundness precondition

```c++
uint64_t public_hash(const PublicView& view) noexcept;   // XXH3-64 over sizeof(view) bytes
uint64_t public_hash(const RunController& rc) noexcept;  // encode, then hash
```

It is the identity of a **public information set**: two states a
perfect-memory player could not tell apart hash equal; any difference the
player *can* see changes the hash. That is what makes it a legitimate key for
search statistics.

**The soundness precondition is that the byte hash is meaningful at all**, and
it rests on two properties that are held by test, not by convention (the full
argument is on the declaration in `public_view.hpp`):

1. **No implicit padding.** Every gap is a declared pad member (`pad0`,
   `pad_tail`, `PvMask::pad_end`), and the layout-walk tests in
   `tests/public_view_test.cpp` require each member to start where the
   previous one ended — for the struct and for every element type it contains.
2. **`encode_public_view` assigns every byte.** Its first statement is
   `out = PublicView{}`; with no implicit padding, "every member" is "every
   byte". `PublicHash.EncodingTwiceIntoDirtyBuffersHashesEqual` pins that a
   view encoded into a dirty buffer hashes the same as into a fresh one.

If you ever add a field to the view without a declared pad, both properties
break silently and every stored hash becomes host-dependent — the same trap
`conventions.md` §8 records for `RunState`.

**What it is not:** it is *not* stable across `PUBLIC_VIEW_VERSION` bumps (the
stamp is itself a hashed field), and it is *not* a substitute for
`hash_state()` — it deliberately cannot distinguish states that differ only in
hidden realizations. That is the point.

## 4. The mask is an observation channel

Plan §2.1: a legality bit computed from hidden state is a leak that an
observation-equality test cannot catch. So `RunActionMask` is a **member** of
`PublicView` (`action_mask`, a `PvMask`), not a parallel object a consumer
might hash and forget. Hashing `sizeof(PublicView)` hashes the legality
channel too, structurally.

Consequences for the training repo:

- Feed the mask from the view you already hashed. Do not re-derive it beside
  the view and assume they agree.
- `sizeof(RunActionMask)` is part of this schema: a mask that grows is a
  public-view change and gets reviewed as one.
- Mask bits are classified `derived` wholesale, on purpose — whether each bit
  is truly derivable from public state is a *tested* property (the
  twin-invariant-mask requirement), not a reviewed one.

### 4a. The known mask leak — read this before building an encoder

There is **one recorded, open leak**, fully written up as
[public-view-audit.md](public-view-audit.md) **§9a**. In one line:
`ActionMask.can_choose[i]` for a DRAW-sourced choice (Secret Technique /
Secret Weapon) is computed per draw-pile *array slot*, so it reports the card
types of the first `kHandCap` draw slots — and draw order is a shuffle
realization. The real game randomizes that browse order, so this is our
slot-indexed action space leaking, not the rule.

Its status is pinned by three things you should not "clean up":

- `make_hidden_twin` **pins the draw pile** whenever such a screen is open, so
  the leak gate is green on a defect it has recorded rather than red on one it
  cannot fix;
- the canary test
  **`TwinDrawChoiceLeak.MaskReadsRawDrawSlotsWhileADrawSourcedChoiceIsOpen`**
  (`tests/twin_test.cpp`) asserts the leak **still exists** — it turns red the
  day the action space is repaired, which is the day the pin and the test are
  both deleted;
- `resample_hidden` is deliberately **not** pinned; it must keep sampling the
  true belief.

For the training repo this means: a probe (T1.6) that recovers draw-slot types
while a draw-sourced choice is open is finding *this*, not a new defect.

## 5. `KnowledgeState` projection semantics

`KnowledgeState` ([`knowledge.hpp`](../include/sts/engine/knowledge.hpp)) is a
record **of public reveals**, maintained inside the engine at
placement/reveal/shuffle sites — only the engine knows when a reveal happens.
Nothing in it is hidden, but it cannot be carried raw (its chain holds
`CardPoolIndex` values, which are engine bookkeeping, not information). It is
**projected**, and audit §10 has the row-by-row account.

What reaches the view, and how to read it:

- Order knowledge is expressed as **two annotations parallel to the sorted
  `draw[]`** — because the pile is a sorted multiset, unsorting it to express
  order would put the hidden arrangement straight back into the bytes.
  - `draw_constraint_rank[i]` — `0` = unconstrained; otherwise the 1-based
    position of that card in the known **relative-order** chain. Two annotated
    slots are known to sit in rank order; **nothing is claimed about the gaps
    between them.**
  - `draw_exact_pos[i]` — `0` = not exactly known; otherwise the 1-based
    from-the-top position.
  - Ties bind to the lowest matching sorted slot, so two states with the same
    knowledge over the same multiset annotate **identically** — the tie-break
    is canonical, which is what makes the annotations hashable.
- `knowledge_chain_count`, `knowledge_exact_prefix`, `knowledge_full_order`
  (Frozen Eye — under it every slot carries an exact position, which
  reconstructs the true order exactly).
- `monster_roll_known[7]` / `monster_roll[7]` — the declared reveal channel
  for revealed monster construction rolls (e.g. a Louse's bite damage, public
  once a BITE intent is telegraphed). `MonsterState.pad0` stays excluded
  wholesale.
- The projection runs **only while `phase == COMBAT`** — knowledge is
  combat-scoped and the annotations index the combat section's draw pile.

These are the plan §3.1 order-constraint flags a card token reads. They are
also exactly the class of field the audit exists for: an omitted constraint
flag is twin-invariant, so no test class downstream would catch it.

### 5a. The four declared contract coarsenings (plan §2.2 / §2.6d)

The information contract is deliberately **coarser than the JDK mechanic in
four places**. Each is *weaker* than the exact posterior, i.e. it widens the
belief — which is sound (pessimistic about our own knowledge, never optimistic
about hidden state) — and each is declared at its site so nobody "fixes" it
into a mismatch with the sampler and the distribution tests. **The
distributional suite tests the declared contract, not seed-filtered reality.**

| # | Coarsening | Declared at | The exact mechanic, and why we do not model it |
|---|---|---|---|
| 1 | **Random-insert interleave.** After a random-position insertion (Wild Strike / Reckless Charge) following a known placement (Headbutt), `exact_prefix` and `full_order` drop to zero and only the chain's *relative* order survives — a uniform interleaving. | `knowledge.hpp` | `CardGroup.addToRandomSpot` inserts at `cardRandomRng.random(size-1)`; the top slot is unreachable, so a single insertion can never displace a Headbutt'd top card, and the exact posterior would keep it known-top. The engine mechanic itself stays exact — only what the player is *modeled to retain* is coarser. |
| 2 | **`canSpawn` relic membership.** The remaining relic multiset per tier is treated as public and only its **order** is re-permuted. | `resample.cpp` (`resample_relic_pool_remainders`) | `returnRandomRelicKey` consumes a `canSpawn`-rejected relic anyway and reroutes to an end-pop, so the true remainder is *not* "initial pool minus observed acquisitions" and is not derivable from public history at all. Reconstructing it would mean enumerating rejection histories per tier. The declared law is exactly right for what search consumes — the next pop, uniform over the remainder. |
| 3 | **Match & Keep miss-memory.** Only *currently visible* reveals are pinned (matched-and-taken pairs, plus the card currently face up); the rest of the board is permuted. | `resample.cpp` (`resample_match_and_keep_board`) | A human also remembers cards flipped on failed attempts — most of the event's skill. `EventBoardCard` has no `seen` bit, so the engine records no such history. Narrowing this is a `KnowledgeState` change (a per-slot seen bit) **plus** a matching pin in the sampler, not a sampler-only fix. |
| 4 | **Chest-contents size-band conditioning.** The chest **size** is public and preserved; the contents are redrawn as one fresh d100 interpreted **in that band**. | `resample.cpp` (`resample_treasure_chest_contents`) | `AbstractChest.randomizeReward` is one d100 and the band it is read against is public, so the conditional law given the size *is* a fresh uniform roll in-band. The size roll is not redrawn — a band representative is fed in so `treasure_chest_for_rolls` stays the single authority on the thresholds. |

## 6. The leak gates you inherit, and the ones you owe

Sim-side, per commit (plan §2.6a–c) — these are green at GT0 and stay green:

- **Hidden-twin byte equality.** `make_hidden_twin(state, rng)`
  ([`twin.hpp`](../include/sts/engine/twin.hpp)) is built **on**
  `resample_hidden`, so there is one definition of "hidden".
  `TwinSweep.PublicViewAndMaskAreByteIdenticalInEveryRunPhase` sweeps
  fuzz-generated states across every reachable `RunPhase`;
  `public_view_first_difference` names the leaking member when one fails.
- **Total-byte classification tripwire**
  ([`byte_class.hpp`](../include/sts/engine/byte_class.hpp),
  `tests/tripwire_test.cpp`): every byte of `RunController` and its substructs
  classified, rows tiling `sizeof` exactly, `static_assert` at build time.
  `run_seed` is classified **hidden**. Four parameterized negative controls
  prove it fires.
- **Reveal-timing tests** (`TwinRevealTiming.*`): chest contents twin-variant
  before the open and pinned after; a Louse construction roll twin-variant
  until telegraphed; Match & Keep face-down slots twin-variant with flips
  pinned.

Nightly (plan §2.6d): the sampler distributional suite —
`tests/sampler_dist_test.cpp`, nine pre-registered hypotheses with a
family-wise α of 1e-3 under Holm, fixed sampler seeds so p-values are
byte-identical across nights and hosts, plus three support-complete mutants as
negative controls. Nightly entry point:

```bash
tools/wsl_run.sh release                                  # or a native win-* build
tools/dist_check/sampler_dist.sh release                  # STS_SAMPLER_DIST_MODE=nightly
```

**Training-repo, promotion-gating (plan §2.6e–g — task T1.6, your side of the
contract):** policy-logit invariance across twins; search-statistic invariance
at a pinned sampler seed; and a hidden-fact probe gated on **no advantage over
a reference predictor given the belief marginals** — not absolute chance,
because e.g. a Cultist's next intent is legitimately ~100 % predictable from
public information.

## 7. The omniscient boundary — what a training tree may contain

The engine has two observation surfaces and only one respects the information
contract:

- `PublicView` — what a perfect-memory player could know, mask included,
  hashed by `public_hash()`. **This is the only one training-facing code may
  read.**
- The **omniscient** observation
  ([`omniscient_observation.hpp`](../include/sts/engine/omniscient_observation.hpp)):
  `omniscient_encode_observation` / `OmniscientObsBuffer`, and
  `StepResult::omniscient_obs`. A raw `CombatState` read, true intents and
  all. Legitimate for a debug dump, a diff harness, or an omniscient baseline;
  a silent leak inside an actor or a training pipeline.

Nothing in the type system separates them — both take a state and return
bytes — so the second is spelled with a token nothing else in the tree uses,
and the boundary is a **grep, not vigilance**:

```bash
tools/check_omniscient_boundary.sh              # the repo's declared set
tools/check_omniscient_boundary.sh --scan DIR   # scan DIR as if training-facing
```

Its contract:

- **Scanned by default:** the training-facing trees `include/sts/training`,
  `src/training`, `tools/training`, `tests/training` (declared ahead of
  existing, so the guard is in place the day the first one appears; a missing
  path is skipped, not an error), **plus a denylist** of individual files that
  must stay public-side wherever they live — the public-view encoder and the
  belief sampler. A **missing denylist file is a hard error**, so the check
  cannot silently degrade to a no-op.
- Files considered: code and build files only (`.h/.hpp/.ipp`,
  `.c/.cc/.cpp/.cxx`, `.py`, `.cmake`, `CMakeLists.txt`); tracked *or*
  untracked-but-not-ignored, so a violation is caught before it is staged.
- Exit codes: `0` clean, `1` violations, `2` usage/environment error.
- A deliberate reference in prose or a contrasting comment uses the
  `omniscient-boundary-ok` line hatch.
- It runs as the third step of the `stale-numbers` CI job, and it is a
  **git-side** check in default mode — run it from Git-Bash on the Windows
  host, never through WSL (`conventions.md` §6: WSL's git cannot read a linked
  worktree's `gitdir: D:/…`).
- `--scan DIR` uses no git at all, which is what lets
  `tests/omniscient_boundary_test.cpp` run it against committed fixture
  directories on either host.

Note that the batch `advance()` API is untouched and is a legitimate engine
API — the boundary is about the *observation* surface, not about stepping.

**Rule for a training-repo tree:** it may link the engine and call
`encode_public_view` / `public_hash` / `legal_actions` / `advance` /
`resample_hidden` / `make_hidden_twin`. It may **not** reference the
omniscient spelling anywhere under a scanned path. When the training repo
grows an in-repo surface here (T1.1), add its path to `kTrainingPaths` in the
same change.

## 8. Twin fixtures — the export you consume

The container format, generation command, and refuse-on-mismatch rules are
specified in
[../tests/golden/twin_fixtures/README.md](../tests/golden/twin_fixtures/README.md)
and, byte-normatively, in
[../tools/twin_fixtures/include/sts/twin/twin_fixture.hpp](../tools/twin_fixtures/include/sts/twin/twin_fixture.hpp).
**Read those; the summary here is only enough to decide whether you need to.**

`tests/golden/twin_fixtures/twins_v1.bin` stores each case as a **recipe plus a
payload**: the recipe is `(run_seed, ascension, policy, policy_seed, action
prefix, twin_seed)` — replaying the prefix gives the TRUE state and
`make_hidden_twin` gives its twin — and the payload is the `PublicView` (mask
included) that **both** encode to, stored verbatim. It is a recipe rather than
a state dump because `RunController` cannot be written as bytes: `MonsterLists`
holds `std::string_view` encounter keys, so a controller's representation
contains pointers whose values move with ASLR. That is the same
reconstruction-by-replay doctrine plan §5 uses for restricted sidecars.

A loader **refuses** any file whose magic, format version, engine
`SCHEMA_VERSION`, `PUBLIC_VIEW_VERSION`, or either struct size differs from the
reading build.

**The property to assert (this is T1.6's job):** any function of the
observation — encoder output, policy logits, search statistics at a pinned
sampler seed — must be **identical on the two rebuilt states of every case**.
They differ only in hidden state, and every stored payload is byte-identical
between them by construction.

## 9. Where each thing lives

| Thing | Home |
|---|---|
| `PublicView`, `PvMask`, `encode_public_view`, `public_hash` decl | [`include/sts/engine/public_view.hpp`](../include/sts/engine/public_view.hpp) |
| Field-by-field audit, flags bit audits, schema-evolution note | [public-view-audit.md](public-view-audit.md) |
| `KnowledgeState`, the coarsening #1 note | [`include/sts/engine/knowledge.hpp`](../include/sts/engine/knowledge.hpp) |
| `resample_hidden`, coarsenings #2–#4 | [`include/sts/engine/resample.hpp`](../include/sts/engine/resample.hpp) + `src/engine/resample.cpp` |
| `make_hidden_twin`, `public_view_first_difference` | [`include/sts/engine/twin.hpp`](../include/sts/engine/twin.hpp) |
| Byte classification tripwire | [`include/sts/engine/byte_class.hpp`](../include/sts/engine/byte_class.hpp) |
| Twin fixture container | [`tools/twin_fixtures/include/sts/twin/twin_fixture.hpp`](../tools/twin_fixtures/include/sts/twin/twin_fixture.hpp) |
| Omniscient boundary check | [`tools/check_omniscient_boundary.sh`](../tools/check_omniscient_boundary.sh) |
| Nightly sampler suite entry | [`tools/dist_check/sampler_dist.sh`](../tools/dist_check/sampler_dist.sh) |
| Gate evidence | [verification/gt0-info-layer.md](verification/gt0-info-layer.md) |
| Phase T spec / ledger | [training-plan.md](training-plan.md) · [training-tasks.md](training-tasks.md) |

## 10. Changing any of this

A change to the view, the mask, the projection, or a coarsening is a change to
*this contract*, and lands under this repo's full conventions:

1. Bump `PUBLIC_VIEW_VERSION` and add a version-log entry to the audit's
   schema-evolution note, classified additive or breaking against the
   enumerated cases there.
2. Add the audit row **and** the `byte_class.hpp` row in the same change — the
   header's per-row `note` is the one-line form of the audit's Notes cell, so
   a disagreement between them is a documentation conflict (conventions §4).
3. Re-run the leak gates; a new coarsening gets a declaration at its site and
   a row in §5a here.
4. Consumers are refuse-on-mismatch, so a breaking change is a
   reanalyze-or-quarantine event on stored shards (plan §4.1), not a silent
   reinterpretation.
