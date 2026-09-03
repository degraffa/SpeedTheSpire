package communicationmod.patches;

import com.evacipated.cardcrawl.modthespire.lib.SpireInstrumentPatch;
import com.evacipated.cardcrawl.modthespire.lib.SpirePatch;
import com.megacrit.cardcrawl.cards.AbstractCard;
import com.megacrit.cardcrawl.cards.CardGroup;
import com.megacrit.cardcrawl.core.Settings;
import com.megacrit.cardcrawl.dungeons.AbstractDungeon;
import com.megacrit.cardcrawl.random.Random;
import com.megacrit.cardcrawl.shop.ShopScreen;
import communicationmod.CommunicationMod;
import javassist.CannotCompileException;
import javassist.expr.ExprEditor;
import javassist.expr.MethodCall;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import java.util.ArrayList;
import java.util.Collections;

/**
 * SpeedTheSpire oracle-contract patch -- seed the identity of the card The
 * Courier restocks into a colored shop slot (S3.24, 2026-09-03).
 *
 * THE PROBLEM. With The Courier owned, buying a colored card does not retire
 * its slot; the merchant replaces the card in place
 * (ShopScreen.purchaseCard:598-643). Every half of that replacement is seeded
 * except one:
 *
 * <pre>
 *     AbstractCard c = AbstractDungeon.getCardFromPool(
 *             AbstractDungeon.rollRarity(), hoveredCard.type, false).makeCopy();
 *                                                            ^^^^^
 *                                            ShopScreen.java:615-617
 * </pre>
 *
 * {@code rollRarity()} is one seeded {@code cardRng.random(99)}. The
 * {@code useRng = false} argument, however, sends
 * {@code CardGroup.getRandomCard(CardType, boolean)} (CardGroup.java:540-553)
 * down its {@code MathUtils.random(tmp.size() - 1)} branch -- libGDX's GLOBAL
 * RandomXS128, seeded from JVM-startup entropy, whose stream position also
 * advances with rendering and VFX draws. The restocked card's identity is
 * therefore not a function of {@code (seed, actions)} in PRINCIPLE, not merely
 * in practice: no simulator can reproduce it, and no capture of it can be
 * diffed against one. It was the last such value in scope, and the simulator's
 * answer since S1 was a named refusal -- the slot restocked as an unnameable
 * sentinel that was kept off the legal-action mask.
 *
 * THE FIX, AND WHY IT LIVES IN THE FORK. **The contract is the patched fork,
 * not the retail client** -- the established shape here (precedent: the
 * Discovery wasted-regens boundary, the Explosive-Potion THORNS boundary, and
 * OraclePlaytimePinPatch's SecretPortal wall-clock pin; PROTOCOL.md 5.4). Where
 * a retail behaviour is not a function of {@code (seed, actions)}, the ORACLE is
 * what moves, and the capture is scored against the fork. So this patch replaces
 * the {@code getCardFromPool} CALL inside {@code purchaseCard} -- and only that
 * call -- with {@link #restockCardFromPool}, which reproduces
 * {@code getCardFromPool}'s pool walk exactly and takes the one index draw from
 * a dedicated seeded stream instead of {@code MathUtils.random}.
 *
 * THE STREAM, which the simulator constructs identically
 * (shop.hpp's {@code courier_restock_stream}, src/engine/shop.cpp's colored
 * restock branch):
 *
 * <pre>
 *     new Random(Settings.seed + 1000003L + AbstractDungeon.cardRng.counter)
 * </pre>
 *
 * then ONE {@code random(size - 1)} on it. Three properties make that the right
 * derivation:
 * <ul>
 * <li><b>Both sides can compute it.</b> The counter is read AFTER the restock's
 * own {@code rollRarity()} draw -- Java evaluates the argument before the
 * invocation, so by the time this helper runs {@code cardRng.counter} has
 * already been bumped, which is exactly where the simulator reads it.</li>
 * <li><b>Successive restocks never repeat.</b> Every colored restock spends one
 * {@code rollRarity} draw on {@code cardRng} first, so the counter strictly
 * increases between them; and since S3.24 a restocked slot is buyable, so a slot
 * can restock repeatedly within one visit.</li>
 * <li><b>It cannot alias another derived stream.</b> The game's own derived
 * streams sit at {@code seed + floorNum} (0..~60) and
 * {@code seed + act offset} ({1, 200, 600, 1200}); 1000003 -- the smallest prime
 * above 10^6 -- is past both for any counter a run reaches.
 * {@code RandomXS128}'s constructor murmur-scrambles the seed, so adjacent
 * counters give uncorrelated streams.</li>
 * </ul>
 *
 * The offset is an arbitrary but FROZEN constant, matching
 * {@code kCourierRestockSeedOffset} in shop.hpp: changing it changes every
 * restocked identity on both sides at once.
 *
 * DISTRIBUTIONALLY EXACT. Retail draws uniformly over the type-filtered,
 * {@code Collections.sort}ed rarity view; so does this. Same view, same
 * inclusive {@code random(size - 1)} bound, same "an empty view returns null
 * before it indexes anything, and therefore costs no draw" rule
 * (CardGroup.java:545-547) -- only the source of the index differs.
 *
 * BLAST RADIUS: one call site, or rather the two the same source line pair
 * spells. {@code purchaseCard}'s colored branch calls {@code getCardFromPool}
 * twice -- once at :615 and once inside the {@code while (c.color == COLORLESS)}
 * re-roll guard at :616-617 -- and the instrument patch replaces both, which is
 * correct: they are the same draw. (The guard is dead for the Ironclad; the
 * three RED rarity pools hold no colourless row, which is a property of
 * {@code CardLibrary.addRedCards}, CardLibrary.java:1152-1161.) NOTHING else is
 * touched: the colorless branch's {@code getColorlessCardFromPool} was already
 * seeded on {@code cardRng} and is not matched by name; every OTHER
 * {@code getCardFromPool} caller in the game -- card rewards, Transmute-style
 * effects, events -- is outside {@code purchaseCard} and outside this patch's
 * scope, so they keep retail's behaviour exactly.
 *
 * NO STORED STREAM MOVES. The restock stream is constructed at the draw and
 * discarded; {@code cardRng}, {@code merchantRng} and {@code potionRng} advance
 * per restock exactly as the wave2cap_courier_* campaign measured them (colored
 * = cardRng +1 / merchantRng +1). A capture taken before this patch and one
 * taken after therefore differ in the restocked card's IDENTITY and in nothing
 * else about the RNG accounting.
 *
 * THE FLAG. {@code oracleCourierRestockSeed} in the fork's
 * {@code SpireConfig("CommunicationMod", "config")} store, default TRUE, also a
 * mod-settings toggle. With it FALSE the helper defers straight to
 * {@code AbstractDungeon.getCardFromPool} with the original {@code useRng}, i.e.
 * retail behaviour bit for bit -- so it, like {@code oraclePlaytimePin} and the
 * three strip flags, must be off to reproduce the stock-equivalence baseline.
 */
@SpirePatch(
        clz = ShopScreen.class,
        method = "purchaseCard",
        paramtypez = {AbstractCard.class}
)
public class CourierRestockSeedPatch {
    private static final Logger logger =
            LogManager.getLogger(CourierRestockSeedPatch.class.getName());

    /**
     * The frozen seed offset, identical to shop.hpp's
     * {@code kCourierRestockSeedOffset}. Do not change it without changing both
     * sides in the same commit.
     */
    public static final long SEED_OFFSET = 1000003L;

    // Fully-qualified name as a STRING LITERAL on purpose, matching
    // OraclePlaytimePinPatch: the editor runs during ModTheSpire's patching
    // pass, and a `.class.getName()` would pull the class into the loader
    // mid-pass.
    private static final String ABSTRACT_DUNGEON =
            "com.megacrit.cardcrawl.dungeons.AbstractDungeon";
    private static final String GET_CARD_FROM_POOL = "getCardFromPool";

    /**
     * How many calls the editor actually replaced. Exactly 2 is expected (the
     * :615 draw and the dead colourless re-roll guard's). Logged at patch time
     * so a game-version drift that silently matched nothing is visible in
     * {@code mts_launch<N>.log} rather than only in a divergent capture.
     */
    public static int instrumentedCalls = 0;

    /**
     * The seeded stream one Courier restock indexes its pool with. Constructed
     * at the draw and discarded -- it is not one of the game's streams, is never
     * stored, and never appears in a save file or in the {@code oracle.streams}
     * block.
     *
     * @return a fresh {@code Random(Settings.seed + SEED_OFFSET +
     *         cardRng.counter)}, counter 0.
     */
    public static Random restockRng() {
        long seed = Settings.seed == null ? 0L : Settings.seed;
        long counter = AbstractDungeon.cardRng == null
                ? 0L
                : (long) AbstractDungeon.cardRng.counter;
        return new Random(seed + SEED_OFFSET + counter);
    }

    /**
     * The replacement for {@code AbstractDungeon.getCardFromPool(rarity, type,
     * useRng)} at ShopScreen.java:615-617.
     *
     * @param rarity the rarity {@code rollRarity()} just rolled (that draw has
     *               already been spent on {@code cardRng} by the time this runs)
     * @param type   the replaced card's type, so the slot's type is invariant
     * @param useRng retail's third argument, passed through so the flag-off path
     *               is retail exactly
     */
    public static AbstractCard restockCardFromPool(AbstractCard.CardRarity rarity,
                                                   AbstractCard.CardType type,
                                                   boolean useRng) {
        if (!CommunicationMod.getOracleCourierRestockSeed()) {
            return AbstractDungeon.getCardFromPool(rarity, type, useRng);
        }
        return getCardFromPoolSeeded(rarity, type, restockRng());
    }

    /**
     * {@code CardGroup.getRandomCard(CardType, boolean)} (CardGroup.java:540-553)
     * with the index taken from {@code rng} instead of {@code MathUtils.random}
     * / {@code cardRng}. The empty-view early return is preserved exactly,
     * because it is what makes an empty view cost NO draw -- the property the
     * simulator's pool walk shares.
     */
    private static AbstractCard randomCardOfType(CardGroup pool,
                                                 AbstractCard.CardType type,
                                                 Random rng) {
        if (pool == null) {
            return null;
        }
        ArrayList<AbstractCard> tmp = new ArrayList<AbstractCard>();
        for (AbstractCard c : pool.group) {
            if (c.type != type) {
                continue;
            }
            tmp.add(c);
        }
        if (tmp.isEmpty()) {
            return null;
        }
        Collections.sort(tmp);
        return tmp.get(rng.random(tmp.size() - 1));
    }

    /**
     * {@code AbstractDungeon.getCardFromPool} (AbstractDungeon.java:1538-1576)
     * re-expressed with an explicit {@code rng}. The decompiled switch has NO
     * {@code break} after its error logs, so a null RARE falls through to the
     * UNCOMMON case, a null UNCOMMON to the COMMON case, and a null COMMON to
     * the CURSE case -- downwards -- while a POWER type instead RECURSES upwards
     * ({@code getCardFromPool(UNCOMMON/RARE, POWER, useRng)}). Both directions
     * are reproduced here rather than simplified away.
     *
     * <p>For the Ironclad the walk terminates on the first view every time
     * except COMMON x POWER (an empty view: the Ironclad has no common power),
     * which recurses once to UNCOMMON. That is asserted statically on the
     * simulator side (src/engine/shop.cpp's pool static_asserts).
     */
    public static AbstractCard getCardFromPoolSeeded(AbstractCard.CardRarity rarity,
                                                     AbstractCard.CardType type,
                                                     Random rng) {
        AbstractCard retVal;
        if (rarity == AbstractCard.CardRarity.RARE) {
            retVal = randomCardOfType(AbstractDungeon.rareCardPool, type, rng);
            if (retVal != null) {
                return retVal;
            }
            logger.info("ERROR: Could not find Rare card of type: " + type.name());
            rarity = AbstractCard.CardRarity.UNCOMMON;  // fallthrough, no break
        }
        if (rarity == AbstractCard.CardRarity.UNCOMMON) {
            retVal = randomCardOfType(AbstractDungeon.uncommonCardPool, type, rng);
            if (retVal != null) {
                return retVal;
            }
            if (type == AbstractCard.CardType.POWER) {
                return getCardFromPoolSeeded(AbstractCard.CardRarity.RARE, type, rng);
            }
            logger.info("ERROR: Could not find Uncommon card of type: " + type.name());
            rarity = AbstractCard.CardRarity.COMMON;    // fallthrough, no break
        }
        if (rarity == AbstractCard.CardRarity.COMMON) {
            retVal = randomCardOfType(AbstractDungeon.commonCardPool, type, rng);
            if (retVal != null) {
                return retVal;
            }
            if (type == AbstractCard.CardType.POWER) {
                return getCardFromPoolSeeded(AbstractCard.CardRarity.UNCOMMON, type, rng);
            }
            logger.info("ERROR: Could not find Common card of type: " + type.name());
            rarity = AbstractCard.CardRarity.CURSE;     // fallthrough, no break
        }
        if (rarity == AbstractCard.CardRarity.CURSE) {
            retVal = randomCardOfType(AbstractDungeon.curseCardPool, type, rng);
            if (retVal != null) {
                return retVal;
            }
            logger.info("ERROR: Could not find Curse card of type: " + type.name());
        }
        logger.info("ERROR: Default in getCardFromPool");
        return null;
    }

    @SpireInstrumentPatch
    public static ExprEditor Instrument() {
        return new ExprEditor() {
            @Override
            public void edit(MethodCall m) throws CannotCompileException {
                if (!ABSTRACT_DUNGEON.equals(m.getClassName())) {
                    return;
                }
                if (!GET_CARD_FROM_POOL.equals(m.getMethodName())) {
                    return;
                }
                m.replace("$_ = communicationmod.patches.CourierRestockSeedPatch"
                        + ".restockCardFromPool($1, $2, $3);");
                ++instrumentedCalls;
                logger.info("oracle Courier restock seed: replaced "
                        + "AbstractDungeon.getCardFromPool call #" + instrumentedCalls
                        + " in ShopScreen.purchaseCard (the restocked colored "
                        + "identity, ShopScreen.java:615-617)");
            }
        };
    }
}
