package communicationmod.patches;

import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.evacipated.cardcrawl.modthespire.lib.SpirePatch;
import com.evacipated.cardcrawl.modthespire.lib.SpireReturn;
import com.megacrit.cardcrawl.dungeons.AbstractDungeon;
import communicationmod.CommunicationMod;

/**
 * SpeedTheSpire rendering-strip patch family 1 -- draw suppression
 * (stage-b-design §2.2, task B1.3).
 *
 * Prefix-return the whole dungeon draw body (AbstractDungeon.render(SpriteBatch),
 * AbstractDungeon.java:2153 in the 11-30-2020 decompile) when the
 * `stripDrawSuppression` config flag is on. This removes essentially all
 * combat/room/effect pixel cost in one seam.
 *
 * Why this seam and not CardCrawlGame.render: CardCrawlGame.render()
 * (CardCrawlGame.java:359) calls this.update() at :368 -- a Prefix-return there
 * would freeze all game logic. AbstractDungeon.render is pure iteration + draw:
 * scene bg, effectList draw loops, currMapNode.room.render, overlayMenu.render,
 * and the per-screen render switch (AbstractDungeon.java:2153-2227). The GL
 * surface stays alive because CardCrawlGame.render keeps sb.begin()/end() and
 * glClear (CardCrawlGame.java:371-372,426) around the suppressed body.
 *
 * Input-safe: CommunicationMod injects via update() paths (hb.clicked latches /
 * InputActionPatch), never by reading rendered geometry; hover/click state is
 * set in Hitbox.update, not Hitbox.render (Hitbox.java:40-67 vs :122-131). So
 * suppressing the draw touches no logic and no input.
 *
 * Removes pixels only -- never order or state. Proven by the B1.3 on/off
 * byte-identical oracle-dump acceptance over 20 seeds.
 */
@SpirePatch(
        clz = AbstractDungeon.class,
        method = "render"
)
public class StripDrawSuppressionPatch {
    public static SpireReturn<Void> Prefix(AbstractDungeon __instance, SpriteBatch sb) {
        if (CommunicationMod.getStripDrawSuppression()) {
            return SpireReturn.Return(null);
        }
        return SpireReturn.Continue();
    }
}
