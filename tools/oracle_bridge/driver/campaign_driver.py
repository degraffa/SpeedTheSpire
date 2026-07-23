#!/usr/bin/env python3
"""SpeedTheSpire oracle-bridge campaign driver (Stage B task B1.4).

This is the *real* driver the bring-up child (echo_driver.py, B0.2) grew into.
It is the CommunicationMod-oracle child process: the game launches it (via the
config.properties `command`, see orchestrator.py) and wires *its* stdio to the
game -- state JSON arrives on stdin (one object per line), commands leave on
stdout (\\n-delimited). See ../PROTOCOL.md.

What it does (B1.4 deliverables):
  * seeded A20 Ironclad starts (`start ironclad 20 <SEED>`; PROTOCOL.md 2.1);
  * two action generators -- `random-legal` (uniform over the game's own
    accepted-command set, expanded to concrete legal actions) and `script`
    (a fixed command list) -- both paced through one strict lock-step gate;
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
import json
import os
import random
import sys
import threading
import time
from datetime import datetime, timezone

DRIVER_VERSION = "b1.4.0"
SCHEMA_VERSION = 1

# Static provenance for the artifact header (design 1.2; PROTOCOL.md 0).
GAME_STS_VERSION = "11-30-2020"
GAME_MTS_VERSION = "3.18.1"
BASEMOD_WORKSHOP = "1605833019"
MODS = ["basemod", "CommunicationMod-oracle"]

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

    if any(a in avail for a in ("confirm", "proceed")):
        actions.append("proceed")
    for alias in ("skip", "cancel", "return", "leave"):
        if alias in avail:
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

    def __init__(self, path: str, hb_path: str) -> None:
        self.path = path
        self.hb_path = hb_path
        self.data = None  # type: dict | None

    def load_or_init(self, campaign_id, seed_list, policy, fork_hash) -> dict:
        if os.path.exists(self.path):
            with open(self.path, "r", encoding="utf-8") as fh:
                self.data = json.load(fh)
            # A fresh launch resumed us; bump the launch count.
            self.data["launches"] = self.data.get("launches", 0) + 1
        else:
            self.data = {
                "campaign_id": campaign_id,
                "schema_version": SCHEMA_VERSION,
                "driver_version": DRIVER_VERSION,
                "fork_jar_sha256": fork_hash,
                "policy": policy,
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
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(self.data, fh, indent=2)
            fh.flush()
            os.fsync(fh.fileno())
        os.replace(tmp, self.path)

    def heartbeat(self, seed, floor, actions) -> None:
        try:
            with open(self.hb_path, "w", encoding="utf-8", newline="\n") as fh:
                json.dump({"t": _now(), "utc": _utc(), "seed": seed,
                           "floor": floor, "actions": actions}, fh)
        except OSError:
            pass

    def done_seeds(self) -> set:
        d = self.data
        return {s["seed"] for s in d["seeds_done"]} | {
            s["seed"] for s in d["seeds_failed"]}


# ---------------------------------------------------------------------------

class CampaignDriver:
    def __init__(self, args) -> None:
        self.args = args
        self.rng = random.Random(args.policy_seed)
        self.reader = Reader()
        self.stepper = Stepper(self.reader, args.timeout, args.probe_timeout)
        self.fork_hash = sha256_file(args.fork_jar)
        os.makedirs(self.run_dir(), exist_ok=True)
        self.progress = Progress(
            os.path.join(self.run_dir(), "campaign_progress.json"),
            os.path.join(self.run_dir(), "campaign_heartbeat.json"),
        )
        # Script(s) are loaded per-seed in run_seed (a --script-dir campaign has a
        # distinct command list per seed). A single --script applies to all seeds.
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
        return os.path.join(self.args.data_root, self.args.campaign_id)

    def strip_flags(self) -> dict:
        return {"run_label": self.args.run_label,
                "campaign_id": self.args.campaign_id,
                "policy": self.args.policy}

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
        artifact = os.path.join(self.run_dir(),
                                f"run_{seed}_a20_ironclad.jsonl")
        # Per-seed policy RNG: a run's action sequence depends only on
        # (policy_seed, seed), not on the campaign's position, so any run is
        # reproducible in isolation -- the (seed, action-prefix) reproducibility
        # tier-3 / triage relies on (design 7.5).
        self.rng = random.Random(f"{self.args.policy_seed}:{seed}")
        # start the run; the first in-dungeon dump carries game_state.seed (long)
        kind, state = self.stepper.step(f"start ironclad 20 {seed}")
        if kind == "error":
            raise RuntimeError(f"start rejected: {state.get('error')}")
        if not state.get("in_game"):
            # occasionally the menu fade needs one more settle; wait once more
            kind, state = self.stepper.await_ready()
        gs = state.get("game_state") or {}
        seed_long = gs.get("seed")
        expect_long = seed_to_long(seed)
        seed_ok = (seed_long == expect_long)
        if not seed_ok:
            _log(f"seed crosscheck MISMATCH {seed}: dump={seed_long} "
                 f"getLong={expect_long}")

        header = {
            "record_kind": "header",
            "schema_version": SCHEMA_VERSION,
            "driver_version": DRIVER_VERSION,
            "created_utc": _utc(),
            "game": {"sts_version": GAME_STS_VERSION,
                     "mts_version": GAME_MTS_VERSION,
                     "basemod": BASEMOD_WORKSHOP},
            "mods": MODS,
            "fork_jar_sha256": self.fork_hash,
            "fork_jar_path": self.args.fork_jar.replace("\\", "/"),
            "oracle_block_enabled": ("oracle" in gs),
            "seed": {"string": seed, "long": seed_long,
                     "long_getLong": expect_long, "crosscheck_ok": seed_ok},
            "ascension": 20,
            "character": "IRONCLAD",
            "policy": self.args.policy,
            "attempt": attempt,
            "campaign_id": self.args.campaign_id,
        }
        rl = RunLogger(artifact, header)
        # B1.3 throughput sidecar: one line per injected action with a monotonic
        # timestamp, captured at the moment the command is about to be sent. This
        # is a SEPARATE file from the JSONL artifact, so the equivalence-critical
        # action-record / state_json schema (B1.5) is untouched. measure_
        # throughput.py reads these to compute sustained injected-actions/sec.
        timing = TimingLog(os.path.join(
            self.run_dir(), f"run_{seed}_a20_ironclad.timing.jsonl"),
            self.strip_flags())
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
                    self._return_to_menu_from_gameover()
                    return ov, floor, actions, True

                if is_boss_combat_reward(gs):
                    outcome = self._claim_boss_reward(rl, state, seed, actions)
                    return outcome, floor, outcome_actions(outcome, actions), False

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
                    script_i += 1
                else:
                    cmd = self._policy_command(state)
                    if cmd is None:
                        # no legal progress action; nudge and detect a real wedge
                        stuck += 1
                        if stuck >= 12:
                            rl.terminal("legal_exhaustion", gs, actions)
                            _log(f"seed {seed}: legal-action exhaustion at "
                                 f"floor {floor} screen {gs.get('screen_type')}")
                            return "legal_exhaustion", floor, actions, False
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
                    actions += 1
                    kind, state = self.stepper.step(esc)
        finally:
            rl.close()
            timing.close()

    def _policy_command(self, state):
        acts = expand_legal_actions(state, self.rng)
        if not acts:
            return None
        return self.rng.choice(acts)

    def _claim_boss_reward(self, rl, state, seed, actions):
        """On the Act-1 boss combat-reward screen: claim every reward, then STOP
        (do not `proceed` -- that opens the boss chest, which is S2/out of scope,
        design 1.1). Returns 'act1_boss_reward'."""
        _log(f"seed {seed}: ACT-1 BOSS reward reached -- claiming, then stop")
        guard = 0
        while True:
            gs = state.get("game_state") or {}
            st = gs.get("screen_type")
            choices = gs.get("choice_list") or []
            guard += 1
            if guard > 60:
                rl.terminal("act1_boss_reward", gs, actions)
                return "act1_boss_reward"
            if st == "COMBAT_REWARD":
                if choices:
                    rl.action("choose 0", state)
                    actions += 1
                    kind, state = self.stepper.step("choose 0")
                    continue
                # all rewards claimed -> terminal, do NOT proceed to boss chest
                rl.action("__terminal_observed__", state)
                rl.terminal("act1_boss_reward", gs, actions)
                return "act1_boss_reward"
            if st == "CARD_REWARD":
                cmd = "choose 0" if choices else "skip"
                rl.action(cmd, state)
                actions += 1
                kind, state = self.stepper.step(cmd)
                continue
            # any other transient screen: settle
            kind, state = self.stepper.step("state")

    def _return_to_menu_from_gameover(self):
        """Press proceed on the GAME_OVER screen to return to the main menu so
        the next seed's `start` is legal. Best-effort; failure -> relaunch."""
        try:
            kind, state = self.stepper.step("proceed")
            deadline = _now() + self.args.menu_timeout
            while _now() < deadline:
                gs = state.get("game_state") or {}
                if not state.get("in_game"):
                    return
                kind, state = self.stepper.step("state")
        except GameGone:
            pass

    # -- campaign loop -------------------------------------------------------
    def run(self) -> int:
        self.reader.start()
        # startup handshake: `state` forces a dump without changing anything and
        # unblocks the game's readMessageBlocking (PROTOCOL.md 1.3).
        self.stepper.send("state")

        seed_list = self.args.seeds
        self.progress.load_or_init(self.args.campaign_id, seed_list,
                                   self.args.policy, self.fork_hash)
        _log(f"campaign {self.args.campaign_id}: {len(seed_list)} seeds, "
             f"policy={self.args.policy}, launch #{self.progress.data['launches']}, "
             f"done={len(self.progress.done_seeds())}, fork={self.fork_hash[:12]}")

        try:
            self.stepper.await_ready()  # drain the handshake dump
        except GameGone:
            _log("handshake produced no state")
            return EXIT_GAME_GONE

        while True:
            seed = self.next_seed()
            if seed is None:
                self.progress.data["status"] = "complete"
                self.progress.data["current_seed"] = None
                self.progress.flush()
                self._write_manifest()
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
            "seed_list": d["seed_list"],
            "launches": d["launches"],
            "started_utc": d["started_utc"],
            "finished_utc": _utc(),
            "seeds_done": d["seeds_done"],
            "seeds_failed": d["seeds_failed"],
        }
        with open(os.path.join(self.run_dir(), "campaign_manifest.json"),
                  "w", encoding="utf-8", newline="\n") as fh:
            json.dump(manifest, fh, indent=2)


def outcome_actions(outcome, actions):
    return actions


def parse_args(argv=None) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="SpeedTheSpire campaign driver (B1.4)")
    ap.add_argument("--data-root", required=True,
                    help="non-repo campaign data root (design 7.3)")
    ap.add_argument("--campaign-id", required=True)
    ap.add_argument("--seeds", required=True,
                    help="comma-separated base-35 seeds, or a path to a file "
                         "with one seed per line")
    ap.add_argument("--policy", choices=["random-legal", "script"],
                    default="random-legal")
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
            return [ln.strip().upper() for ln in fh
                    if ln.strip() and not ln.startswith("#")]
    return [s.strip().upper() for s in spec.split(",") if s.strip()]


def main(argv=None) -> int:
    args = parse_args(argv)
    args.seeds = resolve_seeds(args.seeds)
    if args.policy == "script" and not args.script and not args.script_dir:
        _log("--policy script requires --script or --script-dir")
        return EXIT_FATAL
    try:
        return CampaignDriver(args).run()
    except GameGone as e:
        _log(f"game gone: {e}")
        return EXIT_GAME_GONE
    except Exception as e:  # noqa: BLE001
        _log(f"FATAL {e!r}")
        return EXIT_FATAL


if __name__ == "__main__":
    sys.exit(main())
