package communicationmod.patches;

import com.evacipated.cardcrawl.modthespire.lib.SpireInstrumentPatch;
import com.evacipated.cardcrawl.modthespire.lib.SpirePatch;
import com.megacrit.cardcrawl.core.CardCrawlGame;
import com.megacrit.cardcrawl.dungeons.AbstractDungeon;
import com.megacrit.cardcrawl.random.Random;
import communicationmod.CommunicationMod;
import javassist.CannotCompileException;
import javassist.expr.ExprEditor;
import javassist.expr.FieldAccess;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

/**
 * SpeedTheSpire oracle-contract patch -- pin the wall clock the SecretPortal
 * shrine gate reads (s2-design §5 trap 5; S2.43, 2026-08-27).
 *
 * THE PROBLEM. AbstractDungeon.getShrine builds its candidate list `tmp` and
 * then draws `tmp.get(rng.random(tmp.size() - 1))` (AbstractDungeon.java:1937).
 * One candidate, SecretPortal, is gated on WALL CLOCK:
 *
 *     case "SecretPortal":
 *         if (!(CardCrawlGame.playtime >= 800.0f) || !id.equals("TheBeyond"))
 *             continue block22;                       (AbstractDungeon.java:1929-1933)
 *
 * The simulator pins that predicate FALSE (event_framework.hpp's PLAYTIME
 * block): wall clock is not a function of (seed, actions), and determinism in
 * (seed, actions) is the property the whole verification story rests on. But an
 * omitted candidate does not merely go unseen -- it SHORTENS `tmp` and moves the
 * drawn INDEX, so past 800 s of live play EVERY Act-3 `?` room resolves to a
 * different event than the sim's script expects. Two live witnesses:
 * STS108107 at playtime 924.34705 s (game index 13 of 14 = The Woman in Blue,
 * sim index 5 of 13 = Upgrade Shrine) and STS153269 at 960.92236 s (game index
 * 10 of 15 = Fountain of Cleansing, sim index 8 of 14 = Designer).
 *
 * THE FIX, AND WHY IT LIVES IN THE FORK. The replay side already consumes the
 * capture's `oracle.playtime` and scores either clock correctly. What it cannot
 * fix is the SCRIPTED direction: a script the sim emits is computed at playtime
 * 0, so a live capture that crosses 800 s desyncs from its own script at the
 * next Act-3 `?` room, mid-run, and every later record is noise. So the ORACLE
 * is what moves: this patch replaces the ONE `CardCrawlGame.playtime` read
 * inside getShrine with {@link #effectivePlaytime()}, which returns a pinned
 * 0.0f while the `oraclePlaytimePin` config flag is on. The gate is then shut
 * for the whole run, exactly as the simulator has it, and scripted deep
 * captures stay reproducible at any depth. This is the established
 * oracle-contract shape -- **the contract is the patched fork, not the retail
 * client** (precedent: the Discovery wasted-regens boundary and the
 * Explosive-Potion THORNS boundary; see PROTOCOL.md §5.4).
 *
 * WHY AN INSTRUMENT PATCH AND NOT A ZEROED ACCUMULATOR. `CardCrawlGame.playtime`
 * accumulates in AbstractDungeon.update (:2001) and is read by eight other call
 * sites -- the save file, metrics, the death/victory/game-over screens, the
 * SPEED_CLIMBER achievement (AbstractMonster.java:1063), the main-menu save
 * slot, the run-history screen. Holding the FIELD at zero would silently change
 * all of them. Replacing the single getstatic inside getShrine changes the gate
 * and nothing else: every other reader still sees the true wall clock. The
 * audit of all nine readers is in PROTOCOL.md §5.4.
 *
 * THE ANCHOR STAYS TRUTHFUL. `oracle.playtime` (PROTOCOL §5.1) is emitted from
 * this same {@link #effectivePlaytime()} helper (GameStateConverter
 * .getOracleState), so the capture records the EFFECTIVE value the gate saw --
 * 0.0f under the pin -- not a wall clock the gate never consulted. Gate and
 * anchor cannot disagree because they are one function. `--replay` hands the
 * recorded value to `RunController::playtime_seconds`, so the replay's gate
 * input remains exactly the game's gate input.
 *
 * THE FLAG. `oraclePlaytimePin` in the fork's SpireConfig store, default TRUE,
 * also a mod-settings toggle. With it FALSE the gate and the anchor both see
 * the real `CardCrawlGame.playtime` again, i.e. the pre-2026-08-27 fork
 * behaviour bit for bit -- so it, and not only the three strip flags, must be
 * off to reproduce the strip-equivalence / stock-equivalence baseline.
 */
@SpirePatch(
        clz = AbstractDungeon.class,
        method = "getShrine",
        paramtypez = {Random.class}
)
public class OraclePlaytimePinPatch {
    private static final Logger logger =
            LogManager.getLogger(OraclePlaytimePinPatch.class.getName());

    // The pinned value handed to the gate. 0.0f is the simulator's own
    // kUnmodelledPlaytimeSeconds (event_framework.hpp); anything < 800.0f would
    // shut the gate, but matching the sim's constant keeps the two sides
    // literally equal rather than merely equivalent.
    public static final float PINNED_PLAYTIME = 0.0f;

    // Fully-qualified names as STRING LITERALS on purpose: the editor runs
    // during ModTheSpire's patching pass, and `CardCrawlGame.class.getName()`
    // would pull the very class being patched into the loader mid-pass.
    private static final String CARD_CRAWL_GAME = "com.megacrit.cardcrawl.core.CardCrawlGame";
    private static final String PLAYTIME_FIELD = "playtime";

    // How many reads the editor actually replaced. Exactly 1 is expected
    // (AbstractDungeon.java:1930 is the only playtime read in getShrine); the
    // count is logged at patch time so a game-version drift that silently
    // matched nothing is visible in the launch log rather than only in a
    // divergent capture.
    public static int instrumentedReads = 0;

    /**
     * The wall clock the SecretPortal gate -- and the `oracle.playtime` anchor
     * that records what the gate saw -- are allowed to see.
     *
     * @return 0.0f while the pin is on; the true CardCrawlGame.playtime when it
     *         is off.
     */
    public static float effectivePlaytime() {
        return CommunicationMod.getOraclePlaytimePin()
                ? PINNED_PLAYTIME
                : CardCrawlGame.playtime;
    }

    @SpireInstrumentPatch
    public static ExprEditor Instrument() {
        return new ExprEditor() {
            @Override
            public void edit(FieldAccess f) throws CannotCompileException {
                if (!f.isReader()) {
                    return;
                }
                if (!CARD_CRAWL_GAME.equals(f.getClassName())) {
                    return;
                }
                if (!PLAYTIME_FIELD.equals(f.getFieldName())) {
                    return;
                }
                f.replace("$_ = communicationmod.patches.OraclePlaytimePinPatch"
                        + ".effectivePlaytime();");
                ++instrumentedReads;
                logger.info("oracle playtime pin: replaced CardCrawlGame.playtime "
                        + "read #" + instrumentedReads
                        + " in AbstractDungeon.getShrine (the SecretPortal gate, "
                        + "AbstractDungeon.java:1930)");
            }
        };
    }
}
