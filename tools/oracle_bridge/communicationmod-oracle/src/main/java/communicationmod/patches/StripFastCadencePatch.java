package communicationmod.patches;

import com.badlogic.gdx.backends.lwjgl.LwjglApplicationConfiguration;
import com.evacipated.cardcrawl.modthespire.lib.SpireConfig;
import com.evacipated.cardcrawl.modthespire.lib.SpirePatch;
import com.megacrit.cardcrawl.desktop.DesktopLauncher;
import communicationmod.CommunicationMod;

import java.io.IOException;
import java.util.Properties;

/**
 * SpeedTheSpire rendering-strip patch family 3 -- fast update cadence
 * (stage-b-design §2.2, task B1.3).
 *
 * The game caps the frame rate at construction:
 * `config.backgroundFPS = config.foregroundFPS = displayConf.getMaxFPS()` and
 * `config.vSyncEnabled = displayConf.getIsVsync()` (DesktopLauncher.java:118,145).
 * Since CardCrawlGame.update() runs once per surviving render() frame
 * (CardCrawlGame.java:368), uncapping the frame rate lets update() -- and the
 * action-queue drain -- tick as fast as the CPU allows. Combined with animation
 * collapse (each action resolves in one tick) this is what clears the ≥5
 * actions/sec throughput floor; on its own it changes no state and no RNG.
 *
 * Postfix loadSettings (runs after the game reads DisplayConfig, before
 * LwjglApplication is constructed at DesktopLauncher.java:98) and, when the
 * `stripFastCadence` flag is on, set foregroundFPS = backgroundFPS = 0 (LWJGL2:
 * 0 == uncapped) and vSyncEnabled = false. backgroundFPS matters: unattended
 * campaign windows run unfocused and LWJGL2 throttles to backgroundFPS when
 * unfocused, which would silently collapse throughput.
 *
 * This patch runs during DesktopLauncher.main, BEFORE this mod's constructor,
 * so it cannot use CommunicationMod's live SpireConfig (not yet created). It
 * reads the same "CommunicationMod/config" store through its own SpireConfig
 * instance (read-only -- it never saves, so it cannot clobber keys the mod's own
 * config writes later).
 */
@SpirePatch(
        clz = DesktopLauncher.class,
        method = "loadSettings",
        paramtypez = {LwjglApplicationConfiguration.class}
)
public class StripFastCadencePatch {
    public static void Postfix(LwjglApplicationConfiguration config) {
        if (readCadenceFlag()) {
            config.foregroundFPS = 0;
            config.backgroundFPS = 0;
            config.vSyncEnabled = false;
        }
    }

    private static boolean readCadenceFlag() {
        try {
            Properties defaults = new Properties();
            defaults.put(CommunicationMod.STRIP_CADENCE_OPTION,
                    Boolean.toString(CommunicationMod.DEFAULT_STRIP));
            SpireConfig cfg = new SpireConfig("CommunicationMod", "config", defaults);
            return cfg.getBool(CommunicationMod.STRIP_CADENCE_OPTION);
        } catch (IOException e) {
            e.printStackTrace();
            return CommunicationMod.DEFAULT_STRIP;
        }
    }
}
