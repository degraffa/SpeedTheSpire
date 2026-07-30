#!/usr/bin/env python3
"""SpeedTheSpire oracle-bridge campaign driver (Stage B task B1.4).

This is the *real* driver the bring-up child (echo_driver.py, B0.2) grew into.
It is the CommunicationMod-oracle child process: the game launches it (via the
config.properties `command`, see orchestrator.py) and wires *its* stdio to the
game -- state JSON arrives on stdin (one object per line), commands leave on
stdout (\\n-delimited). See ../PROTOCOL.md.

What it does (B1.4 deliverables):
  * seeded A20 Ironclad starts (`start ironclad 20 <SEED>`; PROTOCOL.md 2.1);
  * three action generators -- `random-legal` (uniform over the game's own
    accepted-command set, expanded to concrete legal actions), `greedy` (the
    same expansion, ranked by greedy_policy.score_action -- a depth-seeking
    heuristic) and `script` (a fixed command list) -- all paced through one
    strict lock-step gate;
  * JSONL artifacts per design 2.7: one file per run, a version-stamped header
    (fork-jar sha256, seed in BOTH encodings, versions), then one record per
    injected action `{action_command, sim_action_bits?, state_json}`, then a
    terminal record;
  * crash detection (stdin EOF / per-command wall-clock watchdog);
  * campaign resume across game relaunches via a durable progress file;
  * batch mode over a seed list.

The single most important correctness invariant (the B0.2 contamination
postmortem, ledger B1.1 Log): **one command per fresh `ready_for_command`**.
A command is only ever sent after we have observed a state that (a) has
`ready_for_command == true` and (b) arrived *after* the previous command. We
NEVER blind-resend on silence -- prolonged silence is a crash (watchdog), not a
cue to retry.

Process-lifecycle (launch / kill / relaunch of the game) is owned by the outer
orchestrator (orchestrator.py); this driver owns the per-run protocol, the
artifacts, and the resume file. Resume granularity is one seed (the protocol
exposes no mid-run save): a seed interrupted by a game kill is re-run from
`start` on the next launch (retry-once, then marked failed so one pathological
seed cannot wedge the campaign).

Dependency-free (Python 3 standard library only): it runs under the game's own
Windows Python, outside the WSL build/CI world.
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import os
import random
import re
import sys
import threading
import time
from datetime import datetime, timezone

import greedy_policy
from campaign_paths import (
    ORACLE_LAUNCH_TOKEN_ENV,
    campaign_dir_under_root,
    campaign_file_under_root,
    exact_path_without_redirect,
    validate_campaign_id,
    validate_seed_list,
)

DRIVER_VERSION = "b1.5.3"
SCHEMA_VERSION = 1

# A temporarily empty legal-action expansion is normal while an animated
# reward/confirmation screen hands control back to the dungeon.  Communication
# Mod can answer a burst of `state` requests inside one render frame, so twelve
# immediate probes are not twelve independent chances for the screen to
# advance.  Yield a small, bounded amount between probes even when the
# operator's script-only settle delay is zero.
TRANSIENT_NO_ACTION_SLEEP_S = 0.05

# Windows' CRT opens do not necessarily share delete access. The
# orchestrator's read of campaign_progress.json can therefore race the
# driver's atomic os.replace and produce ERROR_SHARING_VIOLATION even though
# both processes are behaving correctly. Retry only Windows sharing/lock/
# access codes, for a bounded sub-second window; every other error and an
# exhausted sharing error still fail loud.
ATOMIC_REPLACE_ATTEMPTS = 40
ATOMIC_REPLACE_RETRY_SLEEP_S = 0.025
_WINDOWS_REPLACE_RETRY_ERRORS = frozenset((5, 32, 33))

# The SANCTIONED runtime stack (design 1.2, amended at B4.5 / design 11 v0.1.7).
#
# These are EXPECTATIONS, never the values reported in an artifact. Until B4.5
# they were `GAME_STS_VERSION`/`GAME_MTS_VERSION` and were stamped straight into
# every artifact header -- so every artifact ever written claimed the frozen
# stack no matter what actually launched, and an entire corpus (including the G4
# gate corpus b13_on20b) was captured on a stack nobody could see from the
# artifacts. The header now reports what `observe_runtime_stack` READ from the
# launch log, and a mismatch refuses. Do not reintroduce a code path that copies
# a constant below into a header.
SANCTIONED_STS_VERSION = "12-18-2022"
SANCTIONED_MTS_VERSION = "3.30.3"
SANCTIONED_BASEMOD_VERSION = "5.56.0"
BASEMOD_WORKSHOP = "1605833019"
MODS = ["basemod", "CommunicationMod-oracle"]

# The fork's modid, and the stock mod it must never be loaded beside: both carry
# the same patch classes and share SpireConfig("CommunicationMod", ...), so a
# stock CommunicationMod loaded alongside the fork still spawns this driver and
# still produces plausible-looking artifacts -- with no oracle block. That is
# exactly how the invalid `b45_rewards` capture happened.
FORK_MODID = "CommunicationMod-oracle"
STOCK_MODID = "CommunicationMod"

# ModTheSpire prints this block once per launch, before any mod initializes:
#
#     Version Info:
#      - Java version (1.8.0_144)
#      - Slay the Spire (12-18-2022)
#      - ModTheSpire (3.30.3)
#     Mod list:
#      - basemod (5.56.0)
#      - CommunicationMod-oracle (1.2.1-oracle.0)
#
# orchestrator.py captures it to `mts_launch<N>.log` in the campaign directory.
# It is the ONLY ground truth for what launched: the mod's own ModTheSpire.json
# merely *declares* versions, and ModTheSpire does not reject a game-version
# mismatch (PROTOCOL.md 0.1), so a declaration proves nothing.
_VERSION_ENTRY_RE = re.compile(r"\A\s*-\s*(?P<name>.+?)\s*\((?P<version>[^()]*)\)\s*\Z")
_LAUNCH_LOG_RE = re.compile(r"\Amts_launch[1-9][0-9]*\.log\Z")
_STS_ENTRY = "Slay the Spire"
_MTS_ENTRY = "ModTheSpire"


def parse_launch_log(path: str) -> dict:
    """Extract the observed runtime stack from one ModTheSpire launch log.

    Returns {"sts_version", "mts_version", "mods": {modid: version}}; a value is
    None when the log did not name it. Only the FIRST `Version Info:` block is
    read -- one log is one launch, and a relaunch gets its own file.
    """
    observed = {"sts_version": None, "mts_version": None, "mods": {}}
    section = None
    seen_version_info = False
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            stripped = line.strip()
            if stripped == "Version Info:":
                if seen_version_info:
                    break          # a second block: different launch, stop
                seen_version_info, section = True, "version"
                continue
            if stripped == "Mod list:":
                section = "mods"
                continue
            if section is None:
                continue
            m = _VERSION_ENTRY_RE.match(line)
            if m is None:
                # Any non-entry line ends the block. Do NOT `break`: the two
                # blocks are adjacent but not contiguous in every MTS build.
                section = None
                continue
            name, version = m.group("name"), m.group("version")
            if section == "version":
                if name == _STS_ENTRY:
                    observed["sts_version"] = version
                elif name == _MTS_ENTRY:
                    observed["mts_version"] = version
            else:
                observed["mods"][name] = version
    return observed


def check_runtime_stack(observed: dict) -> list:
    """Every way `observed` fails the sanctioned stack, as a list of strings."""
    problems = []
    for label, got, want in (
        ("Slay the Spire", observed.get("sts_version"), SANCTIONED_STS_VERSION),
        ("ModTheSpire", observed.get("mts_version"), SANCTIONED_MTS_VERSION),
        ("basemod", (observed.get("mods") or {}).get("basemod"),
         SANCTIONED_BASEMOD_VERSION),
    ):
        if got != want:
            problems.append(f"{label}: observed {got!r}, sanctioned {want!r}")
    mods = observed.get("mods") or {}
    if FORK_MODID not in mods:
        problems.append(
            f"{FORK_MODID} is not in the launch log's mod list "
            f"(loaded: {sorted(mods)})")
    if STOCK_MODID in mods:
        problems.append(
            f"stock {STOCK_MODID} loaded alongside the fork: both carry the "
            "same patch classes and share the fork's config namespace, so the "
            "driver attaches and writes artifacts with no oracle block")
    return problems

# base-35 seed alphabet (SeedHelper.CHARACTERS; stage-a 3.5). getLong() below is
# a bit-exact port -- validated STS12345 -> 1790052133945 at build time.
_SEED_CHARS = "0123456789ABCDEFGHIJKLMNPQRSTUVWXYZ"

# Raw byte streams so Windows never rewrites '\n' -> '\r\n' (a stray '\r' would
# be swallowed into the command and corrupt it). Correctness invariant, verbatim
# from echo_driver.py.
_STDOUT = sys.stdout.buffer
_STDIN = sys.stdin.buffer


def _now() -> float:
    return time.time()


def _utc() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")


def atomic_replace_with_retry(source: str, destination: str) -> None:
    """os.replace with a bounded retry for transient Windows sharing errors."""
    for attempt in range(ATOMIC_REPLACE_ATTEMPTS):
        try:
            os.replace(source, destination)
            return
        except OSError as exc:
            retryable = (
                getattr(exc, "winerror", None)
                in _WINDOWS_REPLACE_RETRY_ERRORS
            )
            if not retryable or attempt + 1 >= ATOMIC_REPLACE_ATTEMPTS:
                raise
            time.sleep(ATOMIC_REPLACE_RETRY_SLEEP_S)


def _log(msg: str) -> None:
    """Driver diagnostics -> stderr -> communication_mod_errors.log (game dir)."""
    sys.stderr.write(f"[campaign_driver {_utc()}] {msg}\n")
    sys.stderr.flush()


def seed_to_long(seed_str: str) -> int:
    """Bit-exact port of SeedHelper.getLong (base-35, 64-bit signed wrap)."""
    s = seed_str.upper().replace("O", "0")
    total = 0
    for ch in s:
        idx = _SEED_CHARS.find(ch)
        total = (total * 35 + (idx if idx >= 0 else 0)) & 0xFFFFFFFFFFFFFFFF
    return total - (1 << 64) if total >= (1 << 63) else total


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest().upper()


# ---------------------------------------------------------------------------
# Exit signals. Because the *game* (not the orchestrator) is our parent, our
# exit code is not seen by the orchestrator -- it reconciles via the progress
# and heartbeat files. These are recorded for the errors log only.
EXIT_OK = 0            # campaign complete (all seeds terminal)
EXIT_GAME_GONE = 3     # stdin EOF / watchdog: the game is gone -> relaunch
EXIT_NEED_RESTART = 4  # run ended mid-dungeon (boss reward / cap): need a clean
#                        menu, which the protocol cannot reach -> relaunch
EXIT_FATAL = 5


class GameGone(Exception):
    """stdin EOF or watchdog trip: the game process is gone / hung."""


class FatalEnvironmentDrift(RuntimeError):
    """The launched game/mod stack cannot produce authoritative oracle data."""

    def __init__(self, message: str,
                 kind: str = "missing_oracle_block") -> None:
        super().__init__(message)
        self.kind = kind


class CampaignIdentityError(RuntimeError):
    """An existing resume ledger belongs to a different campaign invocation."""


class Reader:
    """Reads state lines off the game (our stdin) on a dedicated thread and
    publishes the newest parsed message under a condition variable, tagged with
    a monotonic arrival index. The main loop blocks on *fresh* messages; EOF is
    detected promptly and turned into GameGone."""

    def __init__(self) -> None:
        self._cv = threading.Condition()
        self.count = 0
        self.latest = None      # type: dict | None
        self.latest_idx = 0
        self.eof = False

    def start(self) -> None:
        t = threading.Thread(target=self._loop, daemon=True)
        t.start()

    def _loop(self) -> None:
        while True:
            line = _STDIN.readline()
            if not line:
                with self._cv:
                    self.eof = True
                    self._cv.notify_all()
                return
            text = line.decode("utf-8", errors="replace").rstrip("\r\n")
            if not text:
                continue
            try:
                msg = json.loads(text)
            except json.JSONDecodeError:
                _log(f"unparseable state line ({len(text)} B); ignoring")
                continue
            with self._cv:
                self.count += 1
                self.latest = msg
                self.latest_idx = self.count
                self._cv.notify_all()

    def snapshot_count(self) -> int:
        with self._cv:
            return self.count

    def await_actionable(self, after_idx: int, deadline: float):
        """Return the first (idx, msg) arriving after `after_idx` that is either
        an error object or has ready_for_command==true. Skips intermediate
        not-ready dumps. Raises GameGone on EOF; returns None on watchdog."""
        k = after_idx
        with self._cv:
            while True:
                if self.latest_idx > k and self.latest is not None:
                    msg = self.latest
                    idx = self.latest_idx
                    if "error" in msg or msg.get("ready_for_command"):
                        return idx, msg
                    k = idx  # a not-ready dump: advance past it and keep waiting
                    continue
                if self.eof:
                    raise GameGone("stdin EOF")
                rem = deadline - _now()
                if rem <= 0:
                    return None
                self._cv.wait(min(rem, 0.5))

    def await_any(self, after_idx: int, deadline: float):
        """Return the first (idx, msg) arriving after `after_idx`, regardless of
        readiness. Used to probe liveness after a watchdog trip. Raises GameGone
        on EOF; returns None on timeout."""
        with self._cv:
            while True:
                if self.latest_idx > after_idx and self.latest is not None:
                    return self.latest_idx, self.latest
                if self.eof:
                    raise GameGone("stdin EOF")
                rem = deadline - _now()
                if rem <= 0:
                    return None
                self._cv.wait(min(rem, 0.5))


class Stepper:
    """Strict lock-step command gate over the Reader."""

    def __init__(self, reader: Reader, timeout: float,
                 probe_timeout: float = 20.0) -> None:
        self.reader = reader
        self.timeout = timeout
        self.probe_timeout = probe_timeout
        self.sent = 0

    def send(self, cmd: str) -> None:
        _STDOUT.write((cmd + "\n").encode("utf-8"))
        _STDOUT.flush()
        self.sent += 1

    def step(self, cmd: str):
        """Send exactly one command, then wait for the next fresh actionable
        state. Returns (kind, msg), kind in {'ready','error','noop'}. NEVER
        resends the command.

        On a watchdog trip we do NOT assume a crash. `state` forces a dump
        regardless of `waitingForCommand` (PROTOCOL.md 2), so we ping it: if the
        game answers, it is ALIVE and the command was an advertised no-op that
        left readiness unrestored (observed at B1.4 on Living Wall's event GRID:
        `cancel` on the grid-confirm is advertised but never re-arms
        ready_for_command). We return ('noop', dump) so the run loop can escape
        by completing the pending screen instead of losing the seed to a false
        'crash'. Only a game that ignores even `state` is truly GameGone."""
        k = self.reader.snapshot_count()
        self.send(cmd)
        res = self.reader.await_actionable(k, _now() + self.timeout)
        if res is None:
            k2 = self.reader.snapshot_count()
            self.send("state")
            probe = self.reader.await_any(k2, _now() + self.probe_timeout)
            if probe is None:
                raise GameGone(
                    f"watchdog: no fresh state {self.timeout}s after {cmd!r}, "
                    f"and no reply to a `state` liveness ping "
                    f"({self.probe_timeout}s) -- game gone/hung")
            _pidx, pmsg = probe
            return ("error" if "error" in pmsg else "noop"), pmsg
        _idx, msg = res
        return ("error" if "error" in msg else "ready"), msg

    def await_ready(self):
        """Wait for a fresh actionable state without sending (post-handshake)."""
        k = self.reader.snapshot_count()
        # use the last already-latched state if it is already actionable
        with self.reader._cv:
            if self.reader.latest is not None and (
                "error" in self.reader.latest
                or self.reader.latest.get("ready_for_command")
            ):
                m = self.reader.latest
                return ("error" if "error" in m else "ready"), m
        res = self.reader.await_actionable(k, _now() + self.timeout)
        if res is None:
            raise GameGone("watchdog: no state during handshake")
        _idx, msg = res
        return ("error" if "error" in msg else "ready"), msg


# ---------------------------------------------------------------------------
# Random-legal policy: expand the game's own available_commands into concrete
# legal actions, then choose uniformly. key/click/wait/state/start are excluded
# (raw-input escape hatches / info no-ops / not in-dungeon). See scoping 4.

def _live_monster_indices(gs: dict) -> list:
    cs = gs.get("combat_state") or {}
    out = []
    for i, m in enumerate(cs.get("monsters") or []):
        if not m.get("is_gone") and m.get("current_hp", 0) > 0:
            out.append(i)
    return out


def expand_legal_actions(state: dict, rng: random.Random) -> list:
    """Concrete legal command strings for a fresh in-game state."""
    avail = state.get("available_commands") or []
    gs = state.get("game_state") or {}
    actions: list = []

    if "play" in avail:
        cs = gs.get("combat_state") or {}
        live = _live_monster_indices(gs)
        for idx, card in enumerate(cs.get("hand") or [], start=1):
            if not card.get("is_playable"):
                continue
            hidx = idx if idx < 10 else 0  # 1-based; index 0 means slot 10
            if card.get("has_target"):
                for t in live:
                    actions.append(f"play {hidx} {t}")
            else:
                actions.append(f"play {hidx}")

    if "end" in avail:
        actions.append("end")

    if "choose" in avail:
        choices = gs.get("choice_list") or []
        for i in range(len(choices)):
            actions.append(f"choose {i}")

    if "potion" in avail:
        live = _live_monster_indices(gs)
        for slot, p in enumerate(gs.get("potions") or []):
            if p.get("id") in (None, "Potion Slot", "PotionSlot"):
                continue
            if p.get("can_use"):
                if p.get("requires_target") and live:
                    for t in live:
                        actions.append(f"potion use {slot} {t}")
                elif not p.get("requires_target"):
                    actions.append(f"potion use {slot}")
            if p.get("can_discard"):
                actions.append(f"potion discard {slot}")

    # Calling Bell's three Neow relic rows are mandatory even though the
    # protocol advertises `proceed` beside them.  Proceeding early closes the
    # reward screen without advancing Neow, after which no progress command is
    # exposed and the live campaign ends in legal_exhaustion.  A Neow
    # COMBAT_REWARD whose remaining rows are all relics is therefore a forced
    # claim screen; once the final row is gone, `proceed` becomes legal again.
    choices = gs.get("choice_list") or []
    forced_neow_relic_reward = (
        gs.get("screen_type") == "COMBAT_REWARD"
        and gs.get("room_type") == "NeowRoom"
        and bool(choices)
        and all(str(choice).lower() == "relic" for choice in choices)
    )
    if any(a in avail for a in ("confirm", "proceed")) \
            and not forced_neow_relic_reward:
        actions.append("proceed")
    for alias in ("skip", "cancel", "return", "leave"):
        if alias in avail:
            # B5.2 live campaign trap: after a GRID selection, the game
            # advertises confirm + cancel, but cancel only clears the selected
            # card and never re-arms ready_for_command. Stepper must then wait
            # its full watchdog, probe, and select again. This made a
            # random-legal run alternate forever until max_noops. Once the
            # GRID's own confirm_up says the pick is committable, proceeding is
            # the only progress action. Keep cancel everywhere else, including
            # HAND_SELECT and an unconfirmed GRID.
            if alias == "cancel" and gs.get("screen_type") == "GRID" and \
                    (gs.get("screen_state") or {}).get("confirm_up") and \
                    any(a in avail for a in ("confirm", "proceed")):
                continue
            actions.append(alias)
            break

    return actions


# ---------------------------------------------------------------------------
# Termination (scoping 4.3 / design 1.1).

def cmd_verb_ready(state: dict, cmd: str) -> bool:
    """True if the game currently advertises the verb of `cmd`.

    Script replay (B1.3 A/B) must send each command only when the game is at the
    matching interactive state -- exactly the invariant the random-legal baseline
    satisfied for free (it only ever emitted commands drawn from
    available_commands). Blind replay can otherwise fire a command into a
    transient not-ready window (e.g. a Neow/event phase where waitTimer has hit 0
    -- so the fork reports ready_for_command -- but the option buttons are not yet
    presented, so `choose` is absent). Waiting for the verb makes replay robust to
    strip-on/off timing differences WITHOUT masking a real divergence: the wait is
    bounded, and a verb that never appears ends the run as a divergence."""
    avail = state.get("available_commands") or []
    verb = cmd.split()[0] if cmd else ""
    if verb in ("state", "wait", "key", "click"):
        return True
    if verb == "proceed":
        return any(a in avail for a in ("proceed", "confirm"))
    if verb in ("skip", "cancel", "return", "leave"):
        return any(a in avail for a in ("skip", "cancel", "return", "leave"))
    # play / end / choose / potion
    return verb in avail


def cmd_args_ready(state: dict, cmd: str):
    """Argument-level sibling of `cmd_verb_ready`: (ok, reason).

    `cmd_verb_ready` validates the VERB only, which leaves a hole that only
    script mode can fall into. A mis-indexed `choose 3` against a 2-option
    screen has a legal verb, so the settle gate passes it, the command fires,
    the game answers with an InvalidCommand error object -- and the run loop
    tolerates eight of those (`error_wedge`) before giving up. In between, the
    script is silently one command out of step with the run it is supposed to
    be replaying, which is the exact failure mode a replay exists to detect.

    What is checked, and only when the state actually carries the collection:

      * `choose <i>`      -- 0 <= i < len(game_state.choice_list)
      * `play <i> [t]`    -- i names a real hand slot (1-based, 0 == slot 10;
                             PROTOCOL.md 2) and, when given, t indexes a real
                             monster in combat_state.monsters

    Deliberately NOT checked:

      * `choose <name>`   -- the protocol matches the exact choice string first
                             and only then parses an integer index
                             (CommandExecutor.java:557-570, PROTOCOL.md 2
                             notes). A name form is passed through untouched.
      * anything whose collection is absent from the dump -- absence is not
                             evidence of an out-of-range argument, and inventing
                             a divergence out of a missing field would be worse
                             than the hole this closes.

    `random-legal` and `greedy` both draw from `expand_legal_actions`, so their
    commands are in range by construction and this check is a no-op for them.
    """
    parts = (cmd or "").split()
    if len(parts) < 2:
        return True, ""
    verb, args = parts[0], parts[1:]
    gs = state.get("game_state") or {}

    if verb == "choose":
        try:
            index = int(args[0])
        except ValueError:
            return True, ""          # `choose <name>`: not ours to judge
        choices = gs.get("choice_list")
        if not isinstance(choices, list):
            return True, ""
        if index < 0 or index >= len(choices):
            return False, (f"choose {index} but choice_list has "
                           f"{len(choices)} option(s): {choices!r}")
        return True, ""

    if verb == "play":
        cs = gs.get("combat_state") or {}
        try:
            slot = int(args[0])
        except ValueError:
            return False, f"play argument {args[0]!r} is not an index"
        hand = cs.get("hand")
        if isinstance(hand, list):
            if slot < 0 or slot > 10:
                return False, f"play {slot} is outside the 0-10 slot range"
            index = 9 if slot == 0 else slot - 1
            if index >= len(hand):
                return False, (f"play {slot} but the hand holds "
                               f"{len(hand)} card(s)")
        if len(args) >= 2:
            try:
                target = int(args[1])
            except ValueError:
                return False, f"play target {args[1]!r} is not an index"
            monsters = cs.get("monsters")
            if isinstance(monsters, list) and (
                    target < 0 or target >= len(monsters)):
                return False, (f"play target {target} but the room holds "
                               f"{len(monsters)} monster(s)")
        return True, ""

    return True, ""


def is_boss_combat_reward(gs: dict) -> bool:
    """Act-1 boss combat rewards up: the S1 terminal (design 1.1 -- stop BEFORE
    the boss chest / boss-relic pick)."""
    return (
        gs.get("act") == 1
        and gs.get("room_type") == "MonsterRoomBoss"
        and gs.get("screen_type") in ("COMBAT_REWARD", "CARD_REWARD")
    )


def game_over_outcome(gs: dict):
    if gs.get("screen_type") == "GAME_OVER":
        ss = gs.get("screen_state") or {}
        return "victory" if ss.get("victory") else "death"
    return None


class RunLogger:
    """One JSONL file per run: header, then one record per injected action,
    then a terminal record. state_json is captured VERBATIM (lossless) -- the
    translator (B1.5) enforces unknown-field-is-error, so nothing is pruned."""

    def __init__(self, path: str, header: dict) -> None:
        path = exact_path_without_redirect(path)
        self._fh = open(path, "w", encoding="utf-8", newline="\n")
        self._seq = 0
        self._write(header)

    def _write(self, rec: dict) -> None:
        self._fh.write(json.dumps(rec, ensure_ascii=False) + "\n")
        self._fh.flush()

    def action(self, action_command: str, state: dict) -> None:
        self._write({
            "record_kind": "action",
            "seq": self._seq,
            "action_command": action_command,
            "sim_action_bits": None,  # B1.5 (translator) fills this; stable shape
            "ready_for_command": state.get("ready_for_command"),
            "available_commands": state.get("available_commands"),
            "state_json": state,
        })
        self._seq += 1

    def terminal(self, outcome: str, gs: dict, actions: int) -> None:
        self._write({
            "record_kind": "terminal",
            "seq": self._seq,
            "outcome": outcome,
            "floor": (gs or {}).get("floor"),
            "act": (gs or {}).get("act"),
            "actions": actions,
            "created_utc": _utc(),
        })

    def close(self) -> None:
        self._fh.close()


class TimingLog:
    """B1.3 throughput sidecar: one JSONL line per injected action, stamped with
    a monotonic clock at the instant the command is about to be sent. Separate
    from the state artifact so the equivalence-critical schema is untouched.

    The lock-step transport sends exactly one command per fully-resolved
    ready-for-command state, so the wall time between consecutive marks is the
    time the game took to resolve the previous injected action. Sustained
    throughput = (#marks - 1) / (t_last - t_first) over a run's in-dungeon
    actions (measure_throughput.py). perf_counter is process-local and monotonic;
    a run completes within a single game launch, so its marks share one clock."""

    def __init__(self, path: str, meta: dict) -> None:
        path = exact_path_without_redirect(path)
        self._fh = open(path, "w", encoding="utf-8", newline="\n")
        self._write({"record_kind": "timing_header",
                     "created_utc": _utc(), **meta})

    def _write(self, rec: dict) -> None:
        self._fh.write(json.dumps(rec, ensure_ascii=False) + "\n")
        self._fh.flush()

    def mark(self, seq: int, cmd: str, floor, screen) -> None:
        self._write({"record_kind": "mark", "seq": seq,
                     "t_mono": time.perf_counter(), "t_wall": _now(),
                     "cmd": cmd, "floor": floor, "screen": screen})

    def close(self) -> None:
        try:
            self._fh.close()
        except OSError:
            pass


class Progress:
    """Durable campaign resume state (design 7.1(2)). Rewritten and fsync'd on
    every seed transition; a lightweight heartbeat file is bumped each action so
    the orchestrator can tell a working driver from a wedged one."""

    def __init__(self, path: str, hb_path: str,
                 tmp_path: str | None = None,
                 hb_tmp_path: str | None = None) -> None:
        self.path = path
        self.hb_path = hb_path
        self.tmp_path = tmp_path if tmp_path is not None else path + ".tmp"
        self.hb_tmp_path = (
            hb_tmp_path if hb_tmp_path is not None else hb_path + ".tmp")
        self.data = None  # type: dict | None

    def load_or_init(self, campaign_id, seed_list, policy, fork_hash,
                      policy_seed=None) -> dict:
        if os.path.exists(self.path):
            with open(exact_path_without_redirect(self.path), "r",
                      encoding="utf-8") as fh:
                self.data = json.load(fh)
            expected = {
                "campaign_id": campaign_id,
                "seed_list": seed_list,
                "policy": policy,
                "fork_jar_sha256": fork_hash,
                "schema_version": SCHEMA_VERSION,
                "driver_version": DRIVER_VERSION,
            }
            # policy_seed is checked separately (not folded into `expected`)
            # so a caller that omits it (policy_seed=None -- every test that
            # is not exercising this field) does not spuriously demand a
            # `None` stored value; the real driver always passes it.
            if policy_seed is not None:
                expected["policy_seed"] = policy_seed
            mismatches = [
                f"{key}={self.data.get(key)!r} (expected {value!r})"
                for key, value in expected.items()
                if type(self.data.get(key)) is not type(value) or
                self.data.get(key) != value
            ]
            if mismatches:
                raise CampaignIdentityError(
                    "existing campaign_progress.json identity mismatch: " +
                    "; ".join(mismatches))
            # A fresh launch resumed us; bump the launch count.
            self.data["launches"] = self.data.get("launches", 0) + 1
            # A restart request is bound to the one-use token of the launch
            # that wrote it.  The outer orchestrator already acted on it (or
            # this new launch would not exist), so do not leave stale control
            # state in the durable ledger.
            self.data.pop("restart_requested", None)
        else:
            self.data = {
                "campaign_id": campaign_id,
                "schema_version": SCHEMA_VERSION,
                "driver_version": DRIVER_VERSION,
                "fork_jar_sha256": fork_hash,
                "policy": policy,
                # Additive (design 7.5 reproducibility gap): NOT in
                # validate_artifacts.STRICT_PROGRESS_KEYS, so an old
                # campaign_progress.json without this field still validates.
                "policy_seed": policy_seed,
                "seed_list": seed_list,
                "status": "in_progress",
                "seeds_done": [],
                "seeds_failed": [],
                "current_seed": None,
                "current_seed_attempt": 0,
                "launches": 1,
                "started_utc": _utc(),
                "updated_utc": _utc(),
            }
        self.flush()
        return self.data

    def flush(self) -> None:
        self.data["updated_utc"] = _utc()
        tmp = exact_path_without_redirect(self.tmp_path)
        destination = exact_path_without_redirect(self.path)
        with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(self.data, fh, indent=2)
            fh.flush()
            os.fsync(fh.fileno())
        atomic_replace_with_retry(
            exact_path_without_redirect(tmp),
            exact_path_without_redirect(destination))

    def heartbeat(self, seed, floor, actions) -> None:
        """Bump the heartbeat file -- through tmp + fsync + rename, same as
        `flush()` above, so a reader (the orchestrator's watchdog) never
        observes a truncated/partial write. Before this fix the write was a
        plain truncating `open(..., "w")`, which g6_campaign_spotdiff.md §9
        names as the structural half of "orchestrator kills a healthy game":
        a reader racing that truncation window would see an unreadable
        sample, and the watchdog's fallback treated that identically to a
        stall dating back to launch. Best-effort like the original: a failed
        heartbeat costs one missed sample, not the run, so both the
        filesystem (OSError) and the redirect guard (ValueError) are
        swallowed here -- unlike `flush()`, which must fail loud."""
        try:
            tmp = exact_path_without_redirect(self.hb_tmp_path)
            destination = exact_path_without_redirect(self.hb_path)
            with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
                json.dump({"t": _now(), "utc": _utc(), "seed": seed,
                           "floor": floor, "actions": actions}, fh)
                fh.flush()
                os.fsync(fh.fileno())
            atomic_replace_with_retry(tmp, destination)
        except (OSError, ValueError):
            pass

    def request_restart(self, launch_token: str, reason: str) -> None:
        """Durably ask the orchestrator to retire this exact game launch.

        The driver is a child of the game, not of the orchestrator, so its exit
        code is invisible.  Binding the request to the launch's one-use token
        lets the orchestrator react on its next three-second poll without
        mistaking a stale request for an instruction to kill the fresh launch.
        Store only a digest; the binding nonce itself stays out of artifacts.
        """
        token_digest = hashlib.sha256(
            launch_token.encode("utf-8")).hexdigest()
        self.data["restart_requested"] = {
            "launch_token_sha256": token_digest,
            "reason": reason,
            "utc": _utc(),
        }
        self.flush()

    def fatal_environment_drift(self, seed: str | None, attempt: int,
                                message: str, kind: str) -> None:
        """Persist a non-retryable environment failure for the orchestrator.

        The driver is the only process that can inspect the first in-dungeon
        dump.  A missing fork-only oracle block therefore has to cross the
        process boundary through this durable status rather than an exit code
        (the game, not the orchestrator, is the driver's parent).
        """
        self.data["status"] = "fatal_environment_drift"
        self.data["fatal"] = {
            "kind": kind,
            "message": message,
            "seed": seed,
            "attempt": attempt,
            "utc": _utc(),
        }
        self.data["current_seed"] = seed
        self.data["current_seed_attempt"] = attempt
        self.flush()

    def fatal_identity_mismatch(self, message: str) -> None:
        """Make a resume identity mismatch durable and non-retryable."""
        self.data["status"] = "fatal_environment_drift"
        self.data["fatal"] = {
            "kind": "campaign_identity_mismatch",
            "message": message,
            "seed": self.data.get("current_seed"),
            "attempt": self.data.get("current_seed_attempt", 0),
            "utc": _utc(),
        }
        self.flush()

    def done_seeds(self) -> set:
        d = self.data
        return {s["seed"] for s in d["seeds_done"]} | {
            s["seed"] for s in d["seeds_failed"]}


# ---------------------------------------------------------------------------

class CampaignDriver:
    # Class-level default so a driver built with __new__ (the unit tests, which
    # bypass __init__ to reach one method) never trips over a missing attribute.
    # `--policy greedy` replaces it with the real table in __init__.
    card_table: dict = {}

    def __init__(self, args) -> None:
        self.args = args
        validate_campaign_id(args.campaign_id)
        args.seeds = validate_seed_list(args.seeds)
        self.rng = random.Random(args.policy_seed)
        self.reader = Reader()
        self.stepper = Stepper(self.reader, args.timeout, args.probe_timeout)
        self.fork_hash = sha256_file(args.fork_jar)
        os.makedirs(self.run_dir(), exist_ok=True)
        # Re-resolve after mkdir to catch an existing/redirection race.
        campaign_dir_under_root(args.data_root, args.campaign_id)
        self.progress = Progress(
            self.run_file("campaign_progress.json"),
            self.run_file("campaign_heartbeat.json"),
            self.run_file("campaign_progress.json.tmp"),
        )
        # Script(s) are loaded per-seed in run_seed (a --script-dir campaign has a
        # distinct command list per seed). A single --script applies to all seeds.
        if args.policy == "greedy":
            self.card_table = greedy_policy.load_side_table(
                getattr(args, "card_table", None))
            _log(f"greedy policy: card side table has "
                 f"{len(self.card_table)} entries")
        self.single_script = None
        if args.policy == "script" and not args.script_dir:
            with open(args.script, "r", encoding="utf-8") as fh:
                self.single_script = self._parse_script(fh)

    @staticmethod
    def _parse_script(lines) -> list:
        return [ln.strip() for ln in lines
                if ln.strip() and not ln.startswith("#")]

    def _load_script(self, seed: str) -> list:
        if self.args.script_dir:
            path = os.path.join(self.args.script_dir, f"script_{seed}.txt")
            with open(path, "r", encoding="utf-8") as fh:
                return self._parse_script(fh)
        return list(self.single_script)

    def run_dir(self) -> str:
        return campaign_dir_under_root(
            self.args.data_root, self.args.campaign_id)

    def run_file(self, name: str) -> str:
        return campaign_file_under_root(
            self.args.data_root, self.args.campaign_id, name)

    def observed_stack(self) -> dict:
        """The runtime stack this launch ACTUALLY has, or refuse.

        Ground truth is the campaign's exact, launch-token-bound
        `mts_launch<N>.log` (see the module header). Three ways this refuses,
        all as `fatal_environment_drift` so the orchestrator stops instead of
        relaunching into the same wrong stack:

          * `stack_unobservable`   -- no bound/readable launch log or the
            orchestrator-only token is absent. This includes the GUI-launch
            case that produced the invalid `b45_rewards` campaign. Refusing is
            the point -- stale evidence or silence must never be reported as
            the sanctioned stack.
          * `stack_unparseable`    -- a log with no readable Version Info block.
          * `stack_version_mismatch` -- it launched, and it is the wrong stack.

        Returns the observed values (plus `version_source`) for the header. The
        caller must not substitute the SANCTIONED_* constants for a missing
        value; that substitution IS the defect this method exists to remove.
        """
        # The orchestrator binds this driver to one exact append-only log and
        # passes the same one-use launch token in the game process environment.
        # Selecting "the highest log" is not sufficient: a resumed orchestrator
        # used to restart numbering at 1, so a stale higher-numbered log from the
        # prior process could be accepted as the current runtime. A later GUI
        # launch also inherits the persisted config command but not this
        # orchestrator-only environment token.
        expected_token = self.args.launch_token
        inherited_token = os.environ.get(ORACLE_LAUNCH_TOKEN_ENV)
        if inherited_token is None or not hmac.compare_digest(
                inherited_token, expected_token):
            raise FatalEnvironmentDrift(
                "this driver is not bound to the orchestrator launch that "
                "created its runtime log: the inherited launch token is "
                "missing or different. Refusing stale-log reuse; launch "
                "through orchestrator.py (a GUI launch inherits the persisted "
                "command but not the one-use token)",
                kind="stack_unobservable")
        if _LAUNCH_LOG_RE.fullmatch(self.args.launch_log) is None:
            raise FatalEnvironmentDrift(
                f"invalid bound launch-log name {self.args.launch_log!r}; "
                "expected mts_launch<N>.log",
                kind="stack_unobservable")
        try:
            path = self.run_file(self.args.launch_log)
            observed = parse_launch_log(path)
        except (OSError, ValueError) as exc:
            raise FatalEnvironmentDrift(
                f"bound launch log {self.args.launch_log!r} is absent, "
                f"redirected, or unreadable: {exc}",
                kind="stack_unobservable") from exc
        if observed["sts_version"] is None and observed["mts_version"] is None:
            raise FatalEnvironmentDrift(
                f"{os.path.basename(path)} has no readable 'Version Info:' "
                "block; refusing to write an artifact whose runtime stack "
                "cannot be established",
                kind="stack_unparseable")
        problems = check_runtime_stack(observed)
        if problems:
            raise FatalEnvironmentDrift(
                "runtime stack drift observed in "
                f"{os.path.basename(path)}: " + "; ".join(problems)
                + ". Refusing artifact/policy. Either restore the sanctioned "
                "stack or amend design 1.2 (and these constants) deliberately",
                kind="stack_version_mismatch")
        observed["version_source"] = os.path.basename(path)
        return observed

    def strip_flags(self, seed: str, attempt: int) -> dict:
        return {"run_label": self.args.run_label,
                "campaign_id": self.args.campaign_id,
                "policy": self.args.policy,
                "seed": seed,
                "attempt": attempt,
                "schema_version": SCHEMA_VERSION,
                "driver_version": DRIVER_VERSION,
                "fork_jar_sha256": self.fork_hash}

    # -- menu / seed helpers -------------------------------------------------
    def wait_menu(self):
        """Block until a fresh state that is at the menu with `start` legal.
        The game only dumps on a state change, and at boot the menu may not be
        interactive yet, so we nudge with `state` (a no-op that just forces a
        re-dump; PROTOCOL.md 2) until `start` is advertised."""
        deadline = _now() + self.args.menu_timeout
        with self.reader._cv:
            m = self.reader.latest
        if m is not None and not m.get("in_game") and \
                "start" in (m.get("available_commands") or []):
            return m
        while _now() < deadline:
            k = self.reader.snapshot_count()
            self.stepper.send("state")
            res = self.reader.await_actionable(k, min(_now() + 3.0, deadline))
            if res is not None:
                _idx, m = res
                if not m.get("in_game") and \
                        "start" in (m.get("available_commands") or []):
                    return m
        raise GameGone("never reached a menu-ready state")

    def next_seed(self):
        done = self.progress.done_seeds()
        cur = self.progress.data.get("current_seed")
        # An in-progress seed from a prior (interrupted) launch is retried first.
        if cur is not None and cur not in done:
            return cur
        for s in self.progress.data["seed_list"]:
            if s not in done:
                return s
        return None

    # -- one run -------------------------------------------------------------
    def run_seed(self, seed: str, attempt: int):
        """Play one seeded A20 Ironclad run to a terminal state.
        Returns (outcome, floor, actions, menu_returnable)."""
        validate_seed_list([seed])
        artifact = self.run_file(f"run_{seed}_a20_ironclad.jsonl")
        # Per-seed policy RNG: a run's action sequence depends only on
        # (policy_seed, seed), not on the campaign's position, so any run is
        # reproducible in isolation -- the (seed, action-prefix) reproducibility
        # tier-3 / triage relies on (design 7.5).
        self.rng = random.Random(f"{self.args.policy_seed}:{seed}")
        # start the run; the first in-dungeon dump carries game_state.seed (long)
        kind, state = self.stepper.step(f"start ironclad 20 {seed}")
        if kind == "error":
            raise RuntimeError(f"start rejected: {state.get('error')}")
        # `start` can answer from a still-ready menu/fade state before the first
        # dungeon dump exists. Force fresh, non-mutating dumps until we actually
        # cross the in_game boundary; never infer fork health from a menu state.
        dungeon_deadline = _now() + self.args.menu_timeout
        while not state.get("in_game"):
            if _now() >= dungeon_deadline:
                raise GameGone(
                    "start never produced a first in-dungeon state")
            kind, state = self.stepper.step("state")
            if kind == "error":
                raise RuntimeError(
                    "state rejected while awaiting first in-dungeon dump: "
                    f"{state.get('error')}")
        gs = state.get("game_state") or {}
        if not isinstance(gs.get("oracle"), dict):
            raise FatalEnvironmentDrift(
                "first in-dungeon dump has no game_state.oracle object; "
                "CommunicationMod-oracle is not active or oracleBlock is "
                "disabled. Refusing to advance policy or create an artifact")
        seed_long = gs.get("seed")
        expect_long = seed_to_long(seed)
        seed_ok = (seed_long == expect_long)
        if not seed_ok:
            raise FatalEnvironmentDrift(
                f"seed crosscheck mismatch for {seed}: dump={seed_long!r}, "
                f"getLong={expect_long!r}; refusing artifact/policy",
                kind="seed_identity_mismatch")

        observed = self.observed_stack()

        header = {
            "record_kind": "header",
            "schema_version": SCHEMA_VERSION,
            "driver_version": DRIVER_VERSION,
            "created_utc": _utc(),
            # OBSERVED, not declared -- see observed_stack() and the module
            # header's note on SANCTIONED_*. `version_source` names the file
            # these came from so the claim is auditable from the artifact alone.
            "game": {"sts_version": observed["sts_version"],
                     "mts_version": observed["mts_version"],
                     "basemod": BASEMOD_WORKSHOP,
                     "basemod_version": observed["mods"].get("basemod"),
                     "version_source": observed["version_source"]},
            "mods": MODS,
            "mods_loaded": observed["mods"],
            "fork_jar_sha256": self.fork_hash,
            "fork_jar_path": self.args.fork_jar.replace("\\", "/"),
            "oracle_block_enabled": ("oracle" in gs),
            "seed": {"string": seed, "long": seed_long,
                     "long_getLong": expect_long, "crosscheck_ok": seed_ok},
            "ascension": 20,
            "character": "IRONCLAD",
            "policy": self.args.policy,
            # Reproducibility (design 7.5): the greedy tie-break RNG is
            # seeded Random(f"{policy_seed}:{seed}") and every real choice a
            # tied decision makes flows from that draw, so without the seed
            # in the artifact a campaign cannot be reconstructed from its own
            # output. Additive: NOT in validate_artifacts.HEADER_KEYS, so an
            # old artifact written before this field existed still validates.
            "policy_seed": self.args.policy_seed,
            "attempt": attempt,
            "campaign_id": self.args.campaign_id,
        }
        rl = RunLogger(artifact, header)
        # B1.3 throughput sidecar: one line per injected action with a monotonic
        # timestamp, captured at the moment the command is about to be sent. This
        # is a SEPARATE file from the JSONL artifact, so the equivalence-critical
        # action-record / state_json schema (B1.5) is untouched. measure_
        # throughput.py reads these to compute sustained injected-actions/sec.
        timing = TimingLog(self.run_file(
            f"run_{seed}_a20_ironclad.timing.jsonl"),
            self.strip_flags(seed, attempt))
        script = self._load_script(seed) if self.args.policy == "script" else None
        _log(f"seed {seed} attempt {attempt}: header written "
             f"(long={seed_long}, oracle={'oracle' in gs}); playing")

        actions = 0
        errors = 0
        noops = 0
        script_i = 0
        last_sig = None
        stuck = 0
        try:
            while True:
                gs = state.get("game_state") or {}
                floor = gs.get("floor")
                self.progress.heartbeat(seed, floor, actions)

                # ---- terminal checks (on the fresh decision state) ----
                ov = game_over_outcome(gs)
                if ov is not None:
                    rl.action("__terminal_observed__", state)
                    rl.terminal(ov, gs, actions)
                    # walk back to the menu so the next seed can `start`
                    menu_ok = self._return_to_menu_from_gameover()
                    return ov, floor, actions, menu_ok

                if is_boss_combat_reward(gs):
                    outcome, actions = self._claim_boss_reward(
                        rl, timing, state, seed, actions)
                    return outcome, floor, actions, False

                if actions >= self.args.max_actions:
                    rl.terminal("action_cap", gs, actions)
                    _log(f"seed {seed}: hit action cap {self.args.max_actions}")
                    return "action_cap", floor, actions, False

                # ---- choose a command ----
                if self.args.policy == "script":
                    if script_i >= len(script):
                        rl.terminal("script_exhausted", gs, actions)
                        return "script_exhausted", floor, actions, False
                    cmd = script[script_i]
                    # Wait for the game to actually advertise this command's verb
                    # (see cmd_verb_ready). Bounded: if it never settles, that is
                    # a divergence, not something to blast past.
                    settle = 0
                    while not cmd_verb_ready(state, cmd):
                        # a terminal can appear while settling (e.g. death mid
                        # animation); hand it back to the loop's terminal checks
                        if game_over_outcome(gs) is not None \
                                or is_boss_combat_reward(gs):
                            break
                        settle += 1
                        if settle % 15 == 1:
                            ss = gs.get("screen_state") or {}
                            _log(f"seed {seed}: settle#{settle} for {cmd!r} "
                                 f"screen={gs.get('screen_type')} "
                                 f"ready={state.get('ready_for_command')} "
                                 f"avail={state.get('available_commands')} "
                                 f"choice_list={gs.get('choice_list')} "
                                 f"ss_opts={ss.get('options')}")
                        if settle > self.args.max_settle:
                            rl.terminal("cmd_never_ready", gs, actions)
                            _log(f"seed {seed}: cmd {cmd!r} verb never became "
                                 f"legal at floor {floor} screen "
                                 f"{gs.get('screen_type')} after "
                                 f"{self.args.max_settle} settles -- divergence")
                            return "cmd_never_ready", floor, actions, False
                        if self.args.settle_sleep > 0:
                            time.sleep(self.args.settle_sleep)
                        kind, state = self.stepper.step("state")
                        gs = state.get("game_state") or {}
                    if game_over_outcome(gs) is not None \
                            or is_boss_combat_reward(gs):
                        continue
                    # The verb is legal; now prove the ARGUMENTS are too, on
                    # the very state the command is about to be sent into. A
                    # named divergence beats a silent InvalidCommand that the
                    # error budget absorbs (see cmd_args_ready).
                    args_ok, why = cmd_args_ready(state, cmd)
                    if not args_ok:
                        rl.terminal("cmd_arg_invalid", gs, actions)
                        _log(f"seed {seed}: script cmd {cmd!r} has invalid "
                             f"arguments at floor {floor} screen "
                             f"{gs.get('screen_type')}: {why} -- divergence")
                        return "cmd_arg_invalid", floor, actions, False
                    script_i += 1
                else:
                    cmd = self._policy_command(state)
                    if cmd is None:
                        # No legal progress action can be a one-frame reward or
                        # confirmation transition.  Yield between probes so
                        # this is a bounded settle window, not twelve requests
                        # injected into the same frame.
                        stuck += 1
                        if stuck >= 12:
                            rl.terminal("legal_exhaustion", gs, actions)
                            _log(f"seed {seed}: legal-action exhaustion at "
                                 f"floor {floor} screen {gs.get('screen_type')}")
                            return "legal_exhaustion", floor, actions, False
                        time.sleep(max(
                            self.args.settle_sleep,
                            TRANSIENT_NO_ACTION_SLEEP_S))
                        kind, state = self.stepper.step("state")
                        continue

                # stuck detector (same floor/screen/command repeating)
                sig = (floor, gs.get("screen_type"), cmd)
                stuck = stuck + 1 if sig == last_sig else 0
                last_sig = sig
                if stuck >= 40:
                    rl.terminal("stuck", gs, actions)
                    _log(f"seed {seed}: stuck repeating {cmd!r}")
                    return "stuck", floor, actions, False

                rl.action(cmd, state)
                timing.mark(actions, cmd, floor, gs.get("screen_type"))
                actions += 1
                kind, state = self.stepper.step(cmd)
                if kind == "error":
                    errors += 1
                    _log(f"seed {seed}: InvalidCommand {state.get('error')!r} "
                         f"for {cmd!r} (#{errors})")
                    if errors > 8:
                        rl.terminal("error_wedge", gs, actions)
                        return "error_wedge", floor, actions, False
                    # force a clean fresh dump and re-decide
                    kind, state = self.stepper.step("state")
                elif kind == "noop":
                    # the game is alive (it answered the `state` ping) but the
                    # command left readiness unrestored. Escape by completing the
                    # pending screen (confirm/proceed), else nudge; bounded so a
                    # genuinely wedged screen ends the run instead of hanging.
                    noops += 1
                    gs2 = state.get("game_state") or {}
                    _log(f"seed {seed}: no-op after {cmd!r} (game alive, screen "
                         f"{gs2.get('screen_type')}, ready="
                         f"{state.get('ready_for_command')}) #{noops}")
                    if noops > self.args.max_noops:
                        rl.terminal("noop_wedge", gs2, actions)
                        _log(f"seed {seed}: noop_wedge after {noops} no-ops "
                             f"(game alive but unresponsive to legal actions)")
                        return "noop_wedge", floor, actions, False
                    avail2 = state.get("available_commands") or []
                    if any(a in avail2 for a in ("confirm", "proceed")):
                        esc = "proceed"
                    elif "choose" in avail2 and (gs2.get("choice_list")):
                        esc = "choose 0"
                    else:
                        esc = "state"
                    rl.action(esc, state)
                    timing.mark(actions, esc, gs2.get("floor"),
                                gs2.get("screen_type"))
                    actions += 1
                    kind, state = self.stepper.step(esc)
        finally:
            rl.close()
            timing.close()

    def _policy_command(self, state):
        # Both live policies draw from the SAME expansion of the game's own
        # available_commands, so both are legal by construction; they differ
        # only in how they choose among the candidates.
        acts = expand_legal_actions(state, self.rng)
        if not acts:
            return None
        if self.args.policy == "greedy":
            return greedy_policy.pick(acts, state, self.card_table, self.rng)
        return self.rng.choice(acts)

    def _claim_boss_reward(self, rl, timing, state, seed, actions):
        """On the Act-1 boss combat-reward screen: claim every reward, then STOP
        (do not `proceed` -- that opens the boss chest, which is S2/out of scope,
        design 1.1). Returns ('act1_boss_reward', updated_action_count)."""
        _log(f"seed {seed}: ACT-1 BOSS reward reached -- claiming, then stop")
        guard = 0
        while True:
            gs = state.get("game_state") or {}
            st = gs.get("screen_type")
            choices = gs.get("choice_list") or []
            guard += 1
            if guard > 60:
                rl.terminal("act1_boss_reward", gs, actions)
                return "act1_boss_reward", actions
            if st == "COMBAT_REWARD":
                if choices:
                    rl.action("choose 0", state)
                    timing.mark(actions, "choose 0", gs.get("floor"), st)
                    actions += 1
                    kind, state = self.stepper.step("choose 0")
                    continue
                # all rewards claimed -> terminal, do NOT proceed to boss chest
                rl.action("__terminal_observed__", state)
                rl.terminal("act1_boss_reward", gs, actions)
                return "act1_boss_reward", actions
            if st == "CARD_REWARD":
                cmd = "choose 0" if choices else "skip"
                rl.action(cmd, state)
                timing.mark(actions, cmd, gs.get("floor"), st)
                actions += 1
                kind, state = self.stepper.step(cmd)
                continue
            # any other transient screen: settle
            kind, state = self.stepper.step("state")

    def _return_to_menu_from_gameover(self):
        """Press proceed on the GAME_OVER screen to return to the main menu so
        the next seed's `start` is legal. Return False on any failure so the
        durable restart path relaunches instead of trusting a dead child pipe."""
        try:
            kind, state = self.stepper.step("proceed")
            deadline = _now() + self.args.menu_timeout
            while _now() < deadline:
                if not state.get("in_game"):
                    return True
                kind, state = self.stepper.step("state")
        except GameGone as exc:
            _log(f"game-over menu return lost the game pipe: {exc}")
            return False
        _log("game-over menu return timed out")
        return False

    # -- campaign loop -------------------------------------------------------
    def run(self) -> int:
        self.reader.start()
        # startup handshake: `state` forces a dump without changing anything and
        # unblocks the game's readMessageBlocking (PROTOCOL.md 1.3).
        self.stepper.send("state")

        seed_list = self.args.seeds
        try:
            self.progress.load_or_init(
                self.args.campaign_id, seed_list,
                self.args.policy, self.fork_hash,
                policy_seed=self.args.policy_seed)
        except CampaignIdentityError as exc:
            message = str(exc)
            _log(f"FATAL CAMPAIGN IDENTITY: {message}")
            self.progress.fatal_identity_mismatch(message)
            return EXIT_FATAL
        _log(f"campaign {self.args.campaign_id}: {len(seed_list)} seeds, "
             f"policy={self.args.policy}, launch #{self.progress.data['launches']}, "
             f"done={len(self.progress.done_seeds())}, fork={self.fork_hash[:12]}")
        if self.progress.data.get("status") == "fatal_environment_drift":
            _log("campaign is already stopped on fatal environment drift; "
                 "refusing to resume")
            return EXIT_FATAL

        try:
            self.stepper.await_ready()  # drain the handshake dump
        except GameGone:
            _log("handshake produced no state")
            self.progress.request_restart(
                self.args.launch_token, "handshake_game_gone")
            return EXIT_GAME_GONE

        while True:
            seed = self.next_seed()
            if seed is None:
                failed = self.progress.data["seeds_failed"]
                self.progress.data["status"] = (
                    "complete_with_failures" if failed else "complete")
                self.progress.data["current_seed"] = None
                self.progress.data["current_seed_attempt"] = 0
                self.progress.flush()
                self._write_manifest()
                if failed:
                    _log("campaign COMPLETE WITH FAILURES")
                    return EXIT_FATAL
                _log("campaign COMPLETE")
                return EXIT_OK

            attempt = (self.progress.data["current_seed_attempt"] + 1
                       if self.progress.data.get("current_seed") == seed else 1)
            self.progress.data["current_seed"] = seed
            self.progress.data["current_seed_attempt"] = attempt
            self.progress.flush()

            try:
                self.wait_menu()
                outcome, floor, acts, menu_ok = self.run_seed(seed, attempt)
            except FatalEnvironmentDrift as e:
                message = str(e)
                _log(f"FATAL ENVIRONMENT DRIFT: {message}")
                self.progress.fatal_environment_drift(
                    seed, attempt, message, e.kind)
                return EXIT_FATAL
            except GameGone as e:
                _log(f"seed {seed}: game gone mid-run ({e}); attempt {attempt}")
                if attempt >= self.args.max_attempts:
                    self.progress.data["seeds_failed"].append({
                        "seed": seed, "reason": "crash",
                        "attempts": attempt, "at_floor": None})
                    self.progress.data["current_seed"] = None
                    self.progress.data["current_seed_attempt"] = 0
                    self.progress.flush()
                self.progress.flush()
                self.progress.request_restart(
                    self.args.launch_token, "game_gone_mid_run")
                return EXIT_GAME_GONE
            except Exception as e:  # noqa: BLE001 - defensive, log loudly
                _log(f"seed {seed}: driver error {e!r}; marking failed")
                self.progress.data["seeds_failed"].append({
                    "seed": seed, "reason": f"error:{e}",
                    "attempts": attempt, "at_floor": None})
                self.progress.data["current_seed"] = None
                self.progress.data["current_seed_attempt"] = 0
                self.progress.flush()
                continue

            self.progress.data["seeds_done"].append({
                "seed": seed, "outcome": outcome, "floor": floor,
                "actions": acts, "attempts": attempt,
                "artifact": f"run_{seed}_a20_ironclad.jsonl"})
            self.progress.data["current_seed"] = None
            self.progress.data["current_seed_attempt"] = 0
            self.progress.flush()
            _log(f"seed {seed}: DONE outcome={outcome} floor={floor} "
                 f"actions={acts} menu_returnable={menu_ok}")

            if not menu_ok:
                # ended mid-dungeon (boss reward / cap / wedge). The protocol
                # cannot walk back to the menu -> ask the orchestrator to
                # relaunch for the remaining seeds.
                self.progress.request_restart(
                    self.args.launch_token, "run_ended_mid_dungeon")
                _log("run ended mid-dungeon; requesting relaunch for next seed")
                return EXIT_NEED_RESTART

    def _write_manifest(self) -> None:
        d = self.progress.data
        manifest = {
            "campaign_id": d["campaign_id"],
            "schema_version": SCHEMA_VERSION,
            "driver_version": DRIVER_VERSION,
            "fork_jar_sha256": d["fork_jar_sha256"],
            "policy": d["policy"],
            # Additive (design 7.5), same key `Progress.load_or_init` writes
            # to campaign_progress.json; absent on a manifest from before
            # this field existed, so `.get` rather than `[...]`.
            "policy_seed": d.get("policy_seed"),
            "seed_list": d["seed_list"],
            "status": d["status"],
            "launches": d["launches"],
            "started_utc": d["started_utc"],
            "finished_utc": _utc(),
            "seeds_done": d["seeds_done"],
            "seeds_failed": d["seeds_failed"],
        }
        with open(self.run_file("campaign_manifest.json"),
                  "w", encoding="utf-8", newline="\n") as fh:
            json.dump(manifest, fh, indent=2)

def parse_args(argv=None) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="SpeedTheSpire campaign driver (B1.4)")
    ap.add_argument("--data-root", required=True,
                    help="non-repo campaign data root (design 7.3)")
    ap.add_argument("--campaign-id", required=True)
    ap.add_argument("--seeds", required=True,
                    help="comma-separated base-35 seeds, or a path to a file "
                         "with one seed per line")
    ap.add_argument("--policy", choices=["random-legal", "greedy", "script"],
                    default="random-legal",
                    help="random-legal: uniform over the expanded legal "
                         "actions. greedy: the same expansion ranked by "
                         "greedy_policy.score_action (depth-seeking; use this "
                         "to reach deep floors). script: a fixed command list.")
    ap.add_argument("--card-table", default=None,
                    help="override the greedy policy's card side table "
                         "(default: cards_sidetable.json beside the driver)")
    ap.add_argument("--script", help="command script (one per line) for "
                    "--policy script; applied to every seed")
    ap.add_argument("--script-dir", help="directory of per-seed scripts "
                    "script_<SEED>.txt for --policy script. Takes precedence "
                    "over --script. Used by the B1.3 A/B equivalence replay: the "
                    "same fixed command list is forced on the strip-on and "
                    "strip-off runs so any divergence surfaces at a matching seq.")
    ap.add_argument("--fork-jar", required=True,
                    help="deployed CommunicationMod-oracle.jar (hashed for the "
                         "header)")
    ap.add_argument("--launch-log", default="",
                    help="exact append-only mts_launch<N>.log allocated by the "
                         "orchestrator for this game process")
    ap.add_argument("--launch-token", default="",
                    help="one-use token also inherited through the orchestrator-"
                         "launched game process; rejects stale/GUI launches")
    ap.add_argument("--policy-seed", type=int, default=1234,
                    help="RNG seed for the random-legal policy (driver-side)")
    ap.add_argument("--run-label", default="",
                    help="free-form label recorded in the throughput sidecar "
                         "header (e.g. strip-on / strip-off), for B1.3 reporting")
    ap.add_argument("--timeout", type=float, default=90.0,
                    help="per-command wall-clock watchdog (s); sized for stock "
                         "animation speed (~0.36 states/s baseline)")
    ap.add_argument("--probe-timeout", type=float, default=20.0,
                    help="after a watchdog trip, wait this long for a reply to a "
                         "`state` liveness ping before declaring the game gone")
    ap.add_argument("--max-noops", type=int, default=3,
                    help="advertised no-op recoveries before ending a run as "
                         "noop_wedge (game alive but unresponsive to legal moves)")
    ap.add_argument("--max-settle", type=int, default=60,
                    help="script mode: max `state` nudges to wait for a "
                         "command's verb to become legal before declaring a "
                         "divergence (B1.3 replay robustness)")
    ap.add_argument("--settle-sleep", type=float, default=0.0,
                    help="wall-clock sleep between script settle nudges, to give "
                         "the game frame-loop time to advance (diagnostic)")
    ap.add_argument("--menu-timeout", type=float, default=120.0)
    ap.add_argument("--max-actions", type=int, default=3000,
                    help="per-run action cap (safety bound)")
    ap.add_argument("--max-attempts", type=int, default=2,
                    help="retry a crashed seed this many times before failing it")
    return ap.parse_args(argv)


def resolve_seeds(spec: str) -> list:
    if os.path.exists(spec):
        with open(spec, "r", encoding="utf-8") as fh:
            seeds = [ln.strip().upper() for ln in fh
                     if ln.strip() and not ln.startswith("#")]
    else:
        seeds = [s.strip().upper() for s in spec.split(",") if s.strip()]
    return validate_seed_list(seeds)


def _request_restart_after_unexpected_exit(
        driver, launch_token: str, reason: str) -> bool:
    """Best-effort durable restart once a driver has initialized progress.

    A driver-side exit code is invisible to the orchestrator because the game,
    not the orchestrator, owns this process. If an exception escapes after
    Progress.load_or_init, preserve the in-memory ledger (including a seed that
    completed just before a failed atomic publish) and attach the same
    launch-token-bound restart signal used by the ordinary paths.
    """
    if driver is None or getattr(driver, "progress", None) is None or \
            driver.progress.data is None:
        return False
    try:
        driver.progress.request_restart(launch_token, reason)
        _log(f"durable restart requested after {reason}")
        return True
    except Exception as exc:  # noqa: BLE001 - already handling fatal unwind
        _log(f"could not persist restart request after {reason}: {exc!r}")
        return False


def main(argv=None) -> int:
    args = parse_args(argv)
    try:
        validate_campaign_id(args.campaign_id)
        args.seeds = resolve_seeds(args.seeds)
        campaign_dir_under_root(args.data_root, args.campaign_id)
    except ValueError as exc:
        _log(f"invalid campaign input: {exc}")
        return EXIT_FATAL
    if args.policy == "script" and not args.script and not args.script_dir:
        _log("--policy script requires --script or --script-dir")
        return EXIT_FATAL
    driver = None
    try:
        driver = CampaignDriver(args)
        return driver.run()
    except GameGone as e:
        _log(f"game gone: {e}")
        _request_restart_after_unexpected_exit(
            driver, args.launch_token, "uncaught_game_gone")
        return EXIT_GAME_GONE
    except Exception as e:  # noqa: BLE001
        _log(f"FATAL {e!r}")
        _request_restart_after_unexpected_exit(
            driver, args.launch_token, "unexpected_driver_exception")
        return EXIT_FATAL


if __name__ == "__main__":
    sys.exit(main())
