# Red Skull `atBattleStart` — capture campaign, ABORTED IN FAVOUR OF THE JAVA

Campaign owner: capture-operations (claude01), 2026-07-28.
Branch: `redskull-capture`, cut from `master` at `e844173`.

**Outcome: NO GAME TIME SPENT. Zero runs, zero preflight, zero campaigns.**
The parked question turned out to be answerable from the Java after all — the
class the Wave-C commit called "an unavailable anonymous inner class in this
decompiled tree" is present, intact, in the shipped game jar. It is quoted in
full at §3. Standing task rule: *"if after reading you conclude the parked
question is actually answerable from the Java alone, STOP and report that
instead of burning game time."* This page is that report.

---

## 1. The parked question, in the wave's own words

From `git log wave-integrate` — commit `495b722`, "Red Skull: the relic body":

> The +3 on an already-bloodied ENTRY is OWNER-SPECIFIED (project owner,
> 2026-07-28), NOT derived: `RedSkull.atBattleStart`'s queued action
> (`RedSkull.java:38`) is an unavailable anonymous inner class in this
> decompiled tree, and the class file shows only that it reads/writes
> `isActive` (synthetic `access$000/002`, `:76-83`). Only the `addToBot` queue
> end is Java-pinned. PENDING ORACLE-CAPTURE VALIDATION; the ledger row stays
> open in that narrowed form, owned by the next capture-campaign owner.

And in `src/engine/relics/relics_common.cpp` at the site itself:

> THAT ACTION'S BODY IS NOT DECOMPILABLE HERE: `new /* Unavailable Anonymous
> Inner Class!! */` (`:38`), one of the 145 files with that CFR hole […] Its
> behaviour is therefore OWNER-SPECIFIED […] a Red Skull run capture is what
> turns this from a specification into evidence.

So the open question was exactly: **when a combat is entered with the player
already bloodied, does the action queued at `RedSkull.java:38` grant the +3
Strength, and where in the entry order does it decide?**

## 2. Why no capture was needed — provenance chain

The project's decompiled tree was produced from `D:\STS_BG_Mod\sts-classes.jar`.
That jar has **3376 entries and zero `com/megacrit` inner classes** — every
anonymous inner class was stripped before decompilation. CFR's *"Unavailable
Anonymous Inner Class"* is therefore an artefact of the **extraction**, not
evidence that the behaviour is undecompilable. Nothing was wrong with the
wave's reasoning given the tree it had; the tree was simply lossy.

The real, shipped jar is on this machine and is the **same build**:

| Artefact | Value |
| --- | --- |
| Game jar | `D:\SteamLibrary\steamapps\common\SlayTheSpire\desktop-1.0.jar` |
| Game jar SHA-256 | `cfad868ac8d65a88e71a0bf096fb09f78811e553effe0787c5309a655e081673` |
| Decompile-source jar | `D:\STS_BG_Mod\sts-classes.jar` (`2af3e7ef…d529b3c`) |
| `RedSkull.class` in **both** jars | `ea4d35bd87c3237d9387b56fbfee4512b2853a7319cd97ad0fcbf4999d9ee1b5` |
| `RedSkull$1.class` | present in game jar only, 1675 bytes, `2022-12-20` |
| Decompiler | `D:\STS_BG_Mod\cfr-0.152.jar` (the tree's own CFR 0.152) |

The outer class is **byte-identical** between the jar the project decompiled and
the jar the game runs, and the class dates (`2022-12-20`) match the runbook's
pinned `12-18-2022 [V2.3.4]`. So `RedSkull$1.class` out of the game jar is the
exact companion of the `RedSkull.java` in the tree — not a different build, not
a guess. This is stronger evidence than a capture would have been: a capture
observes one sampled path, this is the shipped instruction stream.

Recovery command (read-only; nothing was written into the repo or the
decompiled tree):

```sh
unzip -o -q desktop-1.0.jar 'com/megacrit/cardcrawl/relics/RedSkull*.class'
java -jar cfr-0.152.jar 'com/megacrit/cardcrawl/relics/RedSkull$1.class' \
     --extraclasspath desktop-1.0.jar
```

## 3. THE EVIDENCE — the body of the action at `RedSkull.java:38`

```java
class RedSkull$1 extends AbstractGameAction {
    @Override
    public void update() {
        if (!RedSkull.this.isActive && AbstractDungeon.player.isBloodied) {
            RedSkull.this.flash();
            RedSkull.this.pulse = true;
            AbstractDungeon.player.addPower(new StrengthPower(AbstractDungeon.player, 3));
            this.addToTop(new RelicAboveCreatureAction(AbstractDungeon.player, RedSkull.this));
            RedSkull.this.isActive = true;
            AbstractDungeon.onModifyPower();
        }
        this.isDone = true;
    }
}
```

Cross-checked against `javap -p -c` on the same class file; the decompile is
faithful. Load-bearing bytecode: the guard is `access$000` → `ifne` then
`AbstractPlayer.isBloodied` → `ifeq` (offsets 0–16); the grant is
`invokevirtual AbstractPlayer.addPower` (offset 48) — **not** an
`ApplyPowerAction`; the latch is `access$002(…, true)` (offset 74);
`AbstractDungeon.onModifyPower()` at 78; `isDone = true` at 81–83, **outside**
the `if`.

## 4. VERDICT

**The owner spec was CORRECT on the outcome.** Entering a combat already
bloodied *does* grant +3 Strength, and it does latch `isActive`.

Four things the Java now pins that were previously unpinned, and one correction:

1. **Guard is `!isActive && player.isBloodied`** — the same "not already
   standing" shape as `onBloodied`, so the no-double-fire property the wave
   tested (`RedSkullDoesNotFireTwiceWhenCombatBeganBloodied`) is real.
2. **No `RoomPhase.COMBAT` clause.** Unlike `onBloodied` (`:45`) and
   `onNotBloodied` (`:56`), the entry action carries no phase guard — it does
   not need one, being queued into a combat action queue by construction.
3. **The grant is a DIRECT `player.addPower(…)`, not an `ApplyPowerAction`.**
   It does not pass the `ApplyPowerAction` door at all. (Artifact is a non-issue
   for it regardless — Artifact blocks debuffs, and +3 is a buff — so the
   wave's Artifact derivation for the −3 stands untouched.)
4. **ENTRY ORDER — the part that was most open, and the interesting answer.**
   `atBattleStart` clears `isActive` *synchronously* during the relic fan-out,
   then `addToBot`s the deciding action. So **the entry grant is decided at the
   very END of the battle-start action drain, and it re-reads
   `player.isBloodied` at that moment** — not at relic-hook dispatch time. This
   is deliberate: both battle-start healers, Blood Vial (`:33`) and Pantograph
   (`:36`), queue their `HealAction` with **`addToTop`**, so they always resolve
   *before* Red Skull's bottom-queued decision, order-independently of relic
   order. Red Skull therefore observes **settled, post-battle-start-healing HP**.
5. Presentation only, no engine consequence: `flash()`, `pulse`,
   `RelicAboveCreatureAction`, and `AbstractDungeon.onModifyPower()` (where the
   other two hooks use `player.hand.applyPowers()`).

## 5. Does the landed implementation match? — OUTCOME YES, MECHANISM NO

`relics_common.cpp`, `relic_native_red_skull`, `AT_BATTLE_START`:

```cpp
const bool bloodied = player_is_bloodied(s);
slot.counter = bloodied ? 1 : 0;
if (bloodied) {
    queue_red_skull_strength(s, 3, /*to_top=*/false);
}
```

This **decides at queue time**. The Java **decides at run time**, at the bottom
of the drain, re-testing both conjuncts. In the common case (nothing heals at
battle start) the two are indistinguishable and the landed code is right. They
come apart whenever a battle-start effect crosses the bloodied threshold before
the queue drains — which is exactly what Blood Vial and Pantograph do, and they
`addToTop`, so they *always* land in that window.

Worked case — Red Skull + Blood Vial, entering at exactly half (e.g. 30/60):

| Step | Java | Landed engine |
| --- | --- | --- |
| relic fan-out | `isActive = false`; decision action → bottom | `counter = 1`; `+3` APPLY_POWER → bottom |
| Blood Vial heal (top) | 30→32, crosses above half | 30→32, crosses above half |
| `onNotBloodied` | guard `isActive` is **false** → no −3 | `counter == 1` → queues **−3** to top, clears latch |
| decision action runs | `isBloodied` now **false** → **no grant** | (already committed at queue time) |
| **final Strength** | **0** — no Strength power ever applied | **0**, but via a spurious −3 then +3 |

Same net without Artifact, but not the same behaviour:

- **With Artifact the final states differ.** The −3 is a debuff, so by the
  wave's own `apply_power_blocked_by_artifact` analysis Artifact eats it and the
  +3 still lands: **engine ends at +3 Strength where the game ends at 0**, and
  it spuriously burns an Artifact charge the game never spends.
- Even without Artifact, the engine transiently sits at −3 Strength mid-drain
  and fires two APPLY_POWER events where the game fires none.
- Pantograph's heal is 25 HP in boss rooms, so the crossing is not a knife-edge
  corner there — it is the common case for a bloodied boss entry.

**This is a stop-the-line item for the orchestrator.** The fix is small and
local — make `AT_BATTLE_START` queue a *deciding* action to the bottom that
re-tests `player_is_bloodied(s)` and `counter == 0` when it runs, rather than
resolving the branch during the hook — but it is engine code and this campaign
is read-only on it, so it is filed, not applied. Note also that the landed
`slot.counter = 1` is set *before* the grant resolves, whereas the Java sets
`isActive` only inside the action; folding the decision into the queued action
fixes both halves at once.

Not affected, and confirmed still correct: the whole `onNotBloodied` −3 path,
the cumulative-deltas conclusion, the `player_is_bloodied` half-rounding
(`hp*2 <= max`), and `b4faf1c`'s damage-side suppression.

`fix-battlestart-order` (`a03f257`) was checked and is unrelated — it branched
from `465258a`, before the Red Skull commit, and touches no relic file.

## 6. Reusable finding for the project — the CFR holes are recoverable

The decompiled tree reports 145 files / 429 `Unavailable Anonymous Inner Class`
sites, but only **16 files / 37 sites are `com/megacrit`** game logic (the rest
are libGDX and other vendored packages). All 16 were spot-checked against the
game jar and their inner classes **are present** and decompilable by the same
two commands in §2:

```
cards/Soul            core/CardCrawlGame     daily/TimeLookup
helpers/SaveHelper    integrations/SteelSeries  localization/LocalizedStrings
metrics/BotDataUploader  metrics/Metrics     monsters/city/Mugger
monsters/exordium/Looter  powers/MayhemPower  relics/RedSkull
relics/deprecated/DEPRECATEDDodecahedron     rooms/AbstractRoom
screens/CardRewardScreen  screens/select/BossRelicSelectScreen
```

Gameplay-relevant among these: `Mugger`, `Looter`, `MayhemPower`,
`AbstractRoom`, `Soul`. **Recommendation:** before any future obligation is
parked as "undecompilable, needs capture", check the shipped jar first — and
consider re-extracting `sts-classes.jar` with inner classes retained so the tree
stops producing false "needs capture" rows. Any *other* row currently parked on
this premise should be re-examined on the same grounds.

## 7. Artefacts

- Recovered class files + decompile (scratchpad, outside the repo):
  `C:\Users\Alex\AppData\Local\Temp\claude\C--Users-Alex\3a4bfd38-eaa1-4710-90bc-0e3773d1c88d\scratchpad\rs\`
- No campaign directories were created; no oracle runs were executed; the
  driver, orchestrator and `seed_scan` were left untouched. The `seed_scan`
  release binary was confirmed to exist at
  `build/release/tools/oracle_bridge/planner/seed_scan` but was not needed.
- Nothing in the repo working tree, the engine tree, or any `wt-*` / `wave-*`
  worktree was modified. This page is the only commit.

## 8. Errors found

- `relics_common.cpp` `AT_BATTLE_START` decides the entry grant at queue time
  rather than at drain time (§5) — **class A, stop-the-line**, engine-owner fix.
- The registry/commit provenance for `RED_SKULL` should be downgraded from
  "OWNER-SPECIFIED, pending oracle-capture validation" to **Java-derived**, with
  the `RedSkull$1` citation from §3. The ledger row's capture obligation is
  **discharged without a capture** and can be closed on this evidence.
