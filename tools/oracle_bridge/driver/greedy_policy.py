#!/usr/bin/env python3
"""`--policy greedy`: a depth-seeking action scorer for the campaign driver.

WHY. The only live policy the bridge had was `random-legal` (uniform over
`campaign_driver.expand_legal_actions`). Across 41 recent runs its median death
floor was 3 and its deepest run was floor 12, so the deep-floor states the
Stage-B captures actually need -- the treasure chest at floor 8+, the Act-1 boss
at 16-17 -- essentially never appear. A sim-side measurement of the same shape
of heuristic (`tools/fuzz/src/policy.cpp` GREEDY_DAMAGE) put the >=floor-8 yield
at roughly 15-20x the random baseline. This module is that heuristic, ported to
the *parsed protocol dump* instead of the sim's structs.

WHAT IS AND IS NOT GUARANTEED. `pick()` chooses among candidates produced by
`campaign_driver.expand_legal_actions`, which only ever emits commands drawn
from the game's own `available_commands` with in-range arguments. So legality is
a property of the *expansion*, not of the scoring; scoring only ranks. Every
function here is a pure function of the parsed state (plus the side table) --
no I/O, no globals -- which is what makes the replay harness in
`test_oracle_campaign.py` able to run recorded captures through it.

DETERMINISM. Ties are broken with the caller's `random.Random`, which
`campaign_driver.run_seed` reseeds per run as `Random(f"{policy_seed}:{seed}")`.
One draw per decision (never a variable number), so the policy's RNG stream --
and therefore the whole action sequence -- is reproducible from
(policy_seed, seed) alone, as design 7.5 requires of a campaign policy. With
`rng=None` the tie-break is the lowest-scoring-index candidate, which is what
the unit tests assert against.

RELATIONSHIP TO THE SIM'S FUZZ POLICY. The combat shape is a deliberate mirror
of `move_score` (`tools/fuzz/src/policy.cpp`): `base + damage*4 + block`,
`end` strictly below every playable card, +2 for focusing the weakest live
monster. The *map* scoring is deliberately INVERTED relative to it -- see
`_score_map`.

--------------------------------------------------------------------------
b1.5.0 -- THE ACT-1 BOSS RULES, AND THE EVIDENCE THEY COME FROM
--------------------------------------------------------------------------

69 captured runs produced 12 Act-1 boss fights and zero boss-reward claims
(g6_campaign2_spotdiff.md 10). Reading the four STS01221 fights record by record
(`g6_boss_ps{7,42,777}` + the main campaign) corrects the diagnosis that runbook
records, and the correction is what these rules are built on:

  * **The Slime Boss was never killed.** `SlimeBoss.damage` (SlimeBoss.java:173-
    182) queues SPLIT when `currentHealth <= maxHealth / 2` -- 75 of 150 at A9+
    -- and `takeTurn` case 3 (:148-159) then suicides the boss and spawns
    `SpikeSlime_L` and `AcidSlime_L` EACH at the boss's `currentHealth` (:155,
    :156). Every capture shows exactly that: ps7 seq 206 `Slime Boss 70/150` ->
    seq 207 `Spike Slime (L) 70/70 + Acid Slime (L) 70/70`, ps42 75 -> 75/75,
    ps777 72 -> 72/72. A `Slime Boss 0/150 GONE` row in the dump is the
    SuicideAction, not a kill. Greedy was ~35 % through the fight's effective
    HP (75 to the split, then ~144 more), not 100 %.
  * **Post-split play was not the defect.** Greedy blocked under attack, focused
    the lower-HP half and spent its last potion there. It simply had nothing
    left to play: the ps7 deck at floor 16 was 13 cards -- 5 Strikes, 4 Defends,
    Bash, Impervious, Reckless Charge, Ascender's Bane. ~15 damage a turn
    against ~220 effective HP is not a targeting problem.
  * **What it never did was take a card.** `REWARD_CARD` sat below
    `REWARD_PROCEED`, so across 16 floors and eight reward screens the card row
    was never opened once. Two of six runs also spent their last potion on
    floor 8 or 12 and walked into the boss with an empty two-slot belt.

Hence three rules, and only three:

  R1 `wants_card_reward` -- open the card row and TAKE while the deck is short
     of attacks. The take/skip decision is a function of the DECK ALONE, never
     of the cards on offer, because skipping a card reward does NOT retire the
     row (verified in b13_off20 run_STS00004 seq 30-33: skip -> COMBAT_REWARD
     still lists `card`). A policy that opened the row and then judged the cards
     would re-open it forever -- the exact 2-cycle the old `REWARD_CARD`
     comment refused to risk. Deck-only gating makes the two screens agree by
     construction, so the row is always consumed by a take.
  R2 `_potion_worth_spending` -- hold potions below `end` unless the room is a
     boss or elite, HP has fallen to `POTION_LOW_HP_FRACTION`, or the belt is
     already full (the belt is 2 slots at A11+, and a held potion that blocks a
     pickup is worth less than a spent one).
  R3 `attacker_count` -- scale the under-attack block weight with the number of
     monsters actually swinging. One boss is one attacker; the split is two, and
     two is where every STS01221 run died.

Everything else is untouched, including the four properties the campaigns
depend on: legal-by-construction (`pick` only ever returns a candidate handed to
it), determinism from (policy_seed, seed) (every rule here is a pure function of
the parsed dump plus the side table, and the tie-break still draws exactly once),
never-claim-SAPPHIRE_KEY (`_score_reward`), and never-swap-the-boss-relic at
Neow (`_score_event` has no opinion that reaches it).

--------------------------------------------------------------------------
b1.7.0 (S2.42) -- THREE-ACT SURVIVAL: ACT PROFILES AND THE BOSS-RELIC PICK
--------------------------------------------------------------------------

S1 stopped the driver at the Act-1 boss combat reward, so every constant above
is Act-1-tuned and every rule above was derived from Act-1 evidence. S2 drives
all three acts (design 6 / S2.42), and two of the S1 tunings are actively wrong
past Act 1:

  * **A 20-card, 10-attack deck is a floor-17 deck.** R1's gate is what stops a
    reward-screen 2-cycle, so it cannot simply be widened -- but its two
    thresholds are numbers, and an Act-3-capable deck is 25-35 cards.
  * **`MAP_ELITE 200` is a survival tuning that starves the run of relics.**
    Skipping every elite is how a run reaches the Act-1 boss; it is also how a
    run arrives at the Act-3 boss with no relics and dies. Act 2/3 must be able
    to want an elite -- but only while healthy, so the rule degrades to the
    Act-1 behaviour exactly when survival is actually at stake.

The mechanism is `ACT_PROFILES`, a per-act overlay over the SAME ALL-CAPS
numeric constants, read through `_const(name, state)`. Three properties, each
with a test:

  * **Act 1 is byte-identical to b1.6.0 by construction.** `ACT_PROFILES` has no
    key `1` and `act_of` answers 1 for a dump with no `act`, so `_const` returns
    the module constant unchanged. The TE.1 cohort's measured 31.0 % Act-1 boss
    reach stays reproducible in behaviour (the binary's SHA-256 necessarily
    moves -- see docs/verification/s242-deep-reach.md).
  * **The cohort config still wins.** `survival_policy_cmd.apply_constants`
    setattrs module constants for a named, SHA-pinned cohort; if a profile
    overlay were consulted afterwards, that cohort would silently be a no-op in
    Acts 2/3 -- a cohort labelled with a policy it did not run, which is the one
    failure the strict config validation exists to prevent. So the overlay
    checks `CONFIG_PINNED` first and yields to any explicitly configured name.
  * **R1's two-screens invariant survives.** `wants_card_reward` still reads the
    DECK ALONE; the act is a property of the same dump and cannot change between
    the COMBAT_REWARD row and the CARD_REWARD screen it opens, so the two
    decisions still agree by construction.

And one genuinely new rule, because the screen did not exist in S1:

  R4 `_score_boss_reward` -- the BOSS_REWARD (boss-chest) pick. The screen
     offers three BOSS-tier relics plus `skip`; the design 6 S2-G2 bar needs
     BOTH a take and a skip witnessed, per Act-2 boss. That is a COHORT
     selection, not a coin flip: `BOSS_RELIC_SKIP_MODE` is an ordinary numeric
     constant, so `policy_bossrelic_take.json` / `policy_bossrelic_skip.json`
     are two SHA-pinned campaign identities over one binary, and the resulting
     cohort is named rather than hoped for.

     WHAT IS NEVER TAKEN, AND WHY IT IS NOT A TASTE LIST. Five BOSS relics do
     not merely make the run harder, they invalidate a rule THIS MODULE owns,
     so taking one would leave the policy scoring a game it is no longer
     playing: Sozu (no potions -- R2 has nothing left to decide), Runic Dome
     (no intents -- `attacker_count`, and therefore R3's whole block weight,
     reads zero forever), Snecko Eye (randomised costs -- the cheap-utility
     term and the side table's cost column stop describing the hand), Pandora's
     Box (rewrites every Strike/Defend -- R1's deck-attack gate is counting a
     deck that no longer exists), and Calling Bell (a three-relic modal reward
     screen plus a curse; the driver carries a b1.5.3 suppression path for
     exactly that screen at Neow). Anything else is takeable. The criterion is
     "names a rule above", which is checkable; "is a bad relic" is not.

     Skip is a REVERSIBLE screen close (boss_chest.hpp: `relicSkipLogic` calls
     `chest.close()`, which does not clear the offers, so the chest reopens with
     the same three). A stateless policy that both opens chests and skips picks
     therefore has a legal 2-cycle available to it. This module does NOT close
     that hole -- `campaign_driver._run_boss_chest` sequences the boss chest
     (open once -> ask this policy for the pick -> leave) precisely so the
     termination argument lives somewhere that can count.
"""

from __future__ import annotations

import json
import os

SIDE_TABLE_FILENAME = "cards_sidetable.json"

# --- score bands -----------------------------------------------------------
#
# Bands are chosen so that the comparisons that actually happen inside one
# `available_commands` set land where they should. They are not a global
# ordering of the game: `choose` on a map screen and `choose` on a shop screen
# are never candidates at the same time.

# Combat (no screen up: `play` / `end` / `potion`).
PLAY_BASE = 1000            # every playable card outranks ending the turn
# The sim has two greedy shapes and this policy is BOTH of them, switched by the
# threat on screen: policy.cpp GREEDY_DAMAGE is `base + damage*4 + block`,
# GREEDY_BLOCK is `base + block*4 + damage`. With nothing swinging at us, kill
# things faster; with an attack intent up, buy the turn. Depth is survival, and
# a policy that could never be talked into playing Defend dies on floor 3 --
# which is precisely what the random baseline already does. LETHAL_BONUS sits
# above both, so "finish something off" still wins either way, which keeps the
# brief's ordering (lethal first, then block under attack, then cheap utility)
# intact at the top of the ladder.
DAMAGE_WEIGHT = 4
BLOCK_WEIGHT = 1
DAMAGE_WEIGHT_UNDER_ATTACK = 1
BLOCK_WEIGHT_UNDER_ATTACK = 4
# R3 (b1.5.0). Each attacker past the first adds to the block weight. One boss
# swinging is the case the 4/1 split was tuned for; the Slime Boss split puts
# two large slimes on the board at once (SlimeBoss.java:155-156) and every
# STS01221 capture died there, so a second banner buys block harder. Capped so
# the weight can never outrun LETHAL_BONUS on a realistic block roll.
BLOCK_WEIGHT_PER_EXTRA_ATTACKER = 2
BLOCK_WEIGHT_UNDER_ATTACK_MAX = 8
LETHAL_BONUS = 400          # kill something this turn -> one fewer enemy turn
FOCUS_FIRE_BONUS = 2        # policy.cpp's +2: breaks target ties only
CHEAP_UTILITY_MAX = 3       # 0-cost utility edges out 3-cost utility, by <= 3
POTION_USE_COMBAT = 500     # below any card play, above ending the turn
POTION_USE_OUT_OF_COMBAT = 20
POTION_HOLD = 0             # R3: strictly below END_TURN -- keep it for the boss
POTION_DISCARD = 0
END_TURN = 1

# R2 (b1.5.0). Rooms whose fight is worth a potion, and the HP floor at which
# any fight is.
HIGH_STAKES_ROOMS = ("MonsterRoomBoss", "MonsterRoomElite")
POTION_LOW_HP_FRACTION = 0.40
EMPTY_POTION_IDS = (None, "", "Potion Slot", "PotionSlot")
# b1.7.0 (A3). From this act onwards EVERY combat room is high-stakes, not just
# boss/elite: an Act-3 normal (Spire Growth, Transient, Reptomancer) hits harder
# than an Act-1 elite, and R2's room-name gate was written when "normal room"
# meant Cultist. Numeric on purpose -- `HIGH_STAKES_ROOMS` is a tuple and so
# cannot be reached by the cohort config surface, which only carries numbers.
# 4 disables the rule (there is no act 4), which is the Act-1-era behaviour.
POTION_HIGH_STAKES_FROM_ACT = 3
ANY_COMBAT_ROOMS = ("MonsterRoom", "MonsterRoomElite", "MonsterRoomBoss")

# Map.
MAP_BOSS = 700
MAP_NON_COMBAT = 600
MAP_MONSTER = 400
MAP_UNKNOWN_SYMBOL = 300
MAP_ELITE = 200
MAP_LEAVE_SCREEN = 0        # `return` backs out of the map without moving
# b1.7.0 (A1). The act profiles raise MAP_ELITE in Acts 2/3 -- but only while
# the run is healthy. At or below this HP fraction the raised value is dropped
# and the Act-1 value applies again, so the elite appetite degrades back to pure
# survival exactly when survival is the binding constraint.
ELITE_APPETITE_HP_FRACTION = 0.60

# Combat / treasure rewards.
REWARD_RELIC = 900
REWARD_GOLD = 850
REWARD_POTION = 800
REWARD_PROCEED = 100        # anything below this is left unclaimed
REWARD_CARD = 60            # < REWARD_PROCEED: the card row is left unopened
# R1 (b1.5.0). The card row when the deck gate is OPEN: above `proceed` so the
# row is opened, but below relic/gold/potion so the claim ORDER is unchanged --
# in particular the relic still outranks the sapphire key on a treasure screen.
REWARD_CARD_TAKE = 780
REWARD_SAPPHIRE_KEY = 0     # < REWARD_PROCEED: NEVER claim -- see _score_reward

# Card-reward screen.
CARD_REWARD_SKIP = 900
CARD_REWARD_TAKE = 10           # gate closed: every card loses to `skip`
CARD_REWARD_TAKE_OPEN = 1000    # gate open: every card beats `skip`
CARD_RANK_DAMAGE_WEIGHT = 2
CARD_RANK_BLOCK_WEIGHT = 1
CARD_RANK_ATTACK_BONUS = 40     # a deck short of attacks wants an ATTACK
CARD_RANK_AOE_BONUS = 6         # the split is two targets (SlimeBoss.java:155-6)
CARD_RANK_MAX = 200             # keeps the take band bounded and comparable

# R1's gate. Ironclad starts with 6 attacks in 10 cards; the Act-1 boss needs
# ~220 effective HP of damage (75 to the split at SlimeBoss.java:175, then two
# spawns at the boss's remaining HP), which ~15 damage a turn cannot pay. Ten
# attacks is three or four takes over sixteen floors -- conservative on purpose,
# and the size cap stops a run of SKILL/POWER offers from diluting the deck.
DECK_ATTACK_TARGET = 10
DECK_SIZE_CAP = 20
CARD_TYPE_ATTACK = "ATTACK"
CARD_TYPE_CURSE = "CURSE"

# Boss-relic pick screen (BOSS_REWARD), b1.7.0 R4. Three relics plus `skip`.
BOSS_RELIC_TAKE = 900       # a takeable relic outranks `skip`
BOSS_RELIC_SKIP = 500       # `skip` on the BOSS_REWARD screen
BOSS_RELIC_AVOID = 100      # < BOSS_RELIC_SKIP: a relic on the never-take list
BOSS_RELIC_SKIP_WINS = 1000  # skip-cohort `skip`: above every relic score
# 0 = take cohort (the default), 1 = skip cohort. A number, so a cohort is a
# SHA-pinned `{"constants": {"BOSS_RELIC_SKIP_MODE": 1}}` config over the one
# binary rather than a probabilistic draw -- see the module header (R4).
BOSS_RELIC_SKIP_MODE = 0

# The five BOSS relics that invalidate a rule this module owns; see the module
# header (R4) for the per-relic reason. Names are the exact registry game ids
# (registry/relics.yaml `game_id`), the same join key the capture artifacts
# carry.
BOSS_RELIC_NEVER_TAKE = (
    "Sozu",             # R2: no potions
    "Runic Dome",       # R3: no intents -> attacker_count is always 0
    "Snecko Eye",       # cheap-utility / side-table cost column
    "Pandora's Box",    # R1: the deck-attack gate counts a rewritten deck
    "Calling Bell",     # three-relic modal reward screen + curse
)

# Chest / rest / shop / event / select screens.
CHEST_OPEN = 900
REST_PREFERRED = 700
REST_SECONDARY = 500
REST_OTHER = 300
SHOP_ROOM_LEAVE = 600
SHOP_ROOM_ENTER = 100
SHOP_SCREEN_LEAVE = 900
SHOP_SCREEN_BUY = 1
EVENT_SAFE = 600
EVENT_NEUTRAL = 500
EVENT_RISKY = 300
SELECT_CONFIRM = 600        # confirm once something is selected
SELECT_PICK = 500
DEFAULT_CHOOSE = 500
DEFAULT_PROCEED = 400
DEFAULT_CANCEL = 300

# Rest at or below this HP fraction and the free heal beats a smith.
REST_HP_FRACTION = 0.70

# Map-node symbols (PROTOCOL.md 3.11 -- MapRoomNode.getRoomSymbol).
SYMBOL_MONSTER = "M"
SYMBOL_ELITE = "E"
NON_COMBAT_SYMBOLS = ("?", "$", "T", "R")

# Rest-room option names (ChoiceScreenUtils.getRestRoomChoices lowercases them).
REST_HEAL = "rest"
REST_SMITH = "smith"

# Event-option keyword buckets. Matched against the lowercased `choice_list`
# entry, which is CommunicationMod's own option label (PROTOCOL.md 3.4), so this
# is presentation text and English-only -- deliberately advisory. An option that
# matches nothing scores EVENT_NEUTRAL, and index 0 wins the resulting tie, so
# the brief's "else index 0" fallback is the failure mode by construction.
EVENT_RISKY_WORDS = (
    "fight", "battle", "combat", "elite", "curse", "lose", "damage",
    "take damage", "pay", "gamble", "steal", "sacrifice", "hp",
)
EVENT_SAFE_WORDS = (
    "leave", "ignore", "refuse", "escape", "no thanks", "skip", "continue",
    "talk", "proceed",
)

_ALIAS_VERBS = ("skip", "cancel", "return", "leave", "proceed", "confirm")


# --- act profiles (b1.7.0) --------------------------------------------------
#
# Per-act overlays over the ALL-CAPS numeric constants above. THERE IS NO KEY
# `1` AND THERE MUST NEVER BE ONE: its absence is what makes Act-1 behaviour
# byte-identical to b1.6.0 (see the module header), and
# `test_act1_profile_is_the_module_constants` pins it.
#
# The numbers, and why each is the number it is:
#   MAP_ELITE          Act 2: above MAP_MONSTER (400) but still below
#                      MAP_NON_COMBAT (600) -- an elite is now preferred to a
#                      normal fight, never to a rest or a shop. Act 3: higher
#                      again, because a run that reaches Act 3 relic-poor
#                      cannot pay the Act-3 boss's HP. Both are gated on
#                      ELITE_APPETITE_HP_FRACTION, so a hurt run keeps the
#                      Act-1 avoidance.
#   DECK_ATTACK_TARGET / DECK_SIZE_CAP
#                      R1's gate. 10-in-20 is a floor-17 deck; a deck that can
#                      pay an Act-2 boss is ~12 attacks in ~28 cards and an
#                      Act-3 one ~14 in ~35. Both thresholds move together so
#                      the gate keeps its shape (attacks short AND under cap).
#   POTION_LOW_HP_FRACTION
#                      R2's floor. Deeper acts kill from a higher HP fraction,
#                      so the "any fight, if hurt enough" arm opens earlier.
ACT_PROFILES = {
    2: {
        "MAP_ELITE": 450,
        "DECK_ATTACK_TARGET": 12,
        "DECK_SIZE_CAP": 28,
        "POTION_LOW_HP_FRACTION": 0.50,
    },
    3: {
        "MAP_ELITE": 500,
        "DECK_ATTACK_TARGET": 14,
        "DECK_SIZE_CAP": 35,
        "POTION_LOW_HP_FRACTION": 0.60,
    },
}

# Names an explicit cohort config pinned via `survival_policy_cmd
# .apply_constants`. A pinned name is NEVER overlaid by ACT_PROFILES: a cohort
# labelled with a policy it did not run in Acts 2/3 is the exact failure the
# strict config validation exists to prevent. Module-level mutable state is
# deliberate and confined -- it is written once at process start, before any
# decision, by the one caller that owns the config surface.
CONFIG_PINNED = set()


def act_of(state):
    """The dump's act, defaulting to 1.

    A dump with no `act` (a pre-run state, a unit-test fixture, a screen the
    game answers before the dungeon exists) resolves to the Act-1 profile,
    which is the module constants -- so an absent field can never silently
    select a deeper act's tuning.
    """
    act = _gs(state).get("act")
    if isinstance(act, bool) or not isinstance(act, int):
        return 1
    return act


def _const(name, state):
    """The value of an ALL-CAPS constant for this state's act.

    Precedence, loudly: cohort config > act profile > module constant. See the
    module header for why the config must win.
    """
    module_value = globals()[name]
    if name in CONFIG_PINNED:
        return module_value
    overlay = ACT_PROFILES.get(act_of(state))
    if overlay is None or name not in overlay:
        return module_value
    return overlay[name]


def hp_fraction(state):
    """current_hp / max_hp, or None when the dump does not carry both."""
    gs = _gs(state)
    max_hp = gs.get("max_hp") or 0
    current = gs.get("current_hp") or 0
    if max_hp <= 0:
        return None
    return float(current) / float(max_hp)


# --- side table ------------------------------------------------------------

def default_side_table_path():
    return os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        SIDE_TABLE_FILENAME)


def load_side_table(path=None):
    """Load `cards_sidetable.json` -> {game_id: row}. Standard library only.

    A missing or unreadable table is NOT fatal: the policy degrades to treating
    every card as zero-damage cheap utility rather than losing a capture run to
    a deployment slip.
    """
    try:
        with open(path or default_side_table_path(), "r",
                  encoding="utf-8") as fh:
            return (json.load(fh) or {}).get("cards") or {}
    except (OSError, ValueError):
        return {}


# --- state accessors (all tolerant of absent/None sub-objects) -------------

def _gs(state):
    return (state or {}).get("game_state") or {}


def _combat(state):
    return _gs(state).get("combat_state") or {}


def _screen_state(state):
    return _gs(state).get("screen_state") or {}


def _choice_list(state):
    return _gs(state).get("choice_list") or []


def live_monsters(state):
    """[(index, monster)] for monsters that are neither gone nor at 0 HP."""
    out = []
    for i, m in enumerate(_combat(state).get("monsters") or []):
        if not m.get("is_gone") and (m.get("current_hp") or 0) > 0:
            out.append((i, m))
    return out


def weakest_monster_index(state):
    """Lowest-HP live monster slot, ties to the lowest index, or None.

    Same rule as policy.cpp `weakest_monster`, so the choice stays a pure
    function of the state.
    """
    best = None
    best_hp = 0
    for i, m in live_monsters(state):
        hp = m.get("current_hp") or 0
        if best is None or hp < best_hp:
            best, best_hp = i, hp
    return best


def attacker_count(state):
    """How many live monsters' banners show an incoming attack (R3).

    `intent` is display-derived (PROTOCOL.md 3.12) and reads DEBUG until the
    banner refreshes, so this is advisory -- it can only ever make the policy
    block one turn later than ideal, never make it emit an illegal command.
    `move_adjusted_damage > 0` is the second witness for the same fact.
    """
    n = 0
    for _i, m in live_monsters(state):
        intent = (m.get("intent") or "").upper()
        if "ATTACK" in intent or (m.get("move_adjusted_damage") or 0) > 0:
            n += 1
    return n


def facing_attack(state):
    """True if anything at all is swinging. `attacker_count(state) > 0`."""
    return attacker_count(state) > 0


def block_weight_under_attack(state):
    """R3: 4 for a lone attacker, +2 per extra banner, capped.

    Pure and monotone in the number of attackers, so it cannot reorder anything
    on a single-monster board -- every pre-b1.5.0 combat preference is
    unchanged there.
    """
    extra = max(0, attacker_count(state) - 1)
    return min(BLOCK_WEIGHT_UNDER_ATTACK
               + extra * BLOCK_WEIGHT_PER_EXTRA_ATTACKER,
               BLOCK_WEIGHT_UNDER_ATTACK_MAX)


def card_type(card, table=None):
    """The card's type, side table first, then the dump's own `type` field.

    Both carry it (`cards_sidetable.json` rows and every hand/deck/reward card
    in the protocol dump), so this is belt-and-braces: an id outside the S1
    registry still classifies from the live dump instead of vanishing from the
    deck census.
    """
    row = (table or {}).get((card or {}).get("id")) or {}
    return (row.get("type") or (card or {}).get("type") or "").upper()


def deck_attack_count(state, table=None):
    """Number of ATTACK cards in the run deck, or None when there is no deck."""
    deck = _gs(state).get("deck")
    if not isinstance(deck, list):
        return None
    return sum(1 for c in deck if card_type(c, table) == CARD_TYPE_ATTACK)


def wants_card_reward(state, table=None):
    """R1's gate: is this deck short enough of attacks to want another card?

    A FUNCTION OF THE DECK ONLY -- deliberately. `skip` on a CARD_REWARD screen
    does not retire the row it came from (b13_off20 run_STS00004: seq 30 skip ->
    COMBAT_REWARD still offering `card`, seq 33 the same again), so a policy
    whose open-the-row decision and whose take-the-card decision could disagree
    would loop between the two screens forever, alternating signatures where the
    driver's stuck detector cannot see it. Because both decisions read the same
    `deck` -- which cannot change between the two screens -- they agree by
    construction, and the row is always retired by a take.

    A dump without a `deck` list answers False: the pre-b1.5.0 behaviour (leave
    the row alone) is the safe default, and it cannot start a cycle.
    """
    deck = _gs(state).get("deck")
    if not isinstance(deck, list) or not deck:
        return False
    # b1.7.0: both thresholds are act-resolved, and BOTH decisions that call
    # this function see the same dump -- so R1's two-screens invariant is
    # unchanged (the act cannot differ between a COMBAT_REWARD row and the
    # CARD_REWARD screen that row opens).
    if len(deck) >= _const("DECK_SIZE_CAP", state):
        return False
    attacks = deck_attack_count(state, table)
    return attacks is not None and attacks < _const("DECK_ATTACK_TARGET", state)


def hand_slot_to_index(slot):
    """`play <slot>` is 1-based and slot 0 means the 10th card (PROTOCOL.md 2).

    Returns the 0-based hand index, or None when the token is not a slot.
    """
    if slot < 0 or slot > 10:
        return None
    return 9 if slot == 0 else slot - 1


# --- card scoring ----------------------------------------------------------

def score_card(card, state, table):
    """(damage, block) for one live hand entry, mirroring policy.cpp score_card.

    An id absent from the S1 registry (or an absent table entry of any kind)
    yields (0, 0) -- cheap-utility fallback. Never raises.
    """
    row = (table or {}).get(card.get("id"))
    if row is None:
        return 0, 0
    upgraded = 1 if (card.get("upgrades") or 0) > 0 else 0

    def _at(key):
        seq = row.get(key) or [0, 0]
        return seq[upgraded] if len(seq) > upgraded else (seq[0] if seq else 0)

    damage = _at("damage")
    block = _at("block")
    if row.get("damage_from_block"):
        # policy.cpp: `sc.damage += cs.player_block`.
        damage += ((_combat(state).get("player") or {}).get("block") or 0)
    if row.get("aoe"):
        # An ALL_ENEMY card lands its damage on every live monster.
        damage *= max(1, len(live_monsters(state)))
    return damage, block


def _score_play(args, state, table):
    hand = _combat(state).get("hand") or []
    try:
        slot = int(args[0])
    except (IndexError, ValueError):
        return PLAY_BASE
    index = hand_slot_to_index(slot)
    if index is None or index >= len(hand):
        return PLAY_BASE
    card = hand[index] or {}
    damage, block = score_card(card, state, table)

    if facing_attack(state):
        score = (PLAY_BASE + damage * DAMAGE_WEIGHT_UNDER_ATTACK
                 + block * block_weight_under_attack(state))
    else:
        score = PLAY_BASE + damage * DAMAGE_WEIGHT + block * BLOCK_WEIGHT

    if damage <= 0 and block <= 0:
        # Cheap utility: among cards that do neither, prefer the cheap one. The
        # bonus is capped below every other term so it only breaks ties.
        cost = card.get("cost")
        cost = CHEAP_UTILITY_MAX if not isinstance(cost, int) or cost < 0 \
            else min(cost, CHEAP_UTILITY_MAX)
        score += CHEAP_UTILITY_MAX - cost

    if len(args) >= 2:
        try:
            target = int(args[1])
        except ValueError:
            return score
        monsters = _combat(state).get("monsters") or []
        if 0 <= target < len(monsters):
            m = monsters[target] or {}
            effective_hp = (m.get("current_hp") or 0) + (m.get("block") or 0)
            if damage > 0 and damage >= effective_hp:
                score += LETHAL_BONUS
        if target == weakest_monster_index(state):
            score += FOCUS_FIRE_BONUS
    return score


# --- screen scoring --------------------------------------------------------

def _score_map(index, state):
    """Map-node preference -- DELIBERATELY INVERTED versus the sim's fuzz policy.

    `tools/fuzz/src/policy.cpp` move_score scores Elite (110) above Monster
    (100) above Boss (90): it is a coverage generator and wants the longest,
    most mechanically varied fights. This driver wants DEPTH -- the number of
    floors a live run survives is the whole point of the policy, because the
    states worth capturing (treasure at 8+, the Act-1 boss at 16-17) only exist
    below a floor the random baseline never reaches. So survival wins: a
    non-combat node beats a monster, a monster beats an elite.
    """
    screen = _screen_state(state)
    if screen.get("boss_available"):
        # `getMapScreenChoices` returns exactly ["boss"] here, so index 0 IS the
        # boss node and `next_nodes` is not the thing being indexed.
        return MAP_BOSS
    nodes = screen.get("next_nodes") or []
    # choice_list and next_nodes are both built from
    # ChoiceScreenUtils.getMapScreenNodeChoices() in one pass, so they are
    # index-parallel (ChoiceScreenUtils.java:678-715, GameStateConverter
    # getMapScreenState).
    if index >= len(nodes):
        return MAP_UNKNOWN_SYMBOL
    symbol = (nodes[index] or {}).get("symbol")
    if symbol in NON_COMBAT_SYMBOLS:
        return MAP_NON_COMBAT
    if symbol == SYMBOL_MONSTER:
        return MAP_MONSTER
    if symbol == SYMBOL_ELITE:
        return elite_map_value(state)
    return MAP_UNKNOWN_SYMBOL


def elite_map_value(state):
    """A1 (b1.7.0): the elite node's map score for this act and this HP.

    Acts 2/3 raise it (a relic-poor deck cannot pay a deep boss), but only
    while the run is above ELITE_APPETITE_HP_FRACTION. Below that the Act-1
    value applies again, so the appetite degrades to the pure-survival Act-1
    behaviour exactly when survival is the binding constraint -- rather than
    flipping the band unconditionally and walking a 30 %-HP run into a
    Gremlin Nob.
    """
    raised = _const("MAP_ELITE", state)
    if raised <= MAP_ELITE:
        return raised
    frac = hp_fraction(state)
    if frac is not None and frac <= ELITE_APPETITE_HP_FRACTION:
        return MAP_ELITE
    return raised


def _score_reward(index, state, table=None):
    """Combat/treasure reward rows.

    THE CLAIM-ORDER TRAP (this is why the key is scored below `proceed`):
    on an Act-1 treasure chest the screen carries a RELIC row and a
    SAPPHIRE_KEY row linked to the same relic. `RewardItem.claimReward`
    (RewardItem.java:255-330) case 6 -- the key -- sets
    `this.relicLink.isDone = true; this.relicLink.ignoreReward = true`, which
    retires the RELIC row WITHOUT granting the relic. Case 4 -- the relic --
    does the mirror image to the key row, which is the harmless direction (the
    key is S2 scope, PROTOCOL.md 3.6). Claiming the key therefore forfeits the
    relic and the capture loses the reward it exists to record. Claim the relic;
    never the key.
    """
    rewards = _screen_state(state).get("rewards") or []
    if index < len(rewards):
        kind = ((rewards[index] or {}).get("reward_type") or "").upper()
    else:
        choices = _choice_list(state)
        kind = (choices[index] if index < len(choices) else "").upper()
    if kind == "SAPPHIRE_KEY":
        return REWARD_SAPPHIRE_KEY
    if kind == "RELIC":
        return REWARD_RELIC
    if kind in ("GOLD", "STOLEN_GOLD"):
        return REWARD_GOLD
    if kind == "POTION":
        return REWARD_POTION
    if kind == "CARD":
        # R1 (b1.5.0). Pre-b1.5.0 this was unconditionally below
        # REWARD_PROCEED: never open the row, because opening it and then
        # judging the cards is how the sim's HOARD_GOLD policy reached a
        # claim/skip 2-cycle (policy.cpp REWARD_CLAIM comment) -- and skipping
        # a card reward really does leave the row on the screen (see
        # `wants_card_reward`). The gate removes the hazard rather than
        # accepting it: the row is only opened when the SAME deck-only
        # predicate that the CARD_REWARD screen will consult says take, so the
        # open is always followed by a take and the row is always retired.
        # A deck at or past its attack target keeps the old behaviour exactly.
        return REWARD_CARD_TAKE if wants_card_reward(state, table) \
            else REWARD_CARD
    return REWARD_CARD


def _card_reward_rank(index, state, table=None):
    """R1: WHICH of the offered cards to take. Never take-vs-skip.

    `choice_list` and `screen_state.cards` are built in one pass over the same
    `rewardGroup` (ChoiceScreenUtils.getCardRewardScreenChoices), so they are
    index-parallel; an index past the card list is the singing-bowl row
    (`bowl_available`) and ranks bottom. Bounded by CARD_RANK_MAX so the whole
    take band stays inside [CARD_REWARD_TAKE_OPEN, +CARD_RANK_MAX].
    """
    cards = _screen_state(state).get("cards") or []
    if index >= len(cards):
        return 0
    card = cards[index] or {}
    ctype = card_type(card, table)
    if ctype == CARD_TYPE_CURSE:
        return 0
    damage, block = score_card(card, state, table)
    rank = damage * CARD_RANK_DAMAGE_WEIGHT + block * CARD_RANK_BLOCK_WEIGHT
    if ((table or {}).get(card.get("id")) or {}).get("aoe"):
        rank += CARD_RANK_AOE_BONUS
    if ctype == CARD_TYPE_ATTACK:
        rank += CARD_RANK_ATTACK_BONUS
    return max(0, min(rank, CARD_RANK_MAX))


def _score_card_reward(index, state, table=None):
    if not wants_card_reward(state, table):
        return CARD_REWARD_TAKE     # < CARD_REWARD_SKIP: skip, as before
    return CARD_REWARD_TAKE_OPEN + _card_reward_rank(index, state, table)


def boss_relic_name(index, state):
    """The offered relic's game id, from `screen_state.relics` or `choice_list`.

    `getBossRewardState` (:320-328) publishes `relics`, and
    `getBossRewardScreenChoices` builds `choice_list` from the SAME list in one
    pass, so the two are index-parallel; the choice list is the fallback for a
    dump whose screen_state slice is absent. "" for an index off the end.
    """
    relics = _screen_state(state).get("relics") or []
    if index < len(relics):
        return ((relics[index] or {}).get("id") or "")
    choices = _choice_list(state)
    return choices[index] if index < len(choices) else ""


def boss_relic_is_takeable(name):
    """R4's never-take list; see the module header for the per-relic reason."""
    return name not in BOSS_RELIC_NEVER_TAKE


def _score_boss_reward(index, state, table=None):
    """R4: the boss-chest relic pick.

    In the SKIP cohort every relic scores below `skip` and none is taken. In
    the TAKE cohort a takeable relic outranks `skip` and one of the five
    never-take relics does not, so a chest offering three of those five skips
    by construction rather than picking one that would silently unseat a rule
    above.

    All takeable relics score EQUAL. The choice among them is left to the
    one-draw tie-break in `pick`, which keeps the decision a pure function of
    (policy_seed, seed): a preference order over BOSS relics would be a taste
    judgement with no captured evidence behind it, and R1/R2/R3's precedent is
    that a rule ships with the evidence that motivated it.
    """
    if BOSS_RELIC_SKIP_MODE:
        return BOSS_RELIC_AVOID
    name = boss_relic_name(index, state)
    return BOSS_RELIC_TAKE if boss_relic_is_takeable(name) \
        else BOSS_RELIC_AVOID


def _score_rest(index, state):
    choices = _choice_list(state)
    option = (choices[index] if index < len(choices) else "").lower()
    gs = _gs(state)
    max_hp = gs.get("max_hp") or 0
    current = gs.get("current_hp") or 0
    hurt = max_hp > 0 and (float(current) / float(max_hp)) <= REST_HP_FRACTION
    if option == REST_HEAL:
        return REST_PREFERRED if hurt else REST_SECONDARY
    if option == REST_SMITH:
        return REST_SECONDARY if hurt else REST_PREFERRED
    return REST_OTHER


def _score_event(index, state):
    choices = _choice_list(state)
    text = (choices[index] if index < len(choices) else "").lower()
    if any(word in text for word in EVENT_RISKY_WORDS):
        return EVENT_RISKY
    if any(word in text for word in EVENT_SAFE_WORDS):
        return EVENT_SAFE
    return EVENT_NEUTRAL


def _score_choose(index, state, table=None):
    screen = _gs(state).get("screen_type")
    if screen == "MAP":
        return _score_map(index, state)
    if screen == "COMBAT_REWARD":
        return _score_reward(index, state, table)
    if screen == "CARD_REWARD":
        return _score_card_reward(index, state, table)
    if screen == "BOSS_REWARD":
        return _score_boss_reward(index, state, table)
    if screen == "CHEST":
        return CHEST_OPEN
    if screen == "REST":
        return _score_rest(index, state)
    if screen == "SHOP_ROOM":
        return SHOP_ROOM_ENTER
    if screen == "SHOP_SCREEN":
        return SHOP_SCREEN_BUY
    if screen == "EVENT":
        return _score_event(index, state)
    if screen in ("GRID", "HAND_SELECT"):
        return SELECT_PICK
    return DEFAULT_CHOOSE


def _score_alias(verb, state, table=None):
    """`proceed` / `confirm` / `skip` / `cancel` / `return` / `leave`.

    `confirm` and `proceed` are the same button whenever both can appear
    (campaign_driver.expand_legal_actions and cmd_verb_ready already treat
    them as interchangeable -- `any(a in avail for a in ("confirm",
    "proceed"))` -- so scoring unifies them too.
    """
    screen = _gs(state).get("screen_type")
    if verb in ("proceed", "confirm"):
        if screen == "COMBAT_REWARD":
            return REWARD_PROCEED
        if screen == "SHOP_ROOM":
            # Buy nothing (gold hoarding is fine), and do not even walk in:
            # entering re-presents the same SHOP_ROOM `choose shop` afterwards,
            # so preferring the shop would be an unbounded enter/leave cycle
            # that the driver's stuck detector cannot see (its signature
            # alternates between the two screens).
            return SHOP_ROOM_LEAVE
        if screen in ("GRID", "HAND_SELECT"):
            screen_state = _screen_state(state)
            confirm_up = screen_state.get("confirm_up")
            if confirm_up is None:
                # HAND_SELECT carries no confirm_up field (PROTOCOL.md
                # 3.19); fall back to the selection count.
                selected = screen_state.get("selected") or \
                    screen_state.get("selected_cards") or []
                confirmable = bool(selected)
            else:
                # GRID is authoritative here -- confirm_up is the game's own
                # signal that a selection is committable. `selected_cards`
                # is NOT a safe substitute: the live dump has been observed
                # reporting it EMPTY (`[]`) on a GRID screen that also
                # reports confirm_up: true, i.e. after a real selection was
                # made (pilot campaign
                # b4x_greedy_pilot_20260728T041406Z_claude01: 8/6
                # proceed/cancel ties over 14 decisions, one run lost to
                # noop_wedge at STS00275 seq 54-59, because the old
                # selected_cards-only gate scored proceed level with cancel
                # and the tie-break RNG re-opened the grid).
                confirmable = bool(confirm_up)
            return SELECT_CONFIRM if confirmable else DEFAULT_CANCEL
        return DEFAULT_PROCEED
    # skip / cancel / return / leave
    if screen == "BOSS_REWARD":
        # R4. `skip` is the BOSS_REWARD cancel button
        # (ChoiceScreenUtils.getCancelButtonText -> "skip"). In the skip cohort
        # it must beat every relic; in the take cohort it sits between a
        # takeable relic and a never-take one, so a chest offering only
        # never-take relics leaves without picking.
        return BOSS_RELIC_SKIP_WINS if BOSS_RELIC_SKIP_MODE \
            else BOSS_RELIC_SKIP
    if screen == "CARD_REWARD":
        # Unchanged constant; R1 moves the CARDS above it, not this below them,
        # so a closed gate still skips and an absent side table still skips.
        return CARD_REWARD_SKIP
    if screen == "SHOP_SCREEN":
        return SHOP_SCREEN_LEAVE
    if screen == "MAP":
        return MAP_LEAVE_SCREEN
    return DEFAULT_CANCEL


def belt_is_full(state):
    """Every potion slot occupied: a held potion is now blocking a pickup."""
    potions = _gs(state).get("potions") or []
    if not potions:
        return False
    return all((p or {}).get("id") not in EMPTY_POTION_IDS for p in potions)


def potion_worth_spending(state):
    """R2: is this the fight to spend a potion in?

    Boss and elite rooms yes; any room once HP has fallen to the floor; and any
    room where the belt is already full, because a potion that cannot be
    replaced by a pickup has stopped being a saved resource. Everything else
    holds -- six STS01221 captures walked into floor 16 with a two-slot belt
    holding at most one potion, two of them having spent the last one on floor
    8 or 12.
    """
    gs = _gs(state)
    if gs.get("room_type") in HIGH_STAKES_ROOMS:
        return True
    # A3 (b1.7.0): from POTION_HIGH_STAKES_FROM_ACT onwards a NORMAL fight is
    # high-stakes too. Deliberately not folded into HIGH_STAKES_ROOMS: that
    # tuple is not reachable from the numeric cohort-config surface, and a
    # cohort must be able to move this.
    if act_of(state) >= POTION_HIGH_STAKES_FROM_ACT and \
            gs.get("room_type") in ANY_COMBAT_ROOMS:
        return True
    frac = hp_fraction(state)
    if frac is not None and frac <= _const("POTION_LOW_HP_FRACTION", state):
        return True
    return belt_is_full(state)


def _score_potion(args, state):
    if args and args[0] == "discard":
        return POTION_DISCARD
    in_combat = bool(_combat(state).get("monsters"))
    if not in_combat:
        # Out of combat the belt-overflow unstick path is the only user of
        # this, and it is unchanged.
        return POTION_USE_OUT_OF_COMBAT
    if potion_worth_spending(state):
        return POTION_USE_COMBAT
    return POTION_HOLD


# --- the scorer ------------------------------------------------------------

def score_action(cmd, state, table=None):
    """Preference for one concrete command in one parsed state. Higher wins.

    Pure: no I/O, no globals, no RNG. Unknown verbs and unparseable arguments
    fall through to a neutral score rather than raising -- a policy that throws
    inside a live capture costs the whole run.
    """
    parts = (cmd or "").split()
    if not parts:
        return 0
    verb, args = parts[0], parts[1:]
    if verb == "play":
        return _score_play(args, state, table)
    if verb == "end":
        return END_TURN
    if verb == "potion":
        return _score_potion(args, state)
    if verb == "choose":
        try:
            index = int(args[0])
        except (IndexError, ValueError):
            return DEFAULT_CHOOSE
        return _score_choose(index, state, table)
    if verb in _ALIAS_VERBS:
        return _score_alias(verb, state, table)
    return 0


def pick(actions, state, table=None, rng=None):
    """Argmax of `score_action` over `actions`, with a one-draw tie-break.

    `actions` must come from `campaign_driver.expand_legal_actions`; the return
    value is always one of them (or None for an empty list), which is what makes
    the greedy policy legal-by-construction exactly as random-legal is.
    """
    if not actions:
        return None
    scores = [score_action(cmd, state, table) for cmd in actions]
    best = max(scores)
    maxima = [cmd for cmd, score in zip(actions, scores) if score == best]
    if rng is None or len(maxima) == 1:
        return maxima[0]
    return rng.choice(maxima)
