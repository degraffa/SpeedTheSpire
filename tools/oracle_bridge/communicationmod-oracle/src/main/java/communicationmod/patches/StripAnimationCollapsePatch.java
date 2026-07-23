package communicationmod.patches;

import com.badlogic.gdx.backends.lwjgl.LwjglGraphics;
import com.evacipated.cardcrawl.modthespire.lib.SpirePatch;
import com.evacipated.cardcrawl.modthespire.lib.SpireReturn;
import communicationmod.CommunicationMod;

/**
 * SpeedTheSpire rendering-strip patch family 2 -- animation-time collapse
 * (stage-b-design §2.2, task B1.3).
 *
 * The game's frame-rate-independent timers all count down by
 * Gdx.graphics.getDeltaTime(): AbstractGameAction.tickDuration
 * (AbstractGameAction.java:74-79), the inline `duration -= getDeltaTime()` action
 * ticks (e.g. DrawCardAction), AbstractRoom.waitTimer (AbstractRoom.java:233) and
 * endBattleTimer (:279), the screen fade timers (AbstractDungeon.java:2311,2318),
 * and AbstractEvent.waitTimer (AbstractEvent.java:103). Inflating the delta these
 * timers read collapses EVERY one of them to complete on its next tick, in a
 * single well-audited chokepoint.
 *
 * In the LWJGL2 backend getDeltaTime() and getRawDeltaTime() are SEPARATE methods
 * that both read the private `deltaTime` field (LwjglGraphics.java:132-139). We
 * Prefix ONLY getDeltaTime(), so getRawDeltaTime() still returns the true frame
 * time -- the frame-skip guard `if (Gdx.graphics.getRawDeltaTime() > 0.1f) return`
 * (CardCrawlGame.java:362) keeps its stability role and is not tripped by this
 * patch.
 *
 * Mechanism -- a FIXED, SMALL, NON-ROUND TIMESTEP, not a one-frame leap:
 * getDeltaTime() returns a fixed STEP each frame (game-time advances STEP per
 * frame) while the frame rate is uncapped (fast-cadence family), so wall-clock
 * time per timer = timer_duration * STEP / real_frame_time collapses by the
 * frame-rate/STEP ratio -- a classic fast-forward (fixed timestep, many real
 * frames per game-second), NOT a single-frame skip.
 *
 * Two edge hazards a naive delta hits, and why STEP avoids both:
 *
 * (1) Leap-over. Some logic fires on an INTERMEDIATE countdown threshold, not
 *     just start/done. E.g. BattleStartEffect.update (vfx/combat/
 *     BattleStartEffect.java) calls MonsterGroup.showIntent() -- which sets each
 *     monster's dumped `intent` to move.intent (AbstractMonster.createIntent
 *     :408-409) -- only after its duration falls below 3.0 and its first
 *     sub-timer elapses. A huge delta (e.g. 100f) drives duration from >3 past
 *     done in ONE frame, skipping the branch, so `intent` never leaves its DEBUG
 *     default and the on/off dumps diverge. A small STEP steps THROUGH the window
 *     over many frames.
 *
 * (2) Exact-zero landing. Several state transitions fire on a strict `timer <
 *     0.0f` edge, e.g. AbstractEvent.update (:101-107) shows the event dialog
 *     options only on the frame waitTimer crosses from >0 to <0 (then pins
 *     waitTimer = 0.0). If STEP EVENLY DIVIDES the timer's start value, the timer
 *     lands on EXACTLY 0.0 and the `< 0.0f` branch never runs -- the options are
 *     never shown, yet waitTimer == 0.0 makes the fork report ready_for_command
 *     (GameStateListener :129). Neow's waitTimer starts at 1.5; a round STEP like
 *     0.05 (1.5/0.05 = 30) or 0.1 (15) lands exactly on 0.0 and hangs the event.
 *     A NON-ROUND STEP never evenly divides the round timer values the game uses
 *     (multiples of 0.05/0.1/0.25/0.5/1.0/1.5), so every countdown crosses zero
 *     into the negative and its edge fires.
 *
 * STEP = 0.043 satisfies both: < 0.05 so a 0.1s timer window still gets >= 2
 * frames (no leap-over of any gameplay-relevant window), and non-round so it
 * evenly divides none of the game's round timer start values (no exact-zero
 * landing). getRawDeltaTime() is left untouched (LwjglGraphics.java:132-139 --
 * separate methods over the same field), so the frame-skip guard
 * (CardCrawlGame.java:362, raw delta > 0.1s) keeps seeing the true tiny delta and
 * never trips.
 *
 * Removes time only -- never order or state:
 *  - Action logic edges fire in update() around tickDuration (start-value edge
 *    then isDone edge, e.g. DamageAction.java:69 then :80); intermediate
 *    presentation thresholds are stepped through, not skipped. No queued action
 *    (even a presentation WaitAction) is removed or reordered.
 *  - No seeded RNG advances per frame anywhere in the animation/VFX path
 *    (verified zero hits for all 13 dungeon streams across vfx/ and
 *    actions/animations/). Edge-triggered rolls (aiRng move rolls, the boss-gold
 *    miscRng.random(-5,5) on the endBattleTimer<0 edge, AbstractRoom.java:291)
 *    fire once on their timer-crossing edge regardless of how fast the timer
 *    crosses zero -- draw counts are unchanged.
 *  - Every `+= getDeltaTime()` accumulator audited cosmetic (card hover/alpha,
 *    intent angles, orb spin, playtime); none is dumped or branches game state,
 *    and dumps are taken at stable states where transient effects have expired.
 *
 * Proven by the B1.3 on/off byte-identical acceptance over 20 seeds.
 */
@SpirePatch(
        clz = LwjglGraphics.class,
        method = "getDeltaTime"
)
public class StripAnimationCollapsePatch {
    // Fixed game-time step per frame while stripping (seconds). Small (< 0.05 so
    // a 0.1s window still gets >= 2 frames) and NON-ROUND (evenly divides none of
    // the game's round timer values, so no countdown lands exactly on 0.0 and
    // skips a `< 0.0f` edge). See the class comment for both hazards. Uncapped FPS
    // turns the step/frame-time ratio into the wall-clock speedup.
    public static final float STEP = 0.043f;

    public static SpireReturn<Float> Prefix(LwjglGraphics __instance) {
        if (CommunicationMod.getStripAnimationCollapse()) {
            return SpireReturn.Return(STEP);
        }
        return SpireReturn.Continue();
    }
}
