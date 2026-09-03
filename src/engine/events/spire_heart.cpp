// spire_heart.cpp -- the `Spire Heart` dialog: the Act-3 terminal, the key
// gate, and the Door.
//
// This is the event VictoryRoom(EventType.HEART) constructs on entry
// (VictoryRoom.java:26-34), i.e. the room the last Act-3 boss's proceed builds
// (ProceedButton.goToVictoryRoomOrTheDoor, :199-208). The ROOM is a full
// transition and lives in run_advance.cpp; this file is only the dialog.
//
// Provenance (each read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * SpireHeart (the whole class)        SpireHeart.java:43-208
//       constructor (score/leaderboard)   :64-92
//       goToFinalAct                      :94-98
//       buttonEffect (the four arms)      :118-188
//       the three-key gate                :151
//       the DEATH arm                     :170-177
//       the GO_TO_ENDING arm              :178-184
//   * VictoryRoom                          VictoryRoom.java:17-65
//   * ProceedButton.goToVictoryRoomOrTheDoor
//                                          ProceedButton.java:199-208
//   * DoorUnlockScreen.exit                DoorUnlockScreen.java:143-161
//   * Metrics (victory / trueVictor)       Metrics.java:82, :107
//   * DeathScreen.<init>                   DeathScreen.java:291-299
//   * VictoryScreen.<init>                 VictoryScreen.java:254-269
//
// THE CONSTRUCTOR CHANGES NO RUN STATE. SpireHeart.java:64-92 is entirely
// publisher stats, the win-streak counter, a leaderboard upload and
// GameOverScreen.calcScore -- score bookkeeping the simulator deliberately does
// not model (s3-design §8). It draws no RNG and touches no HP, gold, deck or
// pool, so `on_enter` has nothing to do but park the dialog at INTRO.
//
// THE SCREEN ORDINALS ARE READ, NOT DERIVED. `SpireHeart$CUR_SCREEN` was
// stripped from the decompile-source jar, so CFR rendered buttonEffect's switch
// with bare integer labels and s3-design §4.1 could only DERIVE the mapping from
// the arms' bodies. It has now been recovered mechanically from the shipped game
// jar (D:\SteamLibrary\...\SlayTheSpire\desktop-1.0.jar, SHA-256
// cfad868ac8d65a88e71a0bf096fb09f78811e553effe0787c5309a655e081673 -- the value
// RECOVERED-INNER-CLASSES.md pins) by reading the javac switch-map class
// `SpireHeart$1` and the enum's own field order with javap:
//
//     $SwitchMap[...CUR_SCREEN.INTRO.ordinal()]        = 1
//     $SwitchMap[...CUR_SCREEN.MIDDLE.ordinal()]       = 2
//     $SwitchMap[...CUR_SCREEN.MIDDLE_2.ordinal()]     = 3
//     $SwitchMap[...CUR_SCREEN.DEATH.ordinal()]        = 4
//     $SwitchMap[...CUR_SCREEN.GO_TO_ENDING.ordinal()] = 5
//
// and the enum declares its constants in exactly that order, so the ORDINALS are
// INTRO 0, MIDDLE 1, MIDDLE_2 2, DEATH 3, GO_TO_ENDING 4 and the decompile's
// `case N:` labels are those ordinals plus one. The derivation in the design doc
// was correct; it is now attested. `EventDialogState::screen` below IS the
// game's ordinal, so no second numbering exists to drift.
//
// FOUR CLICKS, MODELLED -- NOT COLLAPSED. The engine's landed convention
// collapses a dialog click that changes no state (shrines.cpp,
// beyond_events.cpp), and clicks 1 and 2 are exactly that shape. They are
// modelled anyway, because the REPLAY DIFFER COMPARES RECORD COUNTS and the
// capture side is four presses: every three-act victory artifact carries a
// five-record post-victory tail (four `Spire Heart` `choose 0` records plus the
// `__terminal_observed__`), and the first of those five is where the ROOM
// TRANSITION's state change is visible -- floor 51 -> 52 at A20, the five
// floor-scoped streams reseeded to seed + 52, MonsterRoomBoss -> VictoryRoom --
// while records 2, 3 and 4 change nothing but the option label. Collapsing the
// dialog would leave the sim with no press to answer records 2-4 with. This
// discharges the s3-tasks.md "clicks 1-2: collapse or model?" row: MODEL, and
// the entire dialog is presentation -- the only STATE is the room transition
// that precedes it and the terminal that ends it.

#include "event_common.hpp"

namespace sts::engine {

namespace {

// SpireHeart$CUR_SCREEN's ordinals, recovered as recorded in the file header.
// These are `EventDialogState::screen` values, unshifted.
inline constexpr uint8_t kScreenIntro = 0;
inline constexpr uint8_t kScreenMiddle = 1;
inline constexpr uint8_t kScreenMiddle2 = 2;
inline constexpr uint8_t kScreenDeath = 3;
inline constexpr uint8_t kScreenGoToEnding = 4;

void spire_heart_on_enter(RunController& /*rc*/, EventDialogState& es) {
    // `screen = CUR_SCREEN.INTRO` is the field initialiser (SpireHeart.java:51)
    // and the constructor adds exactly one dialog option (:70-72). Nothing else
    // in :64-92 is run state.
    es.screen = kScreenIntro;
}

// Every screen offers exactly ONE always-enabled option. The dialog is built
// once with a single option (:71) and each buttonEffect arm calls
// updateDialogOption(0, ...) -- it rewrites the LABEL, never the count, and no
// arm ever disables it. The DEATH and GO_TO_ENDING arms hide the panel instead
// of offering a fifth press (:173, :181), so they are terminals, not screens
// with menus; they are still given the one-option menu here because the run
// layer leaves EVENT_DIALOG in the same step it enters them.
void spire_heart_build_menu(const RunController& /*rc*/,
                            const EventDialogState& /*es*/,
                            EventDialogMenu& out) {
    events::one_proceed_menu(out);
}

// The three-key gate, verbatim (SpireHeart.java:151):
//
//     Settings.isFinalActAvailable && Settings.hasRubyKey
//         && Settings.hasEmeraldKey && Settings.hasSapphireKey
//
// All four conjuncts, in the game's own order. `isFinalActAvailable` is the
// profile constant kFinalActAvailable (run_state.hpp) -- the same one the
// burning elite's reward row and the campfire Recall read -- and the three key
// bits are RunState::keys, whose writers are the campfire Recall (ruby), the
// burning elite's reward row (emerald) and the chest's linked row (sapphire),
// all live since S3.11.
[[nodiscard]] bool door_is_open(const RunState& rs) noexcept {
    return kFinalActAvailable && (rs.keys & kKeyRuby) != 0 &&
           (rs.keys & kKeyEmerald) != 0 && (rs.keys & kKeySapphire) != 0;
}

EventDialogStatus spire_heart_choose(RunController& rc, EventDialogState& es,
                                     uint8_t /*option*/) {
    switch (es.screen) {
        case kScreenIntro:
            // :120-125 -- body text becomes the character's Spire-Heart text,
            // the option's label becomes OPTIONS[1]. No state.
            es.screen = kScreenMiddle;
            return EventDialogStatus::CONTINUE;

        case kScreenMiddle:
            // :126-149 -- the score read-out and the slash VFX. The whole arm
            // is presentation: `damageDealt` was frozen in the constructor and
            // the DamageHeartEffect loop draws only from libGDX's own
            // MathUtils.random (:146), which is a static generator this
            // simulator does not model and which no seeded stream feeds. No
            // run state, no engine RNG.
            es.screen = kScreenMiddle2;
            return EventDialogStatus::CONTINUE;

        case kScreenMiddle2:
            // :150-168 -- THE ONE BRANCH. Everything else in the arm (the
            // screen shake, the rumble, the NumberFormat'd global/total damage
            // read-out) is presentation.
            es.screen = door_is_open(rc.run) ? kScreenGoToEnding : kScreenDeath;
            return EventDialogStatus::CONTINUE;

        case kScreenDeath: {
            // :170-177 -- `player.isDying = true`, the panel hides,
            // `player.isDead = true`, `new DeathScreen(null)`. THE RUN ENDS
            // HERE, and it ends as a VICTORY that is not a TRUE victory:
            // DeathScreen's constructor submits `victory = true, trueVictor =
            // false` for exactly this shape (DeathScreen.java:291-299) while
            // only VictoryScreen submits both (VictoryScreen.java:254-269).
            // That independence is why RunState carries a three-valued kind
            // rather than a boolean (run_state.hpp, RunVictoryKind).
            //
            // Nothing else moves: no gold, no HP that matters, no stream. The
            // last thing the run DID spend was the Act-3 boss room's discarded
            // miscRng gold draw, three clicks ago.
            rc.run.victory_kind = static_cast<uint8_t>(RunVictoryKind::ACT3_STOP);
            rc.event = EventDialogState{};
            rc.phase = static_cast<uint8_t>(RunPhase::RUN_OVER);
            return EventDialogStatus::TRANSITIONED;
        }

        case kScreenGoToEnding:
        default: {
            // :178-184 -> goToFinalAct (:94-98) -> DoorUnlockScreen.open(true).
            // THE DOOR IS THE ONLY WRITER OF `nextDungeon = "TheEnding"`
            // (DoorUnlockScreen.java:143-161, the eventVersion arm at :152-160),
            // and its exit also sets `getCurrRoom().phase = COMPLETE`,
            // `fadeOut()` and -- the load-bearing one --
            // `isDungeonBeaten = true` (:159). There is no Door ROOM and no
            // Door node: the screen is drawn over the VictoryRoom.
            //
            // BECAUSE isDungeonBeaten IS TRUE, THE CROSSING ADDS NO FLOOR.
            // updateFading's `if (!isDungeonBeaten) nextRoomTransition()` arm
            // (AbstractDungeon.java:2317-2325) is the thing that would have
            // spent one, and it is skipped -- the same mechanism the boss-chest
            // crossing uses. So Act 4 is CONSTRUCTED AT the VictoryRoom's own
            // floor: 51 below A20, 52 at A20 (s3-design §4.3).
            //
            // THE STATE act4_crossing READS, unchanged from the park this
            // arm held between S3.31 and S3.32:
            //   * rc.run.floor    -- unchanged (51 / 52); this IS the Act-4
            //                        floor base, and act4_crossing's first
            //                        statement copies it into
            //                        rc.run.act4_floor_base.
            //   * rc.run.act      -- still 3 on entry; the crossing's
            //                        `++actNum` raises it to 4.
            //   * rc.room_type    -- RoomType::Victory, phase COMPLETE in the
            //                        game's terms; the room is left, not
            //                        re-entered, so no transition pops a list,
            //                        and TheEnding (unlike TheCity) does not
            //                        replace currMapNode, so it stays Victory
            //                        until the first Act-4 node transition.
            //   * the five floor streams -- still seed + floor from the
            //                        VictoryRoom's own reseed, which is what
            //                        TheEnding's construction observes (§4.3)
            //                        and what its BGM change would have drawn
            //                        from if Act 4 had a second track.
            //   * rc.run.victory_kind -- still NONE, and act4_crossing does not
            //                        touch it. The Door is not a victory; HEART
            //                        is written by the TrueVictoryRoom (S3.33),
            //                        and a run that walks through the Door and
            //                        then dies in Act 4 is a LOSS.
            //
            // The dialog is dismissed before the crossing rather than after it
            // so that no Act-4 state is ever observed with a stale event id up;
            // act4_crossing clears rc.event again and leaves MAP_CHOICE.
            rc.event = EventDialogState{};
            act4_crossing(rc);
            return EventDialogStatus::TRANSITIONED;
        }
    }
}

constexpr EventDialogImpl kSpireHeart = {
    &spire_heart_on_enter,
    &spire_heart_build_menu,
    &spire_heart_choose,
};

}  // namespace

const EventDialogImpl* event_spire_heart() noexcept { return &kSpireHeart; }

}  // namespace sts::engine
