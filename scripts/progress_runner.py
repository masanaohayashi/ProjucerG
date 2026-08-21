#!/usr/bin/env python3

"""Run a build command and report progress, elapsed time, and ETA.

Ninja exposes an exact [done/total] counter.  Make and Xcode do not, so the
--count-lines mode uses the number of commands from `make -n` as an estimate
and still emits a heartbeat while the command is running.
"""

from __future__ import annotations

import argparse
import os
import re
import signal
import subprocess
import sys
import time
from typing import Optional


PROGRESS_RE = re.compile(r"\[\s*(\d+)\s*/\s*(\d+)\s*\]")
IMPORTANT_OUTPUT_RE = re.compile(r"(error|failed|fatal|make: \*\*\*)", re.IGNORECASE)


def format_duration(seconds: float) -> str:
    total = max(0, int(seconds))
    hours, remainder = divmod(total, 3600)
    minutes, secs = divmod(remainder, 60)
    if hours:
        return f"{hours}h {minutes:02d}m {secs:02d}s"
    if minutes:
        return f"{minutes}m {secs:02d}s"
    return f"{secs}s"


def format_eta(seconds: Optional[float]) -> str:
    if seconds is None:
        return "ETA calculating"
    return f"about {format_duration(seconds)} remaining"


def parse_args() -> tuple[argparse.Namespace, list[str]]:
    if "--" not in sys.argv:
        raise SystemExit("progress_runner.py: use -- before the command")

    separator = sys.argv.index("--")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", required=True)
    parser.add_argument("--total", type=int, default=0)
    parser.add_argument("--count-lines", action="store_true")
    parser.add_argument("--event-pattern")
    parser.add_argument("--suppress-unmatched", action="store_true")
    parser.add_argument("--heartbeat", type=float, default=5.0)
    parser.add_argument(
        "--single-line",
        action="store_true",
        help="rewrite progress updates on one terminal line",
    )
    args = parser.parse_args(sys.argv[1:separator])
    command = sys.argv[separator + 1 :]
    if not command:
        raise SystemExit("progress_runner.py: no command to run")
    return args, command


def terminate_process(process: subprocess.Popen[str], signum: int, _frame) -> None:
    if process.poll() is None:
        try:
            os.killpg(process.pid, signum)
        except ProcessLookupError:
            pass


class ProgressLine:
    """Render progress updates without scrolling an interactive terminal."""

    def __init__(self, enabled: bool) -> None:
        self.stream = None
        self.owns_stream = False
        if not enabled:
            return

        if sys.stdout.isatty():
            self.stream = sys.stdout
            return

        # setup_ios.sh sends child output through tee, so stdout is a pipe even
        # when the user is running it in a terminal.  Write the live status to
        # the controlling terminal in that case; build output still goes to
        # tee and remains available in the setup log.
        try:
            terminal = open("/dev/tty", "w", buffering=1)
        except OSError:
            return
        self.stream = terminal
        self.owns_stream = True

    def update(self, message: str) -> None:
        if self.stream is None:
            print(message, flush=True)
            return
        self.stream.write(f"\r\033[K{message}")
        self.stream.flush()

    def finish(self) -> None:
        if self.stream is None:
            return
        self.stream.write("\r\033[K\n")
        self.stream.flush()
        if self.owns_stream:
            self.stream.close()

    def clear_for_output(self) -> None:
        if self.stream is None:
            return
        self.stream.write("\r\033[K\n")
        self.stream.flush()


def main() -> int:
    args, command = parse_args()
    total = max(0, args.total)
    command_text = " ".join(command)
    progress_line = ProgressLine(args.single_line)
    print(f"[{args.label}] started: {command_text}", flush=True)
    if total:
        print(f"[{args.label}] estimated total: {total} tasks", flush=True)

    started = time.monotonic()
    last_report = 0.0
    completed = 0
    observed_total = total
    progress_total_seen = False
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        universal_newlines=True,
        start_new_session=True,
    )

    previous_handlers = {}
    for signum in (signal.SIGINT, signal.SIGTERM):
        previous_handlers[signum] = signal.signal(
            signum, lambda sig, frame: terminate_process(process, sig, frame)
        )

    assert process.stdout is not None
    try:
        for raw_line in process.stdout:
            line = raw_line.rstrip("\n")
            match = PROGRESS_RE.search(line)

            if match:
                completed = max(completed, int(match.group(1)))
                actual_total = int(match.group(2))
                if not progress_total_seen:
                    observed_total = actual_total
                    progress_total_seen = True
                else:
                    observed_total = max(observed_total, actual_total)
                now = time.monotonic()
                if now - last_report >= 0.35 or completed >= observed_total:
                    elapsed = now - started
                    remaining = None
                    if completed > 0 and observed_total > completed:
                        remaining = elapsed / completed * (observed_total - completed)
                    progress_line.update(
                        f"[{args.label}] {completed}/{observed_total} "
                        f"({completed / observed_total * 100:.1f}%) "
                        f"elapsed {format_duration(elapsed)}, {format_eta(remaining)}"
                    )
                    last_report = now
                continue

            if args.count_lines and line.strip():
                completed += 1
                now = time.monotonic()
                if now - last_report >= 0.35:
                    effective_total = max(observed_total, total, completed)
                    elapsed = now - started
                    remaining = None
                    if completed > 0 and effective_total > completed:
                        remaining = elapsed / completed * (effective_total - completed)
                    progress_line.update(
                        f"[{args.label}] approximately {completed}/{effective_total} "
                        f"({completed / effective_total * 100:.1f}%) "
                        f"elapsed {format_duration(elapsed)}, {format_eta(remaining)}"
                    )
                    last_report = now
                if IMPORTANT_OUTPUT_RE.search(line):
                    progress_line.clear_for_output()
                    print(line, flush=True)
                continue

            if args.event_pattern and re.search(args.event_pattern, line):
                completed += 1
                now = time.monotonic()
                effective_total = max(observed_total, total, completed)
                if now - last_report >= 0.35:
                    elapsed = now - started
                    remaining = None
                    if completed > 0 and effective_total > completed:
                        remaining = elapsed / completed * (effective_total - completed)
                    progress_line.update(
                        f"[{args.label}] approximately {completed}/{effective_total} "
                        f"({completed / effective_total * 100:.1f}%) "
                        f"elapsed {format_duration(elapsed)}, {format_eta(remaining)}"
                    )
                    last_report = now
                if IMPORTANT_OUTPUT_RE.search(line):
                    progress_line.clear_for_output()
                    print(line, flush=True)
                continue

            if line and not (
                args.suppress_unmatched and not IMPORTANT_OUTPUT_RE.search(line)
            ):
                progress_line.clear_for_output()
                print(line, flush=True)

            now = time.monotonic()
            if now - last_report >= args.heartbeat:
                elapsed = now - started
                progress = f"{completed}/{observed_total}" if observed_total else "running"
                progress_line.update(
                    f"[{args.label}] {progress}, elapsed {format_duration(elapsed)}, "
                    f"{format_eta(None)}"
                )
                last_report = now
    finally:
        process.stdout.close()
        for signum, handler in previous_handlers.items():
            signal.signal(signum, handler)

    return_code = process.wait()
    exit_code = return_code if return_code >= 0 else 128 + (-return_code)
    elapsed = time.monotonic() - started
    progress_line.finish()
    if exit_code == 0:
        print(
            f"[{args.label}] completed: elapsed {format_duration(elapsed)}, "
            f"processed {completed if completed else 'unknown'}",
            flush=True,
        )
    else:
        print(
            f"[{args.label}] failed: exit {exit_code}, "
            f"elapsed {format_duration(elapsed)}",
            file=sys.stderr,
            flush=True,
        )
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
