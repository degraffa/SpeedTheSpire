#!/usr/bin/env python3
"""Minimal CommunicationMod bridge child (Stage B task B0.2).

CommunicationMod launches one external child process and wires *its* stdio to
the game (PROTOCOL.md B0.1 §1): the game writes each game-state JSON object to
the child's **stdin** (one object per line, newline-terminated) and reads
commands back from the child's **stdout** (delimited by ``\\n`` or NUL). On
startup the game blocks until the child emits its first line, so the child must
speak promptly (default timeout 10 s, CommunicationMod.java:260-266).

Because the child's own stdin/stdout are owned by the game, a human cannot type
commands into them directly. This driver therefore takes commands from a
**side-channel file** (``--commands``): every newline-appended line is forwarded
verbatim to the game and logged. "Hand-typed" commands are just lines appended
to that file (e.g. ``echo 'start ironclad 20 <SEED>' >> commands.txt``).

Two modes:
  * default (bridge): read state from stdin -> append to the JSONL log; tail the
    command file -> forward lines to stdout. This is what config.properties'
    ``command`` points at.
  * ``--verify <log.jsonl>``: re-parse a captured log and report that every
    recorded state line is valid JSON (the B0.2 acceptance: "captured JSONL
    replays cleanly through echo_driver.py parsing"). Runs standalone, no game.

Records are newline-delimited JSON, one per line, each:
  {"seq": <int>, "t": <epoch_s>, "dir": "recv"|"send"|"meta", ...}
`recv` carries the raw game message under "raw" (a JSON string); `send` carries
the forwarded command under "cmd"; `meta` carries driver lifecycle notes. The
log is a bridge artifact (design §7.3) and is never committed.

Dependency-free (Python 3 standard library only): it runs under the game's own
Windows Python, outside the WSL build/CI world.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import threading
import time

# Written straight to the byte streams so Windows never rewrites '\n' to
# '\r\n' -- the game reads a command up to the first '\n', and a stray '\r'
# would be swallowed into the command text and corrupt it.
_STDOUT = sys.stdout.buffer
_STDIN = sys.stdin.buffer


def _now() -> float:
    return time.time()


class Logger:
    """Serialises JSONL records from both threads under one lock."""

    def __init__(self, path: str) -> None:
        self._fh = open(path, "a", encoding="utf-8", newline="\n")
        self._lock = threading.Lock()
        self._seq = 0

    def write(self, dir_: str, **fields) -> None:
        with self._lock:
            rec = {"seq": self._seq, "t": round(_now(), 6), "dir": dir_}
            rec.update(fields)
            self._fh.write(json.dumps(rec, ensure_ascii=False) + "\n")
            self._fh.flush()
            self._seq += 1

    def close(self) -> None:
        with self._lock:
            self._fh.close()


class Stats:
    """Throughput counters (design §2.2 floor is measured off these)."""

    def __init__(self) -> None:
        self.recv = 0
        self.sent = 0
        self.first_recv: float | None = None
        self.last_recv: float | None = None

    def note_recv(self) -> None:
        now = _now()
        if self.first_recv is None:
            self.first_recv = now
        self.last_recv = now
        self.recv += 1

    def rate(self) -> float:
        if self.first_recv is None or self.last_recv is None:
            return 0.0
        span = self.last_recv - self.first_recv
        # recv-1 intervals across the span; guard the single-message case.
        return (self.recv - 1) / span if span > 0 and self.recv > 1 else 0.0


def _send(logger: Logger, stats: Stats, cmd: str, lock: threading.Lock) -> None:
    with lock:
        _STDOUT.write((cmd + "\n").encode("utf-8"))
        _STDOUT.flush()
    stats.sent += 1
    logger.write("send", cmd=cmd)


def _reader_loop(
    logger: Logger, stats: Stats, latest_path: str | None, stop: threading.Event
) -> None:
    """Log every state line the game pushes to our stdin."""
    while True:
        line = _STDIN.readline()
        if not line:  # EOF: the game closed the pipe -> tell the driver to exit.
            logger.write("meta", note="stdin EOF -- game closed the pipe")
            stop.set()
            return
        text = line.decode("utf-8", errors="replace").rstrip("\r\n")
        if not text:
            continue
        stats.note_recv()
        logger.write("recv", raw=text)
        if latest_path:
            # Snapshot the newest state for easy out-of-band inspection.
            try:
                with open(latest_path, "w", encoding="utf-8", newline="\n") as fh:
                    fh.write(text)
            except OSError:
                pass


def _command_loop(
    logger: Logger,
    stats: Stats,
    cmd_path: str,
    handshake: str,
    poll_s: float,
    stop: threading.Event,
) -> None:
    """Emit the startup handshake, then forward newly-appended command lines."""
    out_lock = threading.Lock()

    # Truncate/create the command file so a stale line never auto-fires.
    with open(cmd_path, "w", encoding="utf-8", newline="\n"):
        pass
    offset = 0

    # Unblock the game's readMessageBlocking: `state` forces a dump without
    # changing game state (PROTOCOL.md §2), the safe readiness signal.
    if handshake:
        _send(logger, stats, handshake, out_lock)

    pending = ""
    while not stop.is_set():
        time.sleep(poll_s)
        try:
            size = os.path.getsize(cmd_path)
        except OSError:
            continue
        if size < offset:  # file was truncated/rotated -- restart tailing.
            offset = 0
            pending = ""
        if size == offset:
            continue
        with open(cmd_path, "r", encoding="utf-8", newline="") as fh:
            fh.seek(offset)
            chunk = fh.read()
            offset = fh.tell()
        pending += chunk
        while "\n" in pending:
            raw, pending = pending.split("\n", 1)
            cmd = raw.strip()
            if not cmd or cmd.startswith("#"):
                continue
            if cmd in ("__quit__", "__exit__"):
                logger.write("meta", note="quit sentinel -- driver exiting")
                logger.write(
                    "meta",
                    note="throughput",
                    recv=stats.recv,
                    sent=stats.sent,
                    recv_per_s=round(stats.rate(), 3),
                )
                logger.close()
                os._exit(0)
            _send(logger, stats, cmd, out_lock)


def run_bridge(args: argparse.Namespace) -> int:
    logger = Logger(args.log)
    stats = Stats()
    logger.write(
        "meta",
        note="driver start",
        log=os.path.abspath(args.log),
        commands=os.path.abspath(args.commands),
        handshake=args.handshake,
        pid=os.getpid(),
    )
    stop = threading.Event()
    reader = threading.Thread(
        target=_reader_loop, args=(logger, stats, args.latest, stop), daemon=True
    )
    reader.start()
    try:
        _command_loop(
            logger, stats, args.commands, args.handshake, args.poll, stop
        )
    except KeyboardInterrupt:
        pass
    finally:
        logger.write("meta", note="driver stop", recv=stats.recv, sent=stats.sent)
        logger.close()
    return 0


def run_verify(path: str) -> int:
    """Re-parse a captured log: every `recv` raw must be valid JSON."""
    total = recv = ok = 0
    bad: list[tuple[int, str]] = []
    with open(path, "r", encoding="utf-8") as fh:
        for lineno, line in enumerate(fh, 1):
            line = line.strip()
            if not line:
                continue
            total += 1
            try:
                rec = json.loads(line)
            except json.JSONDecodeError as exc:
                bad.append((lineno, f"record not JSON: {exc}"))
                continue
            if rec.get("dir") != "recv":
                continue
            recv += 1
            try:
                json.loads(rec["raw"])
                ok += 1
            except (KeyError, json.JSONDecodeError) as exc:
                bad.append((lineno, f"recv.raw not JSON: {exc}"))
    print(f"records={total} recv={recv} recv_parsed_ok={ok} errors={len(bad)}")
    for lineno, msg in bad[:20]:
        print(f"  line {lineno}: {msg}")
    return 0 if not bad and recv > 0 else 1


def main() -> int:
    ap = argparse.ArgumentParser(description="CommunicationMod bridge child (B0.2)")
    ap.add_argument("--log", default="communication_log.jsonl",
                    help="JSONL capture path (game cwd if relative)")
    ap.add_argument("--commands", default="communication_commands.txt",
                    help="side-channel command file; appended lines are forwarded")
    ap.add_argument("--latest", default=None,
                    help="optional path kept overwritten with the newest state JSON")
    ap.add_argument("--handshake", default="state",
                    help="first command sent on startup to unblock the game "
                         "('' to send nothing)")
    ap.add_argument("--poll", type=float, default=0.03,
                    help="command-file poll interval in seconds")
    ap.add_argument("--verify", metavar="LOG",
                    help="parse-check a captured JSONL log and exit (no game)")
    args = ap.parse_args()

    if args.verify:
        return run_verify(args.verify)
    return run_bridge(args)


if __name__ == "__main__":
    sys.exit(main())
