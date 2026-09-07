# S3 source audit and provisional progress — 2026-09-07

The owner asked to use source analysis, manual reasoning and saved scripts to
make progress before stronger policies can produce full oracle runs. This
bounded audit began at engine `edc3d09dd830d5f4c1113af04c4e13e845068ef8`.
It found concrete defects and corrected a reachability claim. It does not
estimate a percentage of simulator correctness or close S3.62/S3-G2.

The evidence categories used here are deliberately separate:

- **Source mismatch:** a complete game-source method and its simulator or
  bridge counterpart disagree on a supported input. Repair and document the
  condition; retain a live-witness obligation.
- **Source consistent:** the examined paths agree. This is positive, bounded
  evidence, not a claim about every interaction or real-game execution.
- **Residual uncertainty:** a callback, action-order or capture-boundary
  question remains. Name a discriminating future observation instead of
  treating intuition as a verified result.

No new game session or oracle capture ran during this work. No unit tests were
added or run. Builds and any directed simulator probes are recorded in the
individual repair reports.

## Concrete findings and repairs

| Finding | Consequence | Repair/evidence |
|---|---|---|
| The bridge bypassed the Surrounded-facing writes in ordinary targeted card and potion input. | A script could target the Shield while the live player stayed facing the Spear; simulator and game would disagree for a bridge-caused reason. | [Bridge source repair](s3-bridge-facing-source-audit.md), JDK 8 compiled twice to identical bytes; jar preserved, not installed. |
| The bridge also omitted the targeted-potion combat HandCheckAction. | Its input path missed a retail hand update before potion-use relic callbacks. | Same bridge repair; HandCheckAction does not itself resolve the separate hand-layout marker-timing question. |
| Smoke Bomb eligibility ignored BackAttack under a stale claim that the power was unimplemented. | The simulator could offer an escape which `SmokeBomb.canUse` refuses. | [Smoke Bomb source repair](s3-smoke-bomb-source-audit.md); inspect actual power presence on every group member, including dead slots, independently of its numeric amount. |
| S3.61 said its routes never entered the guard fight. | Useful partial capture material was incorrectly considered unavailable. | Saved script steps below prove guard-combat entry. Neither route wins the fight; neither establishes a Heart route. |

The simulator's facing and damage arithmetic were not changed to match the
bridge's missing input behavior. The bridge repair preserves the game's
ordinary input semantics. Both repairs remain pending a new live witness.

## Source-consistent paths examined

Game-source paths below are relative to
`D:/STS_BG_Mod/SlayTheSpireDecompiled/com/megacrit/cardcrawl/`.
Complete cited methods were read; the repository never incorporates the
decompiled source as an artifact.

| Area | Source and simulator correspondence | Remaining boundary |
|---|---|---|
| Act-4 construction | `dungeons/TheEnding.java:40–51,72–143,162–217` creates the fixed map and literal encounter lists. `src/engine/run_advance.cpp`'s `generate_special_map`, `act_transition` and `act4_crossing` use those fixed structures, avoid procedural map/monster draws, and preserve the floor base from the Door. | Existing A20 prefix supports early entry. Below-A20 floor/transition capture and later room witnesses remain distinct obligations; lower-difficulty combat is not certified by this A20 review. |
| Transition and terminal | `AbstractDungeon.dungeonTransitionSetup:2562–2604`, `DoorUnlockScreen.exit:143–161`, `ProceedButton.update:81–177` and its transition helpers `:189–220` agree with the split between Act-3 double boss, Door crossing and Act-4 true victory. `TrueVictoryRoom:20–32` has no further player decision; the simulator terminates on its entry after room-transition hooks. | A full terminal capture still needs to observe the combined path and any active relic hooks. |
| Elite and terminal rewards | `rooms/AbstractRoom.update:206–389` generates boss gold before its separate reward-screen suppression, and `addPotionToRewards:580–608` checks the assembled item count after White Beast Statue. `MonsterRoomElite.dropReward/addEmeraldKey:80–98` orders relic, Black Star relic, then key. `combat_rewards.cpp` and the Heart branch in `run_advance.cpp` preserve these guards and ordering. | Four-item suppression, Courier restock and relic-specific room-entry witnesses remain useful. The unclaimed Heart gold roll is not player gold income. |
| Guard A20 behavior | Complete `SpireShield`/`SpireSpear` methods agree with their C++ move cycles, Artifact tier, Burn placement, Skewer hits and flat Smash block. Source geometry supports the simulator's facing predicate; post-super guard-death cleanup has the same ordering for both kill orders. | Actual hand-layout callbacks are approximated at input seams. The BackAttack marker has readers, including Smoke Bomb eligibility and guard death cleanup; its timing must not be dismissed as purely cosmetic. |
| Heart cap and turns | `InvinciblePower:18–48` clips and drains the remaining pool per delivered hit and refills at monster turn start. `interp_damage.cpp`, `power_invincible.cpp` and the monster turn pump implement those same boundaries. `CorruptHeart`'s Debilitate/attack/buff cycle, negative-Strength cancellation and buff ladder match `monster_corrupt_heart.cpp`. | A live over-cap hit, several hits into the remaining pool, the refill, and emitted `misc_field=maxAmt` will distinguish the important alternatives. |
| Beat of Death and lethal actions | `UseCardAction.update:75–88` visits powers after the card program, with no dying-monster filter; `BeatOfDeathPower.onAfterUseCard:39–44` queues retaliation. The simulator's USE_CARD boundary, power iteration and post-combat survivor drain preserve that retaliation even on a killing card. | A lethal Heart card with possible player death is the strongest missing ordering witness. Source consistency does not certify every queued relic/card interaction. |

Heart Blood Shots targets the player; its separate damage actions and
player-death stopping behavior were also reviewed. It is not an Invincible
interaction: the cap applies to hits received by the Heart.

Detailed external reviewer notes:

- `_oracle_data/s3/source_audit_20260907/heart.md`, SHA256
  `b00dd6805b010a571314f205101f3aaf8e6e3c05120cab6b0920bf74d06a7ac3`.
- `_oracle_data/s3/source_audit_20260907/guards.md`.

## The guard fight is already reachable in saved scripts

The integrated, corrected plain route is
`_oracle_data/s3/s362/integrated_scripts_20260907/STS511413__sim_search_keys__ps76.script.jsonl`,
SHA256 `4dfcbc052663254b9b7b8467f1974d44acf95229385908eef66ef6bfed7a711d`.
Its header records 769 actions and final hash `d594a50d1e40f524`.
Steps 765–768 are Act-4 floor-55 COMBAT: Strike targeting SpireSpear,
Strike targeting SpireShield, Pommel Strike targeting SpireSpear, then end
turn. Thus it offers a concrete target-switching witness before any elite win.

The earlier deep route
`_oracle_data/s3/s361_scripts/STS511413__sim_search_keys_deep__ps804.script.jsonl`,
SHA256 `ca2657f0c5223af8b0575738f7133f1f8de514b1dc5f75fc60071eaa09993267`,
also has floor-55 COMBAT steps 837–839. That archived file retains the old
map-symbol bug, so its combat steps establish simulated reach but the file
must be regenerated before another live use.

This corrects S3.61's report and the active S3.62 brief. It does not turn the
previous truncated live capture into a complete guard capture, and it does
not supply either guard-kill order or a Heart victory.

## How this changes the work order

1. Continue source audits and small, explicitly described simulator probes;
   fix concrete mismatches and replay the existing corpus for regressions.
2. Continue eligible training work on reviewed, pinned simulator versions.
   The trainer remains on its current pin until a separate reviewed pin
   update; these engine fixes do not silently alter existing training data.
3. Use stronger policies and selected routes to increase future capture
   depth and build variety. Keep ordinary/random breadth as well, so selected
   successful routes do not hide unrelated failures.
4. When live runs resume, deploy and record the repaired bridge deliberately.
   First use the existing partial guard route for facing/marker comparison;
   follow with targeted potion and Smoke Bomb checks, both guard-kill orders,
   and eventually Heart cap/buff/lethal/terminal cases.

These steps avoid making all provisional progress depend on an end-to-end
Heart victory. The final S3-G2 gate still requires its named live evidence;
S3-G1's existing capture-debt rows remain open unless separately discharged.

## Integration acceptance

The repair reports record the bridge's deterministic JDK 8 builds and the
simulator's Windows debug/ASan/release and WSL release builds. The existing
three-archive corpus passed all replay, cost and mask arms, with all injected
controls failing as required. After combining the repairs, the orchestrator
independently built `replay_run_diff` with `tools/win_build.cmd win-release
--target replay_run_diff --parallel 4` and replayed the existing
`s362_depth_STS511413_sim_search_keys_ps76.worker-001-of-001` capture.
It remained CLEAN across 780 compared records, with two attested key-animation
races and two post-boss handoffs. It stopped at artifact exhaustion, not a
terminal victory, and is not a new guard/Heart witness.

The integration build and replay outputs are preserved externally as
`_oracle_data/s3/source_audit_20260907/integrated_win_release_build.txt` and
`integrated_act4_prefix_replay.txt`. Documentation-link, stale-count and diff
whitespace checks also passed. No unit tests were run.
