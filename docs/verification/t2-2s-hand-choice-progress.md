# T2.2s: finite progress on optional hand choices

The SIM_SEARCH family no longer deselects cards on an optional combat hand
choice. Legal unselected additions retain their search scores and legal CONFIRM
remains available. This repairs a policy livelock without changing the engine's
choice rules or legal-action surface. Non-search policies are untouched.

## Scope and provenance

Engine base: `06ad695b06fb5dbc14a6de788f4099ace3673141`. The sole code change is
`tools/fuzz/src/policy_search.cpp`. Existing policy IDs are retained; the new
engine commit versions their changed behavior. Historical trajectories remain
bound to their original `sim_commit`, including T2.2r's
`019fa9fdf7154802931b2e7d1ae16af76573afda` pin.

All artifacts are external under `D:/STS_BG_Mod/_train_data/t22s/`. Inputs are
the 212 unique seeds whose trajectories were `advance_cap` in T2.2r's visible
primary train/dev CSVs, plus six visible seeds for sanitizer coverage. No
holdout payload was read, registered artifact changed, model fitted, or game
launched. This is real-simulator policy acceptance, not live-oracle evidence or
a currency qualification.

## Defect and repair

The old policy could prefer selecting a card and then prefer deselecting that
same card. Its `+1` CONFIRM bonus handles ties only. An optional CHOOSE toggles
the hand's selected suffix; it does not advance combat time or close the
screen. Consequently the search's turn limit never breaks this cycle.

Two original-pin replays reproduce the registered final run hashes:

| Seed | Boss / choice | Old final run hash | Old result, both policies |
|---|---|---|---|
| 1000053 | Slime Boss, turn 1, Elixir exhaust | `1c076e9b6d225176` | cap at 20,000 |
| 1000057 | Hexaghost, turn 2, Elixir exhaust | `156370d26f1f2854` | cap at 20,000 |

At seed 1000053, selected Defend cards oscillate while boss HP stays 136 and
player HP stays 73. At seed 1000057, Sever Soul oscillates while boss HP stays
240 and player HP stays 71. CONFIRM is legal throughout. A diagnostic forced
CONFIRM on seed 1000053 closes the screen immediately and normal continuation
reaches death; that counterfactual does not replace any registered label.

The repair reuses the actual decision's enumerated legal moves and queue-front
selected count. CHOICE_CONFIRM identifies the optional screen without a second
legal_actions call. It assigns the minimum score to COMBAT_CHOOSE moves
addressing the selected suffix and skips their rollout. It retains additions,
CONFIRM, potion choices, and the existing one-draw tie-break. Selected cards
therefore cannot oscillate back out through this policy. Simulated future
choices are unchanged; this limits the repair to actual optional-choice
decisions and preserves unrelated action prefixes.

## Real-run acceptance

`progress_replay.cpp` constructs real runs at ascension 20, reproduces the
trainer's once-per-run RNG initialization from policy seed 20260903, and drives
policies 5 and 6 with the original 20,000-advance cap. It records immediate
Act-1 boss-combat exit kills, observed player death, or typed unresolved
outcomes. No fabricated states or unit tests are used.

All 424 previously capped visible trajectories resolve: 106 observed Act-1
boss kills and 318 observed deaths. The maximum is 311 advances. Across 1,484
optional-choice decisions there are 470 confirms and zero deselections. The
two named seeds now reach observed death at 230 and 161 advances respectively,
under both policies. Policy pairs are not independent samples: their behavior
differs at the boss chest, beyond this collection horizon.

Primary execution uses four workers with ascending dispatch. Repeat execution
uses two workers with reversed dispatch and the same canonical output order.
The complete CSV bytes match:

`repaired_primary.csv` = `repaired_repeat.csv`, SHA256
`2b38aff8454003b949443e818f503c4e52be1c629421b1e3f9a6551dbcb31a46`.

The non-optional prefix comparison uses the exact unmodified engine-base
policy source linked to the same current engine library, avoiding attribution
of unrelated changes between engine pins. Both policies retain the first
optional-choice steps and rolling action/state prefix hashes:

| Seed | First optional decision | Base and repaired prefix hash |
|---|---:|---|
| 1000053 | 199 | `6c9e68de9edb4d98` |
| 1000057 | 137 | `fa925d298a3203e3` |

These prefix hashes are the harness's deterministic 64-bit rolling digest;
artifact provenance uses SHA256. The unmodified base still caps both seeds
with the original registered final run hashes.

`fuzz_core` builds pass under win-debug, win-release and win-asan. Each preset
then runs seeds 1000053, 1000057 and 1000001 through 1000006 under both policies.
The 16-trajectory CSVs match across all three presets, SHA256
`d9ddb236217836fb21244c532365145c41099639a679c746679feec3919e63a0`.
ASan/UBSan execution exits zero with empty stderr. The external harness uses
the preset's matching CRT; sanitizer execution adds the LLVM runtime DLL
directory to PATH. No CTest or unit-test binary was run.

## Reproduction and receipt

Build commands from the engine worktree:

```text
tools\win_build.cmd win-release --target fuzz_core --parallel 4
tools\win_build.cmd win-debug --target fuzz_core --parallel 4
tools\win_build.cmd win-asan --target fuzz_core --parallel 4
```

External `build_progress_replay.cmd <preset>` links the standalone real-run
driver against those libraries. `build_progress_replay_old.cmd` binds the
historical trainer build. `build_progress_replay_base.cmd` binds the exact
unmodified base policy source. `verify_progress.py` checks actual output
identities, repeat equality, typed resolution, zero deselections, prefix
invariance and preset equality, then emits `progress_receipt.json`.

Receipt SHA256:
`2c25798548f07dc5bc5f28717729944b4981feedafaab01815f33bacf97d5147`.
It inventories the driver, executables, seed list and output CSV hashes.

This targeted repair does not reverse T2.2r's `inconclusive_hold`, qualify a
currency, or authorize interpreting the diagnosis population as a fresh
evaluation cohort. A reviewed trainer engine-pin move and newly registered
collection remain separate work. Further non-choice caps, engine assertions,
or malformed trajectories must remain typed unresolved outcomes, never
converted into negative training labels.
