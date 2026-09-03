# B4.5 oracle spot-diff runbook — combat reward screens

The B4.5 acceptance's oracle leg: **>= 3 bridge runs' reward screens zero-diff
through the differ**, where "zero-diff" means the **post-claim `RunState`**
(gold, potions, deck, pity, counters) — *not* the offer (no new storage, no
schema bump). The tier-2 leg (pity vs. hand-derivation, trap-13/18 stream
attribution, the claim flow) is `combat_rewards_test` and runs in CI; this leg
needs the live game, which is **launched manually by a human operator**
(CLAUDE.md, oracle-bridge section) — an agent cannot start it.

**STATUS: this runbook has been executed and B4.5's oracle leg PASSED**
(2026-07-27; §7 records the capture). It is kept as the procedure for the next
combat-reward capture, not as an open task. The one step that still needs a
human is the launch in §2/§3; §4 onward is now a single binary
(`replay_run_diff`, §5).

## 1. Environment decision — RESOLVED 2026-07-26

**The stack question is settled: the installed stack is sanctioned.** The
frozen environment is now **Slay the Spire `12-18-2022` (`[V2.3.4]`),
ModTheSpire `3.30.3`, BaseMod `5.56.0`** (design §1.2, amended at §11 v0.1.7).
This runbook is no longer blocked on it.

Nothing was downgraded, and nothing needed to be: the `11-30-2020` label was a
documentation error inherited from *upstream* CommunicationMod's declared
`sts_version`, never an observation of this install. The decompiled Java that
every `File.java:line` citation in these docs resolves against is itself
`12-18-2022` (`CardCrawlGame.VERSION_NUM`), so the spec the simulator was built
from and the runtime every campaign was captured on are the same build. No
prior evidence is invalidated and none is re-blessed.

The preserved `b45_rewards` campaign is still **not acceptance evidence**, for
the reason that always applied: its headers say `oracle_block_enabled: false`
because a GUI launch loaded stock `CommunicationMod`, which shares the fork's
config namespace and therefore still spawned the driver. Preserve it; never
reuse or overwrite it.

**What the operator must still do before the capture:** redeploy the fork jar.
`ModTheSpire.json` was amended, so the jar was rebuilt and its pinned SHA-256
changed (§2 check 2). The jar currently in `<game>\mods\` is the **old** build.
Run `build_fork.ps1` *without* `-NoDeploy`, or copy
`build\oracle_fork\CommunicationMod-oracle.jar` into `<game>\mods\` by hand.

## 2. One-seed preflight (operator, Windows host)

Allocate a **new, immutable campaign tag for this attempt** (UTC timestamp plus
an operator suffix is recommended). Never rerun a failed or completed B4.5 id:
preserve its directory as evidence and allocate another tag. In the same
`cmd.exe` window:

```bat
set B45_TAG=20260726T210000Z_alex01
set B45_PREFLIGHT_ID=b45_rewards_preflight_%B45_TAG%

C:\Python39\python.exe orchestrator.py ^
    --campaign-id %B45_PREFLIGHT_ID% ^
    --seeds STS00041 ^
    --policy random-legal ^
    --max-actions 1 ^
    --fresh
```

The preflight is a gate, not a smoke test. All of these checks are required:

1. `mts_launch1.log` names **Slay the Spire (12-18-2022)**,
   **ModTheSpire (3.30.3)**, **basemod (5.56.0)** and
   **CommunicationMod-oracle (1.2.1-oracle.0)** in its version/mod list; it
   must not list stock `CommunicationMod`.

   **The driver now enforces this itself** — the orchestrator allocates a new
   append-only `mts_launch<N>.log` above every preserved numeric index and binds
   that exact filename to the child with a one-use nonce inherited through the
   launched game. The driver parses only that bound log before it will write an
   artifact header, records what it observed in `game.sts_version` /
   `game.mts_version` /
   `game.basemod_version` / `game.version_source`, and refuses with
   `fatal_environment_drift` on a mismatch, on stock `CommunicationMod` being
   loaded beside the fork, or on the log/token binding being absent (including a
   later GUI launch with persisted config). So this check is now a confirmation,
   not the only line of defence. Until B4.5 those header fields were **static
   constants**, which is why the whole existing corpus claimed `11-30-2020`
   while running `12-18-2022`; a header is only evidence because of that change
   (design §11 v0.1.7).
2. The deployed fork jar's SHA-256 is
   `7DC814AD240CBBD9100B2E8C92B6AA97B4ADFBED62FFED7961C6E5DE15884733`.
   (Re-derived at B4.5: amending `ModTheSpire.json` changed the jar's contents.
   The previous pin `04477E4E…B2C36636` is the **pre-amendment** build — if you
   see it here, the fork has not been redeployed yet; see §1.)
   Verify the deployed file itself:

   ```powershell
   (Get-FileHash -Algorithm SHA256 `
     'D:\SteamLibrary\steamapps\common\SlayTheSpire\mods\CommunicationMod-oracle.jar').Hash
   ```

   The artifact header alone is not proof of which mod loaded: the driver
   hashes its `--fork-jar` argument independently of ModTheSpire.
3. `campaign_progress.json` is `complete`, never
   `fatal_environment_drift`, and the preflight artifact passes the strict
   oracle validator:

   ```bat
   C:\Python39\python.exe validate_artifacts.py --require-oracle ^
       --campaign D:/STS_BG_Mod/_oracle_data/campaigns/%B45_PREFLIGHT_ID%
   ```

   Strict mode requires `oracle_block_enabled: true`, a `game_state.oracle`
   object on every in-game action record, **at least one such in-game action**,
   both reward pity fields
   (`cardBlizzRandomizer`, `blizzardPotionMod`), and complete
   `{counter,s0,s1}` triples for `cardRng`, `treasureRng`, `potionRng`,
   `relicRng`, and `miscRng`. With `--campaign`, strict mode also requires a
   complete, failure-free progress/manifest ledger whose ordered `seed_list`
    and `seeds_done` match exactly, then proves a bijection to the run and timing
    artifacts. Missing, extra, stale, cross-campaign, or failed-seed evidence is
    fatal. It also joins the filename/header/in-game/oracle seed identities,
    requires exactly one final terminal with contiguous action sequence and
    matching terminal/done counts, and parses every timing row with exact
    mark-for-action correspondence. A header-only timing file, malformed tail,
    duplicate terminal, action after terminal, or missing summary is fatal.

If any check fails, preserve the preflight directory for diagnosis and stop.
Do not relaunch, reuse its artifact, or advance to the reward campaign.

## 3. Capture (operator, Windows host)

Pick three seeds and put them (base-35 strings, one per line) in
`D:/STS_BG_Mod/_oracle_data/campaigns/b45_seeds.txt`. Prefer seeds/paths whose
early floors avoid:

- ~~**Looter fights**~~ — **no longer a reason to avoid a seed.** That caveat
  predated the Looter/mugged landing: Looter combats are implemented and the
  mugged / STOLEN_GOLD reward screen is fully modelled (`RewardOutcome` in
  `combat_rewards.hpp`, and the run layer's escape settlement). Treat a
  divergence on a Looter floor as a real divergence.
- **The emerald elite** — the elite carrying the emerald key shows an
  `EMERALD_KEY` reward item and (on claim) sets a key the sim does not store;
  that flag is documented out of S1 scope (see `map_rooms.hpp`'s
  setEmeraldElite note). Any other elite is clean.

Then, per the standard flow (`README.md` in this directory), let the
orchestrator launch the explicit fork-only mod list. Derive a second,
never-before-used id from the successful preflight tag:

```bat
set B45_REWARD_ID=b45_rewards_oracle_%B45_TAG%

C:\Python39\python.exe orchestrator.py ^
    --campaign-id %B45_REWARD_ID% ^
    --seeds D:/STS_BG_Mod/_oracle_data/campaigns/b45_seeds.txt ^
    --policy random-legal ^
    --fresh
```

`random-legal` claims reward items and picks/skips reward cards, so each run's
JSONL carries several COMBAT_REWARD / CARD_REWARD screens with the states on
both sides of every claim. Artifacts land under
`D:/STS_BG_Mod/_oracle_data/campaigns/%B45_REWARD_ID%/`. This distinct id is
required: never reuse or overwrite the preserved invalid `b45_rewards`
campaign, a prior preflight, or a prior reward attempt. `--fresh` authorizes
only bounded cleanup of that invocation's control files, launch logs, and the
exact requested seeds' run/timing artifacts; it does not erase unexpected
files, which strict validation reports instead. The id must remain the one safe
path component shown above: rooted ids, path separators, `.`/`..`, symlink
escapes, and any symlink/junction/reparse redirect at a campaign directory or
direct-child file are rejected. `--fresh` never follows an owned-looking name
to another target, including another file inside the campaign.
The orchestrator hashes the requested fork before it accepts even an already
complete ledger; changing fork/schema/seed/policy requires a new campaign id,
and an in-progress ledger also refuses a changed driver revision.

Before translation, require the oracle gate on the reward campaign too:

```bat
C:\Python39\python.exe validate_artifacts.py --require-oracle ^
    --campaign D:/STS_BG_Mod/_oracle_data/campaigns/%B45_REWARD_ID%
```

## 4. Translate

Build `translate_cli` (any preset; it is a tools target) and run it over the
three artifacts:

```bash
tools/wsl_run.sh debug          # builds translate_cli among everything else
build/debug/tools/oracle_bridge/translator/translate_cli \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/$B45_REWARD_ID/run_*.jsonl
```

In WSL, either export `B45_REWARD_ID` again or replace it with the exact
preserved directory name printed by the Windows capture step.

Expected: `OK` per file, **zero unknown-field errors**. The B4.5 translator
slice content-validates the reward screens (enumerated `reward_type`, typed
gold, id-joined potions/relics/cards), so an unexpected reward shape fails loud
here rather than passing silently.

## 5. Spot-diff (the read-out)

For each captured reward screen: take the last record **after** the claims/
proceed (the post-claim state) and compare its translated `RunState` against
the sim stepped through the same choices from `run_begin(seed, 20)` — the
fields named by the acceptance:

| Field | Sim source |
|---|---|
| `gold` | `RunState.gold` after `claim_reward` (gold door: Golden Idol bonus, Ectoplasm suppression) |
| `potions[]` | slots after the potion claim (first-free-slot; Sozu discards) |
| `master_deck[]` | after `reward_take_card` (the `add_card_to_master_deck` door) |
| `card_blizz_randomizer` | pity after the card rolls |
| `blizzard_potion_mod` | the +/-10 ratchet after the potion roll |
| stream counters | `cardRng` / `treasureRng` / `potionRng` / `relicRng` (+ `miscRng` for boss gold) |

**This is no longer done by hand.** `tools/oracle_bridge/replay/replay_run_diff`
does the whole read-out; point it at the artifacts and read the summary:

```bash
tools/wsl_run.sh debug        # builds it among everything else
build/debug/tools/oracle_bridge/replay/replay_run_diff \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/$B45_REWARD_ID/run_*[!g].jsonl
```

Per reward screen it prints an `ASSEMBLY` line (streams + both pity counters +
the item list, seeded from the captured pre-battle-over `RunState`) and a
`CLAIM` line (the whole post-claim `RunState` through `diff_run_states`, which
is where gold / potions / deck are proved), and it exits non-zero if any file
had a failure. `--replay` re-drives a whole run from `run_begin` instead, and
`--combat` adds a per-record `CombatState` diff for triage; both are diagnosis
modes, not the acceptance.

`--vitals` (with `--replay`) is the third, and the only one of the three that
is a *comparison* rather than a print. It projects both sides of every
in-combat record to the index-normalised `CombatVitals`
(`tools/oracle_bridge/translator/include/sts/translate/combat_vitals.hpp`, which
carries the contract and every exclusion) — turn, player block / energy /
powers, each monster SLOT's identity / hp / block / liveness / powers, and each
pile's contents as a `(card, upgrades)` multiset — and prints the FIRST
differing record in full as a `VDIFF` block, later ones as one line each unless
`--verbose`:

```bash
build/debug/tools/oracle_bridge/replay/replay_run_diff --replay --vitals \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/<id>/run_*[!g].jsonl
```

Pool indices and pile order never enter the projection (draw-pile order is
hidden by PROTOCOL.md §3.10 anyway), which is what makes it a compare where
`--combat` can only ever be a print. It exists because the run-level differ
sees a combat-internal drift — a different damage number, a Time Warp count one
off — only once it becomes a run-level symptom several records later; `--vitals`
names the record where it starts. It is a **second report beside** the run-level
one: `CLEAN`/`PART`, the exit code and every existing line are unchanged, and
its own summary says `vitals-clean` or `vitals-divergent` with the first
differing seq. Records the run-level compare already excuses as capture races,
the A20 double-boss handoff, and records where the sim has already left the
fight are skipped and counted rather than compared.

## 6. Caveat the capture ALSO resolved: card-pool library order — CLOSED

The generated pools used to be emitted in **registry-id order**, a documented
interim deviation: the game fills its pools in CardLibrary HashMap iteration
order ("library order"), the **index drawn** is stream-exact either way, but
**which card that index names** was not — so the deck column of the diff was
expected to mismatch on exactly the picked-card identity.

It did, on 8 of 13 screens, and then it was fixed. The order needed no empirical
recovery: a Java `HashMap`'s iteration order is a pure function of the keys, the
final capacity and insertion order, so `emit/cards.py` **computes** it
(`library_order_key`). See §7 and
[the log entry](../../../docs/stage-b-log.md#b45-spotdiff-readout) for the rule
and the two distinct orders it produces. Nothing here is outstanding; if a
future capture ever shows a deck mismatch whose count and upgrade agree but
whose card id differs, that is now a **regression**, not the known deviation.

## 7. The capture that closed this runbook — 2026-07-27

Two campaigns, both strict-validated with `--require-oracle`:

| Campaign | Seeds | With reward screens |
|---|---|---|
| `b45_rewards_oracle_20260727T204809Z_claude01` | STS00042-46 | STS00042, STS00043 |
| `b45_rewards_oracle2_20260727T204809Z_claude01` | STS00047-52 | STS00048, STS00049, STS00051, STS00052 |

The five runs not listed as having reward screens died in the floor-1 fight
before reaching one; they are still validated members of their campaigns and
must be preserved. The six that did carry **13 combat reward screens, and all 13
zero-diff** — the acceptance asked for ≥ 3 runs.

Two operating notes for whoever captures next:

- **`random-legal` dies early and often at A20.** Five of eleven runs never
  reached a reward screen at all, and the deepest got to floor 7. Budget seeds
  accordingly; ~6 runs is a comfortable margin for a ≥ 3 requirement.
- **A Neow boss-relic swap can make a run un-replayable while leaving its
  reward screens perfectly diffable.** Three of the six drew a relic whose body
  is on the deferred obligations table (Philosopher's Stone, Fusion Hammer,
  Astrolabe), so `--replay` desyncs in the first combat. The default reward
  mode seeds from the captured post-combat state and is unaffected — which is
  precisely why it is the default.
