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
LETHAL_BONUS = 400          # kill something this turn -> one fewer enemy turn
FOCUS_FIRE_BONUS = 2        # policy.cpp's +2: breaks target ties only
CHEAP_UTILITY_MAX = 3       # 0-cost utility edges out 3-cost utility, by <= 3
POTION_USE_COMBAT = 500     # below any card play, above ending the turn
POTION_USE_OUT_OF_COMBAT = 20
POTION_DISCARD = 0
END_TURN = 1

# Map.
MAP_BOSS = 700
MAP_NON_COMBAT = 600
MAP_MONSTER = 400
MAP_UNKNOWN_SYMBOL = 300
MAP_ELITE = 200
MAP_LEAVE_SCREEN = 0        # `return` backs out of the map without moving

# Combat / treasure rewards.
REWARD_RELIC = 900
REWARD_GOLD = 850
REWARD_POTION = 800
REWARD_PROCEED = 100        # anything below this is left unclaimed
REWARD_CARD = 60            # < REWARD_PROCEED: the card row is never opened
REWARD_SAPPHIRE_KEY = 0     # < REWARD_PROCEED: NEVER claim -- see _score_reward

# Card-reward screen.
CARD_REWARD_SKIP = 900
CARD_REWARD_TAKE = 10

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

_ALIAS_VERBS = ("skip", "cancel", "return", "leave", "proceed")


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


def facing_attack(state):
    """True if any live monster's banner shows an incoming attack.

    `intent` is display-derived (PROTOCOL.md 3.12) and reads DEBUG until the
    banner refreshes, so this is advisory -- it can only ever make the policy
    block one turn later than ideal, never make it emit an illegal command.
    `move_adjusted_damage > 0` is the second witness for the same fact.
    """
    for _i, m in live_monsters(state):
        intent = (m.get("intent") or "").upper()
        if "ATTACK" in intent:
            return True
        if (m.get("move_adjusted_damage") or 0) > 0:
            return True
    return False


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
                 + block * BLOCK_WEIGHT_UNDER_ATTACK)
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
        return MAP_ELITE
    return MAP_UNKNOWN_SYMBOL


def _score_reward(index, state):
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
        # Below REWARD_PROCEED on purpose. Opening the row opens the
        # CARD_REWARD screen, and skipping is always safe, so the cheapest
        # survival default is never to open it: fewer actions, no deck
        # dilution, and no way to reach the claim/skip 2-cycle that the sim's
        # HOARD_GOLD policy fell into (policy.cpp REWARD_CLAIM comment).
        return REWARD_CARD
    return REWARD_CARD


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


def _score_choose(index, state):
    screen = _gs(state).get("screen_type")
    if screen == "MAP":
        return _score_map(index, state)
    if screen == "COMBAT_REWARD":
        return _score_reward(index, state)
    if screen == "CARD_REWARD":
        return CARD_REWARD_TAKE
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


def _score_alias(verb, state):
    """`proceed` / `skip` / `cancel` / `return` / `leave`."""
    screen = _gs(state).get("screen_type")
    if verb == "proceed":
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
            selected = _screen_state(state)
            picked = selected.get("selected") or selected.get(
                "selected_cards") or []
            # Confirm only once something is selected -- an empty confirm is
            # either refused or a no-op on most select screens.
            return SELECT_CONFIRM if picked else DEFAULT_CANCEL
        return DEFAULT_PROCEED
    # skip / cancel / return / leave
    if screen == "CARD_REWARD":
        return CARD_REWARD_SKIP
    if screen == "SHOP_SCREEN":
        return SHOP_SCREEN_LEAVE
    if screen == "MAP":
        return MAP_LEAVE_SCREEN
    return DEFAULT_CANCEL


def _score_potion(args, state):
    if args and args[0] == "discard":
        return POTION_DISCARD
    in_combat = bool(_combat(state).get("monsters"))
    return POTION_USE_COMBAT if in_combat else POTION_USE_OUT_OF_COMBAT


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
        return _score_choose(index, state)
    if verb in _ALIAS_VERBS:
        return _score_alias(verb, state)
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
