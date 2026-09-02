#!/usr/bin/env python3
"""`--policy external` script follower: replay an STS-SCRIPT v1 line (S2.V2).

WHAT THIS IS. The live half of the sim-consulting scripted driver. The
planner (`seed_scan --script-dir`, tools/oracle_bridge/planner) selects
(seed, policy, policy_seed) triples whose SIM line reaches an S2-G2 depth
target and emits each line as an STS-SCRIPT v1 file: one JSON object per
decision, in a vocabulary of screen kind + STABLE IDENTITY of the chosen
thing (card game id + upgrades + same-identity ordinal, reward-row kind +
payload id, map column + symbol, event option index in the game's own
enabled-only `choose` space). This binary serves STS-POLICY-IO v1 (the same
seam as survival_policy_cmd.py, SHA-pinned into the campaign identity the
same way) and, per decide request, matches the script's next step against the
live CommunicationMod dump to produce the concrete command.

THE STOP CONTRACT (the load-bearing design decision). On ANY mismatch --
the advertised screen is not the one the step names, the identified card /
relic / reward row is absent, the derived command is not among the driver's
legal candidates, the script runs out while the game still asks -- this
binary writes a divergence record (JSON, into the script's directory or the
configured `divergence_dir`) and EXITS NON-ZERO. Over STS-POLICY-IO that is a
broken pipe; campaign_driver.ExternalPolicy latches `broken` and the
campaign stops durably on its FatalEnvironmentDrift path. A desync between
the sim's scripted line and the live game IS the capture evidence S2.43's
depth cohorts exist to collect -- it is never something to improvise around,
so there is deliberately no fallback policy, no re-match, no skipping of
steps.

GLUE COMMANDS (the known granularity seams, all driver-visible facts rather
than improvisation). Three of them are evaluated ONLY AFTER the script's next
step failed to match the live state (match-first, so a scripted decision is
never glued past); the fourth is keyed on a screen the sim provably never
records a step for, and is therefore evaluated BEFORE the match:
  * a state whose ONLY PROGRESS candidate is `proceed` answers `proceed`
    without consuming a script step -- the sim had no decision to record
    where the live game shows a confirmation-only screen. "Progress"
    excludes EVERY BELT COMMAND -- `potion discard N` and `potion use N
    [t]` alike. CommunicationMod advertises the `potion` verb on ANY
    screen while the belt holds a discardable/usable potion
    (campaign_driver.expand_legal_actions emits one command per
    `can_discard` slot and one per `can_use` slot, independently of the
    screen), and neither form gates the screen's own progress, so neither
    can be what a scripted step means. Third live witness,
    divergence_STS100439_ps0: the post-rest campfire screen offers two
    potion discards beside its leave-`proceed`. Eighth live witness,
    divergence_STS205404_ps17: the SAME vestigial REST screen arrives as
    `['potion discard 0', 'potion use 1', 'potion discard 1', 'proceed']`
    -- the belt held an out-of-combat-USABLE potion (the Blood Potion /
    Fruit Juice / Entropic Brew class), so a discard-only filter no
    longer left `proceed` alone and the follower stopped on a screen the
    recorded corpus answers with `proceed` every time it appears
    (tests/golden/oracle_corpus: `three_act_a20_5` carries that screen
    with a non-empty belt, `act1_a20_50` with an empty one -- STS71037
    seq 77-81 walks the whole smith, rest->smith, GRID pick, GRID
    confirm, THIS screen, map -- and both corpora show `potion use N`
    advertised beside the progress command on MAP / CHEST / EVENT /
    CARD_REWARD / COMBAT_REWARD / BOSS_REWARD / SHOP). The same wave
    produced the shape after the plain HEAL option too
    (divergence_STS216298_ps107, floor 46: `rest opt=rest` then the map,
    with the screen between them) and with a one-slot belt
    (divergence_STS227212_ps88, and ps20 of the same seed as ps17), so
    it follows EVERY rest-site option and one usable potion is enough. A
    scripted `potion` or `potion_discard` still matches FIRST and is
    consumed, so the sim's own belt actions are never glued past;
  * a GRID whose `confirm_up` is set (the game's own signal that a selection
    is committable; see campaign_driver.expand_legal_actions's B5.2 note)
    answers `proceed` without consuming a step when the next step is not
    another pick on the same grid -- the sim's grid pick is one action where
    the live grid wants pick-then-confirm;
  * a screen whose ONLY PROGRESS candidate is a single `choose` answers it
    without consuming a step -- the collapsed one-click dialog (first live
    witness: the s2v2_take campaign's divergence_STS100009_ps0 at Neow's
    opening `talk`). The engine deliberately collapses no-state-change
    dialog clicks (the Woman in Blue / Sensory Stone precedent, argued at
    shrines.cpp applyResult), so a sim-emitted line records no decision
    there; a one-candidate click also carries no information, so answering
    it cannot steer the run. Match-first still holds: a single-option
    choice the sim DID record is consumed, never glued past, and a genuine
    desync surfaces at the next screen with the cursor unmoved. "Sole"
    reads the same PROGRESS filter as rule 1 -- the belt commands are
    excluded here too (divergence_STS221674_ps7, floor 39, Sensory
    Stone: the event's closing one-click `leave` page arrives as
    `['choose 0', 'potion use 0', 'potion discard 0']`, and counting the
    belt left the collapsed click looking like a three-way decision) --
    and the command answered is that sole progress candidate, not
    `candidates[0]`: the belt commands carry no guaranteed position in
    the list, and answering one would spend a potion.
  * the `COMPLETE` SCREEN answers `proceed` without consuming a step, and
    is the ONE rule evaluated BEFORE the match. `COMPLETE` is
    ChoiceScreenUtils' label (:80-83) for a room at RoomPhase.COMPLETE with
    no screen over it, and in S2's scope it has exactly two producers --
    the two Act-3 boss rooms AbstractRoom.java:327 denies a reward screen
    to. The engine has ALREADY made both transitions when the capture shows
    the button: the first Act-3 kill runs ProceedButton's `goToDoubleBoss`
    (:210-220) inline off the kill (S2.28's deliberate collapse; engine
    commit 3481c08 models that handoff end to end and the replay layer maps
    this press as a NOOP), and the second press is the run terminal
    (:104-105). The sim therefore records NO decision here, ever -- which
    is why this rule cannot be match-first. Tenth live witness,
    divergence_STS205404_ps20 (s2v3_wave2, floor 50 -> 51): the line killed
    the Time Eater at seq 893, the handoff `COMPLETE` arrived at seq 894 as
    `['proceed']`, and the NEXT scripted step was Gambling Chip's floor-51
    `confirm` (the optional turn-1 hand-select prompt, discarding nothing).
    Under match-first that `confirm` false-matched the handoff press
    through the confirm/proceed alias and was consumed; the live Gambling
    Chip `HAND_SELECT` then met the FOLLOWING `end` step and the follower
    stopped with "derived command 'end' for end turn is not among the 9
    legal candidates" -- a stop with no engine defect behind it. The three
    earlier double-boss captures (tests/golden/oracle_corpus/
    three_act_a20_5: STS128113 seq 655/666, STS103509 seq 622/662,
    STS105835 seq 680) survived only because their next scripted step was a
    `play`/`end` that could not match, so glue rule 1 answered instead --
    the seam was one relic away from firing the whole time. Screen-keyed,
    not sole-progress-keyed: the label names a press with no decision in
    it, whatever else the belt advertises beside it.
One SKIP rule mirrors the seam in the other direction (second live
witness, same first campaign, step 2 of the same run): the sim records a
`proceed`/`confirm` where the live game auto-advances (Neow's blessing
click opens the MAP directly; the sim leaves the event with an explicit
proceed). A scripted proceed-kind step whose state offers NEITHER alias
can never match any candidate, so it is consumed without emitting a
command and matching retries the next step against the same state. A real
desync is still caught: the following step will not match either, and the
divergence record lands one step later with the mismatch named.
Anything else that fails to match stops the run, by design.

A `choose_card` step carrying `skip: 1` is the DISCOVERY SKIP, not an
identity join: the sim pressed the generated screen's Skip button, so the
step names no card and `card` is the empty string (script.cpp's COMBAT arm
writes `card: ""` + `skip: 1` for CHOOSE(kChooseSkipCard); the button is
legal only on a TYPED discovery -- `ActionMask::can_skip_choice` is false
unless `choice_from_generated`, from DiscoveryAction.java:49 and
CardRewardScreen.java:485-500). `skip` is honoured wherever it appears on a
`choose_card` step -- regardless of `src`, regardless of `card` -- because
the flag IS the decision and the empty identity is only its consequence;
reading `card` first is what produced the ninth live witness,
divergence_STS209702_ps255 at floor 50, where the follower tried to join
`''+0` against a live CARD_REWARD offering flex / bloodletting / entrench
and stopped on a step that had already said what it wanted. This is a
matcher arm, not glue: the step IS consumed, and a screen offering no skip
alias still stops.

CONFIG (`--config <json>`), strict like survival_policy_cmd:

    {
      "script_dir": "path/to/scripts",   # required: the seed_scan --script-dir
      "policy": "sim_search",            # sim policy in the file names
                                         # (default sim_search)
      "divergence_dir": "path"           # optional: where divergence records
                                         # land (default: script_dir)
    }

Script files are looked up as `<seed>__<policy>__ps<policy_seed>.script.jsonl`
using each request's own seed and policy_seed, so one config drives a whole
cohort of seeds.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time

PROTOCOL = "STS-POLICY-IO v1"
SCRIPT_FORMAT = "STS-SCRIPT v1"

# Exit code for a script/game desync (distinct from 2 = config/protocol).
EXIT_DIVERGED = 3


class ConfigError(ValueError):
    pass


class Divergence(Exception):
    """The live game and the scripted line disagree. Stop; do not improvise."""

    def __init__(self, reason, step=None):
        super().__init__(reason)
        self.reason = reason
        self.step = step


# --- dump accessors (tolerant of absent sub-objects, like greedy_policy) ----

def _gs(state):
    return (state or {}).get("game_state") or {}


def _screen(state):
    return _gs(state).get("screen_state") or {}


def _choice_list(state):
    return _gs(state).get("choice_list") or []


def _require(cmd, candidates, step, what):
    if cmd not in candidates:
        raise Divergence(f"derived command {cmd!r} for {what} is not among "
                         f"the {len(candidates)} legal candidates", step)
    return cmd


def _nth_index(entries, pred, ord_):
    """Index of the ord_-th entry satisfying pred, or None."""
    n = 0
    for i, e in enumerate(entries):
        if pred(e):
            if n == ord_:
                return i
            n += 1
    return None


# --- per-kind matchers ------------------------------------------------------

def _match_play(step, state, candidates):
    hand = (_gs(state).get("combat_state") or {}).get("hand") or []
    idx = _nth_index(
        hand,
        lambda c: (c or {}).get("id") == step.get("card")
        and ((c or {}).get("upgrades") or 0) == (step.get("up") or 0),
        step.get("ord") or 0)
    if idx is None:
        raise Divergence(
            f"hand has no #{step.get('ord') or 0} copy of "
            f"{step.get('card')!r}+{step.get('up') or 0}", step)
    hidx = idx + 1 if idx + 1 < 10 else (0 if idx + 1 == 10 else idx + 1)
    target = step.get("t", -1)
    cmd = f"play {hidx}" if target is None or target < 0 \
        else f"play {hidx} {target}"
    return _require(cmd, candidates, step, "play")


def _match_potion(step, state, candidates):
    slot = step.get("slot")
    potions = _gs(state).get("potions") or []
    live = potions[slot] if isinstance(slot, int) and slot < len(potions) \
        else None
    if not live or live.get("id") != step.get("potion"):
        raise Divergence(
            f"potion slot {slot} holds "
            f"{(live or {}).get('id')!r}, script says "
            f"{step.get('potion')!r}", step)
    target = step.get("t", -1)
    if target is None or target < 0:
        # The sim recorded no target. CommunicationMod expands SOME
        # untargeted potions with a target argument anyway (sixth live
        # witness, divergence_STS108107_ps153: Explosive Potion is
        # `potion use 0 0` live) -- accept the bare form, else a UNIQUE
        # same-slot targeted form; two or more target variants would be a
        # real target decision the sim failed to record, so that stops.
        cmd = f"potion use {slot}"
        if cmd in candidates:
            return cmd
        prefixed = sorted(c for c in candidates
                          if str(c).startswith(f"potion use {slot} "))
        if prefixed:
            # Seventh witness (skip_108173, floor 20): with several
            # monsters alive the game offers one variant per target for a
            # potion whose EFFECT ignores the target (Explosive hits all).
            # The lowest-index variant is the deterministic canonical
            # pick; if the target ever did matter, the sim's untargeted
            # model would diverge downstream and the stop contract still
            # catches it -- one step later, with the state named.
            return prefixed[0]
        raise Divergence(
            f"untargeted potion use {slot} has no live form "
            f"(candidates {candidates})", step)
    return _require(f"potion use {slot} {target}", candidates, step,
                    "potion use")


def _match_potion_discard(step, state, candidates):
    slot = step.get("slot")
    return _require(f"potion discard {slot}", candidates, step,
                    "potion discard")


def _match_map(step, state, candidates):
    screen = _screen(state)
    if _gs(state).get("screen_type") != "MAP":
        raise Divergence(
            f"script expects the MAP screen, game shows "
            f"{_gs(state).get('screen_type')!r}", step)
    if screen.get("boss_available"):
        raise Divergence("script names a map column but the game offers "
                         "only the boss node", step)
    nodes = screen.get("next_nodes") or []
    want_x = step.get("x")
    want_sym = step.get("sym")
    for i, node in enumerate(nodes):
        if (node or {}).get("x") == want_x and \
                (not want_sym or (node or {}).get("symbol") == want_sym):
            return _require(f"choose {i}", candidates, step, "map node")
    raise Divergence(
        f"no next node at x={want_x} sym={want_sym!r} "
        f"(game offers {[(n.get('x'), n.get('symbol')) for n in nodes]})",
        step)


def _match_map_boss(step, state, candidates):
    if not _screen(state).get("boss_available"):
        raise Divergence("script takes the boss edge but the game does not "
                         "offer it", step)
    # getMapScreenChoices returns exactly ["boss"] here (greedy_policy
    # _score_map cites the same fact), so index 0 IS the boss node.
    return _require("choose 0", candidates, step, "boss edge")


_REWARD_TYPES = {"GOLD", "STOLEN_GOLD", "RELIC", "POTION", "CARD",
                 "SAPPHIRE_KEY", "EMERALD_KEY"}


def _reward_row_matches(row, step):
    row = row or {}
    if (row.get("reward_type") or "").upper() != step.get("rtype"):
        return False
    want_id = step.get("id")
    if want_id:
        payload = row.get("relic") or row.get("potion") or {}
        got = payload.get("id") or payload.get("name")
        if got is not None and got != want_id:
            return False
    return True


def _match_claim(step, state, candidates):
    rewards = _screen(state).get("rewards") or []
    idx = _nth_index(rewards, lambda r: _reward_row_matches(r, step),
                     step.get("ord") or 0)
    if idx is None:
        raise Divergence(
            f"reward screen has no #{step.get('ord') or 0} "
            f"{step.get('rtype')} row (id {step.get('id')!r}); game offers "
            f"{[(r or {}).get('reward_type') for r in rewards]}", step)
    return _require(f"choose {idx}", candidates, step, "reward claim")


def _match_take_card(step, state, candidates):
    cards = _screen(state).get("cards") or []
    idx = _nth_index(
        cards,
        lambda c: (c or {}).get("id") == step.get("card")
        and ((c or {}).get("upgrades") or 0) == (step.get("up") or 0),
        step.get("ord") or 0)
    if idx is None:
        raise Divergence(
            f"card screen has no #{step.get('ord') or 0} copy of "
            f"{step.get('card')!r}+{step.get('up') or 0}; game offers "
            f"{[(c or {}).get('id') for c in cards]}", step)
    return _require(f"choose {idx}", candidates, step, "card take")


def _match_grid(step, state, candidates):
    screen = _screen(state)
    cards = screen.get("cards") or screen.get("hand") or []
    idx = _nth_index(
        cards,
        lambda c: (c or {}).get("id") == step.get("card")
        and ((c or {}).get("upgrades") or 0) == (step.get("up") or 0),
        step.get("ord") or 0)
    if idx is None:
        raise Divergence(
            f"grid has no #{step.get('ord') or 0} copy of "
            f"{step.get('card')!r}+{step.get('up') or 0}", step)
    return _require(f"choose {idx}", candidates, step, "grid pick")


def _match_event(step, state, candidates):
    if _gs(state).get("screen_type") != "EVENT":
        raise Divergence(
            f"script expects an EVENT screen, game shows "
            f"{_gs(state).get('screen_type')!r}", step)
    want = step.get("event")
    shown = _screen(state).get("event_id") or _screen(state).get("event_name")
    if want and shown and shown != want:
        raise Divergence(
            f"script expects event {want!r}, game shows {shown!r}", step)
    index = step.get("index")
    if index is None or index < 0:
        raise Divergence("script step carries no enabled-option index", step)
    return _require(f"choose {index}", candidates, step, "event option")


def _match_by_choice_name(step, state, candidates, name, what):
    choices = _choice_list(state)
    for i, label in enumerate(choices):
        if str(label).lower() == name:
            return _require(f"choose {i}", candidates, step, what)
    raise Divergence(f"{what}: no choice named {name!r} in {choices}", step)


def _match_boss_pick(step, state, candidates):
    relics = _screen(state).get("relics") or []
    for i, relic in enumerate(relics):
        if (relic or {}).get("id") == step.get("relic"):
            return _require(f"choose {i}", candidates, step,
                            "boss relic pick")
    raise Divergence(
        f"boss chest does not offer {step.get('relic')!r}; game offers "
        f"{[(r or {}).get('id') for r in relics]}", step)


def _match_alias(step, state, candidates, aliases, what):
    for alias in aliases:
        if alias in candidates:
            return alias
    raise Divergence(f"{what}: none of {aliases} is legal "
                     f"(candidates {candidates})", step)


def match_step(step, state, candidates):
    """One script step -> the concrete live command, or raise Divergence."""
    kind = step.get("k")
    if kind == "play":
        return _match_play(step, state, candidates)
    if kind == "end":
        return _require("end", candidates, step, "end turn")
    if kind == "potion":
        return _match_potion(step, state, candidates)
    if kind == "potion_discard":
        return _match_potion_discard(step, state, candidates)
    if kind == "confirm":
        return _match_alias(step, state, candidates, ("proceed", "confirm"),
                            "confirm")
    if kind == "map":
        return _match_map(step, state, candidates)
    if kind == "map_boss":
        return _match_map_boss(step, state, candidates)
    if kind == "claim":
        return _match_claim(step, state, candidates)
    if kind == "take_card":
        return _match_take_card(step, state, candidates)
    if kind == "skip_card":
        return _match_alias(step, state, candidates, ("skip", "cancel"),
                            "card skip")
    if kind == "sing":
        return _match_by_choice_name(step, state, candidates, "singing bowl",
                                     "singing bowl")
    if kind in ("neow", "event"):
        if kind == "neow":
            index = step.get("index")
            if index is None:
                raise Divergence("neow step carries no index", step)
            # The blessing menu always offers several options; a one-option
            # screen here is the opening `talk` (or the post-blessing result
            # dialog), which glue rule 3 answers without consuming. Without
            # this guard a blessing with index 0 false-matches the talk
            # click and the cursor runs ahead of the game (fifth live
            # witness: s2v2_skip_b, STS108173, `neow index 0`).
            chooses = [c for c in candidates
                       if str(c).startswith("choose ")]
            if len(chooses) <= 1:
                raise Divergence(
                    "neow blessing cannot match a one-option screen "
                    f"(candidates {candidates})", step)
            return _require(f"choose {index}", candidates, step,
                            "neow blessing")
        return _match_event(step, state, candidates)
    if kind == "proceed":
        return _match_alias(step, state, candidates, ("proceed",), "proceed")
    if kind == "rest":
        return _match_by_choice_name(step, state, candidates, step.get("opt"),
                                     "rest option")
    if kind == "grid":
        return _match_grid(step, state, candidates)
    if kind == "grid_cancel":
        return _match_alias(step, state, candidates,
                            ("cancel", "return", "leave"), "grid cancel")
    if kind in ("open_chest", "boss_open"):
        return _match_by_choice_name(step, state, candidates, "open",
                                     "chest open")
    if kind == "boss_pick":
        return _match_boss_pick(step, state, candidates)
    if kind == "boss_skip":
        return _match_alias(step, state, candidates, ("skip", "cancel"),
                            "boss relic skip")
    if kind == "choose_card":
        # The Skip button on a typed discovery screen (module header):
        # `skip` is the decision, and the empty `card` it comes with is
        # only the consequence -- so the flag is read BEFORE any identity
        # join, on every `src`. Today only `src: "generated"` can carry it
        # (can_skip_choice is false unless choice_from_generated), but the
        # flag, not the source, is what this arm keys on.
        if step.get("skip"):
            return _match_alias(step, state, candidates, ("skip", "cancel"),
                                "discovery skip")
        # Combat choice screens (GRID / HAND_SELECT / the discovery card
        # screen): match by identity over whichever card list the screen
        # carries; the sim-side index is provenance, not the join key.
        #
        # NO DESELECT ARM, deliberately. The sim's OPTIONAL hand-select
        # toggles, so a select can be taken back by choosing the same card
        # again; that is an emitter concern and the emitter owns it (it drops
        # a cancelling run of toggles and emits the net selection --
        # planner/script.cpp `optional_hand_select_open`). Mirroring it here
        # would be wrong twice over: HandCardSelectScreen moves a picked card
        # OUT of `hand` into `selectedCards` and CommunicationMod publishes
        # only `hand`, so a card the screen STILL lists has demonstrably not
        # been picked -- a second `choose` of it is a genuine desync, and
        # re-reading it as a deselect would send a `choose` that selects a
        # SECOND copy and then silently walk on. Stop instead.
        return _match_grid(step, state, candidates)
    raise Divergence(f"script step kind {kind!r} has no live matcher "
                     "(this policy never emits it)", step)


def progress_candidates(candidates):
    """Candidates minus every BELT command (module header, glue rule 1).

    `potion discard N` AND `potion use N [t]`: CommunicationMod advertises
    the `potion` verb on any screen while the belt holds a
    discardable/usable potion -- expand_legal_actions emits one command per
    `can_discard` slot and one per `can_use` slot with no reference to the
    screen -- and neither form gates that screen's own progress. Filtering
    only the discards left `potion use N` counting as a decision on the
    vestigial post-smith REST screen (divergence_STS205404_ps17), which is
    exactly the confirmation-only shape this filter exists to expose. A
    scripted `potion`/`potion_discard` matches BEFORE any glue rule
    consults this filter, so the sim's own belt actions are never glued
    past."""
    return [c for c in candidates
            if not str(c).startswith("potion ")]


def sole_choice_glue(candidates):
    """Glue rule 3 -- the collapsed one-click dialog (module header): the
    screen's ONLY PROGRESS candidate is a single `choose`, so there is no
    decision to make and the sim-emitted line recorded none. Returns that
    command, or None. Evaluated only AFTER the script's next step failed to
    match this state (match-first), so a scripted single-option choice is
    consumed rather than glued past.

    It returns the progress candidate rather than `candidates[0]`: the belt
    commands `progress_candidates` filters out carry no guaranteed position
    in the candidate list, and answering one of them here would spend a
    potion instead of clicking the dialog."""
    progress = progress_candidates(candidates)
    if len(progress) == 1 and str(progress[0]).startswith("choose "):
        return progress[0]
    return None


def is_complete_screen_glue(state, candidates):
    """Glue rule 4 -- the `COMPLETE` screen (module header).

    ChoiceScreenUtils :80-83 labels a room at RoomPhase.COMPLETE with no
    screen over it; in S2's scope that is only the two Act-3 boss rooms
    AbstractRoom.java:327 denies a reward screen to, and the engine has
    made both crossings already when the capture shows the button (the
    handoff `proceed` maps to a replay NOOP, the finished-Act-3 one to the
    run terminal -- tests/replay_command_map_test.cpp's COMPLETE section).
    The sim records no decision here, so the press is glue.

    This is the ONE rule evaluated BEFORE the match, and it has to be: the
    press's live command is `proceed`, which is exactly what a scripted
    `confirm`/`proceed` step's alias set answers, so match-first hands the
    screen a step that belongs to the NEXT floor (the tenth live witness,
    divergence_STS205404_ps20, is Gambling Chip's turn-1 hand-select
    `confirm` eaten by the handoff). Screen-keyed rather than
    sole-progress-keyed: what makes this press decision-free is the label,
    not how many commands the belt puts beside it."""
    return (_gs(state).get("screen_type") == "COMPLETE"
            and "proceed" in candidates)


def is_grid_confirm_glue(state, candidates):
    """The GRID pick-then-confirm seam (module header): a committable grid
    selection whose remaining progress command is `proceed`. Evaluated only
    AFTER the script's next step failed to match this state, so a still-
    matching pick or a scripted confirm is never glued past."""
    gs = _gs(state)
    if gs.get("screen_type") not in ("GRID", "HAND_SELECT"):
        return False
    if not (_screen(state).get("confirm_up") or
            _screen(state).get("selected_cards")):
        return False
    return "proceed" in candidates


# --- the script store -------------------------------------------------------

class Script:
    def __init__(self, path):
        self.path = path
        with open(path, "r", encoding="utf-8") as fh:
            lines = [json.loads(line) for line in fh if line.strip()]
        if not lines or lines[0].get("format") != SCRIPT_FORMAT:
            raise ConfigError(f"{path} is not an {SCRIPT_FORMAT} file")
        self.header = lines[0]
        self.steps = lines[1:]
        if len(self.steps) != self.header.get("steps"):
            raise ConfigError(
                f"{path}: header says {self.header.get('steps')} steps, "
                f"file carries {len(self.steps)}")
        self.cursor = 0

    def peek(self):
        return self.steps[self.cursor] if self.cursor < len(self.steps) \
            else None

    def consume(self):
        step = self.steps[self.cursor]
        self.cursor += 1
        return step


class ScriptPolicy:
    def __init__(self, config):
        script_dir = config.get("script_dir")
        if not script_dir or not os.path.isdir(script_dir):
            raise ConfigError(
                f"script_dir must name an existing directory: {script_dir!r}")
        self.script_dir = script_dir
        self.policy = config.get("policy") or "sim_search"
        self.divergence_dir = config.get("divergence_dir") or script_dir
        self._seed = None
        self._script = None

    def script_path(self, seed, policy_seed):
        return os.path.join(
            self.script_dir,
            f"{seed}__{self.policy}__ps{policy_seed}.script.jsonl")

    def decide(self, request):
        seed = request.get("seed")
        candidates = request.get("candidates")
        state = request.get("state")
        if not isinstance(candidates, list) or not candidates:
            raise Divergence("decide request carries no candidates")
        if seed != self._seed:
            path = self.script_path(seed, request.get("policy_seed"))
            if not os.path.isfile(path):
                raise ConfigError(f"no script for seed {seed!r}: {path}")
            self._script = Script(path)
            self._seed = seed
        # GLUE RULE 4 FIRST, AND ONLY THIS ONE. A `COMPLETE` screen is a
        # press the sim never records a step for (module header), and its
        # live command is `proceed` -- the very alias a scripted
        # `confirm`/`proceed` answers -- so leaving it to the match would
        # let a step belonging to the next floor be eaten by the crossing.
        # It consumes nothing, so a real desync still surfaces at the next
        # screen with the cursor unmoved.
        if is_complete_screen_glue(state, candidates):
            return "proceed"
        # MATCH FIRST, GLUE ON MISMATCH. The script's next step gets the
        # first claim on this state; only when it does not match here do the
        # three post-match glue rules answer -- so a scripted proceed/confirm
        # is consumed rather than glued past, and no glue advances the cursor.
        step = self._script.peek()
        # SIM-ONLY PROCEED SKIP (module header): a proceed-kind step with
        # neither alias legal can never match this or any state's candidate
        # list -- the live game auto-advanced where the sim stepped.
        while (step is not None
               and step.get("k") in ("proceed", "confirm")
               and "proceed" not in candidates
               and "confirm" not in candidates):
            self._script.consume()
            step = self._script.peek()
        if step is not None:
            try:
                cmd = match_step(step, state, candidates)
            except Divergence as exc:
                progress = progress_candidates(candidates)
                if len(progress) == 1 and progress[0] == "proceed":
                    return "proceed"  # confirmation-only screen: glue rule 1
                if is_grid_confirm_glue(state, candidates):
                    return "proceed"  # pick-then-confirm seam: glue rule 2
                click = sole_choice_glue(candidates)
                if click is not None:
                    return click  # one-click dialog: glue rule 3
                raise exc
            self._script.consume()
            return cmd
        # Script exhausted: trailing confirmation-only screens still glue
        # (the run's terminal can sit behind one); anything else is a desync.
        progress = progress_candidates(candidates)
        if len(progress) == 1 and progress[0] == "proceed":
            return "proceed"
        if is_grid_confirm_glue(state, candidates):
            return "proceed"
        click = sole_choice_glue(candidates)
        if click is not None:
            return click
        raise Divergence(
            "script exhausted but the game still asks for a decision "
            f"(seed {seed}, {len(self._script.steps)} steps played)")

    def divergence_record(self, request, exc):
        return {
            "kind": "script_divergence",
            "format": SCRIPT_FORMAT,
            "seed": request.get("seed"),
            "policy_seed": request.get("policy_seed"),
            "script": getattr(self._script, "path", None),
            "step_index": getattr(self._script, "cursor", None),
            "step": getattr(exc, "step", None),
            "reason": exc.reason if isinstance(exc, Divergence) else str(exc),
            "screen_type": _gs(request.get("state")).get("screen_type"),
            "choice_list": _choice_list(request.get("state")),
            "candidates": request.get("candidates"),
            "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        }

    def write_divergence(self, request, exc):
        record = self.divergence_record(request, exc)
        name = f"divergence_{request.get('seed')}_" \
               f"ps{request.get('policy_seed')}.json"
        path = os.path.join(self.divergence_dir, name)
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(record, fh, indent=2)
        return path


# --- STS-POLICY-IO server ---------------------------------------------------

def load_config(path):
    if not path:
        raise ConfigError("--config is required (script_dir lives there)")
    with open(path, "r", encoding="utf-8") as fh:
        config = json.load(fh)
    if not isinstance(config, dict):
        raise ConfigError("policy config must be a JSON object")
    unknown = set(config) - {"script_dir", "policy", "divergence_dir"}
    if unknown:
        raise ConfigError(f"unknown policy config keys: {sorted(unknown)}")
    return config


def serve(stdin, stdout, config):
    policy = ScriptPolicy(config)
    print(f"[script_policy] {SCRIPT_FORMAT} follower, scripts in "
          f"{policy.script_dir} (policy {policy.policy})", file=sys.stderr,
          flush=True)
    for line in stdin:
        if not line.strip():
            continue
        request = json.loads(line)
        if request.get("format") != PROTOCOL or \
                request.get("kind") != "decide":
            print(f"[script_policy] out-of-contract request: "
                  f"{ {k: request.get(k) for k in ('format', 'kind')} }",
                  file=sys.stderr, flush=True)
            return 2
        try:
            command = policy.decide(request)
        except Divergence as exc:
            path = policy.write_divergence(request, exc)
            print(f"[script_policy] DIVERGENT: {exc.reason} "
                  f"(record: {path})", file=sys.stderr, flush=True)
            return EXIT_DIVERGED
        response = {
            "format": PROTOCOL,
            "kind": "decision",
            "command": command,
        }
        stdout.write(json.dumps(response, ensure_ascii=False) + "\n")
        stdout.flush()
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="STS-SCRIPT v1 follower (S2.V2 sim-consulting driver)")
    parser.add_argument("--config", default=None,
                        help="JSON config: script_dir (required), policy, "
                             "divergence_dir")
    args = parser.parse_args(argv)
    try:
        config = load_config(args.config)
    except (OSError, ValueError) as exc:
        print(f"[script_policy] bad config: {exc}", file=sys.stderr,
              flush=True)
        return 2
    try:
        return serve(sys.stdin, sys.stdout, config)
    except ConfigError as exc:
        print(f"[script_policy] {exc}", file=sys.stderr, flush=True)
        return 2


if __name__ == "__main__":
    sys.exit(main())
