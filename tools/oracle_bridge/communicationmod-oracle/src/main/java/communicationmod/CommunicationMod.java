package communicationmod;

import basemod.*;
import basemod.interfaces.PostDungeonUpdateSubscriber;
import basemod.interfaces.PostInitializeSubscriber;
import basemod.interfaces.PostUpdateSubscriber;
import basemod.interfaces.PreUpdateSubscriber;
import com.evacipated.cardcrawl.modthespire.lib.SpireConfig;
import com.evacipated.cardcrawl.modthespire.lib.SpireInitializer;
import com.google.gson.Gson;
import com.megacrit.cardcrawl.core.Settings;
import com.megacrit.cardcrawl.dungeons.AbstractDungeon;
import com.megacrit.cardcrawl.helpers.FontHelper;
import com.megacrit.cardcrawl.helpers.ImageMaster;
import communicationmod.patches.InputActionPatch;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import java.io.File;
import java.io.IOException;
import java.lang.ProcessBuilder;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Properties;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

@SpireInitializer
public class CommunicationMod implements PostInitializeSubscriber, PostUpdateSubscriber, PostDungeonUpdateSubscriber, PreUpdateSubscriber, OnStateChangeSubscriber {

    private static Process listener;
    private static StringBuilder inputBuffer = new StringBuilder();
    public static boolean messageReceived = false;
    private static final Logger logger = LogManager.getLogger(CommunicationMod.class.getName());
    private static Thread writeThread;
    private static BlockingQueue<String> writeQueue;
    private static Thread readThread;
    private static BlockingQueue<String> readQueue;
    private static final String MODNAME = "Communication Mod";
    private static final String AUTHOR = "Forgotten Arbiter";
    private static final String DESCRIPTION = "This mod communicates with an external program to play Slay the Spire.";
    public static boolean mustSendGameState = false;
    private static ArrayList<OnStateChangeSubscriber> onStateChangeSubscribers;

    private static SpireConfig communicationConfig;
    private static final String COMMAND_OPTION = "command";
    private static final String GAME_START_OPTION = "runAtGameStart";
    private static final String VERBOSE_OPTION = "verbose";
    private static final String INITIALIZATION_TIMEOUT_OPTION = "maxInitializationTimeout";
    // SpeedTheSpire fork addition (stage-b-design §2.5): gate the "oracle" state
    // block so stock CommunicationMod consumers (this key absent) are unaffected.
    private static final String ORACLE_BLOCK_OPTION = "oracleBlock";
    // SpeedTheSpire fork addition (stage-b-design §2.2, task B1.3): the three
    // rendering-strip / fast-forward patch families, each individually toggleable.
    // Public so the strip patches (incl. the pre-init DesktopLauncher patch) can
    // reference the exact key names. With all three false the fork is
    // byte-identical to its pre-B1.3 behavior (the strip-equivalence baseline).
    public static final String STRIP_DRAW_OPTION = "stripDrawSuppression";
    public static final String STRIP_ANIM_OPTION = "stripAnimationCollapse";
    public static final String STRIP_CADENCE_OPTION = "stripFastCadence";
    private static final String DEFAULT_COMMAND = "";
    private static final long DEFAULT_TIMEOUT = 10L;
    private static final boolean DEFAULT_VERBOSITY = true;
    private static final boolean DEFAULT_ORACLE_BLOCK = true;
    public static final boolean DEFAULT_STRIP = true;

    public CommunicationMod(){
        BaseMod.subscribe(this);
        onStateChangeSubscribers = new ArrayList<>();
        CommunicationMod.subscribe(this);
        readQueue = new LinkedBlockingQueue<>();
        try {
            Properties defaults = new Properties();
            defaults.put(GAME_START_OPTION, Boolean.toString(false));
            defaults.put(INITIALIZATION_TIMEOUT_OPTION, Long.toString(DEFAULT_TIMEOUT));
            defaults.put(VERBOSE_OPTION, Boolean.toString(DEFAULT_VERBOSITY));
            defaults.put(ORACLE_BLOCK_OPTION, Boolean.toString(DEFAULT_ORACLE_BLOCK));
            defaults.put(STRIP_DRAW_OPTION, Boolean.toString(DEFAULT_STRIP));
            defaults.put(STRIP_ANIM_OPTION, Boolean.toString(DEFAULT_STRIP));
            defaults.put(STRIP_CADENCE_OPTION, Boolean.toString(DEFAULT_STRIP));
            communicationConfig = new SpireConfig("CommunicationMod", "config", defaults);
            String command = communicationConfig.getString(COMMAND_OPTION);
            // I want this to always be saved to the file so people can set it more easily.
            if (command == null) {
                communicationConfig.setString(COMMAND_OPTION, DEFAULT_COMMAND);
                communicationConfig.save();
            }
            communicationConfig.save();
        } catch (IOException e) {
            e.printStackTrace();
        }

        if(getRunOnGameStartOption()) {
            boolean success = startExternalProcess();
        }
    }

    public static void initialize() {
        CommunicationMod mod = new CommunicationMod();
    }

    public void receivePreUpdate() {
        if(listener != null && !listener.isAlive() && writeThread != null && writeThread.isAlive()) {
            logger.info("Child process has died...");
            writeThread.interrupt();
            readThread.interrupt();
        }
        if(messageAvailable()) {
            try {
                boolean stateChanged = CommandExecutor.executeCommand(readMessage());
                if(stateChanged) {
                    GameStateListener.registerCommandExecution();
                }
            } catch (InvalidCommandException e) {
                HashMap<String, Object> jsonError = new HashMap<>();
                jsonError.put("error", e.getMessage());
                jsonError.put("ready_for_command", GameStateListener.isWaitingForCommand());
                Gson gson = new Gson();
                sendMessage(gson.toJson(jsonError));
            }
        }
    }

    public static void subscribe(OnStateChangeSubscriber sub) {
        onStateChangeSubscribers.add(sub);
    }

    public static void publishOnGameStateChange() {
        for(OnStateChangeSubscriber sub : onStateChangeSubscribers) {
            sub.receiveOnStateChange();
        }
    }

    public void receiveOnStateChange() {
        sendGameState();
    }

    public static void queueCommand(String command) {
        readQueue.add(command);
    }

    public void receivePostInitialize() {
        setUpOptionsMenu();
    }

    public void receivePostUpdate() {
        if(!mustSendGameState && GameStateListener.checkForMenuStateChange()) {
            mustSendGameState = true;
        }
        if(mustSendGameState) {
            publishOnGameStateChange();
            mustSendGameState = false;
        }
        InputActionPatch.doKeypress = false;
    }

    public void receivePostDungeonUpdate() {
        if (GameStateListener.checkForDungeonStateChange()) {
            mustSendGameState = true;
        }
        if(AbstractDungeon.getCurrRoom().isBattleOver) {
            GameStateListener.signalTurnEnd();
        }
    }

    private void setUpOptionsMenu() {
        ModPanel settingsPanel = new ModPanel();
        ModLabeledToggleButton gameStartOptionButton = new ModLabeledToggleButton(
                "Start external process at game launch",
                350, 550, Settings.CREAM_COLOR, FontHelper.charDescFont,
                getRunOnGameStartOption(), settingsPanel, modLabel -> {},
                modToggleButton -> {
                    if (communicationConfig != null) {
                        communicationConfig.setBool(GAME_START_OPTION, modToggleButton.enabled);
                        try {
                            communicationConfig.save();
                        } catch (IOException e) {
                            e.printStackTrace();
                        }
                    }
                });
        settingsPanel.addUIElement(gameStartOptionButton);

        ModLabel externalCommandLabel = new ModLabel(
                "", 350, 600, Settings.CREAM_COLOR, FontHelper.charDescFont,
                settingsPanel, modLabel -> {
                    modLabel.text = String.format("External Process Command: %s", getSubprocessCommandString());
                });
        settingsPanel.addUIElement(externalCommandLabel);

        ModButton startProcessButton = new ModButton(
                350, 650, settingsPanel, modButton -> {
                    BaseMod.modSettingsUp = false;
                    startExternalProcess();
                });
        settingsPanel.addUIElement(startProcessButton);

        ModLabel startProcessLabel = new ModLabel(
                "(Re)start external process",
                475, 700, Settings.CREAM_COLOR, FontHelper.charDescFont,
                settingsPanel, modLabel -> {
                    if(listener != null && listener.isAlive()) {
                        modLabel.text = "Restart external process";
                    } else {
                        modLabel.text = "Start external process";
                    }
                });
        settingsPanel.addUIElement(startProcessLabel);

        ModButton editProcessButton = new ModButton(
                850, 650, settingsPanel, modButton -> {});
        settingsPanel.addUIElement(editProcessButton);

        ModLabel editProcessLabel = new ModLabel(
                "Set command (not implemented)",
                975, 700, Settings.CREAM_COLOR, FontHelper.charDescFont,
                settingsPanel, modLabel -> {});
        settingsPanel.addUIElement(editProcessLabel);

        ModLabeledToggleButton verbosityOption = new ModLabeledToggleButton(
                "Suppress verbose log output",
                350, 500, Settings.CREAM_COLOR, FontHelper.charDescFont,
                getVerbosityOption(), settingsPanel, modLabel -> {},
                modToggleButton -> {
                    if (communicationConfig != null) {
                        communicationConfig.setBool(VERBOSE_OPTION, modToggleButton.enabled);
                        try {
                            communicationConfig.save();
                        } catch (IOException e) {
                            e.printStackTrace();
                        }
                    }
                });
        settingsPanel.addUIElement(verbosityOption);

        // SpeedTheSpire fork addition (stage-b-design §2.5): toggle the oracle block.
        ModLabeledToggleButton oracleBlockOption = new ModLabeledToggleButton(
                "Emit SpeedTheSpire oracle state block",
                350, 450, Settings.CREAM_COLOR, FontHelper.charDescFont,
                getOracleBlockOption(), settingsPanel, modLabel -> {},
                modToggleButton -> {
                    if (communicationConfig != null) {
                        communicationConfig.setBool(ORACLE_BLOCK_OPTION, modToggleButton.enabled);
                        try {
                            communicationConfig.save();
                        } catch (IOException e) {
                            e.printStackTrace();
                        }
                    }
                });
        settingsPanel.addUIElement(oracleBlockOption);

        // SpeedTheSpire fork additions (stage-b-design §2.2, task B1.3): one
        // toggle per rendering-strip family. Changing fast cadence only takes
        // effect on the next game launch (it is read at DesktopLauncher init).
        ModLabeledToggleButton stripDrawOption = new ModLabeledToggleButton(
                "Strip: suppress rendering (draw)",
                350, 400, Settings.CREAM_COLOR, FontHelper.charDescFont,
                getStripDrawSuppression(), settingsPanel, modLabel -> {},
                modToggleButton -> {
                    if (communicationConfig != null) {
                        communicationConfig.setBool(STRIP_DRAW_OPTION, modToggleButton.enabled);
                        try {
                            communicationConfig.save();
                        } catch (IOException e) {
                            e.printStackTrace();
                        }
                    }
                });
        settingsPanel.addUIElement(stripDrawOption);

        ModLabeledToggleButton stripAnimOption = new ModLabeledToggleButton(
                "Strip: collapse animation time",
                350, 350, Settings.CREAM_COLOR, FontHelper.charDescFont,
                getStripAnimationCollapse(), settingsPanel, modLabel -> {},
                modToggleButton -> {
                    if (communicationConfig != null) {
                        communicationConfig.setBool(STRIP_ANIM_OPTION, modToggleButton.enabled);
                        try {
                            communicationConfig.save();
                        } catch (IOException e) {
                            e.printStackTrace();
                        }
                    }
                });
        settingsPanel.addUIElement(stripAnimOption);

        ModLabeledToggleButton stripCadenceOption = new ModLabeledToggleButton(
                "Strip: fast update cadence (uncap FPS -- next launch)",
                350, 300, Settings.CREAM_COLOR, FontHelper.charDescFont,
                communicationConfig != null && communicationConfig.getBool(STRIP_CADENCE_OPTION),
                settingsPanel, modLabel -> {},
                modToggleButton -> {
                    if (communicationConfig != null) {
                        communicationConfig.setBool(STRIP_CADENCE_OPTION, modToggleButton.enabled);
                        try {
                            communicationConfig.save();
                        } catch (IOException e) {
                            e.printStackTrace();
                        }
                    }
                });
        settingsPanel.addUIElement(stripCadenceOption);

        BaseMod.registerModBadge(ImageMaster.loadImage("Icon.png"),"Communication Mod", "Forgotten Arbiter", null, settingsPanel);
    }

    private void startCommunicationThreads() {
        writeQueue = new LinkedBlockingQueue<>();
        writeThread = new Thread(new DataWriter(writeQueue, listener.getOutputStream(), getVerbosityOption()));
        writeThread.start();
        readThread = new Thread(new DataReader(readQueue, listener.getInputStream(), getVerbosityOption()));
        readThread.start();
    }

    private static void sendGameState() {
        String state = GameStateConverter.getCommunicationState();
        sendMessage(state);
    }

    public static void dispose() {
        logger.info("Shutting down child process...");
        if(listener != null) {
            listener.destroy();
        }
    }

    private static void sendMessage(String message) {
        if(writeQueue != null && writeThread.isAlive()) {
            writeQueue.add(message);
        }
    }

    private static boolean messageAvailable() {
        return readQueue != null && !readQueue.isEmpty();
    }

    private static String readMessage() {
        if(messageAvailable()) {
            return readQueue.remove();
        } else {
            return null;
        }
    }

    private static String readMessageBlocking() {
        try {
            return readQueue.poll(getInitializationTimeoutOption(), TimeUnit.SECONDS);
        } catch (InterruptedException e) {
            throw new RuntimeException("Interrupted while trying to read message from subprocess.");
        }
    }

    private static String[] getSubprocessCommand() {
        if (communicationConfig == null) {
            return new String[0];
        }
        return communicationConfig.getString(COMMAND_OPTION).trim().split("\\s+");
    }

    private static String getSubprocessCommandString() {
        if (communicationConfig == null) {
            return "";
        }
        return communicationConfig.getString(COMMAND_OPTION).trim();
    }

    private static boolean getRunOnGameStartOption() {
        if (communicationConfig == null) {
            return false;
        }
        return communicationConfig.getBool(GAME_START_OPTION);
    }

    private static long getInitializationTimeoutOption() {
        if (communicationConfig == null) {
            return DEFAULT_TIMEOUT;
        }
        return (long)communicationConfig.getInt(INITIALIZATION_TIMEOUT_OPTION);
    }

    private static boolean getVerbosityOption() {
        if (communicationConfig == null) {
            return DEFAULT_VERBOSITY;
        }
        return communicationConfig.getBool(VERBOSE_OPTION);
    }

    // SpeedTheSpire fork addition (stage-b-design §2.5, task B1.2): when true, the
    // fork appends the "oracle" hidden-state block to every in-dungeon state dump
    // (see GameStateConverter.getOracleState). When false the fork behaves exactly
    // like stock CommunicationMod (no "oracle" key), which is how B1.3 proves the
    // rendering-strip patches leave the dump byte-identical.
    public static boolean getOracleBlockOption() {
        if (communicationConfig == null) {
            return DEFAULT_ORACLE_BLOCK;
        }
        return communicationConfig.getBool(ORACLE_BLOCK_OPTION);
    }

    // SpeedTheSpire fork additions (stage-b-design §2.2, task B1.3). The two
    // runtime-checked strip families (draw suppression, animation-time collapse)
    // read through the mod's live SpireConfig -- by the time the game is drawing
    // frames or ticking actions the config is loaded. The fast-cadence family is
    // read pre-init in the DesktopLauncher patch via its own SpireConfig, because
    // loadSettings runs before this mod's constructor (see FastCadencePatch).
    public static boolean getStripDrawSuppression() {
        if (communicationConfig == null) {
            return DEFAULT_STRIP;
        }
        return communicationConfig.getBool(STRIP_DRAW_OPTION);
    }

    public static boolean getStripAnimationCollapse() {
        if (communicationConfig == null) {
            return DEFAULT_STRIP;
        }
        return communicationConfig.getBool(STRIP_ANIM_OPTION);
    }

    private boolean startExternalProcess() {
        if(readThread != null) {
            readThread.interrupt();
        }
        if(writeThread != null) {
            writeThread.interrupt();
        }
        if(listener != null) {
            listener.destroy();
            try {
                boolean success = listener.waitFor(2, TimeUnit.SECONDS);
                if (!success) {
                    listener.destroyForcibly();
                }
            } catch (InterruptedException e) {
                e.printStackTrace();
                listener.destroyForcibly();
            }
        }
        ProcessBuilder builder = new ProcessBuilder(getSubprocessCommand());
        File errorLog = new File("communication_mod_errors.log");
        builder.redirectError(ProcessBuilder.Redirect.appendTo(errorLog));
        try {
            listener = builder.start();
        } catch (IOException e) {
            logger.error("Could not start external process.");
            e.printStackTrace();
        }
        if(listener != null) {
            startCommunicationThreads();
            // We wait for the child process to signal it is ready before we proceed. Note that the game
            // will hang while this is occurring, and it will time out after a specified waiting time.
            String message = readMessageBlocking();
            if(message == null) {
                // The child process waited too long to respond, so we kill it.
                readThread.interrupt();
                writeThread.interrupt();
                listener.destroy();
                logger.error("Timed out while waiting for signal from external process.");
                logger.error("Check communication_mod_errors.log for stderr from the process.");
                return false;
            } else {
                logger.info(String.format("Received message from external process: %s", message));
                if (GameStateListener.isWaitingForCommand()) {
                    mustSendGameState = true;
                }
                return true;
            }
        }
        return false;
    }

}
