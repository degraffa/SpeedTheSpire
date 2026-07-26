# B4.5 oracle spot-diff runbook — combat reward screens

The B4.5 acceptance's oracle leg: **>= 3 bridge runs' reward screens zero-diff
through the differ**, where "zero-diff" means the **post-claim `RunState`**
(gold, potions, deck, pity, counters) — *not* the offer (no new storage, no
schema bump). The tier-2 leg (pity vs. hand-derivation, trap-13/18 stream
attribution, the claim flow) is `combat_rewards_test` and runs in CI; this leg
needs the live game, which is **launched manually by a human operator**
(CLAUDE.md, oracle-bridge section) — an agent cannot start it. Everything up to
the capture is prepared below; the capture and the diff read-out are the
remaining human steps.

## 1. Stop-line environment decision

The preserved `b45_rewards` campaign is **not acceptance evidence**. Its
headers say `oracle_block_enabled: false`: a GUI launch loaded stock
`CommunicationMod`, which shares the fork's config namespace and therefore
still spawned the driver. Separately, the installed launch log reported Slay
the Spire `12-18-2022`, ModTheSpire `3.30.3`, and BaseMod `5.56.0`, while the
frozen environment names Slay the Spire `11-30-2020` and ModTheSpire `3.18.1`.
Those newer versions are recorded as the observed drift, **not sanctioned**.

Do not run the B4.5 campaign until the owner either restores the frozen stack
or formally approves a frozen-design/environment amendment. This runbook
continues to express the existing frozen choice; it does not make that
decision.

## 2. One-seed preflight (operator, Windows host)

After the environment decision is resolved, run a distinct one-seed campaign
before any reward capture:

```bat
C:\Python39\python.exe orchestrator.py ^
    --campaign-id b45_rewards_preflight ^
    --seeds STS00041 ^
    --policy random-legal ^
    --max-actions 1 ^
    --fresh
```

The preflight is a gate, not a smoke test. All of these checks are required:

1. `mts_launch1.log` names **Slay the Spire (11-30-2020)**,
   **ModTheSpire (3.18.1)**, and
   **CommunicationMod-oracle (1.2.1-oracle.0)** in its version/mod list; it
   must not list stock `CommunicationMod`. The BaseMod line must match the
   owner-approved frozen installation. A different or unresolved BaseMod
   version stops the campaign rather than being inferred from the hard-coded
   artifact header.
2. The deployed fork jar's SHA-256 is
   `04477E4EAA07FC14774F9A687AC971EFBDB64EA7ECCB56804481B008B2C36636`.
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
       --campaign D:/STS_BG_Mod/_oracle_data/campaigns/b45_rewards_preflight
   ```

   Strict mode requires `oracle_block_enabled: true`, a `game_state.oracle`
   object on every in-game action record, both reward pity fields
   (`cardBlizzRandomizer`, `blizzardPotionMod`), and complete
   `{counter,s0,s1}` triples for `cardRng`, `treasureRng`, `potionRng`,
   `relicRng`, and `miscRng`.

If any check fails, preserve the preflight directory for diagnosis and stop.
Do not relaunch, reuse its artifact, or advance to the reward campaign.

## 3. Capture (operator, Windows host)

Pick three seeds and put them (base-35 strings, one per line) in
`D:/STS_BG_Mod/_oracle_data/campaigns/b45_seeds.txt`. Prefer seeds/paths whose
early floors avoid:

- **Looter fights** (`Exordium Thugs` / `Looter`) — the Looter is the parked
  B3.15 remainder; a Looter combat diverges at the *combat* level before the
  reward screen is even reached, and its mugged/STOLEN_GOLD screen is
  deliberately unmodelled.
- **The emerald elite** — the elite carrying the emerald key shows an
  `EMERALD_KEY` reward item and (on claim) sets a key the sim does not store;
  that flag is documented out of S1 scope (see `map_rooms.hpp`'s
  setEmeraldElite note). Any other elite is clean.

Then, per the standard flow (`README.md` in this directory), let the
orchestrator launch the explicit fork-only mod list:

```bat
C:\Python39\python.exe orchestrator.py ^
    --campaign-id b45_rewards_oracle ^
    --seeds D:/STS_BG_Mod/_oracle_data/campaigns/b45_seeds.txt ^
    --policy random-legal ^
    --fresh
```

`random-legal` claims reward items and picks/skips reward cards, so each run's
JSONL carries several COMBAT_REWARD / CARD_REWARD screens with the states on
both sides of every claim. Artifacts land under
`D:/STS_BG_Mod/_oracle_data/campaigns/b45_rewards_oracle/`. This distinct id is
required: never reuse or overwrite the preserved invalid `b45_rewards`
campaign.

Before translation, require the oracle gate on the reward campaign too:

```bat
C:\Python39\python.exe validate_artifacts.py --require-oracle ^
    --campaign D:/STS_BG_Mod/_oracle_data/campaigns/b45_rewards_oracle
```

## 4. Translate

Build `translate_cli` (any preset; it is a tools target) and run it over the
three artifacts:

```bash
tools/wsl_run.sh debug          # builds translate_cli among everything else
build/debug/tools/oracle_bridge/translator/translate_cli \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/b45_rewards_oracle/run_*.jsonl
```

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

The run-level replay is still manual: the generalized "seed a sim replay from
any translated RunState" adapter was deferred by B1.6 to B4.4 and recorded
**undischarged** in the ledger's obligations table (owner needs re-owning), so
until it exists the sim side is driven by hand (a ~20-line gtest or scratch
main that calls `run_begin` and feeds the artifact's `action_command` sequence
as run-level `CHOOSE`/`PLAY_CARD` actions). `diff_run_states`
(`tools/diff_harness`, `sts/diff/differ.hpp`) prints the field-by-field diff.

## 6. Known caveat the capture ALSO resolves: card-pool library order

The three generated reward pools (`kIroncladCommonPool` / `...UncommonPool` /
`...RarePool`, like `kIroncladAttackPool` before them) are emitted in
**registry-id order** — a documented interim deviation. The game's pools fill
in CardLibrary HashMap iteration order ("library order"), which no oracle
capture has pinned yet. The **index drawn** is stream-exact either way; **which
card that index names** is not, so the *deck* column of the diff may mismatch
on exactly the picked-card identity until the order is pinned.

**While reading the diff:** treat a deck mismatch whose *count and upgrade*
agree but whose *card id* differs as the library-order deviation, not a stream
bug — the stream counters and pity must still zero-diff regardless.

**To pin the order from this capture** (discharges the B3.6-deferred
obligation): each CARD_REWARD screen in the artifact lists the offered card
ids; together with the sim's drawn indices for the same `cardRng` states, three
runs' offers over-determine the per-rarity pool orders. Write the recovered
orders into `tools/registry_gen/stsgen/emit/cards.py` (replace the
`sort(key=lambda r: r["id"])` for the four pools with an explicit library-order
key — the "one-line gen.py fix" the ledger anticipates), regenerate, and re-run
the diff: the deck column must then zero-diff too.
